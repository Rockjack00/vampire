/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file InequalityFactoring.hpp
 * Defines class InequalityFactoring
 *
 */

#ifndef __InequalityFactoring__
#define __InequalityFactoring__

#include "Forwards.hpp"

#include "Inferences/InferenceEngine.hpp"
#include "Kernel/Ordering.hpp"
#include "Indexing/LascaIndex.hpp"
#include "Shell/Options.hpp"

namespace Inferences {
namespace LASCA {

using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

class InequalityFactoring
: public GeneratingInferenceEngine
{
public:
  CLASS_NAME(InequalityFactoring);
  USE_ALLOCATOR(InequalityFactoring);

  InequalityFactoring(InequalityFactoring&&) = default;
  InequalityFactoring(shared_ptr<LascaState> shared)
    : _shared(std::move(shared))
  {  }

  void attach(SaturationAlgorithm* salg) final override;
  void detach() final override;

  template<class NumTraits>
  ClauseIterator generateClauses(Clause* premise, 
    Literal* lit1, LascaLiteral<NumTraits> l1, Monom<NumTraits> j_s1,
    Literal* lit2, LascaLiteral<NumTraits> l2, Monom<NumTraits> k_s2);

  template<class NumTraits>
  Option<Clause*> applyRule(SelectedSummand const& l1, SelectedSummand const& l2);
  Option<Clause*> applyRule(SelectedSummand const& l1, SelectedSummand const& l2);

  template<class NumTraits>
  ClauseIterator generateClauses(
      Clause* premise,
      Literal* lit1, LascaLiteral<NumTraits> L1,
      Literal* lit2, LascaLiteral<NumTraits> L2
    );

  ClauseIterator generateClauses(Clause* premise) final override;
  

#if VDEBUG
  virtual void setTestIndices(Stack<Indexing::Index*> const&) final override;
#endif

private:

  shared_ptr<LascaState> _shared;
};
#define _lascaFactoring true

} // namespace LASCA 
} // namespace Inferences 

#endif /*__InequalityFactoring__*/
