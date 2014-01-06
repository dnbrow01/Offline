#ifndef TTrackerGeom_SupportStructure_hh
#define TTrackerGeom_SupportStructure_hh

//
// A model for the TTracker support structure described in Mu2e-doc-888-v5
// (current as of Dec, 2012).
//
//  $Id: SupportStructure.hh,v 1.2 2014/01/06 20:50:40 kutschke Exp $
//  $Author: kutschke $
//  $Date: 2014/01/06 20:50:40 $
//
//  Original author Rob Kutschke
//

#include <string>
#include <vector>

#include "GeomPrimitives/inc/PlacedTubs.hh"

namespace mu2e {


  class SupportStructure{

    friend class TTrackerMaker;

  public:

    SupportStructure();

    // Accept compiler supplied destructor, copy c'tor and assignment operator.

    // Accessors for device support information
    PlacedTubs const& centerPlate()             const { return _centerPlate;}
    PlacedTubs const& gasUpstream()             const { return _gasUpstream;}
    PlacedTubs const& gasDownstream()           const { return _gasDownstream;}
    PlacedTubs const& innerRing()               const { return _innerRing;}
    PlacedTubs const& outerRingUpstream()       const { return _outerRingUpstream;}
    PlacedTubs const& outerRingDownstream()     const { return _outerRingDownstream;}
    PlacedTubs const& coverUpstream()           const { return _coverUpstream;}
    PlacedTubs const& coverDownstream()         const { return _coverDownstream;}
    PlacedTubs const& innerChannelUpstream()    const { return _innerChannelUpstream;}
    PlacedTubs const& innerChannelDownstream()  const { return _innerChannelDownstream;}
    PlacedTubs const& g10Upstream()             const { return _g10Upstream;}
    PlacedTubs const& g10Downstream()           const { return _g10Downstream;}
    PlacedTubs const& cuUpstream()              const { return _cuUpstream;}
    PlacedTubs const& cuDownstream()            const { return _cuDownstream;}

    // Accessors for outer support structure.
    std::vector<PlacedTubs> const& stiffRings()   const { return _stiffRings;    }
    std::vector<PlacedTubs> const& staveBody()    const { return _staveBody;     }
    std::vector<PlacedTubs> const& staveService() const { return _staveServices; }

    void print ( std::ostream& ost ) const;

  private:

    // The pieces described in Mu2e-doc-888
    PlacedTubs _centerPlate;
    PlacedTubs _gasUpstream;
    PlacedTubs _gasDownstream;
    PlacedTubs _innerRing;
    PlacedTubs _outerRingUpstream;
    PlacedTubs _outerRingDownstream;
    PlacedTubs _coverUpstream;
    PlacedTubs _coverDownstream;

    // Channels in the inner ring to make a place for the straws to fit.
    PlacedTubs _innerChannelUpstream;
    PlacedTubs _innerChannelDownstream;

    // These form a model of the materials inside the space reserved for
    // electronics, cables, power distribution and gas distribution.
    PlacedTubs _g10Upstream;
    PlacedTubs _g10Downstream;
    PlacedTubs _cuUpstream;
    PlacedTubs _cuDownstream;

    // Rings
    std::vector<PlacedTubs> _stiffRings;

    // Each stave is represented by a body plus an average material to represent the
    // services that are present in the cut-out.
    std::vector<PlacedTubs> _staveBody;

    // Not yet implemented:
    std::vector<PlacedTubs> _staveServices;

  };

  inline std::ostream& operator<<(std::ostream& ost,
                                  const SupportStructure& ss ){
    ss.print(ost);
    return ost;
  }

}

#endif /* TTrackerGeom_SupportStructure_hh */
