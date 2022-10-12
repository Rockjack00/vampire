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
 * @file MismatchHandler.cpp
 * Defines class MismatchHandler.
 *
 */

#include "Shell/Options.hpp"
#include "Lib/Environment.hpp"
#include "Lib/BiMap.hpp"

#include "Forwards.hpp"
#include "Signature.hpp"
#include "Term.hpp"
#include "RobSubstitution.hpp"
#include "SortHelper.hpp"

#include "MismatchHandler.hpp"
#include "Shell/UnificationWithAbstractionConfig.hpp"

namespace Kernel
{

VSpecVarToTermMap MismatchHandler::_termMap;

typedef Shell::UnificationWithAbstractionConfig uwaconf;

bool UWAMismatchHandler::isConstraintPair(TermList t1, TermList t2, TermList sort)
{
  CALL("UWAMismatchHandler::isConstraintPair");

  if(uwaconf::isNumeral(t1) && uwaconf::isNumeral(t2)){
    return false;
  }

  switch (_mode) {
    case Shell::Options::UnificationWithAbstraction::INTERP_DIFF_TOPS: 
      return isConstraintTerm(t1, sort).maybe() && isConstraintTerm(t2, sort).maybe() &&
             t1.term()->functor() != t2.term()->functor();
    case Shell::Options::UnificationWithAbstraction::INTERP_ONLY:
    case Shell::Options::UnificationWithAbstraction::ONE_INTERP_NO_VARS:
    case Shell::Options::UnificationWithAbstraction::ONE_INTERP:
    case Shell::Options::UnificationWithAbstraction::ONE_SIDE_NL:
      return (isConstraintTerm(t1, sort).isTrue() && isConstraintTerm(t2, sort).maybe())  ||
             (isConstraintTerm(t1, sort).maybe()  && isConstraintTerm(t2, sort).isTrue()) ||
             (isConstraintTerm(t1, sort).isTrue() && isConstraintTerm(t2, sort).isTrue());
    case Shell::Options::UnificationWithAbstraction::OFF:
      ASSERTION_VIOLATION
      return false;
  }
}

TermList UWAMismatchHandler::transformSubterm(TermList trm, TermList sort)
{
  CALL("UWAMismatchHandler::transformSubterm");

  switch (_mode) {
    case Shell::Options::UnificationWithAbstraction::INTERP_DIFF_TOPS: 
      if(isConstraintTerm(trm, sort).maybe()){
        return MismatchHandler::getVSpecVar(trm);
      }
      break;
    case Shell::Options::UnificationWithAbstraction::ONE_SIDE_NL:      
    case Shell::Options::UnificationWithAbstraction::INTERP_ONLY:
    case Shell::Options::UnificationWithAbstraction::ONE_INTERP_NO_VARS:
    case Shell::Options::UnificationWithAbstraction::ONE_INTERP:
      if(isConstraintTerm(trm, sort).isTrue()){
        return MismatchHandler::getVSpecVar(trm);
      }
      break;
    case Shell::Options::UnificationWithAbstraction::OFF:
      ASSERTION_VIOLATION
  } 
  return trm;
}

MaybeBool UWAMismatchHandler::isConstraintTerm(TermList t, TermList sort, bool topLevel){
  CALL("UWAMismatchHandler::isConstraintTerm");
  
  static bool uwaAtTop = env.options->uwaAtTopLevel();

  if(!uwaAtTop && topLevel){
    // the case where we are checking whether a top level term is a constraint term
    // but we don't want to create constraints at the top. Just return false
    return false;
  }

  auto isInterpretedOrPoly = [](TermList sort){
    return sort.isVar() || sort.isIntSort() || sort.isRatSort() || sort.isRealSort();
  };

  switch (_mode) {
    case Shell::Options::UnificationWithAbstraction::ONE_SIDE_NL:{
      if(t.isTerm() && env.signature->getFunction(t.term()->functor())->finalLoopCount()){
        return MaybeBool::UNKNOWN;        
      }
      return uwaconf::isInterpreted(t) && !uwaconf::isNumeral(t);
    }
    case Shell::Options::UnificationWithAbstraction::INTERP_ONLY: {
      return uwaconf::isInterpreted(t);
    }
    case Shell::Options::UnificationWithAbstraction::INTERP_DIFF_TOPS:{
      if(uwaconf::isInterpreted(t)){
        return MaybeBool::UNKNOWN;
      }
      return false;
    }
    case Shell::Options::UnificationWithAbstraction::ONE_INTERP:
      if(t.isVar() && isInterpretedOrPoly(sort)){
        return MaybeBool::UNKNOWN;
      }
      // deliberately no break here
    case Shell::Options::UnificationWithAbstraction::ONE_INTERP_NO_VARS:{
      if(t.isVar()) return false;

      if(uwaconf::isInterpreted(t)){
        return true;
      }
      
      if(isInterpretedOrPoly(sort)){
        return MaybeBool::UNKNOWN;
      } 

      return false;
    }
    case Shell::Options::UnificationWithAbstraction::OFF:
      ASSERTION_VIOLATION
      return false;
  }
  ASSERTION_VIOLATION
}

void MismatchHandler::introduceConstraint(TermList t1,unsigned index1, TermList t2,unsigned index2, 
  UnificationConstraintStack& ucs, BacktrackData& bd, bool recording)
{
  CALL("AtomicMismatchHandler::introduceConstraint");

  auto constraint = make_pair(make_pair(t1,index1),make_pair(t2,index2));
  if(recording){
    ucs.backtrackablePush(constraint, bd);
  } else {
    ucs.push(constraint);
  }
}

AtomicMismatchHandler::~AtomicMismatchHandler() {}

bool MismatchHandler::handle(TermList t1, unsigned index1, TermList t2, unsigned index2, 
  UnificationConstraintStack& ucs,BacktrackData& bd, bool recording)
{
  CALL("MismatchHandler::handle");
  ASS(t1.isVSpecialVar() || t2.isVSpecialVar());

  t1 = t1.isVSpecialVar() ? get(t1.var()) : t1;
  t2 = t2.isVSpecialVar() ? get(t2.var()) : t2;

  // assuming that we never want to create a constraint between 2 variables
  // will probably be proved wrong at some point...
  if(t1.isVar() && t2.isVar()) return false;

  TermList sort = SortHelper::getResultSort(t1.isTerm() ? t1.term() : t2.term());

  // should never be trying to create a constraint between terms of
  // of different sorts
  ASS(t1.isVar() || t2.isVar() || SortHelper::getResultSort(t1.term()) == SortHelper::getResultSort(t2.term()));

  return handle(t1, index1, t2, index2, sort, ucs, bd, recording);
}

bool MismatchHandler::handle(TermList t1, unsigned index1, TermList t2, unsigned index2, 
  TermList sort, UnificationConstraintStack& ucs,BacktrackData& bd, bool recording)
{
  CALL("MismatchHandler::handle");
  ASS(!t1.isVSpecialVar() && !t2.isVSpecialVar());

  for (auto& h : _inners) {
    if(h->isConstraintPair(t1,t2,sort)){
      introduceConstraint(t1,index1,t2,index2,ucs,bd,recording); 
      return true;
    }
  }
  return false;
}

void MismatchHandler::addHandler(unique_ptr<AtomicMismatchHandler> hndlr){
  CALL("MismatchHandler::addHandler");
  _inners.push(std::move(hndlr));
}

MaybeBool MismatchHandler::isConstraintTerm(TermList t, TermList sort, bool topLevel){
  CALL("MismatchHandler::isConstraintTerm");
  
  for (auto& h : _inners) {
    auto res = h->isConstraintTerm(t, sort, topLevel);
    if(!res.isFalse()){
      return res;
    }
  }
  return false; 
}

TermList MismatchHandler::transformSubterm(TermList trm){
  CALL("MismatchHandler::transformSubterm");

  if(_appTerms.size()){
    TermList t = _appTerms.pop();
    if(t.isApplication() && trm == *t.term()->nthArgument(2)){
      _appTerms.push( *t.term()->nthArgument(2));
      return trm;
    }
    _appTerms.push(t);
  }

  TermList sort;
  if(trm.isTerm()){
    sort = SortHelper::getResultSort(trm.term());
  } else {
    // trm is a variable ocurring at the top level
    // we don't create vSpecialVars at the top level
    if(_terms.isEmpty()) return trm;
    // we do not use SortHelper::getVariableSort for efficiency reasons.
    int idx = 0; 
    Term* t = _terms.top();   
    TermList* args = t->args();
#if VDEBUG
    bool found = false;
#endif
    while (!args->isEmpty()) {
      if (*args==trm) {
#if VDEBUG
        found = true;
#endif        
        sort = SortHelper::getArgSort(t, idx);
      }
      idx++;
      args=args->next();
    }
    ASS(found);
  }

  for (auto& h : _inners) {
    TermList t = h->transformSubterm(trm, sort);
    if(t != trm){
      return t;
    }
  }
  return trm;
}

void MismatchHandler::onTermEntry(Term* t) {
  CALL("MismatchHandler::onTermEntry");

  if(t->isApplication()){
    _appTerms.push(TermList(t));
  }
  _terms.push(t);
}

void MismatchHandler::onTermExit(Term* t){
  CALL("MismatchHandler::onTermExit");

  if(t->isApplication()){
    _appTerms.pop();
  }
  _terms.pop();
}

TermList MismatchHandler::get(unsigned var)
{
  CALL("MismatchHandler::get");

  auto res = _termMap.tryGet(var);
  ASS(res.isSome());
  return res.unwrap();
}


bool HOMismatchHandler::isConstraintPair(TermList t1, TermList t2, TermList sort)
{
  CALL("HOMismatchHandler::isConstraintPair");

  return !isConstraintTerm(t1, sort).isFalse() && !isConstraintTerm(t2, sort).isFalse();
}

MaybeBool HOMismatchHandler::isConstraintTerm(TermList t, TermList sort, bool topLevel){
  CALL("MismatcHandler::isConstraintTerm");
  
  if(t.isVar()){ return false; }
  
  // don't create Boolean constraints at the top level
  // these are too explosive
  if(sort.isArrowSort() || (sort.isBoolSort() && !topLevel)){
    return true;
  }

  if(sort.isVar()){
    return MaybeBool::UNKNOWN;
  }
  return false;
}

TermList HOMismatchHandler::transformSubterm(TermList trm, TermList sort)
{
  CALL("HOMismatchHandler::transformSubterm");

  if(trm.isVar()) return trm;

  ASS(trm.term()->shared());

  if(!isConstraintTerm(trm, sort).isFalse()){
    return MismatchHandler::getVSpecVar(trm);
  }
  return trm;
}


}
