//
// Namespace for collecting tools used in MC truth evaluation
// Original author: Dave Brown (LBNL) 8/10/2016
//
#ifndef TrkDiag_TrkMCTools_hh
#define TrkDiag_TrkMCTools_hh
#include "MCDataProducts/inc/StrawDigiMC.hh"
#include "MCDataProducts/inc/StrawDigiMCCollection.hh"

#include <vector>
namespace mu2e {
  namespace TrkMCTools {
    // return the earliest StepPointMC associated with the threshold crossing.
    int stepPoint(art::Ptr<StepPointMC>& sp, StrawDigiMC const& mcdigi);
    // determine if a hit was generated by signal (conversion electron)
    bool CEDigi(StrawDigiMC const& mcdigi);
    // determine the sim particle which originated most of the hits specified in the specified set of digis
    unsigned primaryParticle(art::Ptr<SimParticle>& pspp, std::vector<size_t> const& hits, const StrawDigiMCCollection* mcdigis);
    // determine the sim particle corresponding to a digi
    unsigned simParticle(art::Ptr<SimParticle>& spp, StrawDigiMC const& digimc);
  }
}

#endif
