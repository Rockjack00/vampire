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
 * @file IsIntResolution.cpp
 * Implements class IsIntResolution.
 */

#include "IsIntResolution.hpp"
#include "Kernel/LASCA.hpp"
#include "Saturation/SaturationAlgorithm.hpp"
#include "Shell/Statistics.hpp"
#include "Debug/TimeProfiling.hpp"

#define DEBUG(...) //DBG(__VA_ARGS__)

namespace Inferences {
namespace LASCA {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// INDEXING STUFF
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void IsIntResolution::attach(SaturationAlgorithm* salg) 
{
  CALL("IsIntResolution::attach");
  GeneratingInferenceEngine::attach(salg);

  ASS(!_lhsIndex);
  ASS(!_rhsIndex);

  _lhsIndex = static_cast<decltype(_lhsIndex)>(_salg->getIndexManager()->request(LASCA_IS_INT_RESOLUTION_LHS_SUBST_TREE));
  _rhsIndex = static_cast<decltype(_rhsIndex)>(_salg->getIndexManager()->request(LASCA_IS_INT_RESOLUTION_RHS_SUBST_TREE));
  _rhsIndex->setShared(_shared);
  _lhsIndex->setShared(_shared);
}

void IsIntResolution::detach() 
{
  CALL("IsIntResolution::detach");
  ASS(_salg);
  GeneratingInferenceEngine::detach();

  // _index=0;
  // _salg->getIndexManager()->release(LASCA_IS_INT_RESOLUTION_SUBST_TREE);
}

#if VDEBUG
void IsIntResolution::setTestIndices(Stack<Indexing::Index*> const& indices)
{
  _lhsIndex = (decltype(_lhsIndex)) indices[0]; 
  _rhsIndex = (decltype(_rhsIndex)) indices[1]; 
  _lhsIndex->setShared(_shared);
  _rhsIndex->setShared(_shared);
}
#endif

using Lhs = IsIntResolution::Lhs;
using Rhs = IsIntResolution::Rhs;

ClauseIterator IsIntResolution::generateClauses(Clause* premise) 
{
  // TODO refactor so this function is not copied and pasted among all unifying lasca rules
  CALL("IsIntResolution::generateClauses(Clause* premise)")
  ASS(_lhsIndex)
  ASS(_rhsIndex)
  ASS(_shared)
  Stack<Clause*> out;

  for (auto const& lhs : Lhs::iter(*_shared, premise)) {
    DEBUG("lhs: ", lhs)
    for (auto rhs_sigma : _rhsIndex->find(lhs.key())) {
      auto& rhs   = *rhs_sigma.data;
      auto& sigma = rhs_sigma.unifier;
      DEBUG("  rhs: ", rhs)
      auto res = applyRule(lhs, 0, rhs, 1, *sigma);
      if (res.isSome()) {
        out.push(res.unwrap());
      }
    }
  }

  for (auto const& rhs : Rhs::iter(*_shared, premise)) {
    DEBUG("rhs: ", rhs)

    for (auto lhs_sigma : _lhsIndex->find(rhs.key())) {
      auto& lhs   = *lhs_sigma.data;
      auto& sigma = lhs_sigma.unifier;
      if (lhs.clause() != premise) { // <- self application. the same one has been run already in the previous loop
        DEBUG("  lhs: ", lhs)
        auto res = applyRule(lhs, 1, rhs, 0, *sigma);
        if (res.isSome()) {
          out.push(res.unwrap());
        }
      }
    }
  }

  return pvi(ownedArrayishIterator(std::move(out)));
}

Option<Clause*> IsIntResolution::applyRule(
    Lhs const& lhs, unsigned lhsVarBank,
    Rhs const& rhs, unsigned rhsVarBank,
    AbstractingUnifier& uwa
    ) const 
{
  return lhs.numTraits().apply([&](auto numTraits) {
    return applyRule(numTraits, lhs, lhsVarBank, rhs, rhsVarBank, uwa);
  });
}


template<class NumTraits>
Option<Clause*> IsIntResolution::applyRule(NumTraits,
    Lhs const& lhs, unsigned lhsVarBank,
    Rhs const& rhs, unsigned rhsVarBank,
    AbstractingUnifier& uwa
    ) const 
{


  CALL("IsIntResolution::applyRule")
  TIME_TRACE("isInt-resolution")
  auto cnst = uwa.constraintLiterals();
  auto& sigma = uwa.subs();




#define check_side_condition(cond, cond_code)                                                       \
    if (!(cond_code)) {                                                                             \
      DEBUG("side condition not fulfiled: " cond)                                                   \
      return Option<Clause*>();                                                                     \
    }                                                                                               \

    check_side_condition("literals are of the same sort",
        lhs.numTraits() == rhs.numTraits()) // <- we must make this check because variables are unsorted
   
    ASS(lhs.isIsInt())
    ASS(rhs.isIsInt())
    ASS_EQ(lhs.symbol(), LascaPredicate::IS_INT_POS)
    ASS_EQ(lhs.sort(), rhs.sort())

    Stack<Literal*> out( lhs.clause()->size() - 1 // <- C1
                       + rhs.clause()->size() - 1 // <- C2
                       + 1                        // <- k t₁ + j t₂ > 0
                       + cnst->size());      // Cnst


    ASS(!NumTraits::isFractional() || (!lhs.monom().isVar() && !rhs.monom().isVar()))

    // check_side_condition(
    //     "s₁, s₂ are not variables",
    //     !lhs.monom().isVar() && !rhs.monom().isVar())

    auto j = lhs.numeral().unwrap<typename NumTraits::ConstantType>();
    auto k = rhs.numeral().unwrap<typename NumTraits::ConstantType>();

    check_side_condition(
        "k / j ∈ Z",
        (k / j).isInt())

    check_side_condition(
        "symmetry breaking",
        (rhs.symbol() != LascaPredicate::IS_INT_POS || !(j / k).isInt() || lhs < rhs))

    auto L1_sigma = sigma.apply(lhs.literal(), lhsVarBank);
    check_side_condition( 
        "isInt(j s₁ + t₁)σ /⪯ C₁σ",
        lhs.contextLiterals()
           .all([&](auto L) {
             auto L_sigma = sigma.apply(L, lhsVarBank);
             out.push(L_sigma);
             return _shared->notLeq(L1_sigma, L_sigma);
           }));


    auto L2_sigma = sigma.apply(rhs.literal(), rhsVarBank);
    check_side_condition(
        "(~)isInt(k s₂ + t₂)σ /≺ C₂σ",
        rhs.contextLiterals()
           .all([&](auto L) {
             auto L_sigma = sigma.apply(L, rhsVarBank);
             out.push(L_sigma);
             return _shared->notLess(L2_sigma, L_sigma);
           }));


    auto s1_sigma = sigma.apply(lhs.monom(), lhsVarBank);
    auto s2_sigma = sigma.apply(rhs.monom(), rhsVarBank);
    // ASS_REP(_shared->equivalent(s_sigma.term(), s2_sigma().term()), make_pair(s_sigma, s2_sigma()))
    Stack<TermList> t1_sigma(rhs.nContextTerms());
    Stack<TermList> t2_sigma(lhs.nContextTerms());

    check_side_condition(
        "s₁σ /⪯ t₁σ",
        lhs.contextTerms<NumTraits>()
           .all([&](auto ti) {
             auto ti_sigma = sigma.apply(ti.factors->denormalize(), lhsVarBank);
             t1_sigma.push(NumTraits::mulSimpl(ti.numeral, ti_sigma));
             return _shared->notLeq(s1_sigma, ti_sigma);
           }))

    check_side_condition(
        "s₂σ /⪯ t₂σ ",
        rhs.contextTerms<NumTraits>()
           .all([&](auto ti) {
             auto ti_sigma = sigma.apply(ti.factors->denormalize(), rhsVarBank);
             t2_sigma.push(NumTraits::mulSimpl(ti.numeral, ti_sigma));
             return _shared->notLeq(s2_sigma, ti_sigma);
           }))

    auto add = [](auto l, auto r) {
      return l == NumTraits::zero() ? r 
           : r == NumTraits::zero() ? l
           : NumTraits::add(l, r); };

    auto resolventTerm // -> (t₂ - (k / j) t₁)σ
        = add( NumTraits::sum(t2_sigma.iterFifo()),
               NumTraits::mulSimpl(-(k / j), NumTraits::sum(t1_sigma.iterFifo())));

    // (k t₁ + j t₂ > 0)σ
    out.push(LascaPredicateCreateLiteral<NumTraits>(rhs.symbol(), resolventTerm));

    out.loadFromIterator(cnst->iterFifo());

    Inference inf(GeneratingInference2(Kernel::InferenceRule::LASCA_IS_INT_RESOLUTION, lhs.clause(), rhs.clause()));
    auto cl = Clause::fromStack(out, inf);
    DEBUG("out: ", *cl);
    return Option<Clause*>(cl);
}

} // namespace LASCA 
} // namespace Inferences 
