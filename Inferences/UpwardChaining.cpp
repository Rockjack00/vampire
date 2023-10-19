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
 * @file UpwardChaining.cpp
 * Implements class UpwardChaining.
 */

#include "Lib/Metaiterators.hpp"

#include "Kernel/RobSubstitution.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/SubstHelper.hpp"
#include "Kernel/TermIterators.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Options.hpp"

#include "GoalParamodulation.hpp"
#include "UpwardChaining.hpp"

namespace Inferences {

using namespace Lib;
using namespace Kernel;
using namespace std;

// inference

void UpwardChaining::attach(SaturationAlgorithm* salg)
{
  GeneratingInferenceEngine::attach(salg);
  _leftLhsIndex=static_cast<TermIndex*>(
	  _salg->getIndexManager()->request(LEFT_UPWARD_CHAINING_LHS_INDEX) );
  _rightLhsIndex=static_cast<TermIndex*>(
	  _salg->getIndexManager()->request(RIGHT_UPWARD_CHAINING_LHS_INDEX) );
  _leftSubtermIndex=static_cast<TermIndex*>(
	  _salg->getIndexManager()->request(LEFT_UPWARD_CHAINING_SUBTERM_INDEX) );
  _rightSubtermIndex=static_cast<TermIndex*>(
	  _salg->getIndexManager()->request(RIGHT_UPWARD_CHAINING_SUBTERM_INDEX) );
}

void UpwardChaining::detach()
{
  _leftLhsIndex = 0;
  _rightLhsIndex = 0;
  _leftSubtermIndex = 0;
  _rightSubtermIndex = 0;
  _salg->getIndexManager()->release(LEFT_UPWARD_CHAINING_LHS_INDEX);
  _salg->getIndexManager()->release(RIGHT_UPWARD_CHAINING_LHS_INDEX);
  _salg->getIndexManager()->release(LEFT_UPWARD_CHAINING_SUBTERM_INDEX);
  _salg->getIndexManager()->release(RIGHT_UPWARD_CHAINING_SUBTERM_INDEX);
  GeneratingInferenceEngine::detach();
}

ClauseIterator UpwardChaining::generateClauses(Clause* premise)
{
  auto res = ClauseIterator::getEmpty();
  ASS(_salg->getOptions().goalParamodulationChaining());

  if (premise->length()!=1) {
    return res;
  }

  auto lit = (*premise)[0];
  if (!lit->isEquality() || lit->isNegative()) {
    return res;
  }

  if (premise->goalParamodulationDepth()>=_salg->getOptions().maxGoalParamodulationDepth()) {
    return res;
  }

  const auto& ord = _salg->getOrdering();
  auto sc = shouldChain(lit,ord);

  if (sc) {
    auto comp = ord.getEqualityArgumentOrder(lit);
    ASS(comp != Ordering::INCOMPARABLE && comp != Ordering::EQUAL);
    auto side = lit->termArg((comp == Ordering::LESS || comp == Ordering::LESS_EQ) ? 0 : 1);
    if (side.isTerm()) {
      // 1. left, forward
      // rewrite given unuseful s[r]=t into s[l]=t with indexed not unuseful l=r
      res = pvi(getConcatenatedIterator(res, pvi(iterTraits(vi(new PositionalNonVariableNonTypeIterator(side.term())))
        .flatMap([this](pair<Term*,Position> arg) {
          return pvi(pushPairIntoRightIterator(arg, _leftLhsIndex->getUnifications(arg.first, true)));
        })
        .map([side,lit,premise,this,&ord](pair<pair<Term*,Position>,TermQueryResult> arg) -> Clause* {
          auto rwTerm = arg.first.first;
          auto pos = arg.first.second;
          auto qr = arg.second;
          ASS(!shouldChain(qr.literal, ord));
          return perform(premise,lit,TermList(side),TermList(rwTerm),qr.clause,qr.literal,qr.term,pos,qr.substitution,true,true);
        }))));

      // 2. right, backward
      // rewrite indexed not unuseful s=t[l] into s=t[r] with given unuseful l=r
      res = pvi(getConcatenatedIterator(res, pvi(iterTraits(_rightSubtermIndex->getUnifications(side.term(), true))
        .flatMap([](TermQueryResult arg) {
          auto t = arg.term.term();
          auto t0 = arg.literal->termArg(0);
          auto t1 = arg.literal->termArg(1);
          return pushPairIntoRightIterator(arg,
            pvi(getConcatenatedIterator(
              pvi(pushPairIntoRightIterator(t0,getPositions(t0,t))),
              pvi(pushPairIntoRightIterator(t1,getPositions(t1,t)))
            )));
        })
        .map([side,lit,premise,this,&ord](pair<TermQueryResult,pair<TermList,pair<Term*,Position>>> arg) -> Clause* {
          auto eqLHS = side;
          auto rwSide = arg.second.first;
          auto rwTerm = arg.first.term;
          ASS_EQ(arg.second.second.first,rwTerm.term());
          auto pos = arg.second.second.second;
          auto qr = arg.first;
          ASS(!shouldChain(qr.literal, ord));
          return perform(qr.clause,qr.literal,rwSide,rwTerm,premise,lit,eqLHS,pos,qr.substitution,false,false);
        }))));
    }
  } else {
    // 3. left, backward
    // rewrite indexed unuseful s[r]=t into s[l]=t with given not unuseful l=r
    res = pvi(getConcatenatedIterator(res, pvi(iterTraits(EqHelper::getSuperpositionLHSIterator(lit, ord, _salg->getOptions()))
      .map([lit](TypedTermList lhs) {
        return TypedTermList(EqHelper::getOtherEqualitySide(lit, lhs), SortHelper::getEqualityArgumentSort(lit));
      })
      .flatMap([this](TypedTermList lhs) {
        return pvi(pushPairIntoRightIterator(lhs,_leftSubtermIndex->getUnifications(lhs, true)));
      })
      .flatMap([&ord](pair<TypedTermList,TermQueryResult> arg) {
        auto comp = ord.getEqualityArgumentOrder(arg.second.literal);
        ASS(comp != Ordering::INCOMPARABLE && comp != Ordering::EQUAL);
        auto side = arg.second.literal->termArg((comp == Ordering::LESS || comp == Ordering::LESS_EQ) ? 1 : 0);

        auto t = arg.second.term.term();
        return pushPairIntoRightIterator(arg,pvi(pushPairIntoRightIterator(side,getPositions(side,t))));
      })
      .map([lit,premise,this](pair<pair<TypedTermList,TermQueryResult>,pair<TermList,pair<Term*,Position>>> arg) -> Clause* {
        auto eqLHS = arg.first.first;
        auto side = arg.second.first;
        auto rwTerm = arg.first.second.term;
        ASS_EQ(arg.second.second.first,rwTerm.term());
        auto pos = arg.second.second.second;
        auto qr = arg.first.second;
        return perform(qr.clause,qr.literal,side,rwTerm,premise,lit,eqLHS,pos,qr.substitution,false,true);
      }))));

    // 4. right, forward
    // rewrite given not unuseful s=t[l] into s=t[r] with indexed unuseful l=r
    res = pvi(getConcatenatedIterator(res, pvi(iterTraits(EqHelper::getSuperpositionLHSIterator(lit, ord, _salg->getOptions()))
      .map([lit](TermList t) {
        return TypedTermList(EqHelper::getOtherEqualitySide(lit,t), SortHelper::getEqualityArgumentSort(lit));
      })
      .filter([](TypedTermList lhs) {
        return lhs.isTerm();
      })
      .flatMap([](TypedTermList lhs) {
        return pvi(pushPairIntoRightIterator(lhs.term(), vi(new PositionalNonVariableNonTypeIterator(lhs.term()))));
      })
      .flatMap([this](pair<Term*,pair<Term*,Position>> arg) {
        return pvi(pushPairIntoRightIterator(arg,_rightLhsIndex->getUnifications(arg.second.first, true)));
      })
      .map([lit,premise,this](pair<pair<Term*,pair<Term*,Position>>,TermQueryResult> arg) -> Clause* {
        auto side = arg.first.first;
        auto rwTerm = arg.first.second.first;
        auto pos = arg.first.second.second;
        auto qr = arg.second;
        return perform(premise,lit,TermList(side),TermList(rwTerm),qr.clause,qr.literal,qr.term,pos,qr.substitution,true,false);
      }))));
  }

  return pvi(iterTraits(res)
    .filter(NonzeroFn())
    .timeTraced("upward chaining"));
}

Clause* UpwardChaining::perform(
    Clause* rwClause, Literal* rwLit, TermList rwSide, TermList rwTerm,
    Clause* eqClause, Literal* eqLit, TermList eqLHS, const Position& pos,
    ResultSubstitutionSP subst, bool eqIsResult, bool left)
{
  ASS(rwClause->store()==Clause::ACTIVE);
  ASS(eqClause->store()==Clause::ACTIVE);
  ASS_EQ(rwClause->length(),1);
  ASS_EQ(eqClause->length(),1);
  ASS(rwLit->isEquality()&&rwLit->isPositive());
  ASS(eqLit->isEquality()&&eqLit->isPositive());

  if (rwClause->goalParamodulationDepth()+eqClause->goalParamodulationDepth()>=_salg->getOptions().maxGoalParamodulationDepth()) {
    return 0;
  }

  TermList tgtTerm = EqHelper::getOtherEqualitySide(eqLit, eqLHS);
  Ordering& ordering = _salg->getOrdering();

  TermList tgtTermS = subst->apply(tgtTerm, eqIsResult);

  TermList rwTermS = subst->apply(rwTerm, !eqIsResult);
  TermList rwSideS = subst->apply(rwSide, !eqIsResult);
  TermList otherSide = EqHelper::getOtherEqualitySide(rwLit, rwSide);
  TermList otherSideS = subst->apply(otherSide, !eqIsResult);

  auto rwComp = ordering.compare(rwTermS,tgtTermS);
  auto sideComp = ordering.compare(rwSideS,otherSideS);

  if (left) {
    // rewrite the greater side
    if (sideComp != Ordering::GREATER && sideComp != Ordering::GREATER_EQ) {
      return 0;
    }
    // into a not-smaller term
    if (Ordering::isGorGEorE(rwComp)) {
      return 0;
    }
  } else {
    // rewrite the not-greater side
    if (Ordering::isGorGEorE(sideComp)) {
      return 0;
    }
    // into a not-greater term
    if (rwComp != Ordering::GREATER && rwComp != Ordering::GREATER_EQ) {
      return 0;
    }
  }

  ASS(rwSideS.isTerm());
  auto tgtSideS = replaceOccurrence(rwSideS.term(),rwTermS.term(),tgtTermS,pos);
  Literal* rwLitS = subst->apply(rwLit, !eqIsResult);
  Literal* tgtLitS = Literal::createEquality(true, tgtSideS, otherSideS, SortHelper::getEqualityArgumentSort(rwLitS));

  if(EqHelper::isEqTautology(tgtLitS)) {
    return 0;
  }

  Inference inf(GeneratingInference2(InferenceRule::UPWARD_CHAINING, rwClause, eqClause));
  Clause* res = new(1) Clause(1, inf);
  (*res)[0] = tgtLitS;
  // cout << (eqIsResult ? "forward " : "backward ") << "chain " << *res << endl
  //      << "from " << *rwClause << endl
  //      << "and " << *eqClause << endl << endl;
  // cout << left << " " << eqIsResult << endl;
  res->setGoalParamodulationDepth(rwClause->goalParamodulationDepth()+eqClause->goalParamodulationDepth()+1);
  env.statistics->goalParamodulationChaining++;
  return res;
}

}