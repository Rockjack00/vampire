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
 * @file FloorFourierMotzkin.hpp
 * Defines class FloorFourierMotzkin
 *
 */

#ifndef __LASCA_FloorFourierMotzkin__
#define __LASCA_FloorFourierMotzkin__

#include "IntegerFourierMotzkin.hpp"
#include "Coherence.hpp"

namespace Inferences {
namespace LASCA {

using namespace Kernel;
using namespace Indexing; using namespace Saturation;

template<class NumTraits>
struct FloorFourierMotzkinConf
{
  FloorFourierMotzkinConf(std::shared_ptr<LascaState> shared) 
    : _shared(std::move(shared))
  {  }

  using Premise0 = typename IntegerFourierMotzkin<NumTraits>::Premise0;
  using Premise1 = typename IntegerFourierMotzkin<NumTraits>::Premise1;

  using Lhs = Premise0;
  using Rhs = Premise1;

  auto applyRule(
      Premise0 const& prem0, unsigned varBank0,
      Premise1 const& prem1, unsigned varBank1,
      AbstractingUnifier& uwa
      ) const 
  { return applyRule_(prem0, varBank0, 
                      prem1, varBank1, uwa).intoIter(); }

  // prem0:  ⌊s⌋ + t0 > 0
  // prem1: -⌊s⌋ + t1 > 0
  // =========================
  // ⌈1 t0 − 0⌉ + ⌈1 t1 + 0⌉ − 2 > 0 ∨ 1 s + 0 + ⌈1 t0 − 0⌉ − 1 ≈ 0
  Option<Clause*> applyRule_(
      Premise0 const& prem0, unsigned varBank0,
      Premise1 const& prem1, unsigned varBank1,
      AbstractingUnifier& uwa) const
  {
    auto s0 = uwa.subs().apply(prem0.selectedTerm(), varBank0);
    auto s1 = uwa.subs().apply(prem1.selectedTerm(), varBank1);
    if (NumTraits::isFloor(s0) || NumTraits::isFloor(s1)) {
      return IntegerFourierMotzkinConf<NumTraits>::applyRule__(
          prem0, varBank0,
          prem1, varBank1,
          NumTraits::constant(1),
          NumTraits::constantTl(0),
          iterItems<Literal*>(),
          uwa,
          [&](auto lits){
             // TODO not use UnitList here. That's slow
             return Clause::fromIterator(
                std::move(lits),
             // TODO make own inference rule instead of LASCA_INTEGER_FOURIER_MOTZKIN (?)
                Inference(GeneratingInference2(Kernel::InferenceRule::LASCA_INTEGER_FOURIER_MOTZKIN, prem0.clause(), prem1.clause()))
             );
          });
    } else {
      return {};
    }
  }

  std::shared_ptr<LascaState> _shared;
};

template<class NumTraits>
struct FloorFourierMotzkin : public BinInf<FloorFourierMotzkinConf<NumTraits>>  {
  FloorFourierMotzkin(std::shared_ptr<LascaState> state) 
    : BinInf<FloorFourierMotzkinConf<NumTraits>>(state, FloorFourierMotzkinConf<NumTraits>(state)) {}
};

} // namespace LASCA 
} // namespace Inferences 


#endif /*__LASCA_FloorFourierMotzkin__*/
