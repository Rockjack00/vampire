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
 * @file RobSubstitution.cpp
 * Implements polynomial modification of the Robinson unification algorithm.
 */


#include "RobSubstitution.hpp"

#include "Debug/Assertion.hpp"
#include "Debug/Output.hpp"
#include "Debug/Tracer.hpp"
#include "Kernel/BottomUpEvaluation.hpp"
#include "Kernel/NumTraits.hpp"
#include "Lib/DArray.hpp"
#include "Lib/DHSet.hpp"
#include "Lib/DHMap.hpp"
#include "Lib/Int.hpp"

#include "Renaming.hpp"
#include "SortHelper.hpp"
#include "TermIterators.hpp"

namespace Kernel
{

using namespace Lib;

std::ostream& operator<<(std::ostream& out, TermSpec const& self)
{ return self._self.match([&](CompositeTermSpec const& self) -> ostream& { 
        forAnyNumTraits([&](auto n) -> Option<tuple<>> {
            if (n.isAdd(self.functor)) {
              out << "(" << self.arg(0) << " + " << self.arg(1) << ")";
              return some(make_tuple());
            } else if (n.isMinus(self.functor)) {
              out << "-" << self.arg(0);
              return some(make_tuple());
            } else if (n.isMul(self.functor)) {
              out << "(" << self.arg(0) << " * " << self.arg(1) << ")";
              return some(make_tuple());
            } else return {};
        }) || [&]() -> tuple<> {
          out << env.signature->getFunction(self.functor)->name() << "(" << commaSep(self.argsIter()) << ")"; 
          return make_tuple();
        };
        return out;
    },
                          [&](AtomicTermSpec    const& self) -> decltype(auto) { return out << self.term << "/" << self.index; }); }


bool TermSpec::isOutputVar() const
{ return _self.match([](CompositeTermSpec const&) { return false; },
                     [](AtomicTermSpec const& self) { ASS(self.index != RobSubstitution::UNBOUND_INDEX || self.term.isVar()); return  self.index == RobSubstitution::UNBOUND_INDEX; }); }


TermList::Top TermSpec::top() const
{ return _self.match([](CompositeTermSpec const& a) { return TermList::Top::functor(a.functor); },
                     [](AtomicTermSpec const& old) { return old.term.top(); }); }

TermSpec const& TermSpec::deref(RobSubstitution const* s) const&
 { return s->derefBound(*this); };

bool TermSpec::definitelyGround() const
{ return _self.match([](CompositeTermSpec const& a) { return iterTraits(a.argsIter()).all([](auto& x) { return x.definitelyGround(); }); },
                     [](AtomicTermSpec const& t) { return t.term.isTerm() && t.term.term()->shared() && t.term.term()->ground(); }); }

unsigned TermSpec::weight() const
{ 
  ASS(definitelyGround())
  return _self.match([](CompositeTermSpec const& a) { return iterTraits(a.argsIter()).map([](auto& x) { return x.weight(); }).sum(); },
                     [](AtomicTermSpec const& t) { return t.term.term()->weight(); }); }

const int RobSubstitution::SPECIAL_INDEX=-2;
const int RobSubstitution::UNBOUND_INDEX=-1;

bool TermSpec::sameTermContent(TermSpec const& other) const
{
  if (top() != other.top()) return false;
  if (isVar()) {
    ASS(other.isVar())
    return (isSpecialVar() && other.isSpecialVar()) || varSpec().index == other.varSpec().index;
  } else {
    ASS(isTerm())
    ASS(other.isTerm())
    auto t1 =       _self.as<AtomicTermSpec>();
    auto t2 = other._self.as<AtomicTermSpec>();
    if (t1.isSome() && t2.isSome()) {
      return t1->term == t2->term && (
           (t1->index == t2->index)
        || (t1->term.term()->shared() && t1->term.term()->ground())
        || (t1->term.term()->arity() == 0)
        );
    } else {
      return allArgs().zip(other.allArgs())
       .all([](auto pair) { return pair.first.sameTermContent(pair.second); });
    }
  }
}

bool TermSpec::isSpecialVar() const 
{ return _self.match([](CompositeTermSpec const&)             { return false; },
                     [](AtomicTermSpec const& self) { return self.term.isSpecialVar(); }); }

bool TermSpec::isVar() const 
{ return _self.match([](CompositeTermSpec const&)             { return false; },
                     [](AtomicTermSpec const& self) { return self.term.isVar(); }); }

bool TermSpec::isTerm() const 
{ return _self.match([](CompositeTermSpec const&)             { return true; },
                     [](AtomicTermSpec const& self) { return self.term.isTerm(); }); }

bool TermSpec::isLiteral() const 
{ return _self.match([](CompositeTermSpec const&)             { return false; },
                     [](AtomicTermSpec const& self) { return self.term.isTerm() && self.term.term()->isLiteral(); }); }

bool TermSpec::isSort() const 
{ return _self.match([](CompositeTermSpec const& a)           { return a.isSort(); },
                     [](AtomicTermSpec const& self) { return self.term.term()->isSort(); }); }


VarSpec TermSpec::varSpec() const 
{ 
  auto s = _self.as<AtomicTermSpec>();
  return VarSpec(s->term.var(), s->term.isSpecialVar() ? RobSubstitution::SPECIAL_INDEX : s->index);
}

unsigned TermSpec::functor() const
{ return _self.match([](CompositeTermSpec const& a)           { return a.functor; },
                     [](AtomicTermSpec const& self) { return self.term.term()->functor(); }); }


unsigned TermSpec::nTypeArgs() const 
{ return _self.match([](CompositeTermSpec const& a)           { return env.signature->getFunction(a.functor)->numTypeArguments(); },
                     [](AtomicTermSpec const& self) { return self.term.term()->numTypeArguments(); }); }

unsigned TermSpec::nTermArgs() const 
{ return _self.match([](CompositeTermSpec const& a)           { return env.signature->getFunction(a.functor)->numTermArguments(); },
                     [](AtomicTermSpec const& self) { return self.term.term()->numTermArguments(); }); }

unsigned TermSpec::nAllArgs() const
{ return _self.match([](CompositeTermSpec const& a)           { return a.args.map([](auto& x) { return x->size(); }).unwrapOr(0); },
                     [](AtomicTermSpec const& self) { return self.term.term()->arity(); }); }


TermSpec TermSpec::termArg(unsigned i) const
{ return _self.match([&](CompositeTermSpec const& a)           { return a.arg(i + nTypeArgs()).clone(); },
                     [&](AtomicTermSpec const& self) { return TermSpec(self.term.term()->termArg(i), self.index); }); }

TermSpec TermSpec::typeArg(unsigned i) const
{ return _self.match([&](CompositeTermSpec const& a)           { return a.arg(i).clone(); },
                     [&](AtomicTermSpec const& self) { return TermSpec(self.term.term()->typeArg(i), self.index); }); }

TermSpec TermSpec::anyArg(unsigned i) const
{ return _self.match([&](CompositeTermSpec const& a)           { return a.arg(i).clone(); },
                     [&](AtomicTermSpec const& self) { return TermSpec(*self.term.term()->nthArgument(i), self.index); }); }


TermList TermSpec::toTerm(RobSubstitution& s) const
{ return _self.match([&](CompositeTermSpec const& a)           { return TermList(Term::createFromIter(a.functor, iterTraits(a.argsIter()).map([&](auto& t) { return t.toTerm(s); }))); },
                     [&](AtomicTermSpec const& self) { return s.apply(self.term, self.index); }); }

TermSpec TermSpec::sort() const
{ return _self.match([&](CompositeTermSpec const& a)           -> TermSpec { 
                        auto f = env.signature->getFunction(a.functor)->fnType();
                        ASS_REP(f->numTypeArguments() == 0, "TODO: tricky because of polymorphism...")
                        return TermSpec(f->result(), {});
                     },
                     [&](AtomicTermSpec const& self) -> TermSpec { return TermSpec(SortHelper::getResultSort(self.term.term()), self.index); }); }


/**
 * Unify @b t1 and @b t2, and return true iff it was successful.
 */
bool RobSubstitution::unify(TermList t1,int index1, TermList t2, int index2)
{
  CALL("RobSubstitution::unify/4");
  return unify(TermSpec(t1,index1), TermSpec(t2,index2));
}

/**
 * Unify arguments of @b t1 and @b t2, and return true iff it was successful.
 *
 * @b t1 and @b t2 can be either terms or literals.
 */
bool RobSubstitution::unifyArgs(Term* t1,int index1, Term* t2, int index2)
{
  CALL("RobSubstitution::unifyArgs");
  ASS_EQ(t1->functor(),t2->functor());

  TermList t1TL(t1);
  TermList t2TL(t2);

  return unify(TermSpec(t1TL,index1), TermSpec(t2TL,index2));
}

bool RobSubstitution::match(TermList base,int baseIndex,
	TermList instance, int instanceIndex)
{
  CALL("RobSubstitution::match(TermList...)");
  return match(TermSpec(base,baseIndex), TermSpec(instance,instanceIndex));
}
/**
 * Match arguments of @b t1 and @b t2, and return true iff it was successful.
 *
 * @b t1 and @b t2 can be either terms or literals.
 */
bool RobSubstitution::matchArgs(Term* base,int baseIndex,
	Term* instance, int instanceIndex)
{
  CALL("RobSubstitution::match(Literal*...)");
  ASS_EQ(base->functor(),instance->functor());

  TermList baseTL(base);
  TermList instanceTL(instance);
  return match(TermSpec(baseTL,baseIndex), TermSpec(instanceTL,instanceIndex));
}

/**
 * Bind variables from @b denormalizedIndex to variables in @b normalIndex
 * in a way, that applying the substitution to a term in @b denormalizedIndex
 * would give the same result as first renaming variables and then applying
 * the substitution in @b normalIndex.
 *
 * @warning All variables, that occured in some term that was matched or unified
 * in @b normalIndex, must be also present in the @b normalizer.
 */
void RobSubstitution::denormalize(const Renaming& normalizer, int normalIndex, int denormalizedIndex)
{
  CALL("RobSubstitution::denormalize");

  VirtualIterator<Renaming::Item> nit=normalizer.items();
  while(nit.hasNext()) {
    Renaming::Item itm=nit.next();
    VarSpec normal(itm.second, normalIndex);
    VarSpec denormalized(itm.first, denormalizedIndex);
    ASS(!_bank.find(denormalized));
    bindVar(denormalized,normal);
  }
}

bool RobSubstitution::isUnbound(VarSpec v) const
{
  CALL("RobSubstitution::isUnbound");
  for(;;) {
    auto binding = _bank.find(v);
    if(binding.isNone() || binding->isOutputVar()) {
      return true;
    } else if(binding->isTerm()) {
      return false;
    }
    v = binding->varSpec();
  }
}

/**
 * If special variable @b specialVar is bound to a proper term,
 * return a term, that has the same top functor. Otherwise
 * return an arbitrary variable.
 */
TermList::Top RobSubstitution::getSpecialVarTop(unsigned specialVar) const
{
  VarSpec v(specialVar, SPECIAL_INDEX);
  for(;;) {
    auto binding = _bank.find(v);
    if(binding.isNone() || binding->isOutputVar()) {
      static TermList auxVarTerm(1,false);
      return auxVarTerm.top();
    } else if(binding->isTerm()) {
      return binding->top();
    }
    v = binding->varSpec();
  }
}

/**
 * If @b t is a non-variable, return @b t. Else, if @b t is a variable bound to
 * a non-variable term, return the term. Otherwise, return the root variable
 * to which @b t belongs.
 */
TermSpec const& RobSubstitution::derefBound(TermSpec const& t_) const
{
  CALL("RobSubstitution::derefBound");
  TermSpec const* t = &t_;
  for(;;) {
    if (t->isTerm() || t->isOutputVar()) {
      return *t;
    } else {
      auto binding = _bank.find(t->varSpec());
      if (!binding || binding->isOutputVar()) {
        return *t;
      } else {
        t = &binding.unwrap();
      }
    }
  }
}

/**
 * If @b v is a bound variable then return the term or root variable
 * it is bound to. Otherwise, return the next unbound variable in the
 * UNBOUND_INDEX. This effectively names unbound variables apart from
 * any variables in the range of bound variables.
 */
TermSpec const& RobSubstitution::deref(VarSpec v) const
{
  CALL("RobSubstitution::deref");
  for(;;) {
    auto binding = _bank.find(v);
    if(binding.isNone()) {
      const_cast<RobSubstitution&>(*this).bindVar(v,VarSpec(_nextUnboundAvailable++, UNBOUND_INDEX));
      return _bank.get(v);
    } else if(binding->isOutputVar() || binding->isTerm()) {
      return binding.unwrap();
    }
    v = binding->varSpec();
  }
}

void RobSubstitution::bind(const VarSpec& v, TermSpec b)
{
  CALL("RobSubstitution::bind");
  //Aux terms don't contain special variables, ergo
  //should be shared.
  //ASS(!b.term.isTerm() || b.index!=AUX_INDEX || b.term.term()->shared());
  ASS_NEQ(v.index, UNBOUND_INDEX);

  if(bdIsRecording()) {
    bdAdd(new BindingBacktrackObject(this, v));
  }
  _bank.set(v,std::move(b));
}

void RobSubstitution::bindVar(const VarSpec& var, const VarSpec& to)
{
  CALL("RobSubstitution::bindVar");
  ASS_NEQ(var,to);

  bind(var,TermSpec(to));
}

VarSpec RobSubstitution::root(VarSpec v) const
{
  CALL("RobSubstitution::root");
  for(;;) {
    auto binding = _bank.find(v);
    if(binding.isNone() || binding->isOutputVar() || binding->isTerm()) {
      return v;
    }
    v = binding->varSpec();
  }
}

bool RobSubstitution::occurs(VarSpec const& toFind_, TermSpec const& ts_)
{
  VarSpec toFind = root(toFind_);
  TermSpec ts = derefBound(ts_).clone();
  if(ts.isVar()) {
    return false;
  }
  typedef DHSet<VarSpec, VarSpec::Hash1, VarSpec::Hash2> EncounterStore;
  Recycled<EncounterStore> encountered;
  Recycled<Stack<TermSpec>> todo;
  todo->push(std::move(ts));

  while (todo->isNonEmpty()){
    auto ts = todo->pop();
    if (ts.isVar()) {
      VarSpec tvar = root(ts.varSpec());
      if(tvar == toFind) {
        return true;

      } else if(!encountered->find(tvar)) {
        TermSpec dtvar = derefBound(TermSpec(tvar)).clone();
        if(!dtvar.isVar()) {
          encountered->insert(tvar);
          todo->push(std::move(dtvar));
        }
      }

    } else {
      todo->loadFromIterator(ts.allArgs());
    }
  }

  return false;
}

bool RobSubstitution::unify(TermSpec s, TermSpec t)
{
  CALL("RobSubstitution::unify/2");
#define DEBUG_UNIFY(lvl, ...) if (lvl < 0) DBG("unify: ", __VA_ARGS__)
  DEBUG_UNIFY(0, *this, ".unify(", s, ",", t, ")")


  if(s.sameTermContent(t)) {
    return true;
  }

  BacktrackData localBD;
  bdRecord(localBD);

  static Stack<UnificationConstraint> toDo(64);
  ASS(toDo.isEmpty());
  toDo.push(UnificationConstraint(std::move(s), std::move(t)));

  // Save encountered unification pairs to avoid
  // recomputing their unification
  Recycled<DHSet<UnificationConstraint>> encountered;

  auto pushTodo = [&](auto pair) {
      // we unify each subterm pair at most once, to avoid worst-case exponential runtimes
      // in order to safe memory we do ot do this for variables.
      // (Note by joe:  didn't make this decision, but just keeping the implemenntation 
      // working as before. i.e. as described in the paper "Comparing Unification 
      // Algorithms in First-Order Theorem Proving", by Krystof and Andrei)
      if (pair.lhs().isVar() && isUnbound(pair.lhs().varSpec()) &&
          pair.rhs().isVar() && isUnbound(pair.rhs().varSpec())) {
        toDo.push(std::move(pair));
      } else if (!encountered->find(pair)) {
        encountered->insert(pair.clone());
        toDo.push(std::move(pair));
      }
  };

  bool mismatch=false;
  // Iteratively resolve unification pairs in toDo
  // the current pair is always in t1 and t2 with their dereferenced
  // version in dt1 and dt2
  while (toDo.isNonEmpty()) {
    auto x = toDo.pop();
    TermSpec const& dt1 = derefBound(x.lhs());
    TermSpec const& dt2 = derefBound(x.rhs());
    DEBUG_UNIFY(1, "next pair: ", tie(dt1, dt2))
    // If they have the same content then skip
    // (note that sameTermContent is best-effort)
    if (dt1.sameTermContent(dt2)) {
    // Deal with the case where eithe rare variables
    // Do an occurs-check and note that the variable 
    // cannot be currently bound as we already dereferenced
    } else if(dt1.isVar() && !occurs(dt1.varSpec(), dt2)) {
      bind(dt1.varSpec(), dt2.clone());

    } else if(dt2.isVar() && !occurs(dt2.varSpec(), dt1)) {
      bind(dt2.varSpec(), dt1.clone());

    } else if(dt1.isTerm() && dt2.isTerm() 
           && dt1.functor() == dt2.functor()) {

      for (auto c : dt1.allArgs().zip(dt2.allArgs())) {
        pushTodo(UnificationConstraint(std::move(c.first), std::move(c.second)));
      }

    } else {
      mismatch = true;
      break;
    }

    ASS(!mismatch)
  }

  if(mismatch) {
    toDo.reset();
  }

  bdDone();

  if(mismatch) {
    localBD.backtrack();
  } else {
    if(bdIsRecording()) {
      bdCommit(localBD);
    }
    localBD.drop();
  }

  DEBUG_UNIFY(0, *this)
  return !mismatch;
}

/**
 * Matches @b instance term onto the @b base term.
 * Ordinary variables behave, as one would expect
 * during matching, but special variables aren't
 * being assigned only in the @b base term, but in
 * the instance ass well. (Special variables appear
 * only in internal terms of substitution trees and
 * this behavior allows easy instance retrieval.)
 */
bool RobSubstitution::match(TermSpec base, TermSpec instance)
{
  CALL("RobSubstitution::match(TermSpec...)");

  if(base.sameTermContent(instance)) {
    return true;
  }

  bool mismatch=false;
  BacktrackData localBD;
  bdRecord(localBD);

  static Stack<TermList*> subterms(64);
  ASS(subterms.isEmpty());

  auto obase     = base.old();
  auto oinstance = instance.old();
  TermList* bt=&obase.term;
  TermList* it=&oinstance.term;

  AtomicTermSpec binding1;
  AtomicTermSpec binding2;

  for (;;) {
    TermSpec bts(*bt,base.old().index);
    TermSpec its(*it,instance.old().index);

    if (!bts.sameTermContent(its) && TermList::sameTopFunctor(bts.old().term,its.old().term)) {
      Term* s = bts.old().term.term();
      Term* t = its.old().term.term();
      ASS(s->arity() > 0);

      bt = s->args();
      it = t->args();
    } else {
      if (! TermList::sameTopFunctor(bts.old().term,its.old().term)) {
	if(bts.old().term.isSpecialVar()) {
	  VarSpec bvs(bts.old().term.var(), SPECIAL_INDEX);
          auto binding = _bank.find(bvs);
	  if(binding) {
            binding1 = binding->old();
	    ASS_EQ(binding1.index, base.old().index);
	    bt=&binding1.term;
	    continue;
	  } else {
	    bind(bvs,its.clone());
	  }
	} else if(its.old().term.isSpecialVar()) {
	  VarSpec ivs(its.old().term.var(), SPECIAL_INDEX);
          auto binding = _bank.find(ivs);
	  if(binding) {
            binding2 = binding->old();
	    ASS_EQ(binding2.index, instance.old().index);
	    it=&binding2.term;
	    continue;
	  } else {
	    bind(ivs,bts.clone());
	  }
	} else if(bts.old().term.isOrdinaryVar()) {
	  VarSpec bvs(bts.old().term.var(), bts.old().index);
          auto binding = _bank.find(bvs);
	  if(binding) {
            binding1 = binding->old();
	    ASS_EQ(binding1.index, instance.old().index);
	    if(!TermList::equals(binding1.term, its.old().term))
	    {
	      mismatch=true;
	      break;
	    }
	  } else {
	    bind(bvs,its.clone());
	  }
	} else {
	  mismatch=true;
	  break;
	}
      }

      if (subterms.isEmpty()) {
	break;
      }
      bt = subterms.pop();
      it = subterms.pop();
    }
    if (!bt->next()->isEmpty()) {
      subterms.push(it->next());
      subterms.push(bt->next());
    }
  }

  bdDone();

  subterms.reset();


  if(mismatch) {
    localBD.backtrack();
  } else {
    if(bdIsRecording()) {
      bdCommit(localBD);
    }
    localBD.drop();
  }

  return !mismatch;
}


Stack<Literal*> RobSubstitution::apply(Stack<Literal*> cl, int index) const
{
  CALL("RobSubstitution::apply(Clause*...)");
  for (unsigned i = 0; i < cl.size(); i++) {
    cl[i] = apply(cl[i], index);
  }
  return cl;
}

Literal* RobSubstitution::apply(Literal* lit, int index) const
{
  CALL("RobSubstitution::apply(Literal*...)");
  static DArray<TermList> ts(32);

  if (lit->ground()) {
    return lit;
  }

  int arity = lit->arity();
  ts.ensure(arity);
  int i = 0;
  for (TermList* args = lit->args(); ! args->isEmpty(); args = args->next()) {
    ts[i++]=apply(*args,index);
  }
  if(lit->isTwoVarEquality()){
    TermList sort = apply(lit->twoVarEqSort(),index);
    return Literal::createEquality(lit->polarity(), ts[0], ts[1], sort);
  }

  return Literal::create(lit,ts.array());
}

TermList RobSubstitution::apply(TermList trm, int index) const
{
  CALL("RobSubstitution::apply(TermList...)");
  // DBG(*this, ".apply(", TermSpec(trm, index), ")")
  
  auto out = evalBottomUp<TermList>(AutoDerefTermSpec(TermSpec(trm, index), this), 
      [&](auto& orig, TermList* args) -> TermList {
        TermList tout;
        if (orig.term.isVar()) {
          ASS(!orig.term.isOutputVar())
          tout = TermList::var(deref(orig.term.varSpec()).varSpec().var);
        } else {
          tout = TermList(orig.term.isSort() ? AtomicSort::create(orig.term.functor(), orig.term.nAllArgs(), args)
                                              : Term::create(orig.term.functor(), orig.term.nAllArgs(), args));
        }
        return tout;
      });
  return out;

  // TODO check the use of nilVS & memorization
  // static Stack<TermList*> toDo(8);
  // static Stack<int> toDoIndex(8);
  // static Stack<Term*> terms(8);
  // static Stack<VarSpec> termRefVars(8);
  // static Stack<TermList> args(8);
  // static DHMap<VarSpec, TermList, VarSpec::Hash1, VarSpec::Hash2> known;
  //
  // //is inserted into termRefVars, if respective
  // //term in terms isn't referenced by any variable
  // const VarSpec nilVS(-1,0);
  //
  // toDo.push(&trm);
  // toDoIndex.push(index);
  //
  // while(!toDo.isEmpty()) {
  //   TermList* tt=toDo.pop();
  //   index=toDoIndex.pop();
  //   if(tt->isEmpty()) {
  //     Term* orig=terms.pop();
  //     //here we assume, that stack is an array with
  //     //second topmost element as &top()-1, third at
  //     //&top()-2, etc...
  //     TermList* argLst=&args.top() - (orig->arity()-1);
  //     args.truncate(args.length() - orig->arity());
  //     TermList constructed;
  //     if(orig->isSort()){
  //       constructed.setTerm(AtomicSort::create(static_cast<AtomicSort*>(orig),argLst));                
  //     } else {
  //       constructed.setTerm(Term::create(orig,argLst));        
  //     }
  //     args.push(constructed);
  //
  //     VarSpec ref=termRefVars.pop();
  //     if(ref!=nilVS) {
  //       ALWAYS(known.insert(ref,constructed));
  //     }
  //     continue;
  //   } else {
  //     //if tt==&trm, we're dealing with the top
  //     //term, for which the next() is undefined
  //     if(tt!=&trm) {
  //       toDo.push(tt->next());
  //       toDoIndex.push(index);
  //     }
  //   }
  //
  //   TermSpec ts(*tt,index);
  //
  //   VarSpec vs;
  //   if(ts.isVar()) {
  //     vs=root(ts.varSpec() );
  //
  //     TermList found;
  //     if(known.find(vs, found)) {
  //       args.push(found);
  //       continue;
  //     }
  //
  //     ts=deref(vs);
  //     if(ts.isVar()) {
  //       ASS(ts.isOutputVar());
  //       args.push(ts.term);
  //       continue;
  //     }
  //   } else {
  //     vs=nilVS;
  //   }
  //   Term* t = ts.term.term();
  //   if(t->shared() && t->ground()) {
  //     args.push(TermList(t));
  //     continue;
  //   }
  //   terms.push(t);
  //   termRefVars.push(vs);
  //
  //   toDo.push(t->args());
  //   toDoIndex.push(ts.index);
  // }
  // ASS(toDo.isEmpty() && toDoIndex.isEmpty() && terms.isEmpty() && args.length()==1);
  // known.reset();
  //
  //
  // return args.pop();
}

TermList RobSubstitution::apply(TermSpec t) 
{ return t.toTerm(*this); }

size_t RobSubstitution::getApplicationResultWeight(TermList trm, int index) const
{
  CALL("RobSubstitution::getApplicationResultWeight");

  return evalBottomUp<size_t>(AutoDerefTermSpec(TermSpec(trm, index), this), 
      [](auto& orig, size_t* sizes) 
      { return orig.term.isVar() ? 1 
                                 : (1 + range(0, orig.term.nAllArgs())
                                           .map([&](auto i) { return sizes[i]; })
                                           .sum()); });
}

size_t RobSubstitution::getApplicationResultWeight(Literal* lit, int index) const
{
  CALL("RobSubstitution::getApplicationResultWeight");
  static DArray<TermList> ts(32);

  if (lit->ground()) {
    return lit->weight();
  }

  size_t res = 1; //the predicate symbol weight
  for (TermList* args = lit->args(); ! args->isEmpty(); args = args->next()) {
    size_t argWeight = getApplicationResultWeight(*args,index);
    res += argWeight;
  }
  return res;
}


/**
 * Return iterator on matching substitutions of @b l1 and @b l2.
 *
 * For guides on use of the iterator, see the documentation of
 * RobSubstitution::AssocIterator.
 */
SubstIterator RobSubstitution::matches(Literal* base, int baseIndex,
	Literal* instance, int instanceIndex, bool complementary)
{
  return getAssocIterator<MatchingFn>(this, base, baseIndex,
	  instance, instanceIndex, complementary);
}

/**
 * Return iterator on unifying substitutions of @b l1 and @b l2.
 *
 * For guides on use of the iterator, see the documentation of
 * RobSubstitution::AssocIterator.
 */
SubstIterator RobSubstitution::unifiers(Literal* l1, int l1Index,
	Literal* l2, int l2Index, bool complementary)
{
  return getAssocIterator<UnificationFn>(this, l1, l1Index,
	  l2, l2Index, complementary);
}

template<class Fn>
SubstIterator RobSubstitution::getAssocIterator(RobSubstitution* subst,
	  Literal* l1, int l1Index, Literal* l2, int l2Index, bool complementary)
{
  CALL("RobSubstitution::getAssocIterator");

  if( !Literal::headersMatch(l1,l2,complementary) ) {
    return SubstIterator::getEmpty();
  }

  if( !l1->commutative() ) {
    return pvi( getContextualIterator(getSingletonIterator(subst),
	    AssocContext<Fn>(l1, l1Index, l2, l2Index)) );
  } else {
    return vi(
	    new AssocIterator<Fn>(subst, l1, l1Index, l2, l2Index));
  }
}

template<class Fn>
struct RobSubstitution::AssocContext
{
  AssocContext(Literal* l1, int l1Index, Literal* l2, int l2Index)
  : _l1(l1), _l1i(l1Index), _l2(l2), _l2i(l2Index) { ASS(!l1->isEquality()); ASS(!l2->isEquality()); } // only used for non-commutative, i.e. also non-equality, literals
  bool enter(RobSubstitution* subst)
  {
    subst->bdRecord(_bdata);
    bool res=Fn::associate(subst, _l1, _l1i, _l2, _l2i);
    if(!res) {
      subst->bdDone();
      ASS(_bdata.isEmpty());
    }
    return res;
  }
  void leave(RobSubstitution* subst)
  {
    subst->bdDone();
    _bdata.backtrack();
  }
private:
  Literal* _l1;
  int _l1i;
  Literal* _l2;
  int _l2i;
  BacktrackData _bdata;
};

/**
 * Iterator on associating[1] substitutions of two literals.
 *
 * Using this iterator requires special care, as the
 * substitution being returned is always the same object.
 * The rules for safe use are:
 * - After the iterator is created and before it's
 * destroyed, or hasNext() gives result false, the original
 * substitution is invalid.
 * - Substitution retrieved by call to the method next()
 * is valid only until the hasNext() method is called again
 * (or until the iterator is destroyed).
 * - Before each call to next(), hasNext() has to be called at
 * least once.
 *
 * There rules are quite natural, and the 3rd one is
 * required by many other iterators as well.
 *
 * Template parameter class Fn has to contain following
 * methods:
 * bool associateEqualitySorts(RobSubstitution* subst,
 *  Literal* l1, int l1Index, Literal* l2, int l2Index)
 * bool associate(RobSubstitution*, Literal* l1, int l1Index,
 * 	Literal* l2, int l2Index, bool complementary)
 * bool associate(RobSubstitution*, TermList t1, int t1Index,
 * 	TermList t2, int t2Index)
 *
 * There is supposed to be one Fn class for unification and
 * one for matching.
 *
 * [1] associate means either match or unify
 */
template<class Fn>
class RobSubstitution::AssocIterator: public IteratorCore<RobSubstitution*> {
public:
  AssocIterator(RobSubstitution* subst, Literal* l1, int l1Index, Literal* l2,
      int l2Index) :
      _subst(subst), _l1(l1), _l1i(l1Index), _l2(l2), _l2i(l2Index),
      _state(FIRST), _used(true) {
    ASS_EQ(_l1->functor(), _l2->functor());
    ASS(_l1->commutative());
    ASS_EQ(_l1->arity(), 2);
  }
  ~AssocIterator() {
    CALL("RobSubstitution::AssocIterator::~AssocIterator");
    if (_state != FINISHED && _state != FIRST) {
      backtrack(_bdataMain);
      backtrack(_bdataEqAssoc);
    }
    ASS(_bdataMain.isEmpty());
    ASS(_bdataEqAssoc.isEmpty());
  }
  bool hasNext() {
    CALL("RobSubstitution::AssocIterator::hasNext");

    if (_state == FINISHED) {
      return false;
    }
    if (!_used) {
      return true;
    }
    _used = false;

    if (_state != FIRST) {
      backtrack(_bdataMain);
    } else {
      _subst->bdRecord(_bdataEqAssoc);
      if (!Fn::associateEqualitySorts(_subst, _l1, _l1i, _l2, _l2i)) {
        backtrack(_bdataEqAssoc); // this might not be necessary
        _state = FINISHED;
        return false;
      }
    }

    _subst->bdRecord(_bdataMain);

    switch (_state) {
    case NEXT_STRAIGHT:
      if (Fn::associate(_subst, _l1, _l1i, _l2, _l2i)) {
        _state = NEXT_REVERSED;
        break;
      }
      //no break here intentionally
    case NEXT_REVERSED: {
      TermList t11 = *_l1->nthArgument(0);
      TermList t12 = *_l1->nthArgument(1);
      TermList t21 = *_l2->nthArgument(0);
      TermList t22 = *_l2->nthArgument(1);
      if (Fn::associate(_subst, t11, _l1i, t22, _l2i)) {
        if (Fn::associate(_subst, t12, _l1i, t21, _l2i)) {
          _state = NEXT_CLEANUP;
          break;
        }
        //the first successful association will be undone
        //in case NEXT_CLEANUP
      }
    }
      //no break here intentionally
    case NEXT_CLEANUP:
      //undo the previous match
      backtrack(_bdataMain);
      //undo associateEqualitySorts
      backtrack(_bdataEqAssoc);
      _state = FINISHED;
      break;
    case FINISHED:
      ASSERTION_VIOLATION;
    }
    ASS(_state != FINISHED || (_bdataMain.isEmpty() && _bdataEqAssoc.isEmpty()));
    return _state != FINISHED;
  }

  RobSubstitution* next() {
    _used = true;
    return _subst;
  }
private:
  void backtrack(BacktrackData &_bdata) {
    CALL("RobSubstitution::AssocIterator::backtrack");

    ASS_EQ(&_bdata, &_subst->bdGet());
    _subst->bdDone();
    _bdata.backtrack();
  }

  enum State {
    FIRST = 0,
    NEXT_STRAIGHT = 0,
    NEXT_REVERSED = 1,
    NEXT_CLEANUP = 2,
    FINISHED = 3
  };

  RobSubstitution* _subst;
  Literal* _l1;
  int _l1i;
  Literal* _l2;
  int _l2i;
  BacktrackData _bdataMain;
  BacktrackData _bdataEqAssoc;

  State _state;
  /**
   * true if the current substitution have already been
   * retrieved by the next() method, or if there isn't
   * any (hasNext() hasn't been called yet)
   */
  bool _used;
};

struct RobSubstitution::MatchingFn {
  static bool associateEqualitySorts(RobSubstitution* subst, Literal* l1, int l1Index,
      Literal* l2, int l2Index) {
    /* Only in the case l1 is of the form X = Y ad l2 is of the form 
       t1 = t2 can the literals be matched without their sorts being matched */
    if(l1->isTwoVarEquality()){
      ASS(l2->isEquality());
      TermList sb = SortHelper::getEqualityArgumentSort(l1);
      TermList si = SortHelper::getEqualityArgumentSort(l2);
      return subst->match(sb, l1Index, si, l2Index);
    }
    return true;
  }
  static bool associate(RobSubstitution* subst, Literal* l1, int l1Index,
	  Literal* l2, int l2Index)
  { return subst->matchArgs(l1,l1Index,l2,l2Index); }

  static bool associate(RobSubstitution* subst, TermList t1, int t1Index,
	  TermList t2, int t2Index)
  { return subst->match(t1,t1Index,t2,t2Index); }
};

struct RobSubstitution::UnificationFn {

  static bool associateEqualitySorts(RobSubstitution* subst, Literal* l1, int l1Index,
      Literal* l2, int l2Index) {
    if(l1->isEquality()) {
      ASS(l2->isEquality());
      TermList s1 = SortHelper::getEqualityArgumentSort(l1);
      TermList s2 = SortHelper::getEqualityArgumentSort(l2);
      return subst->unify(s1, l1Index, s2, l2Index);
    }
    return true;
  }

  static bool associate(RobSubstitution* subst, Literal* l1, int l1Index,
	  Literal* l2, int l2Index)
  { return subst->unifyArgs(l1,l1Index,l2,l2Index); }

  static bool associate(RobSubstitution* subst, TermList t1, int t1Index,
	  TermList t2, int t2Index)
  { return subst->unify(t1,t1Index,t2,t2Index); }
};

bool operator==(TermSpec const& lhs, TermSpec const& rhs)
{ return TermSpec::compare(lhs, rhs, [](auto& t) -> decltype(auto) { return t; }) == 0; }

bool operator<(TermSpec const& lhs, TermSpec const& rhs)
{ return TermSpec::compare(lhs, rhs, [](auto& t) -> decltype(auto) { return t; }) < 0; }

template<class HashFn>
unsigned __hash(HashFn hashFn, TermSpec const& t) {
  Recycled<Stack<TermSpec>> todo;
  todo->push(t.clone());
  unsigned hash = 0;
  while (todo->isNonEmpty()) {
    auto t = todo->pop();
    if (t.isTerm()) {
      hash = HashUtils::combine(hash, hashFn(t.functor()));
      todo->loadFromIterator(t.allArgs());
    } else {
      hash = HashUtils::combine(hash, t.varNumber(), t.varSpec().index);
    }
  }
  return 0;
}


unsigned TermSpec::defaultHash() const
{ return __hash([](auto const& x) { return DefaultHash::hash(x); }, *this); }

unsigned TermSpec::defaultHash2() const
{ return __hash([](auto const& x) { return DefaultHash2::hash(x); }, *this); }

std::ostream& operator<<(std::ostream& out, AutoDerefTermSpec const& self)
{ return out << self.term << "@" << *self.subs; }
} // namespace Kernel
