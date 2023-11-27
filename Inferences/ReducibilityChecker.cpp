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
 * @file ReducibilityChecker.cpp
 * Implements class ReducibilityChecker.
 */

#include "Lib/Environment.hpp"
#include "Lib/BitUtils.hpp"

#include "Shell/Statistics.hpp"

#include "Kernel/TermIterators.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/Matcher.hpp"
#include "Kernel/VarOrder.hpp"
#include "Kernel/SubstHelper.hpp"

#include "Indexing/ResultSubstitution.hpp"

#include "ReducibilityChecker.hpp"
#include "ForwardGroundJoinability.hpp"

using namespace std;
using namespace Indexing;

#define LOGGING 0

#if LOGGING
#define LOG1(s,arg) s << arg << endl;
#define LOG2(s,a1,a2) s << a1 << a2 << endl;
#define LOG3(s,a1,a2,a3) s << a1 << a2 << a3 << endl;
#define LOG4(s,a1,a2,a3,a4) s << a1 << a2 << a3 << a4 << endl;
#else
#define LOG1(s,arg)
#define LOG2(s,a1,a2)
#define LOG3(s,a1,a2,a3)
#define LOG4(s,a1,a2,a3,a4)
#endif

namespace Inferences {

bool argReduced(Term* t) {
  return t->isReduced() && static_cast<ReducibilityChecker::ReducibilityEntry*>(t->reducibilityInfo())->reducesTo.isEmpty();
}

void setBits(unsigned x, unsigned y, PoComp c, uint64_t& val)
{
  if (x > y) {
    swap(x,y);
    c = reverse(c);
  }
  size_t idx = y*(y-1)/2 + x;
  size_t pos;
  switch (c) {
    case PoComp::GT:
      pos = 3*idx;
      break;
    case PoComp::EQ:
      pos = 3*idx+1;
      break;
    case PoComp::LT:
      pos = 3*idx+2;
      break;
    case PoComp::INC:
      ASSERTION_VIOLATION;
  }
  val |= 1UL << pos;
}

// ~000 & 111 -> 111 & 111 -> 1
// ~001 & 111 -> 110 & 111 -> 1
// ...
// ~111 & 111 -> 000 & 111 -> 0

bool isReducedUnderAny(uint64_t val)
{
  for (unsigned i = 0; i < 21; i++) {
    size_t pos = 3*i;
    if (!(~val & (0b111 << pos))) {
      return true;
    }
  }
  return false;
}

void ReducibilityChecker::preprocessClause(Clause* cl)
{
  TIME_TRACE("ReducibilityChecker::preprocessClause");
  for (unsigned i = 0; i < cl->numSelected(); i++) {
    Literal* lit=(*cl)[i];
    auto lhsi = EqHelper::getSuperpositionLHSIterator(lit, _ord, _opt);
    while (lhsi.hasNext()) {
      auto side = lhsi.next();
      if (side.isVar()) {
        continue;
      }

      Stack<VarOrder> todo;
      Stack<VarOrder> rest;
      todo.push(VarOrder());
      while (todo.isNonEmpty()) {
        auto vo = todo.pop();
        VarOrder::EqApplicator voApp(vo);
        auto sideS = SubstHelper::apply(side, voApp);
        NonVariableNonTypeIterator stit(sideS.term());
        while (stit.hasNext()) {
          auto st = stit.next();
          auto it = _index->getGeneralizations(st,true);
          while (it.hasNext()) {
            auto qr = it.next();
            TermList rhsS;
            if (!getDemodulationRHSCodeTree(qr, st, rhsS)) {
              continue;
            }
            VarOrder ext = vo;
            if (!_ord.makeGreater(TermList(st),rhsS,ext)) {
              continue;
            }
            auto vos = ForwardGroundJoinability::order_diff(vo,ext);
            for (auto&& evo : vos) {
              todo.push(std::move(evo));
            }
            goto loop_end;
          }
        }
        if (sideS.isVar()) {
          continue;
        }
        {
          auto tgtTermS = SubstHelper::apply(EqHelper::getOtherEqualitySide(lit,side), voApp);
          auto it = _index->getGeneralizations(sideS.term(),true);
          while (it.hasNext()) {
            auto qr = it.next();
            TermList rhsS;
            if (!getDemodulationRHSCodeTree(qr, sideS.term(), rhsS)) {
              continue;
            }
            VarOrder ext = vo;
            if (!_ord.makeGreater(tgtTermS,rhsS,ext)) {
              continue;
            }
            if (!_ord.makeGreater(TermList(sideS),rhsS,ext)) {
              continue;
            }
            auto vos = ForwardGroundJoinability::order_diff(vo,ext);
            for (auto&& evo : vos) {
              todo.push(std::move(evo));
            }
            goto loop_end;
          }
        }
        rest.push(vo);
        // cout << "cached " << vo.to_string() << " for " << side << " in " << *lit << " in " << *cl << endl;
loop_end:
        continue;
      }
      // if (rest.isEmpty()) {
      //   cout << "cached " << side << " in " << *lit << " in " << *cl << endl;
      // }
      _uselessLHSCache.insert(make_pair(side,EqHelper::getOtherEqualitySide(lit,side)), rest.isEmpty());
    }
  }
}

ReducibilityChecker::ReducibilityChecker(DemodulationLHSIndex* index, const Ordering& ord, const Options& opt)
: _index(index), _ord(ord), _opt(opt), _rwTermState(_ord.createState()) {}

bool ReducibilityChecker::pushSidesFromLiteral(Literal* lit, ResultSubstitution* subst, bool result)
{
  _sidesToCheck.reset();

  if (!lit->isEquality()) {
    _sidesToCheck.push(subst->apply(lit,result));
    return false;
  }

  auto t0 = lit->termArg(0);
  auto t1 = lit->termArg(1);
  auto comp = _ord.getEqualityArgumentOrder(lit);
  switch (comp) {
    case Ordering::INCOMPARABLE: {
      auto t0s = subst->apply(t0,result);
      auto t1s = subst->apply(t1,result);
      switch (_ord.compare(t0s,t1s)) {
        case Ordering::INCOMPARABLE:
          if (t0s.isTerm()) { _sidesToCheck.push(t0s.term()); }
          if (t1s.isTerm()) { _sidesToCheck.push(t1s.term()); }
          break;
        case Ordering::GREATER:
        case Ordering::GREATER_EQ:
          if (t0s.isTerm()) { _sidesToCheck.push(t0s.term()); }
          break;
        case Ordering::LESS:
        case Ordering::LESS_EQ:
          if (t1s.isTerm()) { _sidesToCheck.push(t1s.term()); }
          break;
        case Ordering::EQUAL:
          if (lit->isPositive()) { return true; } // we got a tautology
          break;
      }
      break;
    }
    case Ordering::GREATER:
    case Ordering::GREATER_EQ: {
      ASS(t0.isTerm());
      _sidesToCheck.push(subst->apply(t0,result).term());
      break;
    }
    case Ordering::LESS:
    case Ordering::LESS_EQ: {
      ASS(t1.isTerm());
      _sidesToCheck.push(subst->apply(t1,result).term());
      break;
    }
    case Ordering::EQUAL: {
      if (lit->isPositive()) { return true; }
      break;
    }
  }
  return false;
}

bool ReducibilityChecker::checkSup(Clause* rwClause, Clause* eqClause, Literal* eqLit, Term* rwTermS, TermList tgtTermS, ResultSubstitution* subst, bool eqIsResult, Ordering::Result rwComp)
{
  TIME_TRACE("ReducibilityChecker::checkSup");
  if (_opt.reducibilityCheck()==Options::ReducibilityCheck::OFF) {
    return false;
  }
  _ord.initStateForTerm(_rwTermState, rwTermS);
  vstringstream exp;
  // LiteralStack lits;
  for (unsigned i = 0; i < rwClause->numSelected(); i++) {
    auto lit = (*rwClause)[i];
    // lits.push(subst->apply(lit, !eqIsResult));
    if (pushSidesFromLiteral(lit, subst, !eqIsResult)) {
      return true;
    }
    auto r = checkLiteral(rwTermS, &tgtTermS, exp);
    // auto s = checkLiteralSanity(subst->apply(lit,!eqIsResult), rwTermS, exp);
    // if (s != r) {
    //   USER_ERROR("x1 "+exp.str());
    // }
    if (r) {
      return true;
    }
  }
  for (unsigned i = 0; i < eqClause->numSelected(); i++) {
    auto lit = (*eqClause)[i];
    // lits.push(subst->apply(lit, eqIsResult));
    if (lit == eqLit) {
      _sidesToCheck.reset();
      _sidesToCheck.push(rwTermS);
      if (rwComp==Ordering::INCOMPARABLE && tgtTermS.isTerm()) {
        _sidesToCheck.push(tgtTermS.term());
        NEVER(_ord.isGreater(tgtTermS,TermList(rwTermS),nullptr,&_constraintsFromComparison));
        for (const auto& c : _constraintsFromComparison) {
          auto l = get<0>(c);
          auto r = get<1>(c);
          auto strict = get<2>(c);

          setBits(l, r, PoComp::GT, _reducedUnder);
          if (!strict) {
            setBits(l, r, PoComp::EQ, _reducedUnder);
          } else {
            Substitution subst;
            subst.bind(l,TermList(r,false));
            if (SubstHelper::apply(TermList(rwTermS),subst)==SubstHelper::apply(tgtTermS,subst)) {
              setBits(l, r, PoComp::EQ, _reducedUnder);
            }
          }
          if (isReducedUnderAny(_reducedUnder)) {
            TIME_TRACE("conditionally reduced");
            return true;
          }
        }
      }
    } else {
      if (pushSidesFromLiteral(lit, subst, eqIsResult)) {
        return true;
      }
    }
    auto r = checkLiteral(rwTermS, &tgtTermS, exp);
    // auto s = checkLiteralSanity(subst->apply(lit,eqIsResult), rwTermS, exp);
    // if (s != r) {
    //   USER_ERROR("x2 "+exp.str());
    // }
    if (r) {
      return true;
    }
  }

  LOG1(exp,"checking rwTerm");
  auto ptr = getCacheEntryForTerm(rwTermS);
  ASS(!argReduced(rwTermS));
  DHSet<TermList>::Iterator rIt(ptr->reducesTo);
  while (rIt.hasNext()) {
    auto rhs = rIt.next();
    LOG2(exp,"rhs ",rhs.toString());
    if (!_ord.isGreater(tgtTermS,rhs,nullptr,&_constraintsFromComparison)) {
      LOG1(exp,"not greater tgtTerm");
      for (const auto& c : _constraintsFromComparison) {
        auto l = get<0>(c);
        auto r = get<1>(c);
        auto strict = get<2>(c);
        setBits(l, r, PoComp::GT, _reducedUnder);
        if (!strict) {
          setBits(l, r, PoComp::EQ, _reducedUnder);
        }
        if (isReducedUnderAny(_reducedUnder)) {
          TIME_TRACE("conditionally reduced");
          return true;
        }
      }
      continue;
    }
    return true;
  }
  
  DHMap<TermList,uint64_t>::Iterator rcIt(ptr->reducesToCond);
  while (rcIt.hasNext()) {
    TermList rhs;
    uint64_t val;
    rcIt.next(rhs,val);
    LOG2(exp,"rhs ",rhs.toString());
    {TIME_TRACE("tgtTerm comparison");
    if (!_ord.isGreater(tgtTermS,rhs,nullptr,&_constraintsFromComparison)) {
      for (const auto& c : _constraintsFromComparison) {
        auto l = get<0>(c);
        auto r = get<1>(c);
        auto strict = get<2>(c);
        bool reversed = l > r;
        auto idx_x = std::min(l,r);
        auto idx_y = std::max(l,r);
        size_t idx = idx_y*(idx_y-1)/2 + idx_x;
        size_t pos_gt = 3*idx;
        size_t pos_eq = 3*idx+1;
        size_t pos_lt = 3*idx+2;

        if (val & (1UL << (reversed ? pos_lt : pos_gt))) {
          _reducedUnder |= 1UL << (reversed ? pos_lt : pos_gt);
        }
        if (!strict && (val & (1UL << (reversed ? pos_lt : pos_gt)))) {
          _reducedUnder |= 1UL << pos_eq;
        }
        if (isReducedUnderAny(_reducedUnder)) {
          TIME_TRACE("conditionally reduced rwTerm");
          return true;
        }
      }
      continue;
    }}
    _reducedUnder |= val;
    if (isReducedUnderAny(_reducedUnder)) {
      TIME_TRACE("conditionally reduced rwTerm");
      return true;
    }
  }
  if (isReducedUnderAny(_reducedUnder)) {
    TIME_TRACE("conditionally reduced at the end");
    return true;
  }

  // cout << "check sanity " << endl;

  // auto res2 = checkSmallerGround3(lits, rwTermS, &tgtTermS, exp);
  // if (res2) {
  //     TIME_TRACE("reduced by method 3");
  // //   cout << ("problem "+rwTermS->toString()+" "+tgtTermS.toString()+" "+Int::toHexString(_reducedUnder)+" "+lits[0]->toString()+" "+lits[1]->toString()+" "+exp.str()) << endl;
  // }

  // cout << "nope " << Int::toHexString(_reducedUnder) << endl;
  return false;
}

bool ReducibilityChecker::checkLiteral(Literal* lit)
{
  TIME_TRACE("ReducibilityChecker::checkLiteral");
  if (_opt.reducibilityCheck()==Options::ReducibilityCheck::OFF) {
    return false;
  }
  switch (_opt.reducibilityCheck()) {
    case Options::ReducibilityCheck::SMALLER: {
      vstringstream exp;
      _sidesToCheck.reset();
      if (!lit->isEquality()) {
        _sidesToCheck.push(lit);
      } else {
        auto comp = _ord.getEqualityArgumentOrder(lit);
        auto t0 = lit->termArg(0);
        auto t1 = lit->termArg(1);
        switch(comp) {
          case Ordering::INCOMPARABLE:
            if (t0.isTerm()) { _sidesToCheck.push(t0.term()); }
            if (t1.isTerm()) { _sidesToCheck.push(t1.term()); }
            break;
          case Ordering::GREATER:
          case Ordering::GREATER_EQ:
            ASS(t0.isTerm());
            _sidesToCheck.push(t0.term());
            break;
          case Ordering::LESS:
          case Ordering::LESS_EQ:
            ASS(t1.isTerm());
            _sidesToCheck.push(t1.term());
            break;
          case Ordering::EQUAL:
            if (lit->isPositive()) {
              return true;
            }
            break;
        }
      }
      auto res = checkLiteral(nullptr, nullptr, exp);
      // auto res2 = checkLiteralSanity(lit, nullptr, nullptr, exp);
      // if (res != res2) {
      //   USER_ERROR("Sanity failed "+exp.str());
      // }
      return res;
    }
    case Options::ReducibilityCheck::SMALLER_GROUND: {
      // vstringstream exp;
      // return checkSmallerGround3(lits, nullptr, nullptr, exp);
      // // return checkSmallerGround2(lits, nullptr, nullptr, exp);
      // // return checkSmallerGround(lits, nullptr, nullptr, exp);
    }
    default:
      return false;
  }
  ASSERTION_VIOLATION;
}

bool ReducibilityChecker::checkLiteralSanity(Literal* lit, Term* rwTermS, vstringstream& exp)
{
  LOG2(exp,"check literal ",*lit);
  LOG2(exp,"rwTermS ",*rwTermS);
  Stack<Term*> toplevelTerms;
  if (!lit->isEquality()) {
    toplevelTerms.push(lit);
  } else {
    auto comp = _ord.getEqualityArgumentOrder(lit);
    auto t0 = lit->termArg(0);
    auto t1 = lit->termArg(1);
    switch(comp) {
      case Ordering::INCOMPARABLE:
        if (t0.isTerm()) { toplevelTerms.push(t0.term()); }
        if (t1.isTerm()) { toplevelTerms.push(t1.term()); }
        break;
      case Ordering::GREATER:
      case Ordering::GREATER_EQ:
        ASS(t0.isTerm());
        toplevelTerms.push(t0.term());
        break;
      case Ordering::LESS:
      case Ordering::LESS_EQ:
        ASS(t1.isTerm());
        toplevelTerms.push(t1.term());
        break;
      case Ordering::EQUAL:
        if (lit->isPositive()) {
          return true;
        }
        break;
    }
  }
  for (Term* t : toplevelTerms) {
    NonVariableNonTypeIterator stit(t, !t->isLiteral());
    while (stit.hasNext()) {
      auto st = stit.next();
      if (rwTermS && _ord.compare(TermList(rwTermS),TermList(st))!=Ordering::GREATER) {
        continue;
      }
      auto it = _index->getGeneralizations(st,true);
      while (it.hasNext()) {
        auto qr = it.next();
        TermList rhsS;
        if (!getDemodulationRHSCodeTree(qr, st, rhsS)) {
          continue;
        }
        if (_ord.compare(TermList(st),rhsS)!=Ordering::GREATER) {
          continue;
        }
        LOG3(exp, *st, " => ", rhsS);
        LOG4(exp, " in ", *t, " and ", *lit);
        LOG2(exp, " is reducible by ", *qr.clause);
        return true;
      }
    }
  }
  return false;
}

bool ReducibilityChecker::checkRwTermSanity(Term* rwTermS, TermList tgtTermS, vstringstream& exp)
{
  LOG2(exp,"check rwTerm ",*rwTermS);
  auto it = _index->getGeneralizations(rwTermS,true);
  while (it.hasNext()) {
    auto qr = it.next();
    TermList rhsS;
    if (!getDemodulationRHSCodeTree(qr, rwTermS, rhsS)) {
      continue;
    }
    if (_ord.compare(tgtTermS,rhsS) != Ordering::GREATER) {
      continue;
    }
    if (_ord.compare(TermList(rwTermS),rhsS)!=Ordering::GREATER) {
      continue;
    }
    LOG2(exp, "rwTermS ", *rwTermS);
    LOG2(exp, "tgtTermS ", tgtTermS);
    LOG2(exp, "rhsS ", rhsS);
    LOG2(exp, "reducible by ", *qr.clause);
    return true;
  }
  return false;
}

bool ReducibilityChecker::getDemodulationRHSCodeTree(const TermQueryResult& qr, Term* lhsS, TermList& rhsS)
{
  if (!qr.clause->noSplits()) {
    return false;
  }
  static RobSubstitution subst;
  TypedTermList trm(lhsS);
  bool resultTermIsVar = qr.term.isVar();
  if (resultTermIsVar) {
    TermList querySort = trm.sort();
    TermList eqSort = SortHelper::getEqualityArgumentSort(qr.literal);
    subst.reset();
    if (!subst.match(eqSort, 0, querySort, 1)) {
      return false;
    }
  }
  TermList rhs = EqHelper::getOtherEqualitySide(qr.literal,qr.term);
  rhsS = qr.substitution->applyToBoundResult(rhs);
  if (resultTermIsVar) {
    rhsS = subst.apply(rhsS, 0);
  }
  return true;
}

void ReducibilityChecker::clauseActivated(Clause* cl)
{
  TIME_TRACE("ReducibilityChecker::clauseActivated");
  if (cl->length()!=1 || !cl->noSplits()) {
    return;
  }
  LOG2(cout,"demodulator clause activated ",*cl);

  Stack<Term*> toUpdate;

  Literal* lit=(*cl)[0];
  auto lhsi = EqHelper::getDemodulationLHSIterator(lit, true, _ord, _opt);
  while (lhsi.hasNext()) {
    auto lhs = lhsi.next();
    auto qrit = _tis.getInstances(lhs, true);
    while (qrit.hasNext()) {
      auto qr = qrit.next();
      TermList rhs=EqHelper::getOtherEqualitySide(lit, lhs);
      TermList lhsS=qr.term;
      TermList rhsS;

      if(!qr.substitution->isIdentityOnResultWhenQueryBound()) {
        //When we apply substitution to the rhs, we get a term, that is
        //a variant of the term we'd like to get, as new variables are
        //produced in the substitution application.
        //We'd rather rename variables in the rhs, than in the whole clause
        //that we're simplifying.
        TermList lhsSBadVars=qr.substitution->applyToQuery(lhs);
        TermList rhsSBadVars=qr.substitution->applyToQuery(rhs);
        Renaming rNorm, qNorm, qDenorm;
        rNorm.normalizeVariables(lhsSBadVars);
        qNorm.normalizeVariables(lhsS);
        qDenorm.makeInverse(qNorm);
        ASS_EQ(lhsS,qDenorm.apply(rNorm.apply(lhsSBadVars)));
        rhsS=qDenorm.apply(rNorm.apply(rhsSBadVars));
      } else {
        rhsS=qr.substitution->applyToBoundQuery(rhs);
      }

      auto t = static_cast<Term*>(qr.literal);
      LOG2(cout,"possible cached term ",*t);
      LOG2(cout,"possible rhs ",rhsS);
      // auto e = static_cast<ReducibilityEntry*>(t->reducibilityInfo());
      // ASS(e);
      // if (qr.term.term() == t && _ord.isGreater(TermList(t),rhsS)) {
      //   e->reducesTo.push(rhsS);
      // }

      auto e = static_cast<ReducibilityEntry*>(t->reducibilityInfo());
      // ASS(e);
      // if (_ord.makeGreater(TermList(t),rhsS)) {
      //   e->reducesTo.push(rhsS);
      // }
      if (!_ord.isGreater(TermList(t),rhsS,nullptr,&_constraintsFromComparison)) {
        LOG1(cout,"not greater");
        for (const auto& c : _constraintsFromComparison) {
          auto l = get<0>(c);
          auto r = get<1>(c);
          auto strict = get<2>(c);
          bool reversed = l > r;
          auto idx_x = std::min(l,r);
          auto idx_y = std::max(l,r);
          size_t idx = idx_y*(idx_y-1)/2 + idx_x;
          size_t pos_gt = 3*idx;
          size_t pos_eq = 3*idx+1;
          size_t pos_lt = 3*idx+2;

          auto temp = t->reducesUnder();
          t->reducesUnder() |= 1UL << (reversed ? pos_lt : pos_gt);
          if (!strict) {
            t->reducesUnder() |= 1UL << pos_eq;
          }
          uint64_t* ptr;
          e->reducesToCond.getValuePtr(rhsS, ptr, 0);
          (*ptr) |= 1UL << (reversed ? pos_lt : pos_gt);
          if (!strict) {
            (*ptr) |= 1UL << pos_eq;
          }
          // cout << "changed " << *t << " from " << Int::toHexString(temp) << " to " << Int::toHexString(t->reducesUnder()) << endl;
          // TODO this may not be propagated through toUpdate
          for (const auto& st : e->superTerms) {
            st->reducesUnder() |= t->reducesUnder();
            toUpdate.push(st);
          }
        }
        continue;
      }
      LOG1(cout,"rhs reduces");
      ASS(!argReduced(t));
      e->reducesTo.insert(rhsS);
      t->markReduced();
      // toUpdate.push(t);
      for (const auto& st : e->superTerms) {
        st->reducesUnder() |= t->reducesUnder();
        toUpdate.push(st);
      }
    }
  }

  while (toUpdate.isNonEmpty()) {
    auto t = toUpdate.pop();
    auto e = static_cast<ReducibilityEntry*>(t->reducibilityInfo());
    ASS(e);
    // this supertree has been marked reduced already
    if (argReduced(t)) {
      continue;
    }
    e->reducesTo.reset();
    t->markReduced();
    _tis.remove(TypedTermList(t), static_cast<Literal*>(t), nullptr);
    for (const auto& st : e->superTerms) {
      st->reducesUnder() |= t->reducesUnder();
      toUpdate.push(st);
    }
  }
}

ReducibilityChecker::ReducibilityEntryGround* ReducibilityChecker::isTermReducible(Term* t)
{
  ReducibilityEntryGround* vos = static_cast<ReducibilityEntryGround*>(t->reducibilityInfo());
  TIME_TRACE(vos ? "ReducibilityChecker::isTermReducible" : "ReducibilityChecker::isTermReducibleFirst");
  if (vos && vos->valid) {
    return vos;
  }
  if (!vos) {
    vos = new ReducibilityEntryGround();
    t->setReducibilityInfo(vos);
    // TODO eventually clear up the index
    _tis.insert(TypedTermList(t), static_cast<Literal*>(t), nullptr);
    for (unsigned i = t->numTypeArguments(); i < t->arity(); i++) {
      auto arg = t->nthArgument(i);
      if (arg->isVar()) {
        continue;
      }
      auto arg_vos = isTermReducible(arg->term());
      arg_vos->superTerms.push(t);
    }
    auto it = _index->getGeneralizations(t,true);
    while (it.hasNext()) {
      auto qr = it.next();
      TermList rhsS;
      if (!getDemodulationRHSCodeTree(qr, t, rhsS)) {
        continue;
      }
      if (!_ord.isGreater(TermList(t),rhsS)) {
        continue;
      }
      // reduced, add it to the reduced stack
      vos->reducesTo.push(rhsS);
    }
  }
  Stack<VarOrder> todo;
  for (const auto& vo : vos->rest) {
    todo.push(vo);
  }
  vos->rest.reset();
  while (todo.isNonEmpty()) {
    auto vo = todo.pop();
    for (unsigned i = t->numTypeArguments(); i < t->arity(); i++) {
      auto arg = t->nthArgument(i);
      if (arg->isVar()) {
        continue;
      }
      auto arg_vos = isTermReducible(arg->term());
      for (const auto& red : arg_vos->reduced) {
        VarOrder ext = vo;
        if (ext.tryExtendWith(red)) {
          auto diff = ForwardGroundJoinability::order_diff(vo,ext);
          for (auto&& evo : diff) {
            todo.push(std::move(evo));
          }
          vos->reduced.push(ext);
          goto loop_end;
        }
      }
    }
    {
      VarOrder::EqApplicator voApp(vo);
      auto tS = SubstHelper::apply(t, voApp);
      auto it = _index->getGeneralizations(tS,true);
      while (it.hasNext()) {
        auto qr = it.next();
        TermList rhsS;
        if (!getDemodulationRHSCodeTree(qr, tS, rhsS)) {
          continue;
        }
        VarOrder ext = vo;
        if (!_ord.makeGreater(TermList(tS),rhsS,ext)) {
          continue;
        }
        auto diff = ForwardGroundJoinability::order_diff(vo,ext);
        for (auto&& evo : diff) {
          todo.push(std::move(evo));
        }
        // reduced, add it to the reduced stack
        vos->reduced.push(ext);
        goto loop_end;
      }
      // could not reduce under this vo, add it to the rest and save it into the index
      vos->rest.push(vo);
      _tis.insert(TypedTermList(tS), static_cast<Literal*>(t), nullptr);
    }
loop_end:
    continue;
  }
  if (vos->rest.isEmpty()) {
    vos->reduced.reset();
    vos->reduced.push(VarOrder());
  }
  if (vos->rest.size()==1 && vos->rest[0].size()==2 && vos->reduced.size()>2) {
    VarOrder vo;
    auto newReduced = ForwardGroundJoinability::order_diff(vo, vos->rest[0]);
    vos->reduced.reset();
    for (const auto& vo : newReduced) {
      vos->reduced.push(vo);
    }
  }
  vos->valid = true;
  return vos;
}

vstring toString(pair<unsigned,unsigned> p, bitset<3> bv)
{
  return "X" + Int::toString(p.first) + (bv[0] ? " >" : " ") + (bv[1] ? "=" : "") + (bv[2] ? "<" : "") + " X" + Int::toString(p.second);
}

ReducibilityChecker::ReducibilityEntryGround2* ReducibilityChecker::getCacheEntryForTermGround(Term* t)
{
  auto e = static_cast<ReducibilityEntryGround2*>(t->reducibilityInfoAlt());
  TIME_TRACE(e ? "ReducibilityChecker::getCacheEntryForTerm" : "ReducibilityChecker::getCacheEntryForTermFirst");
  if (e && e->valid) {
    return e;
  }
  if (!e) {
    e = new ReducibilityEntryGround2();
    t->setReducibilityInfoAlt(e);
    // _tis.insert(TypedTermList(t), static_cast<Literal*>(t), nullptr);
    for (unsigned i = t->numTypeArguments(); i < t->arity(); i++) {
      auto arg = t->nthArgument(i);
      if (arg->isVar()) {
        continue;
      }
      auto arg_e = getCacheEntryForTermGround(arg->term());
      arg_e->superTerms.push(t);
    }
  }
  {
    for (unsigned i = t->numTypeArguments(); i < t->arity(); i++) {
      auto arg = t->nthArgument(i);
      if (arg->isVar()) {
        continue;
      }
      auto arg_e = getCacheEntryForTermGround(arg->term());
      auto it = arg_e->reducedUnder.items();

      if (arg_e->reduced) {
        e->reduced = true;
      }
      while (it.hasNext()) {
        auto arg_r = it.next();
        // cout << "add " << toString(arg_r.first, arg_r.second) << endl;
        e->addReducedUnder(arg_r.first.first, arg_r.first.second, arg_r.second);
      }
    }
    auto it = _index->getGeneralizations(t,true);
    while (it.hasNext()) {
      auto qr = it.next();
      TermList rhsS;
      if (!getDemodulationRHSCodeTree(qr, t, rhsS)) {
        continue;
      }
      VarOrder vo;
      if (!_ord.makeGreater(TermList(t),rhsS,vo)) {
        continue;
      }
      // reduced, add it to the reduced stack
      // cout << "reduced under " << vo.to_string() << " by " << *qr.literal << endl;
      if (vo.is_empty()) {
        e->reduced = true;
        e->reducesTo.insert(rhsS);
      } else if (vo.size()==2) {
        auto bvo = getBVOFromVO(vo);
        // cout << "bvo " << bvo.toString() << endl;
        e->reducesToCond.insert(bvo,rhsS);
        e->addReducedUnder(bvo._x,bvo._y,ReducibilityEntryGround2::toBitset(bvo._c));
      }
    }
  }
  Substitution subst;
  DHMap<std::pair<unsigned,unsigned>,std::bitset<3>>::Iterator it(e->reducedUnder);
  while (it.hasNext()) {
    pair<unsigned,unsigned> p;
    auto& bv = it.nextRef(p);

    subst.reset();
    subst.bind(p.first,TermList(p.second,false));
    auto tS = SubstHelper::apply(t, subst);
    auto it = _index->getGeneralizations(tS,true);
    while (it.hasNext()) {
      auto qr = it.next();
      if (qr.term.isVar() || MatchingUtils::matchArgs(qr.term.term(),t)) {
        continue;
      }
      TermList rhsS;
      if (!getDemodulationRHSCodeTree(qr, tS, rhsS)) {
        continue;
      }
      if (!_ord.isGreater(TermList(t),rhsS)) {
        continue;
      }
      BinaryVarOrder bvo(p.first,p.second,PoComp::EQ);
      e->reducesToCond.insert(bvo,rhsS);
      bv[1] = true;
    }
    // cout << "result " << toString(p, bv) << endl;
  }
  // cout << endl;
  e->valid = true;
  return e;
}

ReducibilityChecker::ReducibilityEntry* ReducibilityChecker::getCacheEntryForTerm(Term* t)
{
  auto e = static_cast<ReducibilityEntry*>(t->reducibilityInfo());
  // TIME_TRACE(e ? "ReducibilityChecker::getCacheEntryForTerm" : "ReducibilityChecker::getCacheEntryForTermFirst");
  if (e) {
    LOG2(cout,"cache exists ",*t);
#if VDEBUG
    if (!t->isReduced()) {
      NonVariableNonTypeIterator nvi(t);
      while (nvi.hasNext()) {
        auto st = nvi.next();
        ASS(!st->isReduced());
        ASS_REP(!(~t->reducesUnder() & st->reducesUnder()),t->toString()+" "+Int::toHexString(t->reducesUnder())+" "+st->toString()+" "+Int::toHexString(st->reducesUnder()));
      }
    }
#endif
    return e;
  }
  LOG2(cout,"cache term ",*t);
  e = new ReducibilityEntry();
  t->setReducibilityInfo(e);
  if (t->isReduced()) {
    return e;
  }
  for (unsigned i = t->numTypeArguments(); i < t->arity(); i++) {
    auto arg = t->nthArgument(i);
    if (arg->isVar()) {
      continue;
    }
    auto arg_e = getCacheEntryForTerm(arg->term());
    arg_e->superTerms.push(t);
    if (arg->term()->isReduced()) {
      LOG2(cout,"arg reduced ",*arg);
      t->markReduced();
      return e;
    }
    auto temp = t->reducesUnder();
    t->reducesUnder() |= arg->term()->reducesUnder();
  }

  auto it = _index->getGeneralizations(t,true);
  while (it.hasNext()) {
    auto qr = it.next();
    TermList rhsS;
    if (!getDemodulationRHSCodeTree(qr, t, rhsS)) {
      continue;
    }
    LOG2(cout,"rhs ",rhsS);
    if (!_ord.isGreater(TermList(t),rhsS,nullptr,&_constraintsFromComparison)) {
      for (const auto& c : _constraintsFromComparison) {
        auto l = get<0>(c);
        auto r = get<1>(c);
        auto strict = get<2>(c);
        bool reversed = l > r;
        auto idx_x = std::min(l,r);
        auto idx_y = std::max(l,r);
        size_t idx = idx_y*(idx_y-1)/2 + idx_x;
        size_t pos_gt = 3*idx;
        size_t pos_eq = 3*idx+1;
        size_t pos_lt = 3*idx+2;

        auto temp = t->reducesUnder();
        t->reducesUnder() |= 1UL << (reversed ? pos_lt : pos_gt);
        if (!strict) {
          t->reducesUnder() |= 1UL << pos_eq;
        }
        uint64_t* ptr;
        e->reducesToCond.getValuePtr(rhsS, ptr, 0);
        (*ptr) |= 1UL << (reversed ? pos_lt : pos_gt);
        if (!strict) {
          (*ptr) |= 1UL << pos_eq;
        }
      }
      LOG1(cout,"not greater");
      continue;
    }

    t->markReduced();
    e->reducesTo.insert(rhsS);
  }
  if (!argReduced(t)) {
    LOG1(cout,"indexed");
    _tis.insert(TypedTermList(t), static_cast<Literal*>(t), nullptr);
  } else {
    LOG1(cout,"not indexed");
  }
  return e;
}

bool ReducibilityChecker::checkSmallerGround(const Stack<Literal*>& lits, Term* rwTermS, TermList* tgtTermS, vstringstream& exp)
{
  Stack<Term*> toplevelTerms;
  for (const auto& lit : lits) {
    if (!lit->isEquality()) {
      toplevelTerms.push(lit);
    } else {
      auto comp = _ord.getEqualityArgumentOrder(lit);
      auto t0 = lit->termArg(0);
      auto t1 = lit->termArg(1);
      switch(comp) {
        case Ordering::INCOMPARABLE:
          if (t0.isTerm()) { toplevelTerms.push(t0.term()); }
          if (t1.isTerm()) { toplevelTerms.push(t1.term()); }
          break;
        case Ordering::GREATER:
        case Ordering::GREATER_EQ:
          ASS(t0.isTerm());
          toplevelTerms.push(t0.term());
          break;
        case Ordering::LESS:
        case Ordering::LESS_EQ:
          ASS(t1.isTerm());
          toplevelTerms.push(t1.term());
          break;
        case Ordering::EQUAL:
          ASSERTION_VIOLATION;
      }
    }
  }
  DHSet<Term*> attemptedOuter;

  Stack<VarOrder> todo;
  todo.push(VarOrder());
  while (todo.isNonEmpty()) {
    auto vo = todo.pop();
    VarOrder::EqApplicator voApp(vo);
    auto rwTermSS = SubstHelper::apply(rwTermS, voApp);
    TermList tgtTermSS = SubstHelper::apply(*tgtTermS, voApp);
    if (tgtTermSS == TermList(rwTermSS) || _ord.isGreater(tgtTermSS,TermList(rwTermSS),vo)) {
      // the superposition itself is redundant, skip this order
      continue;
    }

    // VarOrder ext = vo;
    // if (_ord.makeGreater(tgtTermSS,TermList(rwTermSS),ext)) {
    //   auto vos = ForwardGroundJoinability::order_diff(vo,ext);
    //   for (auto&& evo : vos) {
    //     todo.push(std::move(evo));
    //   }
    //   continue;
    // }
    DHSet<Term*> attempted;

    // try subterms of rwTermSS
    NonVariableNonTypeIterator stit(rwTermSS);
    while (stit.hasNext()) {
      auto st = stit.next();
      if (!attempted.insert(st)) {
        stit.right();
        continue;
      }

      auto ptr = isTermReducible(st);
      for (const auto& other : ptr->reduced) {
        VarOrder ext = vo;
        if (ext.tryExtendWith(other)) {
          auto vos = ForwardGroundJoinability::order_diff(vo,ext);
          for (auto&& evo : vos) {
            todo.push(std::move(evo));
          }
          goto loop_end;
        }
      }

      // auto it = _index->getGeneralizations(st,true);
      // while (it.hasNext()) {
      //   auto qr = it.next();
      //   TermList rhsS;
      //   if (!getDemodulationRHSCodeTree(qr, st, rhsS)) {
      //     continue;
      //   }
      //   VarOrder ext = vo;
      //   if (!_ord.makeGreater(TermList(st),rhsS,ext)) {
      //     continue;
      //   }
      //   // cout << "not cached for " << *st << " -> " << rhsS << endl;
      //   // cout << "current " << vo.to_string() << endl;
      //   // cout << "ext " << ext.to_string() << endl;
      //   // cout << "reduced " << endl;
      //   // for (const auto& vo : ptr->reduced) {
      //   //   cout << vo.to_string() << endl;
      //   // }
      //   // cout << "rest " << endl;
      //   // for (const auto& vo : ptr->rest) {
      //   //   cout << vo.to_string() << endl;
      //   // }
      //   // USER_ERROR("x");
      //   auto vos = ForwardGroundJoinability::order_diff(vo,ext);
      //   for (auto&& evo : vos) {
      //     todo.push(std::move(evo));
      //   }
      //   goto loop_end;
      // }
    }

    for (Term* t : toplevelTerms) {
      auto sideSS = SubstHelper::apply(t, voApp);
      NonVariableNonTypeIterator stit(sideSS, !sideSS->isLiteral());
      while (stit.hasNext()) {
        auto st = stit.next();
        if (!attempted.insert(st)) {
          stit.right();
          continue;
        }
        // avoid doing anything with variables that are not in rwTerm
        if (~rwTermSS->varmap() & st->varmap()) {
          continue;
        }
        if (rwTermSS == st) {
          continue;
        }
        VarOrder ext = vo;
        if (!rwTermSS->isLiteral() && !_ord.makeGreater(TermList(rwTermSS),TermList(st),ext)) {
          continue;
        }

        auto ptr = isTermReducible(st);
        for (const auto& other : ptr->reduced) {
          VarOrder ext2 = ext;
          if (ext2.tryExtendWith(other)) {
            auto vos = ForwardGroundJoinability::order_diff(vo,ext2);
            for (auto&& evo : vos) {
              todo.push(std::move(evo));
            }
            goto loop_end;
          }
        }

        // auto it = _index->getGeneralizations(st,true);
        // while (it.hasNext()) {
        //   auto qr = it.next();
        //   TermList rhsS;
        //   if (!getDemodulationRHSCodeTree(qr, st, rhsS)) {
        //     continue;
        //   }
        //   VarOrder ext2 = ext;
        //   if (!_ord.makeGreater(TermList(st),rhsS,ext2)) {
        //     continue;
        //   }
        //   auto vos = ForwardGroundJoinability::order_diff(vo,ext2);
        //   for (auto&& evo : vos) {
        //     todo.push(std::move(evo));
        //   }
        //   goto loop_end;
        // }
      }
    }

    {
      // finally, try rwTermSS itself
      auto it = _index->getGeneralizations(rwTermSS,true);
      while (it.hasNext()) {
        auto qr = it.next();
        TermList rhsS;
        if (!getDemodulationRHSCodeTree(qr, rwTermSS, rhsS)) {
          continue;
        }
        VarOrder ext = vo;
        if (!_ord.makeGreater(tgtTermSS,rhsS,ext)) {
          continue;
        }
        if (!_ord.makeGreater(TermList(rwTermSS),rhsS,ext)) {
          continue;
        }
        auto vos = ForwardGroundJoinability::order_diff(vo,ext);
        for (auto&& evo : vos) {
          todo.push(std::move(evo));
        }
        goto loop_end;
      }
    }

    // could not reduce under this partial extension
    return false;
loop_end:
    continue;
  }
  return true;
}

// cheaper but returns more false negatives
bool ReducibilityChecker::checkSmallerGround2(const Stack<Literal*>& lits, Term* rwTermS, TermList* tgtTermS, vstringstream& exp)
{
  Stack<Term*> toplevelTerms;
  for (const auto& lit : lits) {
    if (!lit->isEquality()) {
      toplevelTerms.push(lit);
    } else {
      auto comp = _ord.getEqualityArgumentOrder(lit);
      auto t0 = lit->termArg(0);
      auto t1 = lit->termArg(1);
      switch(comp) {
        case Ordering::INCOMPARABLE:
          if (t0.isTerm()) { toplevelTerms.push(t0.term()); }
          if (t1.isTerm()) { toplevelTerms.push(t1.term()); }
          break;
        case Ordering::GREATER:
        case Ordering::GREATER_EQ:
          ASS(t0.isTerm());
          toplevelTerms.push(t0.term());
          break;
        case Ordering::LESS:
        case Ordering::LESS_EQ:
          ASS(t1.isTerm());
          toplevelTerms.push(t1.term());
          break;
        case Ordering::EQUAL:
          if (lit->isPositive()) {
            return true;
          }
          break;
          // ASSERTION_VIOLATION;
      }
    }
  }
  // DHSet<Term*> attempted;

  for (Term* side : toplevelTerms) {
    NonVariableNonTypeIterator stit(side, !side->isLiteral());
    while (stit.hasNext()) {
      auto st = stit.next();
      // if (!attempted.insert(st)) {
      //   stit.right();
      //   continue;
      // }
      // avoid doing anything with variables that are not in rwTerm
      if (rwTermS && (~rwTermS->varmap() & st->varmap())) {
        continue;
      }
      if (rwTermS && !_ord.isGreater(TermList(rwTermS),TermList(st))) {
        continue;
      }

      auto ptr = isTermReducible(st);
      ASS(ptr->valid);
      if (ptr->rest.isEmpty()) {
        return true;
      }
      stit.right();
    }
  }

  if (rwTermS) {
    auto ptr = isTermReducible(rwTermS);
    ASS(ptr->valid);
    for (const auto& rhs : ptr->reducesTo) {
      if (!_ord.isGreater(*tgtTermS,rhs)) {
        continue;
      }
      return true;
    }
  }
  return false;
}

ReducibilityChecker::BinaryVarOrder ReducibilityChecker::getBVOFromVO(const VarOrder& vo)
{
  auto it = vo.iter_relations();
  ALWAYS(it.hasNext());
  auto tp = it.next();
  ALWAYS(!it.hasNext());
  return BinaryVarOrder(get<0>(tp), get<1>(tp), get<2>(tp));
}

VarOrder ReducibilityChecker::getVOFromBVO(const BinaryVarOrder& bvo)
{
  VarOrder vo;
  switch (bvo._c)
  {
  case PoComp::EQ:
    ALWAYS(vo.add_eq(bvo._x,bvo._y));
    break;
  case PoComp::GT:
    ALWAYS(vo.add_gt(bvo._x,bvo._y));
    break;
  case PoComp::LT:
    ALWAYS(vo.add_gt(bvo._y,bvo._x));
    break;
  default:
    ASSERTION_VIOLATION;
    break;
  }
  return vo;
}

bool ReducibilityChecker::updateBinaries(unsigned x, unsigned y, const bitset<3>& bv)
{
  ASS_L(x,y);
  bitset<3>* e;
  _binaries.getValuePtr(make_pair(x,y),e);
  (*e) |= bv;
  return e->all();
}

// cheaper but returns more false negatives
bool ReducibilityChecker::checkSmallerGround3(const Stack<Literal*>& lits, Term* rwTermS, TermList* tgtTermS, vstringstream& exp)
{
  Stack<Term*> toplevelTerms;
  for (const auto& lit : lits) {
    if (!lit->isEquality()) {
      toplevelTerms.push(lit);
    } else {
      auto comp = _ord.getEqualityArgumentOrder(lit);
      auto t0 = lit->termArg(0);
      auto t1 = lit->termArg(1);
      switch(comp) {
        case Ordering::INCOMPARABLE:
          if (t0.isTerm()) { toplevelTerms.push(t0.term()); }
          if (t1.isTerm()) { toplevelTerms.push(t1.term()); }
          break;
        case Ordering::GREATER:
        case Ordering::GREATER_EQ:
          ASS(t0.isTerm());
          toplevelTerms.push(t0.term());
          break;
        case Ordering::LESS:
        case Ordering::LESS_EQ:
          ASS(t1.isTerm());
          toplevelTerms.push(t1.term());
          break;
        case Ordering::EQUAL:
          if (lit->isPositive()) {
            return true;
          }
          break;
          // ASSERTION_VIOLATION;
      }
    }
  }

  VarOrder redundant;
  if (rwTermS && _ord.makeGreater(*tgtTermS,TermList(rwTermS),redundant) && redundant.size()==2)
  {
    auto bvo = getBVOFromVO(redundant);
    NEVER(updateBinaries(bvo._x,bvo._y,ReducibilityEntryGround2::toBitset(bvo._c)));
    LOG2(exp,"made redundant under ",redundant.to_string());

    Substitution subst;
    subst.bind(bvo._x,TermList(bvo._y,false));
    if (SubstHelper::apply(*tgtTermS,subst) == SubstHelper::apply(TermList(rwTermS),subst)) {
      NEVER(updateBinaries(bvo._x,bvo._y,ReducibilityEntryGround2::toBitset(PoComp::EQ)));
      LOG1(exp,"and under =");
    }
  }

  for (Term* side : toplevelTerms) {
    NonVariableNonTypeIterator stit(side, !side->isLiteral());
    while (stit.hasNext()) {
      auto st = stit.next();
      if (!_attempted2.insert(st)) {
        stit.right();
        continue;
      }
      // avoid doing anything with variables that are not in rwTerm
      if (rwTermS && (~rwTermS->varmap() & st->varmap())) {
        continue;
      }
      VarOrder gt;
      if (rwTermS && !_ord.makeGreater(TermList(rwTermS),TermList(st),gt)) {
        continue;
      }

      auto ptr = getCacheEntryForTermGround(st);
      ASS(ptr->valid);
      if (gt.is_empty() && ptr->reduced) {
        LOG2(exp,"reduced ",*st);
        return true;
      }
      if (gt.size() > 2) {
        continue;
      }
      if (ptr->reduced) {
        auto bvo = getBVOFromVO(gt);
        LOG4(exp,"reduced under ",gt.to_string()," via ",*st);
        if (updateBinaries(bvo._x,bvo._y,ReducibilityEntryGround2::toBitset(bvo._c))) {
          return true;
        }
      }
      decltype(ptr->reducedUnder)::Iterator bit(ptr->reducedUnder);
      while (bit.hasNext()) {
        pair<unsigned,unsigned> p;
        const auto& bv = bit.nextRef(p);
        if (!gt.is_empty()) {
          auto bvo = getBVOFromVO(gt);
          auto bvo_bv = ReducibilityEntryGround2::toBitset(bvo._c);
          if (p.first != bvo._x || p.second != bvo._y || (bv & bvo_bv).none()) {
            continue;
          }
          LOG4(exp,"reduced under ",bvo_bv," via ",*st);
          if (updateBinaries(bvo._x,bvo._y,bvo_bv)) {
            return true;
          }
        } else {
          LOG4(exp,"reduced under ",bv," via ",*st);
          if (updateBinaries(p.first,p.second,bv)) {
            return true;
          }
        }
      }
      if (gt.is_empty()) {
        stit.right();
      }
    }
  }

  if (rwTermS) {
    auto ptr = getCacheEntryForTermGround(rwTermS);
    ASS(ptr->valid);
    DHSet<TermList>::Iterator rIt(ptr->reducesTo);
    while (rIt.hasNext()) {
      auto rhs = rIt.next();
      VarOrder gt;
      if (!_ord.makeGreater(*tgtTermS,rhs,gt) || gt.size()>2) {
        continue;
      }
      if (!gt.is_empty()) {
        auto bvo = getBVOFromVO(gt);
        LOG4(exp,"reduced under ",gt.to_string()," via rhs ",rhs);
        if (updateBinaries(bvo._x,bvo._y,ReducibilityEntryGround2::toBitset(bvo._c))) {
          return true;
        }
      } else {
        LOG4(exp,"reduced under ",gt.to_string()," via rhs ",rhs);
        return true;
      }
    }
    DHMap<BinaryVarOrder,TermList,BinaryVarOrder::Hash,BinaryVarOrder::Hash>::Iterator rcIt(ptr->reducesToCond);
    while (rcIt.hasNext()) {
      BinaryVarOrder bvo;
      auto& rhs = rcIt.nextRef(bvo);
      auto gt = getVOFromBVO(bvo);
      if (!_ord.isGreater(*tgtTermS,rhs,gt)) {
        continue;
      }
      LOG4(exp,"reduced under ",gt.to_string()," via rhs ",rhs);
      if (updateBinaries(bvo._x,bvo._y,ReducibilityEntryGround2::toBitset(bvo._c))) {
        return true;
      }
    }
  }
  return false;
}

// cheaper but returns more false negatives
bool ReducibilityChecker::checkLiteral(Term* rwTermS, TermList* tgtTermS, vstringstream& exp)
{
  ASS(_sidesToCheck.isNonEmpty());

  for (Term* side : _sidesToCheck) {
    NonVariableNonTypeIterator stit(side, !side->isLiteral());
    while (stit.hasNext()) {
      auto st = stit.next();
      LOG2(exp,"checking subterm ",st->toString());
      if (!_attempted.insert(st)) {
        LOG1(exp,"already checked");
        stit.right();
        continue;
      }
      if (rwTermS && !_ord.isGreater(TermList(rwTermS),TermList(st),_rwTermState,&_constraintsFromComparison)) {
        for (const auto& c : _constraintsFromComparison) {
          auto l = get<0>(c);
          auto r = get<1>(c);
          auto strict = get<2>(c);
          bool reversed = l > r;
          auto idx_x = std::min(l,r);
          auto idx_y = std::max(l,r);
          size_t idx = idx_y*(idx_y-1)/2 + idx_x;
          size_t pos_gt = 3*idx;
          size_t pos_eq = 3*idx+1;
          size_t pos_lt = 3*idx+2;

          if (st->isReduced() || (st->reducesUnder() & 1UL << (reversed ? pos_lt : pos_gt))) {
            _reducedUnder |= 1UL << (reversed ? pos_lt : pos_gt);
          }
          if (!strict && (st->isReduced() || (st->reducesUnder() & 1UL << pos_eq))) {
            _reducedUnder |= 1UL << pos_eq;
          }
          if (isReducedUnderAny(_reducedUnder)) {
            TIME_TRACE("conditionally reduced");
            return true;
          }
        }
        LOG1(exp,"not greater");
        continue;
      }

      auto ptr = getCacheEntryForTerm(st);
      if (st->isReduced()) {
        LOG1(exp,"reduced");
        return true;
      }
      _reducedUnder |= st->reducesUnder();
      if (isReducedUnderAny(_reducedUnder)) {
        TIME_TRACE("conditionally reduced");
        return true;
      }
      LOG1(exp,"not reduced");
      stit.right();
    }
  }

  return false;
}

}