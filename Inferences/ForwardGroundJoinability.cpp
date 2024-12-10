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
 * @file ForwardGroundJoinability.cpp
 * Implements class ForwardGroundJoinability.
 */

#include "Lib/DHSet.hpp"
#include "Lib/Environment.hpp"
#include "Lib/VirtualIterator.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Ordering.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/ColorHelper.hpp"

#include "Indexing/IndexManager.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include "DemodulationHelper.hpp"

#include "ForwardGroundJoinability.hpp"

namespace Inferences {

using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Saturation;
using namespace std;

namespace {

struct Applicator : SubstApplicator {
  Applicator(ResultSubstitution* subst) : subst(subst) {}
  TermList operator()(unsigned v) const override {
    return subst->applyToBoundResult(v);
  }
  ResultSubstitution* subst;
};

} // end namespace

void ForwardGroundJoinability::attach(SaturationAlgorithm* salg)
{
  ForwardSimplificationEngine::attach(salg);
  _index=static_cast<DemodulationLHSIndex*>(
	  _salg->getIndexManager()->request(DEMODULATION_LHS_CODE_TREE) );

  auto opt = getOptions();
  _preorderedOnly = opt.forwardDemodulation()==Options::Demodulation::PREORDERED;
  _encompassing = opt.demodulationRedundancyCheck()==Options::DemodulationRedundancyCheck::ENCOMPASS;
  _precompiledComparison = opt.demodulationPrecompiledComparison();
  _skipNonequationalLiterals = opt.demodulationOnlyEquational();
  _helper = DemodulationHelper(opt, &_salg->getOrdering());
}

void ForwardGroundJoinability::detach()
{
  _index=0;
  _salg->getIndexManager()->release(DEMODULATION_LHS_CODE_TREE);
  ForwardSimplificationEngine::detach();
}

using Position = Stack<unsigned>;

bool toTheLeftStrict(const Position& p1, const Position& p2, bool& prefix)
{
  prefix = false;
  for (unsigned i = 0; i < p1.size(); i++) {
    if (p2.size() <= i) {
      return false;
    }
    if (p1[i] != p2[i]) {
      return p1[i] < p2[i];
    }
  }
  prefix = true;
  return false;
}

bool isUnderVariablePosition(const Position& p, TermList lhs)
{
  if (lhs.isVar()) {
    return true;
  }
  auto curr = lhs.term();
  for (unsigned i = 0; i < p.size(); i++) {
    ASS_L(p[i],curr->arity());
    auto next = *curr->nthArgument(i);
    if (next.isVar()) {
      return true;
    }
    curr = next.term();
  }
  return false;
}

string posToString(const Position& pos)
{
  string res;
  for (const auto& i : pos) {
    res += "." + Int::toString(i);
  }
  return res;
}

bool ForwardGroundJoinability::perform(Clause* cl, Clause*& replacement, ClauseIterator& premises)
{
  Ordering& ordering = _salg->getOrdering();

  static DHSet<TermList> attempted;

  if (cl->length()>1) {
    return false;
  }

  auto clit = (*cl)[0];

  DHSet<Literal*> litsSeen;

  struct State {
    Literal* lit;
    OrderingComparatorUP comp;
  };

  Stack<State> todo;
  todo.push({ clit, ordering.createComparator() });

  while (todo.isNonEmpty()) {
    auto state = todo.pop();
    attempted.reset();

    auto lit = state.lit;

    if (iterTraits(decltype(litsSeen)::Iterator(litsSeen)).any([lit](Literal* other) {
      return MatchingUtils::isVariant(lit, other);
    })) {
      continue;
    }
    litsSeen.insert(lit);

    state.comp->insert({ { lit->termArg(0), lit->termArg(1), Ordering::EQUAL } });

    PolishSubtermIterator it(lit);
    while (it.hasNext()) {
      TermList t = it.next();
      if (t.isVar()) {
        continue;
      }
      TypedTermList trm = t.term();
      if (!attempted.insert(trm)) {
        continue;
      }

      bool redundancyCheck = _helper.redundancyCheckNeededForPremise(cl, lit, trm);

      auto git = _index->getGeneralizations(trm.term(), /* retrieveSubstitutions */ true);
      while(git.hasNext()) {
        auto qr=git.next();
        ASS_EQ(qr.data->clause->length(),1);

        if(!ColorHelper::compatible(cl->color(), qr.data->clause->color())) {
          continue;
        }

        auto lhs = qr.data->term;
        if (lhs.isVar()) {
          // we are not interested in these for now
          continue;
        }

        TermList rhs = qr.data->rhs;
        bool preordered = qr.data->preordered;

        auto subs = qr.unifier;
        ASS(subs->isIdentityOnQueryWhenResultBound());

        Applicator appl(subs.ptr());

        auto currComp = ordering.createComparator();
        OrderingConstraints cons;
        OrderingConstraints revCons;
        bool revConsValid = true;

        TermList rhsS;

        auto comp = ordering.compare(AppliedTerm(lhs, &appl, true), AppliedTerm(rhs, &appl, true));
        if (comp == Ordering::LESS) {
          continue;
        } else if (comp == Ordering::INCOMPARABLE) {
          rhsS = subs->applyToBoundResult(rhs);
          cons.push({ trm, rhsS, Ordering::GREATER });
          revCons.push({ rhsS, trm, Ordering::GREATER });
        } else {
          revConsValid = false;
        }

        // encompassing demodulation is fine when rewriting the smaller guy
        if (redundancyCheck && _encompassing) {
          // this will only run at most once;
          // could have been factored out of the getGeneralizations loop,
          // but then it would run exactly once there
          Ordering::Result litOrder = ordering.getEqualityArgumentOrder(lit);
          if ((trm==*lit->nthArgument(0) && litOrder == Ordering::LESS) ||
              (trm==*lit->nthArgument(1) && litOrder == Ordering::GREATER)) {
            redundancyCheck = false;
          }
        }

        if (rhsS.isEmpty()) {
          rhsS = subs->applyToBoundResult(rhs);
        }

        if (redundancyCheck) {
          if (!_helper.isPremiseRedundant(cl, lit, trm, rhsS, lhs, &appl)) {
            auto other = EqHelper::getOtherEqualitySide(lit, trm);
            if (ordering.compare(other, rhsS)==Ordering::INCOMPARABLE) {
              cons.push({ other, rhsS, Ordering::GREATER });
              revCons.push({ other, trm, Ordering::GREATER });
            } else {
              continue;
            }
          } else {
            revConsValid = false;
          }
        }

        // s = t    s = r
        // --------------
        //      t = r
        // s = t > s = r & t = r <=> s > r && t > r
        // t = r > s = r & s = t <=> r > s && t > s

        OrderingComparator::Subsumption subsumption(*state.comp.get(), ordering, cons, true);
        if (subsumption.check()) {
          continue;
        }

        state.comp->insert(cons);

        Literal* resLit = EqHelper::replace(lit,trm,rhsS);
        if(EqHelper::isEqTautology(resLit)) {
          continue;
        }

        auto revComp = ordering.createComparator();
        if (revConsValid) {
          revComp->insert(revCons);
        }
        todo.push({ resLit, std::move(revComp) });
      }
    }

    OrderingComparator::Subsumption subsumption(*state.comp.get(), ordering, OrderingConstraints(), true);
    if (!subsumption.check()) {
      return false;
    }
  }

  env.statistics->groundRedundantClauses++;
  return true;
}

}