#include "Debug/Assertion.hpp"
#include "Inferences/ArithmeticSubtermGeneralization.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/PolynomialNormalizer.hpp"

#define TODO ASSERTION_VIOLATION
#define DEBUG(...) DBG(__VA_ARGS__)

namespace Inferences {

/**
 * Rule' 1)
 *   generalize multiplication
 *   C[k * X] 
 *   ------------ 
 *   C[X]
 *   where 
 *   - k is a non-zero number term
 *   - all occurences of X are in terms of the form `k * X`
 *   - sound due to substitution X -> (1/k) * X
 *   - generalization since obviously
 *
 *
 * Rule' 2)
 *   generalize multiplication
 *   C[X + d] 
 *   ------------ 
 *   C[X]
 *   where 
 *   - all occurences of X are in terms of the form `X + d`
 *   - sound due to substitution X -> X - d
 *   - generalization since obviously
 *
 *
 * Algorithm: 
 * Generalization : Meet Semilattice
 * let map : Var -> Generalization
 *
 * // rule 1
 * for term in clause:
 *   for poly in formula:
 *     for summand in poly:
 *        let generalization = summand \ v
 *        if map contains v 
 *          map[v] = map[v] `meet` varCoeff
 *        else              
 *          map[v] = v
 *
 * let (var, gen) = getAnyEntry(map)
 *
 * newClause = {}
 * for term in clause:
 *   term' = term.generalizeBasedOn(gen)
 *   newClause.replace(term, term')
 *
 *
 */

template<class NumTraits> class GeneralizeMul;
template<class NumTraits> class GeneralizeAdd;

template<class Generalization>
struct ArithmeticSubtermGeneralization
{
  static Clause* simplify(Clause* cl);
};

struct Top {};
struct Bot {};

ostream& operator<<(ostream& out, Bot self) { return out << "bot"; }
ostream& operator<<(ostream& out, Top self) { return out << "top"; }
bool operator==(Top,Top) { return true; }
bool operator==(Bot,Bot) { return true; }

template<template<class> class VarGeneralization> struct VarMap;

template<class NumTraits> struct FnGetInner;
template<class NumTraits> struct FnGetInnerConst;

template<class NumTraits>
void sortByMonom(Stack<PolyPair<NumTraits>>& s)
{ std::sort(s.begin(), s.end()); }

template<class Gen>
struct BuildGeneralizedTerm
{
  Variable var;
  Gen const& gen;
  using Arg    = PolyNf;
  using Result = PolyNf;

  PolyNf operator()(PolyNf term, PolyNf* evaluatedArgs) 
  {
    CALL("BuildGeneralizedTerm::operator()")
    auto out = term.match(
        [&](UniqueShared<FuncTerm> t) -> PolyNf
        { return unique(FuncTerm(t->function(), evaluatedArgs)); },

        [&](Variable v) 
        { return v; },

        [&](AnyPoly p) 
        // { return p.apply(ApplyGeneralizationToPolynom<Gen> {map, evaluatedArgs,}); }
        { return PolyNf(Gen::generalize(var, gen, p, evaluatedArgs)); }
        );
    return out;
  }
};

template<class Gen>
Clause* ArithmeticSubtermGeneralization<Gen>::simplify(Clause* cl_) 
{
  Map<Variable, Gen> map;

  /* populate the map, and computing meets */
  auto& cl = *cl_;
  DEBUG("input clause: ", cl);
  for (unsigned i = 0; i < cl.size(); i++) {
    auto lit = cl[i];
    for (unsigned j = 0; j < lit->arity(); j++) {
      auto norm = PolyNf::normalize(*lit->nthArgument(j));
      norm.iter()
          .forEach([&](PolyNf p) { 
              if (p.is<AnyPoly>()) {
                Gen::processPoly(map, p.unwrap<AnyPoly>());
              }
            });
    }
  }

  /* select an applicable generalization */

  DEBUG("canidated generalizations: ", map)
  using Opt = Optional<typename decltype(map)::Entry&>;
  Opt selected;
  {
    auto iter = map.iter();
    while (iter.hasNext()) {
      auto& gen = iter.next();
      if (!gen.value().isBot()) {
        selected = Opt(gen);
        break;
      }
    }
  }
  if (selected.isNone()) {
    return cl_;
  } 

  auto& var            = selected.unwrap().key();
  auto& generalization = selected.unwrap().value();
  DEBUG("selected generalization: ", var, " -> ", generalization)


  /* apply the selectedGen generalization */
  bool anyChange = false;

  auto stack = iterTraits(cl.iterLits())
    .map([&](Literal* lit) {
        auto args = argIter(lit)
          .map([&](TermList term) -> TermList { 
              auto norm = PolyNf::normalize(term);
              BuildGeneralizedTerm<Gen> eval { var, generalization };
              auto res = evaluateBottomUp(norm, eval);
              if (res != norm) {
                anyChange = true;
                DEBUG("generalized: ", norm, " -> ", res);
                return res.toTerm();
              } else {
                return term;
              }
          })
          .template collect<Stack>();
        return Literal::create(lit, &args[0]);
    })
    .template collect<Stack>();

  ASS (anyChange) 
  Inference inf(SimplifyingInference1(Kernel::InferenceRule::ARITHMETIC_SUBTERM_GENERALIZATION, &cl));
  return Clause::fromStack(stack, inf);
}



template<class NumTraits>
class GeneralizeMul 
{
  using Inner = Coproduct<typename NumTraits::ConstantType, Bot>;
  Inner _inner;
  using PolyPair = PolyPair<NumTraits>;
  using Const = typename NumTraits::ConstantType;
  using Monom = Monom<NumTraits>;


private:
  GeneralizeMul(Bot b) : _inner(b) {}
public:
  GeneralizeMul& operator=(GeneralizeMul&&) = default;
  GeneralizeMul(GeneralizeMul&&) = default;

  GeneralizeMul(Const c);

  static GeneralizeMul bot() { return GeneralizeMul(Bot{}); }

  GeneralizeMul(Variable without, PolyPair pair) : GeneralizeMul(pair.coeff)
  { }

  GeneralizeMul meet(GeneralizeMul&& rhs) && {
    auto& lhs = *this;

    if (lhs._inner.template is<Bot>()) return bot();
    if (rhs._inner.template is<Bot>()) return bot();

    return meet(lhs._inner.template unwrap<Const>(), rhs._inner.template unwrap<Const>());
  }

  bool isBot() const 
  {return _inner.template is<Bot>(); }

  PolyPair cancel(PolyPair p) const;

  friend ostream& operator<<(ostream& out, GeneralizeMul const& self) 
  { return out << self._inner; }

  static AnyPoly generalize(
    Variable var,
    GeneralizeMul const& gen, 
    AnyPoly poly,
    PolyNf* generalizedArgs)
  { 
    if (poly.isType<NumTraits>()) {
      return AnyPoly(_generalize(var,gen,poly.template unwrapType<NumTraits>(),generalizedArgs)); 
    } else {
      return poly.replaceTerms(generalizedArgs);
    }
  }

  static UniqueShared<Polynom<NumTraits>> _generalize(
    Variable var,
    GeneralizeMul const& gen, 
    UniqueShared<Polynom<NumTraits>> poly,
    PolyNf* generalizedArgs);

  template<class GenMap>
  static void processPoly(GenMap& map, AnyPoly p_);
  // static AnyPoly processPoly(Variable, GeneralizeMul<NumTraits> const& map, AnyPoly p, PolyNf* evaluatedArgs);

private:
  static GeneralizeMul meet(Const lhs, Const rhs) {
    if(lhs == rhs) return GeneralizeMul(lhs);
    else return bot();
  }
};

template<class NumTraits>
GeneralizeMul<NumTraits>::GeneralizeMul(Const c) 
    : _inner(
      c == Const(1) || c == Const(0) ? Inner(Bot{}) 
                                     : Inner(c))
  {  }

template<>
GeneralizeMul<IntTraits>::GeneralizeMul(Const c) 
    : _inner(
      c == Const(-1) ? Inner(c) 
                     : Inner(Bot{}))
  {  }

template<class NumTraits>
 UniqueShared<Polynom<NumTraits>> GeneralizeMul<NumTraits>::_generalize(
  Variable var,
  GeneralizeMul const& gen, 
  UniqueShared<Polynom<NumTraits>> poly,
  PolyNf* generalizedArgs) 
{

  using Const     = typename NumTraits::ConstantType;
  using Polynom   = Kernel::Polynom<NumTraits>;
  using VarGeneralization  = GeneralizeMul<NumTraits>;
  using PolyPair  = Kernel::PolyPair<NumTraits>;
  using MonomPair = MonomPair<NumTraits>;

  auto offs = 0;
  return unique(Polynom(
              poly->iter()
               .map([&](PolyPair pair) -> PolyPair { 
                 auto found = (pair.monom->iter()
                     .find([&](MonomPair& monom) 
                       { return monom == MonomPair(var, 1); }));

                 auto newMonom = unique(pair.monom->replaceTerms(&generalizedArgs[offs]));
                 offs += newMonom->nFactors();

                 auto p = PolyPair(pair.coeff, newMonom);

                 if (found.isSome()) {
                    return gen.cancel(p);
                 } else {
                    return p;
                 }
             })
          .template collect<Stack>()));
}

template<class NumTraits>
PolyPair<NumTraits> GeneralizeMul<NumTraits>::cancel(PolyPair p) const 
{ 
   if (_inner.template is<Const>() && _inner != decltype(_inner)(Const(0))) {
      return PolyPair(Const(1), p.monom);
   } else {
      ASS(_inner.template is<Bot>());
      return PolyPair(p.coeff, p.monom);
   }
}

template<class NumTraits>
class GeneralizeAdd 
{
  using PolyPair = PolyPair<NumTraits>;
  using Const = typename NumTraits::ConstantType;
  using Monom = Monom<NumTraits>;

  // TODO get rid of this field
  Stack<PolyPair> _cancellable;

  GeneralizeAdd(decltype(_cancellable) cancel) : _cancellable(cancel) {}

public:
  GeneralizeAdd& operator=(GeneralizeAdd&&) = default;
  GeneralizeAdd(GeneralizeAdd&&) = default;
  ~GeneralizeAdd() { CALL("~GeneralizeAdd()") }

  static GeneralizeAdd bot() 
  { return GeneralizeAdd(decltype(_cancellable){}); }

  GeneralizeAdd(Variable var, UniqueShared<Polynom<NumTraits>> poly) : GeneralizeAdd(decltype(_cancellable)()) 
  {
    _cancellable.reserve(poly->nSummands() - 1);
    poly->iter().forEach([&](PolyPair const& pair){
      if (pair.tryVar() != some(var)) {
        _cancellable.push(pair);
      }
    });
    sortByMonom(_cancellable);
  }

  GeneralizeAdd meet(GeneralizeAdd&& rhs) && {
    CALL("GeneralizeAdd::meet")
    auto& lhs = *this;

    DEBUG("lhs: ", lhs)
    DEBUG("rhs: ", rhs)


    auto& l = lhs._cancellable;
    auto& r = rhs._cancellable;
    if (l.size() == 0) return std::move(lhs);
    if (r.size() == 0) return std::move(rhs);

    auto outOffs = 0;
    auto& out = l;
    auto loffs = 0;
    auto roffs = 0;
    while (loffs < l.size() && roffs < r.size()) {
      if (l[loffs] == r[roffs]) {
        out[outOffs++] = l[loffs];
        loffs++;
        roffs++;
      } else if(l[loffs] < r[roffs]) {
        loffs++;
      } else {
        roffs++;
      }
    }
    
    l.truncate(outOffs);
    DEBUG("out: ", lhs);
    return std::move(lhs);
  }

  bool isBot() const { return _cancellable.isEmpty(); }

  friend ostream& operator<<(ostream& out, GeneralizeAdd const& self)
  { return out << self._cancellable; }

  GeneralizeAdd diff(GeneralizeAdd const& rm_) && {
    CALL("GeneralizeAdd::diff")
    // DBG("in: ", *this, " - ", rm_)
    auto rm = rm_._cancellable;
 
    auto resOffs  = 0;
    auto rmOffs   = 0;
    auto thisOffs = 0;
    while (rmOffs < rm.size() && thisOffs < _cancellable.size() ) {
      if (rm[rmOffs] == _cancellable[thisOffs]) {
        thisOffs++;
      } else if (rm[rmOffs] < _cancellable[thisOffs]) {
        rmOffs++;
      } else {
        _cancellable[resOffs++] = _cancellable[thisOffs++];
      }
    }
    while (thisOffs < _cancellable.size()) {
      _cancellable[resOffs++] = _cancellable[thisOffs++];
    }
    _cancellable.truncate(resOffs);

    // DBG("out: ", *this)
    return std::move(*this);
  }

  static AnyPoly generalize(
    Variable var,
    GeneralizeAdd const& gen, 
    AnyPoly poly,
    PolyNf* generalizedArgs)
  { 
    if (poly.isType<NumTraits>()) {
      return AnyPoly(_generalize(var,gen,poly.template unwrapType<NumTraits>(),generalizedArgs)); 
    } else {
      return poly.replaceTerms(generalizedArgs);
    }
  }

  static UniqueShared<Polynom<NumTraits>> _generalize(
    Variable var,
    GeneralizeAdd<NumTraits> const& gen, 
    UniqueShared<Polynom<NumTraits>> poly,
    PolyNf* generalizedArgs);

  template<class GenMap>
  static void processPoly(GenMap& map, AnyPoly p_);

};



template<class NumTraits>
UniqueShared<Polynom<NumTraits>> GeneralizeAdd<NumTraits>::_generalize(
  Variable var,
  GeneralizeAdd<NumTraits> const& gen, 
  UniqueShared<Polynom<NumTraits>> poly,
  PolyNf* generalizedArgs) 
{

  CALL("GeneralizeAdd::generalize")
  using PolyPair = Kernel::PolyPair<NumTraits>;

  //TODO memo

  auto found = poly->iter()
    .find([&](PolyPair p) 
        { return p.tryVar() == some(var); });
  if (found.isNone()) {
    return unique(poly->replaceTerms(generalizedArgs));
  }
  auto& toCancel = gen._cancellable;

  DBGE(poly)
  DBGE(toCancel)

  Stack<PolyPair> out(poly->nSummands() - toCancel.size());

  unsigned p = 0;
  unsigned genOffs = 0;

  auto pushGeneralized = [&]()  
  { 
    auto monom = unique(poly->summandAt(p).monom->replaceTerms(&generalizedArgs[genOffs]));
    auto coeff = poly->summandAt(p).coeff;

    genOffs += monom->nFactors();
    p++;

    return out.push(PolyPair(coeff, monom));
  };

  auto skipGeneralized = [&]() 
  {
    genOffs += poly->summandAt(p).monom->nFactors();
    p++;
  };

  unsigned c = 0; 
  while (c < toCancel.size() && poly->summandAt(p) < toCancel[c]  ) {
    pushGeneralized();
  }
  while (p < poly->nSummands() && c < toCancel.size()) {
    if (toCancel[c] == poly->summandAt(p)) {
      skipGeneralized();
      c++;
    } else {
      ASS_L(poly->summandAt(p), toCancel[c]);
      pushGeneralized();
    }
  }
  while (p < poly->nSummands()) {
    pushGeneralized();
  }

  // return unique(poly->replaceTerms(generalizedArgs));
  return unique(Polynom<NumTraits>(std::move(out)));
}



template<class NumTraits>
template<class GenMap>
void GeneralizeAdd<NumTraits>::processPoly(GenMap& map, AnyPoly p_)
{
  CALL("GeneralizeAdd::processPoly")
  if (!p_.template isType<NumTraits>()) {
    return;
  }
  auto p = p_.template unwrapType<NumTraits>();
  using Const = typename NumTraits::ConstantType;

  using MonomPair = Kernel::MonomPair<NumTraits>;

  p->iter()
   .forEach([&] (PolyPair pair) {
    auto var = pair.tryVar();
    if (var.isSome()) {
      DBGE(var)
      auto v = var.unwrap();
      auto gen = GeneralizeAdd<NumTraits>(v, p);
      auto entry = map.tryGet(v);
      if (entry.isSome()) {
        auto& val = entry.unwrap();
        val = move(val).meet(std::move(gen));
      } else {
        map.insert(v, std::move(gen));
      }
    } else {
      pair.monom->iter()
         .forEach([&](MonomPair p) {
             if (p.term.template is<Variable>()) {
               auto v = p.term.template unwrap<Variable>();
               map.insert(v, GeneralizeAdd<NumTraits>::bot());
             }
           });
    }
  });
}

template<class NumTraits>
template<class GenMap>
void GeneralizeMul<NumTraits>::processPoly(GenMap& map, AnyPoly p_)
{
  if (!p_.template isType<NumTraits>()) {
    return;
  }
  auto p = p_.template unwrapType<NumTraits>();
  using MonomPair = Kernel::MonomPair<NumTraits>;

  p->iter()
   .forEach([&] (PolyPair pair) {
    auto vars = pair
        .monom->iter()
        .filter([](MonomPair p) { return p.term.template is<Variable>(); })
        .template collect<Stack>();

    if (vars.size() == 1 && vars[0].power == 1) {
      auto v = vars[0].term.template unwrap<Variable>();
      auto c = GeneralizeMul<NumTraits>(v, pair);
      auto entry = map.tryGet(v);
      if (entry.isSome()) {
        auto& val = entry.unwrap();
        val = move(val).meet(std::move(c));
      } else {
        map.insert(v, std::move(c));
      }
    } else {
      // the term contains multiple variables. This means neither of the variables can be generalized
      iterTraits(vars.iter()).forEach([&](MonomPair p) {
          auto v = p.term.template unwrap<Variable>();
          map.replaceOrInsert(v, GeneralizeMul<NumTraits>::bot());
      });
    }
  });
};

POLYMORPHIC_FUNCTION(bool,    isBot, const& t,) { return t.isBot(); }
POLYMORPHIC_FUNCTION(AnyPoly, generalize, const& gen,  
    Variable var;
    AnyPoly poly;
    PolyNf* generalizedArgs;
) { return gen.generalize(var, gen, poly, generalizedArgs); }

template<template<class> class Gen> 
class ParallelNumberGeneralization;

template<template<class> class Gen> 
struct MapWrapper 
{
  using Value = ParallelNumberGeneralization<Gen>;
  Map<Variable, ParallelNumberGeneralization<Gen>>& self;

  template<class C>
  void insert(Variable var, C&& c) 
  { self.insert(var, Value(std::move(c))); }

  template<class C>
  void replaceOrInsert(Variable var, C&& c) 
  { self.replaceOrInsert(var, Value(std::move(c))); }

  Optional<ParallelNumberGeneralization<Gen>&> tryGet(Variable var) 
  { return self.tryGet(var); }

  friend ostream& operator<<(ostream& out, MapWrapper const& self)
  { return out << self.self; }

};

template<template<class> class Gen> 
class ParallelNumberGeneralization 
{
public:
  using Inner = Coproduct<Gen<IntTraits>, Gen<RatTraits>, Gen<RealTraits>>;
private:

  Inner _inner;
public:
  template<class C>
  ParallelNumberGeneralization(Gen<C>&& inner) : _inner(std::move(inner)) {}

  using Self = ParallelNumberGeneralization;

  static void processPoly(Map<Variable, Self>& map_, AnyPoly p) 
  {
    MapWrapper<Gen> map { map_ };
    return p.match(
        [&](UniqueShared<Polynom< IntTraits>>const& p)
        { return Gen<IntTraits>::processPoly(map, p); },

        [&](UniqueShared<Polynom< RatTraits>>const& p)
        { return Gen<RatTraits>::processPoly(map, p); },

        [&](UniqueShared<Polynom<RealTraits>>const& p)
        { return Gen<RealTraits>::processPoly(map, p); }
      );
  }

  bool isBot() const { return _inner.apply(Polymorphic::isBot{}); }

  friend ostream& operator<<(ostream& out, ParallelNumberGeneralization const& self)
  { return out << self._inner; }

  static AnyPoly generalize(
    Variable var,
    ParallelNumberGeneralization const& gen, 
    AnyPoly poly,
    PolyNf* generalizedArgs) 
  { return gen._inner.apply(Polymorphic::generalize{var,poly,generalizedArgs}); }

  ParallelNumberGeneralization meet(ParallelNumberGeneralization&& rhs) && 
  {
    return move(_inner).match(
        [&](Gen< IntTraits>&& lhs) -> ParallelNumberGeneralization
        { return move(lhs).meet(move(rhs._inner).template unwrap<Gen<IntTraits>>()); },

        [&](Gen< RatTraits>&& lhs) -> ParallelNumberGeneralization
        { return move(lhs).meet(move(rhs._inner).template unwrap<Gen<RatTraits>>()); },

        [&](Gen<RealTraits>&& lhs) -> ParallelNumberGeneralization
        { return move(lhs).meet(move(rhs._inner).template unwrap<Gen<RealTraits>>()); }
        );
  }
};


Clause* AdditionGeneralization::simplify(Clause* cl) 
{ 
  CALL("AdditionGeneralization::simplify")
  return ArithmeticSubtermGeneralization<ParallelNumberGeneralization<GeneralizeAdd>>::simplify(cl); 
}

Clause* MultiplicationGeneralization::simplify(Clause* cl) 
{ 
  CALL("MultiplicationGeneralization::simplify")
  return ArithmeticSubtermGeneralization<ParallelNumberGeneralization<GeneralizeMul>>::simplify(cl); 
}

MultiplicationGeneralization::~MultiplicationGeneralization()  {}
AdditionGeneralization::~AdditionGeneralization()  {}

} // Inferences
