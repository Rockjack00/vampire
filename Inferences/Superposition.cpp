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
 * @file Superposition.cpp
 * Implements class Superposition.
 */

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/PairUtils.hpp"
#include "Lib/VirtualIterator.hpp"
#include "Lib/Set.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/ColorHelper.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Ordering.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/LiteralSelector.hpp"
#include "Kernel/RobSubstitution.hpp"

#include "Indexing/Index.hpp"
#include "Indexing/IndexManager.hpp"
#include "Indexing/TermSharing.hpp"
#include "Indexing/CodeTreeInterfaces.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include "Superposition.hpp"
#include "Shell/UnificationWithAbstractionConfig.hpp"

#if VDEBUG
#include <iostream>
#endif
using namespace std;

using namespace Inferences;
using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Saturation;
using std::pair;

void Superposition::attach(SaturationAlgorithm* salg)
{
  GeneratingInferenceEngine::attach(salg);
  _subtermIndex=static_cast<SuperpositionSubtermIndex*> (
	  _salg->getIndexManager()->request(SUPERPOSITION_SUBTERM_SUBST_TREE) );
  _lhsIndex=static_cast<SuperpositionLHSIndex*> (
	  _salg->getIndexManager()->request(SUPERPOSITION_LHS_SUBST_TREE) );
}

void Superposition::detach()
{
  _subtermIndex=0;
  _lhsIndex=0;
  _salg->getIndexManager()->release(SUPERPOSITION_SUBTERM_SUBST_TREE);
  _salg->getIndexManager()->release(SUPERPOSITION_LHS_SUBST_TREE);
  GeneratingInferenceEngine::detach();
}

struct Superposition::ForwardResultFn
{
  ForwardResultFn(Clause* cl, PassiveClauseContainer* passiveClauseContainer, Superposition& parent) : _cl(cl), _passiveClauseContainer(passiveClauseContainer), _parent(parent) {}
  Clause* operator()(pair<pair<Literal*, TypedTermList>, TermQueryResult> arg)
  {
    TermQueryResult& qr = arg.second;
    return _parent.performSuperposition(_cl, arg.first.first, arg.first.second,
	    qr.clause, qr.literal, qr.term, qr.substitution, true, _passiveClauseContainer, qr.constraints);
  }
private:
  Clause* _cl;
  PassiveClauseContainer* _passiveClauseContainer;
  Superposition& _parent;
};


struct Superposition::BackwardResultFn
{
  BackwardResultFn(Clause* cl, PassiveClauseContainer* passiveClauseContainer, Superposition& parent) : _cl(cl), _passiveClauseContainer(passiveClauseContainer), _parent(parent) {}
  Clause* operator()(pair<pair<Literal*, TermList>, TermQueryResult> arg)
  {
    if(_cl==arg.second.clause) {
      return 0;
    }

    TermQueryResult& qr = arg.second;
    return _parent.performSuperposition(qr.clause, qr.literal, qr.term,
	    _cl, arg.first.first, arg.first.second, qr.substitution, false, _passiveClauseContainer, qr.constraints);
  }
private:
  Clause* _cl;
  PassiveClauseContainer* _passiveClauseContainer;
  Superposition& _parent;
};


ClauseIterator Superposition::generateClauses(Clause* premise)
{
  PassiveClauseContainer* passiveClauseContainer = _salg->getPassiveClauseContainer();

  //cout << "SUPERPOSITION with " << premise->toString() << endl;

  //TODO probably shouldn't go here!
  static bool withConstraints = env.options->unificationWithAbstraction()!=Options::UnificationWithAbstraction::OFF;

  auto itf1 = premise->getSelectedLiteralIterator();

  // Get an iterator of pairs of selected literals and rewritable subterms of those literals
  // A subterm is rewritable (see EqHelper) if it is a non-variable subterm of either
  // a maximal side of an equality or of a non-equational literal
  auto itf2 = getMapAndFlattenIterator(itf1,
      [this](Literal* lit)
      // returns an iterator over the rewritable subterms
      { return pushPairIntoRightIterator(lit, env.options->combinatorySup() ? EqHelper::getFoSubtermIterator(lit, _salg->getOrdering())
                                                                            : EqHelper::getSubtermIterator(lit,  _salg->getOrdering())); });

  // Get clauses with a literal whose complement unifies with the rewritable subterm,
  // returns a pair with the original pair and the unification result (includes substitution)
  auto itf3 = getMapAndFlattenIterator(itf2,
      [this](pair<Literal*, TypedTermList> arg)
      { return pushPairIntoRightIterator(arg, _lhsIndex->getUnifications(arg.second, /*retrieveSubstitutions*/ true, withConstraints)); });

  //Perform forward superposition
  auto itf4 = getMappingIterator(itf3,ForwardResultFn(premise, passiveClauseContainer, *this));

  auto itb1 = premise->getSelectedLiteralIterator();
  auto itb2 = getMapAndFlattenIterator(itb1,EqHelper::SuperpositionLHSIteratorFn(_salg->getOrdering(), _salg->getOptions()));
  auto itb3 = getMapAndFlattenIterator(itb2, 
      [this] (pair<Literal*, TermList> arg)
      { return pushPairIntoRightIterator(
              arg, 
              _subtermIndex->getUnifications(TypedTermList(arg.second, SortHelper::getEqualityArgumentSort(arg.first)), 
                /* retrieveSubstitutions */ true, withConstraints)); });

  //Perform backward superposition
  auto itb4 = getMappingIterator(itb3,BackwardResultFn(premise, passiveClauseContainer, *this));

  // Add the results of forward and backward together
  auto it5 = getConcatenatedIterator(itf4,itb4);

  // Remove null elements - these can come from performSuperposition
  auto it6 = getFilteredIterator(it5,NonzeroFn());

  // The outer iterator ensures we update the time counter for superposition
  auto it7 = timeTraceIter("superposition", it6);

  return pvi( it7 );
}

/**
 * Return true iff superposition of @c eqClause into @c rwClause can be performed
 * with respect to colors of the clauses. If the inference is not possible, based
 * on the value of relevant options, report the failure, and/or attempt unblocking
 * the clauses.
 *
 * This function also updates the statistics.
 */
bool Superposition::checkClauseColorCompatibility(Clause* eqClause, Clause* rwClause)
{
  if(ColorHelper::compatible(rwClause->color(), eqClause->color())) {
    return true;
  }
  if(getOptions().showBlocked()) {
    env.beginOutput();
    env.out()<<"Blocked superposition of "<<eqClause->toString()<<" into "<<rwClause->toString()<<std::endl;
    env.endOutput();
  }
  if(getOptions().colorUnblocking()) {
    SaturationAlgorithm* salg = SaturationAlgorithm::tryGetInstance();
    ASS(salg);
    ColorHelper::tryUnblock(rwClause, salg);
    ColorHelper::tryUnblock(eqClause, salg);
  }
  env.statistics->inferencesSkippedDueToColors++;
  return false;
}

/**
 * Return false iff superposition from variable @c eqLHS should not be
 * performed.
 *
 * This function checks that we don't perform superpositions from
 * variables that occur in the remaining part of the clause either in
 * a literal which is not an equality, or in a as an argument of a function.
 * Such situation would mean that there is no ground substitution in which
 * @c eqLHS would be the larger argument of the largest literal.
 */
bool Superposition::checkSuperpositionFromVariable(Clause* eqClause, Literal* eqLit, TermList eqLHS)
{
  ASS(eqLHS.isVar());
  //if we should do rewriting, LHS cannot appear inside RHS
  //ASS_REP(!EqHelper::getOtherEqualitySide(eqLit, eqLHS).containsSubterm(eqLHS), eqLit->toString());

  unsigned clen = eqClause->length();
  for(unsigned i=0; i<clen; i++) {
    Literal* lit = (*eqClause)[i];
    if(lit==eqLit) {
      continue;
    }
    if(lit->isEquality()) {
      for(unsigned aIdx=0; aIdx<2; aIdx++) {
	TermList arg = *lit->nthArgument(aIdx);
	if(arg.isTerm() && arg.containsSubterm(eqLHS)) {
	  return false;
	}
      }
    }
    else if(lit->containsSubterm(eqLHS)) {
      return false;
    }
  }

  return true;
}

/**
 * If the weight of the superposition result will be greater than
 * @c weightLimit, increase the counter of discarded non-redundant
 * clauses and return false. Otherwise return true.
 *
 * The fact that true is returned doesn't mean that the weight of
 * the resulting clause will not be over the weight limit, just that
 * it cannot be cheaply determined at this time.
 */
bool Superposition::earlyWeightLimitCheck(Clause* eqClause, Literal* eqLit,
      Clause* rwClause, Literal* rwLit, TermList rwTerm, TermList eqLHS, TermList eqRHS,
      ResultSubstitutionSP subst, bool eqIsResult, PassiveClauseContainer* passiveClauseContainer, unsigned numPositiveLiteralsLowerBound, const Inference& inf)
{
  unsigned nonInvolvedLiteralWLB=0;//weight lower bound for literals that aren't going to be rewritten

  unsigned rwLength = rwClause->length();
  for(unsigned i=0;i<rwLength;i++) {
    Literal* curr=(*rwClause)[i];
    if(curr!=rwLit) {
      nonInvolvedLiteralWLB+=curr->weight();
    }
  }
  unsigned eqLength = eqClause->length();
  for(unsigned i=0;i<eqLength;i++) {
    Literal* curr=(*eqClause)[i];
    if(curr!=eqLit) {
      nonInvolvedLiteralWLB+=curr->weight();
    }
  }

  //we assume that there will be at least one rewrite in the rwLit
  if(!passiveClauseContainer->fulfilsWeightLimit(nonInvolvedLiteralWLB + eqRHS.weight(), numPositiveLiteralsLowerBound, inf)) {
    env.statistics->discardedNonRedundantClauses++;
    RSTAT_CTR_INC("superpositions weight skipped early");
    return false;
  }

  unsigned lhsSWeight = subst->getApplicationWeight(eqLHS, eqIsResult);
  unsigned rhsSWeight = subst->getApplicationWeight(eqRHS, eqIsResult);
  int rwrBalance = rhsSWeight-lhsSWeight;

  if(rwrBalance>=0) {
    //there must be at least one rewriting, possibly more
    unsigned approxWeight = rwLit->weight()+rwrBalance;
    if(!passiveClauseContainer->fulfilsWeightLimit(nonInvolvedLiteralWLB + approxWeight, numPositiveLiteralsLowerBound, inf)) {
      env.statistics->discardedNonRedundantClauses++;
      RSTAT_CTR_INC("superpositions weight skipped after rewriter weight retrieval");
      return false;
    }
  }
  //if rewrite balance is 0, it doesn't matter how many times we rewrite
  size_t rwrCnt = (rwrBalance==0) ? 0 : rwLit->countSubtermOccurrences(rwTerm);
  if(rwrCnt>1) {
    ASS_GE(rwrCnt, 1);
    unsigned approxWeight = rwLit->weight()+(rwrBalance*rwrCnt);
    if(!passiveClauseContainer->fulfilsWeightLimit(nonInvolvedLiteralWLB + approxWeight, numPositiveLiteralsLowerBound, inf)) {
      env.statistics->discardedNonRedundantClauses++;
      RSTAT_CTR_INC("superpositions weight skipped after rewriter weight retrieval with occurrence counting");
      return false;
    }
  }

  unsigned rwLitSWeight = subst->getApplicationWeight(rwLit, !eqIsResult);

  unsigned finalLitWeight = rwLitSWeight+(rwrBalance*rwrCnt);
  if(!passiveClauseContainer->fulfilsWeightLimit(nonInvolvedLiteralWLB + finalLitWeight, numPositiveLiteralsLowerBound, inf)) {
    env.statistics->discardedNonRedundantClauses++;
    RSTAT_CTR_INC("superpositions weight skipped after rewrited literal weight retrieval");
    return false;
  }

  return true;
}

void getLHSIterator(Literal* lit, ResultSubstitution* subst, bool result, const Ordering& ord, Stack<pair<TermList,TermList>>& sides)
{
  if (!lit->isEquality()) {
    sides.push(make_pair(TermList(lit),TermList(subst->apply(lit,result))));
    return;
  }

  TermList t0=*lit->nthArgument(0);
  TermList t1=*lit->nthArgument(1);
  switch(ord.getEqualityArgumentOrder(lit))
  {
  case Ordering::INCOMPARABLE: {
    // TIME_TRACE("incomparable");
    auto t0S = subst->apply(t0,result);
    auto t1S = subst->apply(t1,result);
    switch(ord.compare(t0S,t1S)) {
      case Ordering::INCOMPARABLE: {
        TIME_TRACE("incomparable inner");
        sides.push(make_pair(t0,t0S));
        sides.push(make_pair(t1,t1S));
        break;
      }
      case Ordering::GREATER:
      case Ordering::GREATER_EQ:
        sides.push(make_pair(t0,t0S));
        break;
      case Ordering::LESS:
      case Ordering::LESS_EQ:
        sides.push(make_pair(t1,t1S));
        break;
      //there should be no equality literals of equal terms
      case Ordering::EQUAL:
        ASSERTION_VIOLATION;
    }
    break;
  }
  case Ordering::GREATER:
  case Ordering::GREATER_EQ:
    sides.push(make_pair(t0,subst->apply(t0,result)));
    break;
  case Ordering::LESS:
  case Ordering::LESS_EQ:
    sides.push(make_pair(t1,subst->apply(t1,result)));
    break;
  //there should be no equality literals of equal terms
  case Ordering::EQUAL:
    ASSERTION_VIOLATION;
  }
}

LeftmostInnermostReducibilityChecker::LeftmostInnermostReducibilityChecker(DemodulationLHSIndex* index, const Ordering& ord, const Options& opt)
: _reducible(), _nonReducible(), _index(index), _ord(ord), _opt(opt) {}

bool LeftmostInnermostReducibilityChecker::check(Clause* cl, Term* rwTermS, ResultSubstitution* subst, bool result)
{
  TIME_TRACE("LeftmostInnermostReducibilityChecker::check");
  switch (_opt.reducibilityCheck()) {
    case Options::ReducibilityCheck::OFF:
      return false;
    case Options::ReducibilityCheck::LEFTMOST_INNERMOST:
      return checkLeftmostInnermost(cl, rwTermS, subst, result);
    case Options::ReducibilityCheck::SMALLER:
      return checkSmaller(cl, rwTermS, subst, result);
  }
  ASSERTION_VIOLATION;
}

bool LeftmostInnermostReducibilityChecker::checkLeftmostInnermost(Clause* cl, Term* rwTermS, ResultSubstitution* subst, bool result)
{
  Stack<pair<TermList,TermList>> sides;
  for (unsigned i = 0; i < cl->numSelected(); i++) {
    sides.reset();
    getLHSIterator((*cl)[i], subst, result, _ord, sides);

    for (auto kv : sides) {
      auto side = kv.second;
      if (side.isVar()) {
        continue;
      }
      if (subst->isRenamingOn2(kv.first, result)) {
        if (!rwTermS->isLiteral() && kv.second.containsSubterm(TermList(rwTermS))) {
          return false;
        }
        continue;
      }
      PolishSubtermIterator nvi(side.term(), &_nonReducible); // we won't get side itself this way, but we don't need it
      while (nvi.hasNext()) {
        auto st = nvi.next();
        if (st.isVar() || _nonReducible.contains(st.term())) {
          continue;
        }
        if (st.term() == rwTermS) {
          // reached rwTerm without finding a reducible term
          return false;
        }
        if (_reducible.contains(st.term())) {
          return true;
        }
        // if (cl->rewrites().find(st.term())) {
        //   TIME_TRACE("reducible by rule");
        //   return true;
        // }
        if (checkTermReducible(st.term())) {
          _reducible.insert(st.term());
          return true;
        }
        _nonReducible.insert(st.term());
      }
      if (side.term() == rwTermS) {
        return false;
      }
    }
  }
  return false;
}

bool LeftmostInnermostReducibilityChecker::checkSmaller(Clause* cl, Term* rwTermS, ResultSubstitution* subst, bool result)
{
  Stack<pair<TermList,TermList>> sides;
  for (unsigned i = 0; i < cl->numSelected(); i++) {
    sides.reset();
    getLHSIterator((*cl)[i], subst, result, _ord, sides);

    for (auto kv : sides) {
      auto side = kv.second;
      if (side.isVar()) {
        continue;
      }
      if (subst->isRenamingOn2(kv.first, result)) {
        continue;
      }
      PolishSubtermIterator nvi(side.term(), &_nonReducible); // we won't get side itself this way, but we don't need it
      while (nvi.hasNext()) {
        auto st = nvi.next();
        if (st.isVar() || _nonReducible.contains(st.term())) {
          continue;
        }
        if (_reducible.contains(st.term())) {
          if (rwTermS->isLiteral() || _ord.compare(TermList(rwTermS),st) == Ordering::Result::GREATER) {
            return true;
          } else {
            continue;
          }
        }
        if (checkTermReducible(st.term())) {
          _reducible.insert(st.term());
          if (rwTermS->isLiteral() || _ord.compare(TermList(rwTermS),st) == Ordering::Result::GREATER) {
            return true;
          }
        } else {
          _nonReducible.insert(st.term());
        }
      }
    }
  }
  return false;
}

bool LeftmostInnermostReducibilityChecker::checkTermReducible(Term* t)
{
  // if (t->reducible()) {
  //   return true;
  // }
  auto it = _index->getGeneralizations(t,true);
  while (it.hasNext()) {
    auto qr = it.next();

    static RobSubstitution subst;
    TypedTermList trm(t);
    bool resultTermIsVar = qr.term.isVar();
    if(resultTermIsVar){
      TermList querySort = trm.sort();
      TermList eqSort = SortHelper::getEqualityArgumentSort(qr.literal);
      subst.reset();
      if(!subst.match(eqSort, 0, querySort, 1)){
        continue;
      }
    }

    TermList rhs=EqHelper::getOtherEqualitySide(qr.literal,qr.term);
    TermList rhsS;
    if(!qr.substitution->isIdentityOnQueryWhenResultBound()) {
      //When we apply substitution to the rhs, we get a term, that is
      //a variant of the term we'd like to get, as new variables are
      //produced in the substitution application.
      TermList lhsSBadVars=qr.substitution->applyToResult(qr.term);
      TermList rhsSBadVars=qr.substitution->applyToResult(rhs);
      Renaming rNorm, qNorm, qDenorm;
      rNorm.normalizeVariables(lhsSBadVars);
      qNorm.normalizeVariables(t);
      qDenorm.makeInverse(qNorm);
      ASS_EQ(TermList(t),qDenorm.apply(rNorm.apply(lhsSBadVars)));
      rhsS=qDenorm.apply(rNorm.apply(rhsSBadVars));
    } else {
      rhsS=qr.substitution->applyToBoundResult(rhs);
    }
    if(resultTermIsVar){
      rhsS = subst.apply(rhsS, 0);
    }

    Ordering::Result argOrder = _ord.getEqualityArgumentOrder(qr.literal);
    bool preordered = argOrder==Ordering::LESS || argOrder==Ordering::GREATER;
    if(!preordered && _ord.compare(TermList(t),rhsS)!=Ordering::GREATER) {
      continue;
    }
    // t->markReducible();
    return true;
  }
  return false;
}

/**
 * If superposition should be performed, return result of the superposition,
 * otherwise return 0.
 */
Clause* Superposition::performSuperposition(
    Clause* rwClause, Literal* rwLit, TermList rwTerm,
    Clause* eqClause, Literal* eqLit, TermList eqLHS,
    ResultSubstitutionSP subst, bool eqIsResult, PassiveClauseContainer* passiveClauseContainer,
    UnificationConstraintStackSP constraints)
{
  TIME_TRACE("perform superposition");
  // we want the rwClause and eqClause to be active
  ASS(rwClause->store()==Clause::ACTIVE);
  ASS(eqClause->store()==Clause::ACTIVE);

  // the first checks the reference and the second checks the stack
  bool hasConstraints = !constraints.isEmpty() && !constraints->isEmpty();
  TermList eqLHSsort = SortHelper::getEqualityArgumentSort(eqLit); 


  if(eqLHS.isVar()) {
    if(!checkSuperpositionFromVariable(eqClause, eqLit, eqLHS)) {
      return 0;
    }
  }

  if(!checkClauseColorCompatibility(eqClause, rwClause)) {
    return 0;
  }

  unsigned rwLength = rwClause->length();
  unsigned eqLength = eqClause->length();
  unsigned conLength = hasConstraints ? constraints->size() : 0;

  TermList tgtTerm = EqHelper::getOtherEqualitySide(eqLit, eqLHS);

  // LRS-specific optimization:
  // check whether we can conclude that the resulting clause will be discarded by LRS since it does not fulfil the age/weight limits (in which case we can discard the clause)
  // we already know the age here so we can immediately conclude whether the clause fulfils the age limit
  // since we have not built the clause yet we compute lower bounds on the weight of the clause after each step and recheck whether the weight-limit can still be fulfilled.

  unsigned numPositiveLiteralsLowerBound = Int::max(eqClause->numPositiveLiterals()-1, rwClause->numPositiveLiterals()); // lower bound on number of positive literals, don't know at this point whether duplicate positive literals will occur
  //TODO update inference rule name AYB
  Inference inf(GeneratingInference2(hasConstraints ? InferenceRule::CONSTRAINED_SUPERPOSITION : InferenceRule::SUPERPOSITION, rwClause, eqClause));
  Inference::Destroyer inf_destroyer(inf);

  bool needsToFulfilWeightLimit = passiveClauseContainer && !passiveClauseContainer->fulfilsAgeLimit(0, numPositiveLiteralsLowerBound, inf) && passiveClauseContainer->weightLimited(); // 0 here denotes the current weight estimate
  if(needsToFulfilWeightLimit) {
    if(!earlyWeightLimitCheck(eqClause, eqLit, rwClause, rwLit, rwTerm, eqLHS, tgtTerm, subst, eqIsResult, passiveClauseContainer, numPositiveLiteralsLowerBound, inf)) {
      return 0;
    }
  }

  Ordering& ordering = _salg->getOrdering();

  TermList eqLHSS = subst->apply(eqLHS, eqIsResult);
  TermList tgtTermS = subst->apply(tgtTerm, eqIsResult);

  Literal* rwLitS = subst->apply(rwLit, !eqIsResult);
  TermList rwTermS = subst->apply(rwTerm, !eqIsResult);

#if VDEBUG
  if(!hasConstraints){
    ASS_EQ(rwTermS,eqLHSS);
  }
#endif

  //cout << "Check ordering on " << tgtTermS.toString() << " and " << rwTermS.toString() << endl;

  //check that we're not rewriting smaller subterm with larger
  auto comp = ordering.compare(tgtTermS,rwTermS);
  if(Ordering::isGorGEorE(comp)) {
    return 0;
  }

  if(rwLitS->isEquality()) {
    //check that we're not rewriting only the smaller side of an equality
    TermList arg0=*rwLitS->nthArgument(0);
    TermList arg1=*rwLitS->nthArgument(1);

    if(!arg0.containsSubterm(rwTermS)) {
      if(Ordering::isGorGEorE(ordering.getEqualityArgumentOrder(rwLitS))) {
        return 0;
      }
    } else if(!arg1.containsSubterm(rwTermS)) {
      if(Ordering::isGorGEorE(Ordering::reverse(ordering.getEqualityArgumentOrder(rwLitS)))) {
        return 0;
      }
    }
  }

  Literal* tgtLitS = EqHelper::replace(rwLitS,rwTermS,tgtTermS);

  static bool doSimS = getOptions().simulatenousSuperposition();

  //check we don't create an equational tautology (this happens during self-superposition)
  if(EqHelper::isEqTautology(tgtLitS)) {
    return 0;
  }

  auto checker = _salg->getReducibilityChecker();

  if (checker) {
    if (checker->check(eqClause,rwTermS.term(),subst.ptr(),eqIsResult)) {
      env.statistics->skippedSuperposition++;
      return 0;
    }

    if (checker->check(rwClause,rwTermS.term(),subst.ptr(),!eqIsResult)) {
      env.statistics->skippedSuperposition++;
      return 0;
    }
  }

  unsigned newLength = rwLength+eqLength-1+conLength;

  static bool afterCheck = getOptions().literalMaximalityAftercheck() && _salg->getLiteralSelector().isBGComplete();

  inf_destroyer.disable(); // ownership passed to the the clause below
  Clause* res = new(newLength) Clause(newLength, inf);

  // If proof extra is on let's compute the positions we have performed
  // superposition on 
  if(env.options->proofExtra()==Options::ProofExtra::FULL){
    /*
    cout << "rwClause " << rwClause->toString() << endl;
    cout << "eqClause " << eqClause->toString() << endl;
    cout << "rwLit " << rwLit->toString() << endl;
    cout << "eqLit " << eqLit->toString() << endl;
    cout << "rwTerm " << rwTerm.toString() << endl;
    cout << "eqLHS " << eqLHS.toString() << endl;
     */
    //cout << subst->toString() << endl;

    // First find which literal it is in the clause, as selection has occured already
    // this should remain the same...?
    vstring rwPlace = Lib::Int::toString(rwClause->getLiteralPosition(rwLit));
    vstring eqPlace = Lib::Int::toString(eqClause->getLiteralPosition(eqLit));

    vstring rwPos="_";
    ALWAYS(Kernel::positionIn(rwTerm,rwLit,rwPos));
    vstring eqPos = "("+eqPlace+").2";
    rwPos = "("+rwPlace+")."+rwPos;

    vstring eqClauseNum = Lib::Int::toString(eqClause->number());
    vstring rwClauseNum = Lib::Int::toString(rwClause->number());

    vstring extra = eqClauseNum + " into " + rwClauseNum+", unify on "+
        eqPos+" in "+eqClauseNum+" and "+
        rwPos+" in "+rwClauseNum;

    //cout << extra << endl;
    //NOT_IMPLEMENTED;

    if (!env.proofExtra) {
      env.proofExtra = new DHMap<const Unit*,vstring>();
    }
    env.proofExtra->insert(res,extra);
  }

  (*res)[0] = tgtLitS;
  int next = 1;
  unsigned weight=tgtLitS->weight();
  for(unsigned i=0;i<rwLength;i++) {
    Literal* curr=(*rwClause)[i];
    if(curr!=rwLit) {
      Literal* currAfter = subst->apply(curr, !eqIsResult);

      if (doSimS) {
        currAfter = EqHelper::replace(currAfter,rwTermS,tgtTermS);
      }

      if(EqHelper::isEqTautology(currAfter)) {
        goto construction_fail;
      }

      if(needsToFulfilWeightLimit) {
        weight+=currAfter->weight();
        if(!passiveClauseContainer->fulfilsWeightLimit(weight, numPositiveLiteralsLowerBound, res->inference())) {
          RSTAT_CTR_INC("superpositions skipped for weight limit while constructing other literals");
          env.statistics->discardedNonRedundantClauses++;
          goto construction_fail;
        }
      }

      if (afterCheck) {
        TIME_TRACE(TimeTrace::LITERAL_ORDER_AFTERCHECK)
        if (i < rwClause->numSelected() && ordering.compare(currAfter,rwLitS) == Ordering::GREATER) {
          env.statistics->inferencesBlockedForOrderingAftercheck++;
          goto construction_fail;
        }
      }

      (*res)[next++] = currAfter;
    }
  }

  {
    Literal* eqLitS = 0;
    if (afterCheck && eqClause->numSelected() > 1) {
      TIME_TRACE(TimeTrace::LITERAL_ORDER_AFTERCHECK);
      eqLitS = Literal::createEquality(true,eqLHSS,tgtTermS,eqLHSsort);
    }

    for(unsigned i=0;i<eqLength;i++) {
      Literal* curr=(*eqClause)[i];
      if(curr!=eqLit) {
        Literal* currAfter = subst->apply(curr, eqIsResult);

        if(EqHelper::isEqTautology(currAfter)) {
          goto construction_fail;
        }
        if(needsToFulfilWeightLimit) {
          weight+=currAfter->weight();
          if(!passiveClauseContainer->fulfilsWeightLimit(weight, numPositiveLiteralsLowerBound, res->inference())) {
            RSTAT_CTR_INC("superpositions skipped for weight limit while constructing other literals");
            env.statistics->discardedNonRedundantClauses++;
            goto construction_fail;
          }
        }

        if (eqLitS && i < eqClause->numSelected()) {
          TIME_TRACE(TimeTrace::LITERAL_ORDER_AFTERCHECK);

          Ordering::Result o = ordering.compare(currAfter,eqLitS);

          if (o == Ordering::GREATER || o == Ordering::GREATER_EQ || o == Ordering::EQUAL) { // where is GREATER_EQ ever coming from?
            env.statistics->inferencesBlockedForOrderingAftercheck++;
            goto construction_fail;
          }
        }

        (*res)[next++] = currAfter;
      }
    }
  }
  if(hasConstraints){
    for(unsigned i=0;i<constraints->size();i++){
      UnificationConstraint con = (*constraints)[i];

      TermList qT = subst->applyTo(con.first.first,con.first.second);
      TermList rT = subst->applyTo(con.second.first,con.second.second);

      TermList sort = SortHelper::getResultSort(rT.term());
      Literal* constraint = Literal::createEquality(false,qT,rT,sort);

      static Options::UnificationWithAbstraction uwa = env.options->unificationWithAbstraction();
      if(uwa==Options::UnificationWithAbstraction::GROUND && 
         !constraint->ground() &&
         (!UnificationWithAbstractionConfig::isInterpreted(qT) 
          && !UnificationWithAbstractionConfig::isInterpreted(rT) )) {

        // the unification was between two uninterpreted things that were not ground 
        res->destroy();
        return 0;
      }

      (*res)[next] = constraint;
      next++;   
    }
  }

  if(needsToFulfilWeightLimit && !passiveClauseContainer->fulfilsWeightLimit(weight, numPositiveLiteralsLowerBound, res->inference())) {
    RSTAT_CTR_INC("superpositions skipped for weight limit after the clause was built");
    env.statistics->discardedNonRedundantClauses++;
    construction_fail:
    res->destroy();
    return 0;
  }

  //TODO update AYB
  if(!hasConstraints){
    if(rwClause==eqClause) {
      env.statistics->selfSuperposition++;
    } else if(eqIsResult) {
      env.statistics->forwardSuperposition++;
    } else {
      env.statistics->backwardSuperposition++;
    }
  } else {
    if(rwClause==eqClause) {
      env.statistics->cSelfSuperposition++;
    } else if(eqIsResult) {
      env.statistics->cForwardSuperposition++;
    } else {
      env.statistics->cBackwardSuperposition++;
    }
  }
  {
    TIME_TRACE("rewrites update");
    auto resRewrites = new DHMap<Term*,TermQueryResult>();
    if (eqClause->rewrites()) {
      DHMap<Term*,TermQueryResult>::Iterator eqIt(*eqClause->rewrites());
      while (eqIt.hasNext()) {
        Term* lhs;
        TermQueryResult qr;
        eqIt.next(lhs,qr);
        auto lhsS = subst->apply(TermList(lhs),eqIsResult);
        resRewrites->insert(lhsS.term(),qr);
      }
    }
    if (rwClause->rewrites()) {
      DHMap<Term*,TermQueryResult>::Iterator rwIt(*rwClause->rewrites());
      while (rwIt.hasNext()) {
        Term* lhs;
        TermQueryResult qr;
        rwIt.next(lhs,qr);
        auto lhsS = subst->apply(TermList(lhs),!eqIsResult);
        resRewrites->insert(lhsS.term(),qr);
      }
    }
    if (comp==Ordering::LESS && eqClause->length()!=1) {
      // cout << "added rule " << rwTermS << " -> " << tgtTermS << endl;
      resRewrites->insert(rwTermS.term(), TermQueryResult(eqLHS,eqLit,eqClause));
    }
    if (resRewrites->isEmpty()) {
      delete resRewrites;
    } else {
      res->setRewrites(resRewrites);
    }
  }

/*
  if(hasConstraints){ 
    cout << "RETURNING " << res->toString() << endl;
    //NOT_IMPLEMENTED;
  }
*/
//  cout << "result " + res->toString() << endl;
  return res;
}
