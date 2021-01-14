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
 * @file EqualityFactoring.cpp
 * Implements class EqualityFactoring.
 */

#include <utility>

#include "Lib/Environment.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/PairUtils.hpp"
#include "Lib/VirtualIterator.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Ordering.hpp"
#include "Kernel/RobSubstitution.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/LiteralSelector.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Statistics.hpp"

#include "EqualityFactoring.hpp"

#if VDEBUG
#include <iostream>
using namespace std;
#endif

namespace Inferences
{

using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

struct EqualityFactoring::IsPositiveEqualityFn
{
  DECL_RETURN_TYPE(bool);
  bool operator()(Literal* l)
  { return l->isEquality() && l->isPositive(); }
};
struct EqualityFactoring::IsDifferentPositiveEqualityFn
{
  IsDifferentPositiveEqualityFn(Literal* lit) : _lit(lit) {}
  DECL_RETURN_TYPE(bool);
  bool operator()(Literal* l2)
  { return l2->isEquality() && l2->polarity() && l2!=_lit; }
private:
  Literal* _lit;
};

struct EqualityFactoring::FactorablePairsFn
{
  FactorablePairsFn(Clause* cl) : _cl(cl) {}
  DECL_RETURN_TYPE(VirtualIterator<pair<pair<Literal*,TermList>,pair<Literal*,TermList> > >);
  OWN_RETURN_TYPE operator() (pair<Literal*,TermList> arg)
  {
    auto it1 = getContentIterator(*_cl);

    auto it2 = getFilteredIterator(it1,IsDifferentPositiveEqualityFn(arg.first));

    auto it3 = getMapAndFlattenIterator(it2,EqHelper::EqualityArgumentIteratorFn());

    auto it4 = pushPairIntoRightIterator(arg,it3);

    return pvi( it4 );
  }
private:
  Clause* _cl;
};

struct EqualityFactoring::ResultFn
{
  ResultFn(Clause* cl, bool afterCheck, Ordering& ordering)
      : _cl(cl), _cLen(cl->length()), _afterCheck(afterCheck), _ordering(ordering) {}
  DECL_RETURN_TYPE(Clause*);
  Clause* operator() (pair<pair<Literal*,TermList>,pair<Literal*,TermList> > arg)
  {
    CALL("EqualityFactoring::ResultFn::operator()");

    Literal* sLit=arg.first.first;  // selected literal ( = factored-out literal )
    Literal* fLit=arg.second.first; // fairly boring side literal
    ASS(sLit->isEquality());
    ASS(fLit->isEquality());

    unsigned srt = SortHelper::getEqualityArgumentSort(sLit);
    if (srt!=SortHelper::getEqualityArgumentSort(fLit)) {
      return 0;
    }

    TermList sLHS=arg.first.second;
    TermList sRHS=EqHelper::getOtherEqualitySide(sLit, sLHS);
    TermList fLHS=arg.second.second;
    TermList fRHS=EqHelper::getOtherEqualitySide(fLit, fLHS);
    ASS_NEQ(sLit, fLit);

    static RobSubstitution subst;
    subst.reset();
    if(!subst.unify(sLHS,0,fLHS,0)) {
      return 0;
    }

    TermList sLHSS=subst.apply(sLHS,0);
    TermList sRHSS=subst.apply(sRHS,0);
    if(Ordering::isGorGEorE(_ordering.compare(sRHSS,sLHSS))) {
      return 0;
    }
    TermList fRHSS=subst.apply(fRHS,0);
    if(Ordering::isGorGEorE(_ordering.compare(fRHSS,sLHSS))) {
      return 0;
    }

    Clause* res = new(_cLen) Clause(_cLen, GeneratingInference1(InferenceRule::EQUALITY_FACTORING, _cl));

    (*res)[0]=Literal::createEquality(false, sRHSS, fRHSS, srt);

    Literal* sLitAfter = 0;
    if (_afterCheck && _cl->numSelected() > 1) {
      TimeCounter tc(TC_LITERAL_ORDER_AFTERCHECK);
      sLitAfter = subst.apply(sLit, 0);
    }

    unsigned next = 1;
    for(unsigned i=0;i<_cLen;i++) {
      Literal* curr=(*_cl)[i];
      if(curr!=sLit) {
        Literal* currAfter = subst.apply(curr, 0);

        if (sLitAfter) {
          TimeCounter tc(TC_LITERAL_ORDER_AFTERCHECK);
          if (i < _cl->numSelected() && _ordering.compare(currAfter,sLitAfter) == Ordering::GREATER) {
            env.statistics->inferencesBlockedForOrderingAftercheck++;
            res->destroy();
            return 0;
          }
        }

        (*res)[next++] = currAfter;
      }
    }
    ASS_EQ(next,_cLen);

    env.statistics->equalityFactoring++;

    return res;
  }
private:
  Clause* _cl;
  unsigned _cLen;
  bool _afterCheck;
  Ordering& _ordering;
};

ClauseIterator EqualityFactoring::generateClauses(Clause* premise)
{
  CALL("EqualityFactoring::generateClauses");

  if(premise->length()<=1) {
    return ClauseIterator::getEmpty();
  }
  ASS(premise->numSelected()>0);

  auto it1 = premise->getSelectedLiteralIterator();

  auto it2 = getFilteredIterator(it1,IsPositiveEqualityFn());

  auto it3 = getMapAndFlattenIterator(it2,EqHelper::LHSIteratorFn(_salg->getOrdering()));

  auto it4 = getMapAndFlattenIterator(it3,FactorablePairsFn(premise));

  auto it5 = getMappingIterator(it4,ResultFn(premise,
      getOptions().literalMaximalityAftercheck() && _salg->getLiteralSelector().isBGComplete(), _salg->getOrdering()));

  auto it6 = getFilteredIterator(it5,NonzeroFn());

  return pvi( it6 );
}

}
