// $Id: FlagBkgHits_module.cc, without diagnostics $
// $Author: brownd & mpettee $ 
// $Date: 2016/11/30 $
//
// framework
#include "art/Framework/Principal/Event.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Framework/Principal/Handle.h"
#include "GeometryService/inc/GeomHandle.hh"
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Services/Optional/TFileService.h"
// conditions
#include "ConditionsService/inc/ConditionsHandle.hh"
#include "ConditionsService/inc/TrackerCalibrations.hh"
#include "GeometryService/inc/getTrackerOrThrow.hh"
#include "TTrackerGeom/inc/TTracker.hh"
#include "ConfigTools/inc/ConfigFileLookupPolicy.hh"
// data
#include "RecoDataProducts/inc/StrawHit.hh"
#include "RecoDataProducts/inc/StrawHitPosition.hh"
#include "RecoDataProducts/inc/StrawHitFlag.hh"
#include "RecoDataProducts/inc/BkgCluster.hh"
#include "RecoDataProducts/inc/BkgQual.hh"
// Mu2e
#include "TrkReco/inc/TLTClusterer.hh"
#include "Mu2eUtilities/inc/MVATools.hh"
//CLHEP
#include "CLHEP/Units/PhysicalConstants.h"
// root 
#include "TMath.h"
#include "TH1F.h"
#include "TMVA/Reader.h"
// boost
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/moment.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/weighted_variance.hpp> 
// C++
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <functional>
#include <float.h>
#include <vector>
#include <set>
#include <map>
#include <numeric>
using namespace std; 
using namespace boost::accumulators;
using CLHEP::Hep3Vector;

namespace mu2e 
{

  class FlagBkgHits : public art::EDProducer
  {
    public:
      enum clusterer { TwoLevelThreshold=1};
      explicit FlagBkgHits(fhicl::ParameterSet const&);
      virtual ~FlagBkgHits();
      virtual void beginJob();
      virtual void produce(art::Event& event ); 
    private:
      // configuration parameters
      int _debug;
      int _printfreq;
      // event object labels
      art::InputTag _shtag, _shptag, _shftag;
      // input collections
      const StrawHitCollection* _shcol;
      const StrawHitPositionCollection* _shpcol;
      const StrawHitFlagCollection* _shfcol;
       // how to treat individual hits
      bool _flagall;
      // simple counting cuts
      unsigned _minnhits, _minnstereo, _minnp, _maxisolated;
    // internal helper functions
      bool findData(const art::Event& evt);
      void classifyClusters(BkgClusterCollection& clusters,BkgQualCollection& cquals) const;
      void fillBkgQual(BkgCluster const& cluster,BkgQual& cqual, TTracker const& tt) const;
      void countHits(BkgCluster const& cluster, unsigned& nactive, unsigned& nstereo) const;
      void countPlanes(BkgCluster const& cluster,BkgQual& cqual, TTracker const& tt) const;
      // clusterer
      BkgClusterer* _clusterer;
      // MVA
      double _cperr2;
      bool _useMVA;
      double _bkgMVAcut;
      MVATools _bkgMVA;
      // use TMVA Reader until problems with MVATools is resolved
      mutable TMVA::Reader _reader;
      mutable std::vector<float> _mvavars;
  };

  FlagBkgHits::FlagBkgHits(fhicl::ParameterSet const& pset) :
    _debug(pset.get<int>("debugLevel",0)),
    _printfreq(pset.get<int>("printFrequency",101)),
    _shtag(pset.get<art::InputTag>("StrawHitCollectionLabel","makeSH")),
    _shptag(pset.get<art::InputTag>("StrawHitPositionCollectionLabel","MakeStereoHits")),
    _shftag(pset.get<art::InputTag>("StrawHitFlagCollectionLabel","FlagStrawHits")),
    _flagall(pset.get<bool>("FlagAllHits",true)), // flag all hits in the cluster, regardless of hit assignment
    _minnhits(pset.get<unsigned>("MinActiveHits",5)),
    _minnstereo(pset.get<unsigned>("MinStereoHits",2)),
    _minnp(pset.get<unsigned>("MinNPlanes",4)),
    _maxisolated(pset.get<unsigned>("MaxIsolated",1)),
    _useMVA(pset.get<bool>("UseBkgMVA",true)),
    _bkgMVAcut(pset.get<double>("BkgMVACut",0.8)),
    _bkgMVA(pset.get<fhicl::ParameterSet>("BkgMVA",fhicl::ParameterSet()))
  {
    produces<StrawHitFlagCollection>();
    produces<BkgClusterCollection>();
    produces<BkgQualCollection>();
    // choose clusterer and configure
    clusterer ctype = static_cast<clusterer>(pset.get<int>("Clusterer",TwoLevelThreshold));
    switch ( ctype ) {
      case TwoLevelThreshold:
	_clusterer = new TLTClusterer(pset.get<fhicl::ParameterSet>("TLTClusterer",fhicl::ParameterSet()));
	break;
      default:
	throw cet::exception("RECO")<< "Unknown clusterer" << ctype << endl;
    }
    // pre-compute
    double cperr = pset.get<double>("ClusterPositionError",10.0); // average per-hit position error on cluster center (mm)
    _cperr2 = cperr*cperr;

    std::vector<std::string> varnames(pset.get<std::vector<std::string> >("MVANames"));
    _mvavars.reserve(varnames.size());
    for(auto const& vname : varnames){
      _mvavars.push_back(0.0);
      _reader.AddVariable(vname,&_mvavars.back());
    }
    // get config file from standard locations
    ConfigFileLookupPolicy configFile;
    string weights = configFile(pset.get<std::string>("BkgMVA.MVAWeights"));
    _reader.BookMVA("MLP method", weights);
  }

  FlagBkgHits::~FlagBkgHits(){}

  void FlagBkgHits::beginJob(){
  // initialize the clusterer:
    _clusterer->init();
  // initialize the cluster classification MVA
    if(_useMVA){
      _bkgMVA.initMVA();
      if(_debug > 0){
      cout << "Cluster MVA : " << endl;
      _bkgMVA.showMVA();
    }
}
}

void FlagBkgHits::produce(art::Event& event ) {
  // event printout
  unsigned iev=event.id().event();
  if(_debug > 0 && (iev%_printfreq)==0)cout<<"FlagBkgHits: event="<<iev<<endl;
  // find the data
  if(!findData(event)){
    throw cet::exception("RECO")<< "FlagBkgHits: Missing input collection" << endl;
  }
  // create outputs
  unique_ptr<StrawHitFlagCollection> bkgfcol(new StrawHitFlagCollection(*_shfcol));
  StrawHitFlagCollection* flags = bkgfcol.get();
  unique_ptr<BkgClusterCollection> bkgccol(new BkgClusterCollection);
  BkgClusterCollection* clusters = bkgccol.get();
  unique_ptr<BkgQualCollection> bkgqcol(new BkgQualCollection);
  BkgQualCollection* cquals = bkgqcol.get();
  // find clusters
  _clusterer->findClusters(*clusters,*_shcol,*_shpcol,*_shfcol);
  // classify the clusters in terms of being low-energy electros
  cquals->reserve(clusters->size());
  classifyClusters(*clusters, *cquals);
  // loop over clusters;
  for(auto const& cluster : *clusters) {
  // flag all hits as 'clustered'
    for(auto const& chit : cluster.hits()) 
      flags->at(chit.index()).merge(StrawHitFlag::bkgclust);
  // if the cluster has been flagged as a background cluster, go over the hits
    if(cluster.flag().hasAllProperties(BkgClusterFlag::bkg)){
    //  if hits are 'active' in the cluster, flag them as background in the global event hit flag 
      for(auto const& chit : cluster.hits()) {
	if(_flagall || chit.flag().hasAllProperties(StrawHitFlag::active)){
	  flags->at(chit.index()).merge(StrawHitFlag::bkg);
	}
      }
    }
  // flag small clusters as 'isolated'
    if(cluster.hits().size() <= _maxisolated)
      for(auto const& chit : cluster.hits()) {
	  flags->at(chit.index()).merge(StrawHitFlag::isolated);
      }
  }
  // put the products in the event
  event.put(std::move(bkgccol));
  event.put(std::move(bkgqcol));
  event.put(std::move(bkgfcol));
}

// find the input data objects 
bool FlagBkgHits::findData(const art::Event& evt){
  bool retval(false);
  _shcol = 0; _shpcol = 0; _shfcol = 0;
  art::Handle<mu2e::StrawHitCollection> shH;
  if(evt.getByLabel(_shtag,shH))
    _shcol = shH.product();
  art::Handle<mu2e::StrawHitPositionCollection> shposH;
  if(evt.getByLabel(_shptag,shposH))
    _shpcol = shposH.product();
  art::Handle<mu2e::StrawHitFlagCollection> shflagH;
  if(evt.getByLabel(_shftag,shflagH))
    _shfcol = shflagH.product();
  // don't require stereo hits, they are used only for diagnostics
  retval = _shcol != 0 && _shpcol != 0 && _shfcol != 0;
  return retval;
}

void FlagBkgHits::classifyClusters(BkgClusterCollection& clusters,BkgQualCollection& cquals) const {
  const TTracker& tracker = dynamic_cast<const TTracker&>(getTrackerOrThrow());
// loop over clusters
  for (auto& cluster : clusters) { 
    // create an empty qual
    BkgQual cqual;
    fillBkgQual(cluster,cqual,tracker);
  // set the final flag
    if(cqual.MVAOutput() > _bkgMVAcut) {
      cluster._flag.merge(BkgClusterFlag::bkg);
    }
    // ALWAYS record a quality to keep the vectors in sync.
    cquals.push_back(cqual);
  }
}

void FlagBkgHits::fillBkgQual(BkgCluster const& cluster, BkgQual& cqual, TTracker const& tracker) const {
  // only process clusters which have a minimum number of hits
  unsigned nactive, nstereo;
  countHits(cluster,nactive,nstereo);
  cqual.setMVAStatus(BkgQual::unset);
  if(nactive >= _minnhits && nstereo >= _minnstereo){
    cqual[BkgQual::nhits] = nactive;
    cqual[BkgQual::sfrac] = static_cast<double>(nstereo)/nactive;
    cqual[BkgQual::crho] = cluster.pos().perp();
    // count planes
    countPlanes(cluster,cqual,tracker);
    if(cqual[BkgQual::np] >= _minnp){
      // finish filling the bkgqual
      // compute averages, spreads
      accumulator_set<double, stats<tag::weighted_variance>, double> racc;
      accumulator_set<double, stats<tag::variance(lazy)> > tacc;
      std::vector<double> hz;
      for(auto const& chit : cluster.hits()) {
	if(chit.flag().hasAllProperties(StrawHitFlag::active)){
	  StrawHit const& sh = _shcol->at(chit.index());
	  StrawHitPosition const& shp = _shpcol->at(chit.index());
	  hz.push_back(shp.pos().z());
	  double dt = sh.time() - cluster.time();
	  tacc(dt);
	  // compute the transverse radius of this hit WRT the cluster center
	  Hep3Vector psep = (shp.pos()-cluster.pos()).perpPart();
	  double rho = psep.mag();
	  // project the hit position error along the radial direction to compute the weight
	  Hep3Vector pdir = psep.unit();
	  Hep3Vector tdir(-shp.wdir().y(),shp.wdir().x(),0.0);
	  double rwerr = shp.posRes(StrawHitPosition::wire)*pdir.dot(shp.wdir());
	  double rterr = shp.posRes(StrawHitPosition::trans)*pdir.dot(tdir);
      // include the cluster center position error when computing the weight
	  double rwt = 1.0/sqrt(rwerr*rwerr + rterr*rterr + _cperr2/nactive);
	  racc(rho,weight=rwt);
	}
      }
      cqual[BkgQual::hrho] = extract_result<tag::weighted_mean>(racc);
      cqual[BkgQual::shrho] = sqrt(std::max(extract_result<tag::weighted_variance>(racc),0.0));
      cqual[BkgQual::sdt] = sqrt(std::max(extract_result<tag::variance>(tacc),0.0));
      // find the min, max and gap from the sorted Z positions
      std::sort(hz.begin(),hz.end());
      cqual[BkgQual::zmin] = hz.front();
      cqual[BkgQual::zmax] = hz.back();
      // find biggest Z gap
      double zgap = 0.0;
      for(unsigned iz=1;iz<hz.size();++iz)
	if(hz[iz]-hz[iz-1] > zgap)zgap = hz[iz]-hz[iz-1]; 
      cqual[BkgQual::zgap] = zgap;
      // compute MVA
      cqual.setMVAStatus(BkgQual::filled);
      if(_useMVA){
// reduce values down to what's actually used in the MVA.  This functionality
// should be in MVATool, FIXME!
	_mvavars[0] = cqual.varValue(BkgQual::hrho);
	_mvavars[1] = cqual.varValue(BkgQual::shrho);
	_mvavars[2] = cqual.varValue(BkgQual::crho);
	_mvavars[3] = cqual.varValue(BkgQual::zmin);
	_mvavars[4] = cqual.varValue(BkgQual::zmax);
	_mvavars[5] = cqual.varValue(BkgQual::zgap);
	_mvavars[6] = cqual.varValue(BkgQual::np);
	_mvavars[7] = cqual.varValue(BkgQual::npfrac);
	_mvavars[8] = cqual.varValue(BkgQual::nhits);
	double readerout = _reader.EvaluateMVA( "MLP method" );
//	std::vector<double> mvavars(9,0.0);
//	for(size_t ivar =0;ivar<_mvavars.size(); ++ivar)
//	  mvavars[ivar] = _mvavars[ivar];
//	double toolout = _bkgMVA.evalMVA(mvavars);
//	if(fabs(toolout - readerout) > 0.01) 
//	  cout << "mva disagreement: MVATool " << toolout << " TMVA::Reader: " << readerout << endl;
	cqual.setMVAValue(readerout);
	cqual.setMVAStatus(BkgQual::calculated);
      }
    } else {
      cqual[BkgQual::hrho] = -1.0;
      cqual[BkgQual::shrho] = -1.0;
      cqual[BkgQual::sdt] = -1.0;
      cqual[BkgQual::zmin] = -1.0;
      cqual[BkgQual::zmax] = -1.0;
      cqual[BkgQual::zgap] = -1.0;
    }
  }
}

void FlagBkgHits::countPlanes(BkgCluster const& cluster, BkgQual& cqual, TTracker const& tracker) const {
  // loop over hits and record which planes they are in
  std::vector<int> hitplanes(tracker.nPlanes(),0);
  for(auto const& chit : cluster.hits()) {
    if(chit.flag().hasAllProperties(StrawHitFlag::active)){
      StrawHit const& sh = _shcol->at(chit.index());
	unsigned iplane = (unsigned)(tracker.getStraw(sh.strawIndex()).id().getPlaneId());
	++hitplanes[iplane];
      }
    }
  // work from either end to find the first and last plane
    unsigned ipmin = 0;
    unsigned ipmax = tracker.nPlanes()-1;
    while(hitplanes[ipmin]==0)++ipmin;
    while(hitplanes[ipmax]==0)--ipmax;
    // count the number of plane that should have hits, skipping the missing planes
    unsigned npexp(0);
    // # of planes with hits in this cluster
    unsigned np(0);
    // average number of hits/plane and the # of planes with no hits
    double nphits(0.0);
    vector<Plane> const& planes = tracker.getPlanes();
    for(unsigned ip = ipmin; ip <= ipmax; ++ip) {
      if(planes[ip].exists())++npexp;
      if(hitplanes[ip]> 0)++np;
      nphits += hitplanes[ip];
    }
    // fill qual variables
    cqual[BkgQual::np] = np; 
    cqual[BkgQual::npexp] = npexp; 
    cqual[BkgQual::npfrac] = np/static_cast<float>(npexp); 
    cqual[BkgQual::nphits] = nphits/static_cast<float>(np);
  }

  void FlagBkgHits::countHits(BkgCluster const& cluster, unsigned& nactive, unsigned& nstereo) const {
    nactive = nstereo = 0;
    for(auto const& chit : cluster.hits()) {
      if(chit.flag().hasAllProperties(StrawHitFlag::active)){
	++nactive;
	if(chit.flag().hasAllProperties(StrawHitFlag::stereo))++nstereo;
      }
    }
  }

}

using mu2e::FlagBkgHits;
DEFINE_ART_MODULE(FlagBkgHits);