//////////////////////////////////////////////////////////////////////////////
// framework
//
// parameter defaults: CalPatRec/fcl/prolog.fcl
//////////////////////////////////////////////////////////////////////////////
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "GeometryService/inc/GeomHandle.hh"
#include "art/Framework/Core/EDProducer.h"
#include "GeometryService/inc/DetectorSystem.hh"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Services/Optional/TFileService.h"
// conditions
#include "ConditionsService/inc/ConditionsHandle.hh"
#include "ConditionsService/inc/TrackerCalibrations.hh"
#include "GeometryService/inc/getTrackerOrThrow.hh"
#include "TTrackerGeom/inc/TTracker.hh"
// root 
#include "TVector2.h"
// data
#include "RecoDataProducts/inc/StrawHit.hh"
#include "RecoDataProducts/inc/StrawHitPositionCollection.hh"
#include "RecoDataProducts/inc/StereoHit.hh"
#include "RecoDataProducts/inc/StrawHitFlag.hh"
#include "RecoDataProducts/inc/StrawHitFlagCollection.hh"
// diagnostics
#include "DataProducts/inc/threevec.hh"

#include "CalPatRec/inc/DeltaFinder_types.hh"

#include "CalPatRec/inc/LsqSums2.hh"
#include "CalPatRec/inc/ModuleHistToolBase.hh"
#include "art/Utilities/make_tool.h"

#include <algorithm>
#include <cmath>
#include "CLHEP/Vector/ThreeVector.h"
#include "GeneralUtilities/inc/TwoLinePCA.hh"

using namespace std; 
using CLHEP::Hep3Vector;

namespace mu2e {
  
  using namespace DeltaFinderTypes;

  class DeltaFinder: public art::EDProducer {
  
  protected:

    struct ChannelID {
      int Station;
      int Plane; 
      int Face; 
      int Panel; 
      int Layer;
    };

    ChannelID C_X;
    ChannelID C_O;
//-----------------------------------------------------------------------------
// NStations stations, 4-1=3 faces (for hit w/ lower z), 3 panels (for hit w/ lower z)
// 2017-07-27 P.Murat: the 2nd dimension should be 3, right? 
//-----------------------------------------------------------------------------
//    PanelZ_t                  _oTracker[kNStations][kNFaces][kNPanelsPerFace];
//-----------------------------------------------------------------------------
// talk-to parameters: input collections and algorithm parameters
//-----------------------------------------------------------------------------
    art::InputTag                       _shTag;
    art::InputTag                       _shfTag;
    art::InputTag                       _shpTag;
    art::InputTag                       _mcDigisTag;
    float                               _minHitTime;           // min hit time
    float                               _maxDt;                // max deltaT
    int                                 _minNFacesWithHits;    // per station per seed
    int                                 _minNSeeds;            // min number of seeds in the delta electron cluster
    float                               _maxElectronHitEnergy; // 
    float                               _maxChi2Stereo;        //
    float                               _maxChi2Neighbor;      //
    float                               _maxChi2Radial;        //
    float                               _maxDxy;
    int                                 _maxGap;
    float                               _sigmaR;

    int                                 _debugLevel;
    int                                 _diagLevel;
    int                                 _testOrder;
    std::unique_ptr<ModuleHistToolBase> _hmanager;
//-----------------------------------------------------------------------------
// cache event/geometry objects
//-----------------------------------------------------------------------------
    const StrawHitCollection*           _shcol;
    const StrawHitFlagCollection*       _shfcol;
    const StrawHitPositionCollection*   _shpcol;

    StrawHitFlagCollection*             _bkgfcol;  // output collection

    const TTracker*                     _tracker;

    DeltaFinderTypes::Data_t            _data;              // all data used
    int                                 _testOrderPrinted;
//-----------------------------------------------------------------------------
// functions
//-----------------------------------------------------------------------------
  public:
    explicit     DeltaFinder(fhicl::ParameterSet const&);
    virtual      ~DeltaFinder();

    void         orderID  (ChannelID* X, ChannelID* Ordered);
    void         deOrderID(ChannelID* X, ChannelID* Ordered);
    void         testOrderID  ();
    void         testdeOrderID();

    bool         findData     (const art::Event&  Evt);

    void         orderHits ();

    void         findSeeds (int Station, int Face);
    void         findSeeds ();
    
    void         getNeighborHits(DeltaSeed* Seed, int Face, int Face2, PanelZ_t* panelz);

    void         getIntersect(const StrawHit* sh, const StrawHit* sh2, CLHEP::Hep3Vector* pos);

    void         pruneSeeds     (int Station);
    int          checkDuplicates(int Station,
				 int Face1, const StrawHit* Hit1,
				 int Face2, const StrawHit* Hit2);

    void         connectSeeds  ();
    void         runDeltaFinder();
//-----------------------------------------------------------------------------
// overloaded methods of the module class
//-----------------------------------------------------------------------------
    virtual void beginJob();
    virtual void beginRun(art::Run& ARun);
    virtual void produce( art::Event& e);
//-----------------------------------------------------------------------------
// do these need to be private ?
//-----------------------------------------------------------------------------
  };

  //-----------------------------------------------------------------------------
  DeltaFinder::DeltaFinder(fhicl::ParameterSet const& pset): 
    _shTag                 (pset.get<string>       ("strawHitCollectionTag"        )),
    _shfTag                (pset.get<string>       ("strawHitFlagCollectionTag"    )),
    _shpTag                (pset.get<string>       ("strawHitPositionCollectionTag")),
    _mcDigisTag            (pset.get<art::InputTag>("strawDigiMCCollectionTag"     )),
    			        
    _minHitTime            (pset.get<float>        ("minHitTime"                   )),
    _maxDt                 (pset.get<float>        ("maxDt"                        )),
    _minNFacesWithHits     (pset.get<int>          ("minNFacesWithHits"            )),
    _minNSeeds             (pset.get<int>          ("minNSeeds"                    )),
    _maxElectronHitEnergy  (pset.get<float>        ("maxElectronHitEnergy"         )),
    _maxChi2Stereo         (pset.get<float>        ("maxChi2Stereo"                )),
    _maxChi2Neighbor       (pset.get<float>        ("maxChi2Neighbor"              )),
    _maxChi2Radial         (pset.get<float>        ("maxChi2Radial"                )),
    _maxDxy                (pset.get<float>        ("maxDxy"                       )),
    _maxGap                (pset.get<int>          ("maxGap"                       )),
    _sigmaR                (pset.get<float>        ("sigmaR"                       )),

    _debugLevel            (pset.get<int>          ("debugLevel"                   )),
    _diagLevel             (pset.get<int>          ("diagLevel"                    )),
    _testOrder             (pset.get<int>          ("testOrder"                    ))
  {
    produces<StrawHitFlagCollection>();
    
    _testOrderPrinted = 0;
    
    if (_diagLevel != 0) _hmanager = art::make_tool<ModuleHistToolBase>(pset.get<fhicl::ParameterSet>("diagPlugin"));
    else                 _hmanager = std::make_unique<ModuleHistToolBase>();
  }

  DeltaFinder::~DeltaFinder() {
  }

//-----------------------------------------------------------------------------
  void DeltaFinder::beginJob() {
    if (_diagLevel > 0) {
      art::ServiceHandle<art::TFileService> tfs;
      _hmanager->bookHistograms(tfs);
    }
  }

//-----------------------------------------------------------------------------
// create a Z-ordered map of the tracker
//-----------------------------------------------------------------------------
  void DeltaFinder::beginRun(art::Run& aRun) {
    mu2e::GeomHandle<mu2e::TTracker> ttHandle;
    _tracker      = ttHandle.get();
    _data.tracker = _tracker;
    
    for (int ist=0; ist<_tracker->nStations(); ist++) {
      const Station* st = &_tracker->getStation(ist);
      for (int ipl=0; ipl<st->nPlanes(); ipl++) {
	const Plane* pln = &st->getPlane(ipl);
	for (int ipn=0; ipn<pln->nPanels(); ipn++) {
	  const Panel* panel = &pln->getPanel(ipn);
	  int face;
	  if(panel->id().getPanel() % 2 == 0) face = 0;
	  else face = 1;
	  for (int il=0; il<panel->nLayers(); il++) {
	    C_X.Station = ist;
	    C_X.Plane   = ipl;
	    C_X.Face    = face;
	    C_X.Panel   = ipn;
	    C_X.Layer   = il;
	    orderID(&C_X, &C_O);
	    int os = C_O.Station; 
	    int of = C_O.Face;
	    int op = C_O.Panel;
	    _data.oTracker[os][of][op].fPanel = panel;	  
	  }
	}	
      }
    }
//-----------------------------------------------------------------------------
// it is enough to print that once
//-----------------------------------------------------------------------------
    if (_testOrder && (_testOrderPrinted == 0)) {
      testOrderID  ();
      testdeOrderID();
      _testOrderPrinted = 1;
    }
  }

//------------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
  void DeltaFinder::orderHits() {
    
    int hitsize = _shcol->size(); 
    for (int h=0; h<hitsize; ++h) {
      const StrawHit*         sh  = &_shcol->at(h);
      const StrawHitPosition* shp = &_shpcol->at(h);
      StrawIndex si               = sh->strawIndex();
      const Straw& straw          = _tracker->getStraw(si);
      C_X.Station                 = straw.id().getStation();
                                              // get original location
      C_X.Plane   = straw.id().getPlane() % 2;
      C_X.Face    = -1;
      C_X.Panel   = straw.id().getPanel();
      C_X.Layer   = straw.id().getLayer();
					      // get Z-ordered location
      orderID(&C_X, &C_O);
     
      int os       = C_O.Station; 
      int of       = C_O.Face;
      int op       = C_O.Panel;
      int ol       = C_O.Layer;
      PanelZ_t* pz = &_data.oTracker[os][of][op];

      if ((os < 0) || (os >= kNStations     )) printf(" >>> ERROR: wrong station number: %i\n",os);
      if ((of < 0) || (of >= kNFaces        )) printf(" >>> ERROR: wrong face    number: %i\n",of);
      if ((op < 0) || (op >= kNPanelsPerFace)) printf(" >>> ERROR: wrong panel   number: %i\n",op);
      if ((ol < 0) || (ol >= 2              )) printf(" >>> ERROR: wrong layer   number: %i\n",ol);

      pz->fHitData[ol].push_back(HitData_t(sh,shp));
    }
  }
  
//-----------------------------------------------------------------------------
  void  DeltaFinder::runDeltaFinder() {

    for (int s=0; s<kNStations; ++s) {
      for (int f=0; f<kNFaces; ++f) { 
	for (int p=0; p<3; ++p) {
	  PanelZ_t* panelz = &_data.oTracker[s][f][p];
	  for (int l=0; l<2; ++l) {
	    panelz->fHitData[l].clear() ;
	  }
	}
      }
    }
    orderHits();
    findSeeds();
    connectSeeds();
  }

//-----------------------------------------------------------------------------
bool DeltaFinder::findData(const art::Event& Evt) {
    _shcol    = 0;
    _shpcol   = 0;
    _shfcol   = NULL;

    auto shH  = Evt.getValidHandle<StrawHitCollection>(_shTag);
    _shcol    = shH.product();

    auto shfH = Evt.getValidHandle<StrawHitFlagCollection>(_shfTag);
    _shfcol   = shfH.product();

    auto shpH = Evt.getValidHandle<StrawHitPositionCollection>(_shpTag);
    _shpcol   = shpH.product();

    return (_shcol != 0) && (_shcol->size() > 0) && (_shpcol != 0);     
  }

//-----------------------------------------------------------------------------
  void DeltaFinder::produce(art::Event& Event) {

    if (_debugLevel) printf(">>> DeltaFinder::produce  event number: %10i\n",Event.event());  
//-----------------------------------------------------------------------------
// clear memory in the beginning of event processing and cache event pointer
//-----------------------------------------------------------------------------
    _data.event  = &Event;

    _data.nseeds = 0;
    
    for (int is=0; is<kNStations; is++) {
      _data.nseeds_per_station[is] = 0;
      _data.seedHolder[is].clear();
    }

    _data.deltaCandidateHolder.clear();
//-----------------------------------------------------------------------------
// process event
//-----------------------------------------------------------------------------
    if (! findData(Event)) {
      const char* message = "mu2e::DeltaFinder_module::produce: data missing or incomplete";
      throw cet::exception("RECO")<< message << endl;
    }
    
    runDeltaFinder();
//-----------------------------------------------------------------------------
// form output - copy input flag collection - do we need it ?
//-----------------------------------------------------------------------------
    unique_ptr<StrawHitFlagCollection> bkgfcol(new StrawHitFlagCollection(*_shfcol));
    _bkgfcol = bkgfcol.get();

    const StrawHit* sh0 = &_shcol->at(0);

    StrawHitFlag deltamask(StrawHitFlag::bkg);

    int ndeltas = _data.deltaCandidateHolder.size();

    for (int i=0; i<ndeltas; i++) {
      DeltaCandidate* dc = &_data.deltaCandidateHolder.at(i);
      for (int station=dc->st_start; station<=dc->st_end; station++) {
	DeltaSeed* ds = dc->seed[station];
	if (ds != NULL) {
//-----------------------------------------------------------------------------
// loop over the hits and flag each of them as delta
//-----------------------------------------------------------------------------
	  for (int face=0; face<kNFaces; face++) {
	    int nh = ds->NHits(face);
	    for (int ih=0; ih<nh; ih++) {
	      const StrawHit* sh = ds->Hit(face,ih);
	      int loc = sh-sh0;
	      bkgfcol->at(loc).merge(deltamask);
	    }
	  }
	}
      }
    }

    Event.put(std::move(bkgfcol));
//-----------------------------------------------------------------------------
// in the end of event processing fill diagnostic histograms
//-----------------------------------------------------------------------------
    if (_diagLevel  > 0) _hmanager->fillHistograms(&_data);
    if (_debugLevel > 0) _hmanager->debug(&_data);
  }

//-----------------------------------------------------------------------------
  void DeltaFinder::orderID(ChannelID* X, ChannelID* O) {
    if (X->Panel % 2 == 0) X->Face = 0;
    else                   X->Face = 1; // define original face
    
    O->Station = X->Station; // stations already ordered
    O->Plane   = X->Plane;   // planes already ordered, but not necessary for ordered construct

    if (X->Station % 2 == 0) {
      if (X->Plane == 0) O->Face = 1 - X->Face;
      else               O->Face = X->Face + 2;
    }
    else {
      if (X->Plane == 0) O->Face = X->Face;
      else               O->Face = 3 - X->Face; // order face
    }
    
    O->Panel = int(X->Panel/2);                // order panel

    int n = X->Station + X->Plane + X->Face;   // pattern has no intrinsic meaning, just works
    if (n % 2 == 0) O->Layer = 1 - X->Layer;
    else            O->Layer = X->Layer;       // order layer    
  }
  
//-----------------------------------------------------------------------------
  void DeltaFinder::deOrderID(ChannelID* X, ChannelID* O) {
    
    X->Station = O->Station;
    
    X->Plane   = O->Plane;
    
    if(O->Station % 2 ==  0) {
      if(O->Plane == 0) X->Face = 1 - O->Face;
      else X->Face = O->Face - 2;
    }
    else {
      if(O->Plane == 0) X->Face = O->Face;
      else X->Face = 3 - O->Face;
    }

    if(X->Face == 0) X->Panel = O->Panel * 2;
    else X->Panel = 1 + (O->Panel * 2);
    
    int n = X->Station + X->Plane + X->Face;
    if(n % 2 == 0) X->Layer = 1 - O->Layer;
    else X->Layer = O->Layer;
  }
 
//-----------------------------------------------------------------------------
// testOrderID & testdeOrderID not used in module, only were used to make sure OrderID and deOrderID worked as intended   
//-----------------------------------------------------------------------------
  void DeltaFinder::testOrderID() {

    ChannelID x, o;
    
    for (int s=0; s<2; ++s) {
      for (int pl=0; pl<2; ++pl) {
	for (int pa=0; pa<6; ++pa) {
	  for (int l=0; l<2; ++l) {
	    x.Station = s;
	    x.Plane   = pl;
	    x.Panel   = pa;
	    x.Layer   = l;
	    orderID(&x, &o);
	    printf(" testOrderID: Initial(station = %i, plane = %i, face = %i, panel = %i, layer = %i)", 
		   x.Station, x.Plane, x.Face, x.Panel, x.Layer);
	    printf("  Ordered(station = %i, plane = %i, face = %i, panel = %i, layer = %i)\n",
		   o.Station, o.Plane, o.Face, o.Panel, o.Layer);
	  }
	}
      }
    }
  }	  

//-----------------------------------------------------------------------------
  void DeltaFinder::testdeOrderID() {

    ChannelID x, o;

    for (int s=0; s<2; ++s) {
      for (int f=0; f<4; ++f) {
	for (int pa=0; pa<3; ++pa) {
	  for (int l=0; l<2; ++l) {
	    
	    o.Station          = s;
	    o.Face             = f;
	    if (f < 2) o.Plane = 0;
	    else       o.Plane = 1;
	    o.Panel            = pa;
	    o.Layer            = l;
	    
	    deOrderID(&x, &o);
	    
	    printf(" testdeOrderID: Initial(station = %i, plane = %i, face = %i, panel = %i, layer = %i)", 
		   x.Station,x.Plane,x.Face,x.Panel,x.Layer);
	    printf("  Ordered(station = %i, plane = %i, face = %i, panel = %i, layer = %i)\n",
		   o.Station,o.Plane,o.Face,o.Panel,o.Layer);
	  }
	}
      }
    }
  }

//-----------------------------------------------------------------------------
// function to get neighboring straws w/ hits contained in them +- 7 mm
// strawhit->strawindex->strawid->straw->radius
// input: straw & panelz
// output: list of pointers to hits
// HitHolder is assumed to be empty 
// when picking up neighboring hits need to have them ordered in distance from the seed hit
//-----------------------------------------------------------------------------
  void DeltaFinder::getNeighborHits(DeltaSeed* Seed, int Face, int Face2, PanelZ_t* panelz) {
    
    const StrawHit                  *sh1, *sh2;

    struct WD_t {
      HitData_t*   hd;
      float        dr;
      
      WD_t(HitData_t* Hd, float Dr) { hd = Hd; dr = Dr; }
    };
    
    vector<WD_t>         hits;

    sh1                 = Seed->Hit(Face,0);
    StrawIndex si1      = sh1->strawIndex();
    const Straw& straw1 = _tracker->getStraw(si1);
    float minrad        = straw1.getMidPoint().perp();
    float maxrad        = minrad;
    
    for (int l=0; l<2; ++l) {
      int nh = panelz->fHitData[l].size();
      for (int h=0; h<nh; ++h) {
	HitData_t* hd      = &panelz->fHitData[l].at(h);
	const StrawHit* sh = hd->fHit;
	if (sh == sh1) continue ;
	if ((sh->time()-Seed->fMinTime > _maxDt) || (Seed->fMaxTime-sh->time() > _maxDt)) continue;
	//	const StrawHitPosition* shp = hd->fPos;
	StrawIndex si               = sh->strawIndex();
	const Straw& straw          = _tracker->getStraw(si);
	float rad                   = straw.getMidPoint().perp();
	float dr                    = fabs(rad-minrad);
	hits.push_back(WD_t(hd,dr));
      }
    }
//-----------------------------------------------------------------------------
// sorted hits in dr wrt the seed hit. 
// I know that sorting could be done with more elegance, don't care at the moment
// also the diagnostics data could be fully separated from the data itself
//-----------------------------------------------------------------------------
    int nhits = hits.size();
    for (int i=0; i<nhits-1; i++) {
      WD_t* hi = &hits.at(i);
      for (int j=i+1; j<nhits; j++) {
	WD_t* hj = &hits.at(j);
	if (hi->dr >= hj->dr) {
	  float      dri = hi->dr;
	  HitData_t* hdi = hi->hd;
	  
	  hi->dr = hj->dr;
	  hi->hd = hj->hd;

	  hj->hd = hdi;
	  hj->dr = dri;
	}
      }
    }
//-----------------------------------------------------------------------------
// hits are orders in distance from the first pre-seed hit
// loop over them again, 'sh2' - the second seed hit
//-----------------------------------------------------------------------------
    sh2 = Seed->Hit(Face2,0);

    for (int i=0; i<nhits; i++) {
      const StrawHit* sh = hits[i].hd->fHit;
      StrawIndex      si = sh->strawIndex();
      const Straw& straw = _tracker->getStraw(si);
      float rad          = straw.getMidPoint().perp();
      if ((minrad-rad > 10) || (rad-maxrad > 10) ) continue;
//-----------------------------------------------------------------------------
// radially we're OK, check distance from the intersection
//-----------------------------------------------------------------------------
      CLHEP::Hep3Vector pos;
      getIntersect(sh, sh2, &pos);

      const StrawHitPosition* shp = hits[i].hd->fPos;
      CLHEP::Hep3Vector dxyz = shp->pos()-pos;
      float dxy = sqrt((dxyz.x())*(dxyz.x()) + (dxyz.y())*(dxyz.y())); //change to perp
      float chi2 = (dxy/(shp->posRes(StrawHitPosition::wire)))*(dxy/(shp->posRes(StrawHitPosition::wire)));
      if (chi2 < _maxChi2Neighbor) {
//-----------------------------------------------------------------------------
// OK along the wire, add hit as a neighbor
//-----------------------------------------------------------------------------
	if (rad < minrad) minrad = rad;
	else              maxrad = rad;

	Seed->hitlist[Face].push_back(sh);

	if (sh->time() < Seed->fMinTime) Seed->fMinTime = sh->time();
	if (sh->time() > Seed->fMaxTime) Seed->fMaxTime = sh->time();
      }
    }  
  }

//-----------------------------------------------------------------------------
  void DeltaFinder::getIntersect(const StrawHit* sh, const StrawHit* sh2, CLHEP::Hep3Vector* Pos) {
    const Hep3Vector& wpos1 = _tracker->getStraw(sh->strawIndex()).getMidPoint();
    const Hep3Vector& wdir1 = _tracker->getStraw(sh->strawIndex()).getDirection();
    const Hep3Vector& wpos2 = _tracker->getStraw(sh2->strawIndex()).getMidPoint();
    const Hep3Vector& wdir2 = _tracker->getStraw(sh2->strawIndex()).getDirection();
    TwoLinePCA pca(wpos1,wdir1,wpos2,wdir2);
    *Pos = 0.5*(pca.point1() + pca.point2());
  }


//-----------------------------------------------------------------------------
// make sure the two hits used to make a new seed are not a part of an already found seed
//-----------------------------------------------------------------------------
  int DeltaFinder::checkDuplicates(int Station, int Face1, const StrawHit* Hit1, int Face2, const StrawHit* Hit2) {
    
    bool h1_found(false), h2_found(false);

    int nseeds = _data.seedHolder[Station].size();
    for (int i=0; i<nseeds; i++) {
      DeltaSeed* seed = &_data.seedHolder[Station].at(i);

      int nhits = seed->hitlist[Face1].size();
      for (int ih=0; ih<nhits; ih++) {
	const StrawHit* hit = seed->hitlist[Face1].at(ih);
	if (hit == Hit1) { 
	  h1_found = true;
	  break;
	}
      }

      if (! h1_found) continue;  // with the next seed
//-----------------------------------------------------------------------------
// Hit1 was found, search the same seed for Hit2
//-----------------------------------------------------------------------------
      nhits = seed->hitlist[Face2].size();
      for (int ih=0; ih<nhits; ih++) {
	const StrawHit* hit = seed->hitlist[Face2].at(ih);
	if (hit == Hit2) { 
	  h2_found = true;
	  break;
	}
      }

      if (h2_found) {
//-----------------------------------------------------------------------------
// both Hit1 and Hit2 were found within the same seed
//-----------------------------------------------------------------------------
	return 1;
      }
    }
    return 0;
  }

//-----------------------------------------------------------------------------
// find delta electron seeds in 'Station' with hits in faces 'f' and 'f+1'
// do not consider proton hits with eDep > _minHitEnergy
//-----------------------------------------------------------------------------
  void DeltaFinder::findSeeds(int Station, int Face) {
    const StrawHit* sh0 = &_shcol->at(0);

    for(int p=0; p<3; ++p) {                        // loop over panels
      PanelZ_t* panelz = &_data.oTracker[Station][Face][p];	  
      const CLHEP::Hep3Vector& wdir = panelz->fPanel->straw0Direction();
//-----------------------------------------------------------------------------
// the next four lines could be pre-calculated
//-----------------------------------------------------------------------------
      float wx   = wdir.x();
      float wy   = wdir.y();
      float pphi = atan(-wx/wy);
      if (wy < 0) pphi = pphi + M_PI;               // correct for incomplete arctan answer
      for (int l=0; l<2; ++l) {                     // loop over layers
	int hitsize1 = panelz->fHitData[l].size(); 
	for (int h1=0; h1<hitsize1; ++h1) {             // loop over hits/hit positions
//-----------------------------------------------------------------------------
// hit has not been used yet to start a seed, 
// however it could've been used as a second seed
//-----------------------------------------------------------------------------
	  HitData_t* hd1 = &panelz->fHitData[l].at(h1);
	  const StrawHit* sh = hd1->fHit; 
	  if (sh->energyDep() >= _maxElectronHitEnergy)  continue;
	  if (fabs(sh->dt())  >= 4.                   )  continue;
	  float ct = sh->time();
	  if (ct              <  _minHitTime          )  continue;
	  const StrawHitPosition* shp = hd1->fPos;
	  std::size_t loc1 = sh-sh0;
	  int counter      = 0;                // number of stereo candidates hits close to set up counter
//-----------------------------------------------------------------------------
// loop over the second faces
//-----------------------------------------------------------------------------
	  int fmax = kNFaces;
	  for (int f2=Face+1; f2<fmax; f2++) {
	    for(int p2=0; p2<3; ++p2) {	       // loop over panels
	      PanelZ_t* panelz2 = &_data.oTracker[Station][f2][p2];
	      //-----------------------------------------------------------------------------
	      // check if the two panels overlap in XY
	      //-----------------------------------------------------------------------------
	      const CLHEP::Hep3Vector& twdir = panelz2->fPanel->straw0Direction();
	      float twx   = twdir.x();
	      float twy   = twdir.y();
	      float tpphi = atan(-twx/twy);
	      if(twy < 0) tpphi = tpphi + M_PI;
	      //-----------------------------------------------------------------------------
	      // 2D angle between the vectors pointing to the panel centers, can't be greater than pi
	      //-----------------------------------------------------------------------------
	      float dphi = tpphi - pphi;
	      if (dphi < -M_PI) dphi += 2*M_PI;
	      if (dphi >  M_PI) dphi -= 2*M_PI;
	      if (abs(dphi) >= 2*M_PI/3.) continue;
	      //-----------------------------------------------------------------------------
	      // panels overlap
	      //-----------------------------------------------------------------------------
	      for (int l2=0; l2<2;++l2) {
		int hitsize2 = panelz2->fHitData[l2].size();
		for (int h2=0; h2<hitsize2;++h2) {
		  HitData_t* hd2 = &panelz2->fHitData[l2].at(h2);
		  const StrawHit* sh2 = hd2->fHit;
		  if (sh2->energyDep() >= _maxElectronHitEnergy)  continue;
		  if (fabs(sh2->dt())  >= 4.                   )  continue;
		  float t2 = sh2->time();
		  if (t2 < _minHitTime)                           continue;
		  float dt = abs(t2 - ct);
		  if (dt >= _maxDt)                               continue;
		  ++counter; 	                                    // number of hits close to the first one
		  std::size_t loc2 = sh2-sh0;
		  StereoHit sth(*_shcol, *_tracker, loc1, loc2);
		  const Straw* straw1              = &_tracker->getStraw(sh->strawIndex());
		  const CLHEP::Hep3Vector& s1_midp = straw1->getMidPoint();
		  CLHEP::Hep3Vector sth_dxyz       = sth.pos()-s1_midp;
		  float wdist = sqrt(sth_dxyz.x()*sth_dxyz.x()+sth_dxyz.y()*sth_dxyz.y());
		  if ( wdist >= straw1->getHalfLength())          continue;
		  const StrawHitPosition* shp2 = hd2->fPos;
		  float chi1 = (shp->wireDist()-sth.wdist1())/shp->posRes(StrawHitPosition::wire);
		  float chi2 = (shp2->wireDist()-sth.wdist2())/shp2->posRes(StrawHitPosition::wire);
		  float chi2dof = (chi1*chi1 + chi2*chi2)/2;
		  if (chi1*chi1 >= _maxChi2Stereo)                continue;
		  if (chi2*chi2 >= _maxChi2Stereo)                continue;
		  //-----------------------------------------------------------------------------
		  // check whether there already is a seed containing 'sh' and 'sh2'
		  //-----------------------------------------------------------------------------
		  int is_duplicate = checkDuplicates(Station,Face,sh,f2,sh2);
		  if (is_duplicate)                               continue; 
		  //-----------------------------------------------------------------------------
		  // create new seed
		  //-----------------------------------------------------------------------------
		  DeltaSeed seed;
		  seed.fStation             =  Station;
		  seed.fNumber              =  _data.seedHolder[Station].size();
		  seed.fType                = 10*Face+f2;
		  seed.fNFacesWithHits      = 2;
		  seed.fFaceProcessed[Face] = 1;
		  seed.fFaceProcessed[f2  ] = 1;

		  hd1->fChi2Min    = chi1*chi1;
		  hd2->fChi2Min    = chi2*chi2;
		  hd1->fSeedNumber = seed.fNumber;
		  hd2->fSeedNumber = seed.fNumber;

		  seed.fMinTime = sh->time();
		  if (sh2->time() > seed.fMinTime) {
		    seed.fMaxTime = sh2->time();
		  }
		  else {
		    seed.fMinTime = sh2->time();
		    seed.fMaxTime = sh->time();
		  }
				
		  seed.hitlist[Face].push_back(sh);
		  seed.hitlist[f2].push_back(sh2);

		  getNeighborHits(&seed, Face, f2, panelz);

		  CLHEP::Hep3Vector smpholder(s1_midp); // use precalculated
		  int nh1 = seed.hitlist[Face].size();
		  for(int h3=1; h3<nh1; ++h3) {
		    const StrawHit* sh3 = seed.hitlist[Face].at(h3);
		    smpholder  += _tracker->getStraw(sh3->strawIndex()).getMidPoint();
		  }

		  getNeighborHits(&seed, f2, Face, panelz2);

		  const Straw* straw2 = &_tracker->getStraw(sh2->strawIndex());

		  CLHEP::Hep3Vector smp2holder(straw2->getMidPoint());
		  int nh2 = seed.hitlist[f2].size();
		  for(int h4=1; h4<nh2; ++h4) {
		    const StrawHit* sh4 = seed.hitlist[f2].at(h4);
		    smp2holder += _tracker->getStraw(sh4->strawIndex()).getMidPoint();
		  }
				
		  CLHEP::Hep3Vector CofMsmp1 = smpholder /nh1;
		  CLHEP::Hep3Vector CofMsmp2 = smp2holder/nh2;

		  const Hep3Vector& CofMdir1 = straw1->getDirection();
		  const Hep3Vector& CofMdir2 = straw2->getDirection();
		  TwoLinePCA pca(CofMsmp1, CofMdir1, CofMsmp2, CofMdir2);

		  seed.CofM         = 0.5*(pca.point1() + pca.point2());
		  seed.fHit[0]      = sh;
		  seed.fHit[1]      = sh2;
		  seed.sth          = sth;
		  seed.chi2dof      = chi2dof;
		  seed.panelz[Face] = panelz;
		  seed.panelz[f2]   = panelz2;
		  seed.fNHitsTot    = nh1+nh2;

		  _data.seedHolder[Station].push_back(seed);
		  //-----------------------------------------------------------------------------
		  // book-keeping: increment total number of found seeds
		  //-----------------------------------------------------------------------------
		  _data.nseeds                      += 1;
		  _data.nseeds_per_station[Station] += 1;
		}
	      }
	    }
	  }
//-----------------------------------------------------------------------------
// this is needed for diagnostics only
//-----------------------------------------------------------------------------
	  if (_diagLevel > 0) {
	    hd1->fNSecondHits  = counter ;
	  }
	}
      }
    }
  }

//-----------------------------------------------------------------------------
// some of found seeds could be duplicates or ghosts
// in case two DeltaSeeds share the first seed hit, leave only the best one
// all seeds we're loooping over have been reconstructed within the same 
//-----------------------------------------------------------------------------
  void DeltaFinder::pruneSeeds(int Station) {
    int nseeds =  _data.seedHolder[Station].size();

    for (int i1=0; i1<nseeds-1; i1++) {
      DeltaSeed* ds1 = &_data.seedHolder[Station].at(i1);
      if (ds1->fGood < 0) continue;
      const StrawHit* h1 = ds1->fHit[0];
      for (int i2=i1+1; i2<nseeds; i2++) {
	DeltaSeed* ds2 = &_data.seedHolder[Station].at(i2);
	if (ds2->fGood < 0) continue;
	const StrawHit* h2 = ds2->fHit[0];
	if (h2 == h1) {
//-----------------------------------------------------------------------------
// the two DeltaSeeds share the same first hit - arbitrate
//-----------------------------------------------------------------------------
	  if (ds1->Chi2() < ds2->Chi2()) ds2->fGood = -i1;
	  else {
	    ds1->fGood = -i2;
	    break;
	  }
	}
	else {
//-----------------------------------------------------------------------------
// so far, allow duplicates during the search
// the two DeltaSeeds share could have significantly overlapping hit content
//-----------------------------------------------------------------------------
	  int noverlap = 0;
	  for (int face=0; face<kNFaces; face++) {
	    int nh1 = ds1->hitlist[face].size();
	    for (int ih1=0; ih1<nh1; ih1++) {
	      const StrawHit* hh1 = ds1->hitlist[face][ih1];
	      int nh2 = ds2->hitlist[face].size();
	      for (int ih2=0; ih2<nh2; ih2++) {
		const StrawHit* hh2 = ds2->hitlist[face][ih2];
		if (hh2 == hh1) {
		  noverlap++;
		  break;
		}
	      }
	    }
	  }

	  if ((noverlap >= ds1->fNHitsTot*0.6) || (noverlap >= 0.6*ds2->fNHitsTot)) {
//-----------------------------------------------------------------------------
// overlap significant, leave in only one DeltaSeed - which one? 
//-----------------------------------------------------------------------------
	    if      (ds1->fNHitsTot > ds2->fNHitsTot) ds2->fGood = -i1;
	    else if (ds1->fNHitsTot < ds2->fNHitsTot) {
	      ds1->fGood = -i2;
	      break;
	    }
	    else {
//-----------------------------------------------------------------------------
// the same number of hits, choose candidate with lower chi2
//-----------------------------------------------------------------------------
	      if (ds1->Chi2() < ds2->Chi2()) ds2->fGood = -i1;
	      else    {
		ds1->fGood = -i2;
		break;
	      }
	    }
	  }
	}
      }
    }
  }
     
// -----FindSeeds-----------------------------------------------------------------------------------------------
// TODO: update the time as more hits are added
//-----------------------------------------------------------------------------
  void DeltaFinder::findSeeds() {

    //split off: after face loop, take rest of code into function
    //function takes two faces, returns preseeds for those faces

    for (int s=0; s<kNStations; ++s) { 
      for (int f1=0; f1<kNFaces-1; ++f1) {
	int last = _data.seedHolder[s].size();
	
	findSeeds(s, f1);
//-----------------------------------------------------------------------------
// for seed intersections with hits in faces (f,f+1), find hits in other two faces
//-----------------------------------------------------------------------------
	int nseeds = _data.seedHolder[s].size();
	for (int iseed=last; iseed<nseeds; iseed++) {
	  DeltaSeed* seed  = &_data.seedHolder[s].at(iseed);
//-----------------------------------------------------------------------------
// simultaneously update CoM coordinates
//-----------------------------------------------------------------------------
	  double sx(0), sy(0), snx2(0),snxny(0), sny2(0), snxnr(0), snynr(0);

	  for (int face=f1; face<kNFaces; face++) {
	    int nh = seed->NHits(face);
	    for (int ih=0; ih<nh; ih++) {
	      const StrawHit* hit = seed->Hit(face,ih);
	      const Straw* straw  = &_tracker->getStraw(hit->strawIndex());

	      double x0 = straw->getMidPoint().x();
	      double y0 = straw->getMidPoint().y();
	      double nx = straw->getDirection().x();
	      double ny = straw->getDirection().y();
	      double nr = nx*x0+ny*y0;
	      
	      sx    += x0;
	      sy    += y0;
	      snx2  += nx*nx;
	      snxny += nx*ny;
	      sny2  += ny*ny;
	      snxnr += nx*nr;
	      snynr += ny*nr;
	    }
	  }
//-----------------------------------------------------------------------------
// loop over remaining two faces, 'f2' - face in question
//-----------------------------------------------------------------------------
	  for (int f2=0; f2<kNFaces; f2++) {
	    if (seed->fFaceProcessed[f2] == 1)                              continue;
//-----------------------------------------------------------------------------
// face is different from the two first faces used
//-----------------------------------------------------------------------------
	    for (int p2=0; p2<3; ++p2) {
	      PanelZ_t* panelz              = &_data.oTracker[s][f2][p2];
	      float phi_ps                  = seed->CofM.phi(); //check to find right panel
	      float phi_p                   = panelz->fPanel->straw0MidPoint().phi(); 
	      const CLHEP::Hep3Vector& wdir = panelz->fPanel->straw0Direction();
	      double dphi            = TVector2::Phi_mpi_pi(phi_ps-phi_p);
	      if (fabs(dphi) >= M_PI/3)                                     continue;
//-----------------------------------------------------------------------------
// panel overlaps with the seed, look at its hits
//-----------------------------------------------------------------------------
	      for(int l=0; l<2; ++l) {
		int psize = panelz->fHitData[l].size();
		for (int h=0; h<psize; ++h) { // find hit
		  //-----------------------------------------------------------------------------
		  // 2017-10-05 PM: consider all hits 
		  // hit time should be consistent with the already existing times - the difference
		  // between any two measured hit times should not exceed _maxDt 
		  // (_maxDt of the order of maximal drift time in the straw, slightly higher)
		  //-----------------------------------------------------------------------------
		  HitData_t* hd = &panelz->fHitData[l].at(h);
		  const StrawHit* sh = hd->fHit;

		  if (sh->energyDep()           >= _maxElectronHitEnergy) continue;
		  if (seed->fMaxTime-sh->time() >  _maxDt               ) continue;
		  if (sh->time()-seed->fMinTime >  _maxDt               ) continue;

		  const StrawHitPosition* shp = hd->fPos;
		  CLHEP::Hep3Vector dxyz  = shp->pos()-seed->CofM; // distance from hit to preseed
		  //-----------------------------------------------------------------------------
		  // split into wire parallel and perpendicular components
		  //-----------------------------------------------------------------------------
		  CLHEP::Hep3Vector d_par    = (dxyz.dot(wdir))/(wdir.dot(wdir))*wdir; 
		  CLHEP::Hep3Vector d_perp_z = dxyz-d_par;
		  float  d_perp    = d_perp_z.perp();
		  double sigw      = shp->posRes(StrawHitPosition::wire);
		  float  chi2_par  = (d_par.mag()/sigw)*(d_par.mag()/sigw);
		  float  chi2_perp = (d_perp/_sigmaR)*(d_perp/_sigmaR);
		  float  chi2      = chi2_par + chi2_perp;
		  if (chi2 >= _maxChi2Radial) continue;
		  //-----------------------------------------------------------------------------
		  // add hit
		  //-----------------------------------------------------------------------------
		  hd->fChi2Min = chi2;
		  seed->hitlist[f2].push_back(sh);

		  if (sh->time() < seed->fMinTime) seed->fMinTime = sh->time();
		  if (sh->time() > seed->fMaxTime) seed->fMaxTime = sh->time();

		  seed->fNHitsTot++;
		  //-----------------------------------------------------------------------------
		  // in parallel, update coordinate sums
		  //-----------------------------------------------------------------------------
		  const Straw* straw  = &_tracker->getStraw(sh->strawIndex());

		  double x0 = straw->getMidPoint().x();
		  double y0 = straw->getMidPoint().y();
		  double nx = straw->getDirection().x();
		  double ny = straw->getDirection().y();
		  double nr = nx*x0+ny*y0;
		      
		  sx    += x0;
		  sy    += y0;
		  snx2  += nx*nx;
		  snxny += nx*ny;
		  sny2  += ny*ny;
		  snxnr += nx*nr;
		  snynr += ny*nr;
		}
	      }
	    }
//-----------------------------------------------------------------------------
// update seed time and X and Y coordinates, accurate knowledge of Z is not very relevant
//-----------------------------------------------------------------------------
	    double x_mean, y_mean, nxny_mean, nx2_mean, ny2_mean, nxnr_mean, nynr_mean;

	    x_mean    = sx/seed->fNHitsTot;
	    y_mean    = sy/seed->fNHitsTot;
	    nxny_mean = snxny/seed->fNHitsTot;
	    nx2_mean  = snx2/seed->fNHitsTot;
	    ny2_mean  = sny2/seed->fNHitsTot;
	    nxnr_mean = snxnr/seed->fNHitsTot;
	    nynr_mean = snynr/seed->fNHitsTot;

	    double d = (1-nx2_mean)*(1-ny2_mean)-nxny_mean*nxny_mean;
	    
	    double x0 = ((x_mean-nxnr_mean)*(1-ny2_mean)+(y_mean-nynr_mean)*nxny_mean)/d;
	    double y0 = ((y_mean-nynr_mean)*(1-nx2_mean)+(x_mean-nxnr_mean)*nxny_mean)/d;

	    seed->CofM.setX(x0);
	    seed->CofM.setY(y0);

	    if (seed->hitlist[f2].size() > 0) seed->fNFacesWithHits++;
	    seed->fFaceProcessed[f2] = 1;
	  }
	}
      }
//-----------------------------------------------------------------------------
// prune list of found seeds
//-----------------------------------------------------------------------------
      pruneSeeds(s);
    }
  }

  // unflagging preseed hits if seed is not completed?
  // start with object, fill in as it goes, then decide whether or not to keep it

  // move to other faces, use phi of preseed pos and phi of panels to determine which panel to check
  // loop through hits with time constraint
  // for chi2, use distance along the wire/resolution and distance across the wire/TBD(1 cm?)
  // find a way to accommodate for preseed size

  // Find "average straw" of all candidates in both directions, then find intersection of these two average straws

//-----------------------------------------------------------------------------
// consider only good seeds
//-----------------------------------------------------------------------------
  void DeltaFinder::connectSeeds() {

    //loop over stations
    //start with a seed
    //move to next station
    //loop over seeds, look for one with similar xy and time (split into components impractical?)
    //if multiple, select one with best chi2
    //continue, incrementing over stations until all finished
    //what to do if there are gaps in the path?
    //update center of mass?

    for (int s=0; s<kNStations; ++s) {
      int pssize = _data.seedHolder[s].size();
      for (int ps=0; ps<pssize; ++ps) {
	DeltaSeed* seed = &_data.seedHolder[s].at(ps);
//-----------------------------------------------------------------------------
// create new delta candidate if a seed has >= _minNFacesWithHits
//-----------------------------------------------------------------------------
	if (seed->used     )                                continue;
	if (seed->fGood < 0)                                continue;
	if (seed->fNFacesWithHits < _minNFacesWithHits)     continue;

	DeltaCandidate delta;
	delta.st_used[s] = true;
	delta.seed[s]    = seed;
	delta.CofM       = seed->CofM;
	delta.n_seeds    = 1;
	delta.st_start   = s;
	delta.st_end     = s;
	delta.fNHits     = seed->fNHitsTot;
	delta.fTzSums.addPoint(seed->CofM.z(),seed->fMinTime);
//-----------------------------------------------------------------------------
// stations 6 and 13 are empty - account for that
//-----------------------------------------------------------------------------
	int sdist = 0;
	for (int s2=s+1; s2<kNStations; ++s2) {
	  if ((s2 == 6) || (s2 == 13)) sdist += 1;
	  if (s2-delta.st_end > _maxGap+1+sdist) break;
//-----------------------------------------------------------------------------
// find the closest seed in station s2
//-----------------------------------------------------------------------------
	  DeltaSeed* closest(NULL);
	  float      dxy, dt, dz, predicted_time, dxy_min(_maxDxy);

	  int ps2size = _data.seedHolder[s2].size();
	  for (int ps2=0; ps2<ps2size; ++ps2) {
	    DeltaSeed* seed2 = &_data.seedHolder[s2].at(ps2);
	    if (seed2->used)                                 continue;
	    if (seed2->fGood < 0)                            continue;
	    if (seed2->fNFacesWithHits < _minNFacesWithHits) continue;

	    CLHEP::Hep3Vector dxyz = seed2->CofM-delta.CofM;
	    dxy                    = dxyz.perp();
	    dz                     = dxyz.z();
	    predicted_time         = delta.fTzSums.yMean()+dz*delta.fTzSums.dydx();
	    dt                     = fabs(seed2->fMinTime-predicted_time);
//-----------------------------------------------------------------------------
// one way to define uncertainty
//-----------------------------------------------------------------------------
	    if ((dxy < dxy_min) && (dt < _maxDt)) {
	      closest = seed2;
	      dxy_min = dxy;
	    }
	  }

	  if (closest) {
	    delta.st_end      = s2;
	    delta.dxy    [s2] = dxy_min;
	    delta.seed   [s2] = closest;
	    delta.st_used[s2] = true;
	    delta.CofM        = (delta.CofM*delta.n_seeds+closest->CofM)/(delta.n_seeds+1);
	    delta.n_seeds    += 1;
	    delta.fNHits     += closest->fNHitsTot;
	    delta.fTzSums.addPoint(closest->CofM.z(),closest->fMinTime);
	  }
	}
//-----------------------------------------------------------------------------
// store only delta candidates with more than 2 stations
//-----------------------------------------------------------------------------
	if (delta.n_seeds >= _minNSeeds) {
	  for (int i=0; i<kNStations; i++) {
	    if (delta.seed[i] != NULL) delta.seed[i]->used = true;
	  }
	  delta.fNumber = _data.deltaCandidateHolder.size();
	  _data.deltaCandidateHolder.push_back(delta);
	}
      }
    }
  }
}
//-----------------------------------------------------------------------------
// magic that makes this class a module.
//-----------------------------------------------------------------------------
DEFINE_ART_MODULE(mu2e::DeltaFinder)
//-----------------------------------------------------------------------------
// done
//-----------------------------------------------------------------------------
