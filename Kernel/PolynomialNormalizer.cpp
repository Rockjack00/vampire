/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "PolynomialNormalizer.hpp"

// using NormalizationResult = Coproduct<PolyNf 
//         , Polynom< IntTraits>
//         , Polynom< RatTraits>
//         , Polynom<RealTraits>
//         , MonomFactors< IntTraits>
//         , MonomFactors< RatTraits>
//         , MonomFactors<RealTraits>
//         >;

namespace Kernel {

using NormalizationResult = PolyNf;

struct PolyNormTerm 
{
  TypedTermList _self;
  PolyNormTerm(TypedTermList t) : _self(std::move(t)) {}
  friend std::ostream& operator<<(std::ostream& out, PolyNormTerm const& self)
  { return out << self._self; }
};



template<class NumTraits>
struct GetSumArgs {
  static bool isAcTerm(TermList t) 
  { return t.isTerm() && t.term()->functor() == NumTraits::addF(); }    

  static TermList getAcArg(TermList t, unsigned i)
  { 
    ASS(i < 2)
    return t.term()->termArg(i);
  }
};

bool isNumeralDiv(IntTraits, Term& term, unsigned f)
{ return false; }

template<class NumTraits>
bool isNumeralDiv(NumTraits, Term& term, unsigned f)
{ 
  if (NumTraits::divF() == f) {
     auto num = NumTraits::tryNumeral(term.termArg(1));
     return num.isSome() && num.unwrap() != NumTraits::constant(0);
  }
  return false;
}


TermList getDivArg(IntTraits, Term& term, unsigned f, unsigned i)
{ ASSERTION_VIOLATION }


template<class NumTraits>
TermList getDivArg(NumTraits, Term& term, unsigned f, unsigned i)
{ 
  ASS(f == NumTraits::divF())
  return i == 0 ? term.termArg(0) 
                : NumTraits::constantTl(NumTraits::constant(1) / NumTraits::tryNumeral(term.termArg(1)).unwrap());
}

template<class NumTraits>
struct GetProductArgs {
  static bool isAcTerm(TermList t) 
  { 
    if (t.isVar()) {
      return false;
    } else {
      auto& term = *t.term();
      auto f = term.functor();

      return f == NumTraits::mulF() 
          || f == NumTraits::minusF() 
          || isNumeralDiv(NumTraits{}, term, f);
    }
  }    

  static TermList getAcArg(TermList t, unsigned i)
  { 
    ASS(i < 2);
    auto& term = *t.term();
    auto f = term.functor();
    if (f == NumTraits::minusF()) {
      return (i == 0  ? NumTraits::constantTl(-1) : term.termArg(0));
    } else if (f == NumTraits::mulF())  {
      return term.termArg(i);
    } else {
      return getDivArg(NumTraits{}, term, f, i);
    }
  }
};




#define DEBUG(...) //DBG(__VA_ARGS__)
//
//
// /** a struct that normalizes an object of type MonomFactors to a Monom */
// struct RenderMonom {
//
//   template<class NumTraits>
//   Monom<NumTraits> operator()(MonomFactors<NumTraits>&& x) const 
//   { 
//     CALL("RenderMonom::operator()(MonomFactors<Numeral>&&)")
//     using Numeral      = typename NumTraits::ConstantType;
//     using Monom        = Monom       <NumTraits>;
//     auto& raw = x.raw();
//     std::sort(raw.begin(), raw.end());
//
//     Numeral num(1);
//     bool found = false;
//     unsigned len = 0;
//     for (auto x : raw) {
//       ASS_EQ(x.power, 1)
//       Option<Numeral> attempt(x.term().template tryNumeral<NumTraits>());
//       if (!found && attempt.isSome()) {
//         found = true;
//         num = attempt.unwrap();
//       } else if (len == 0) {
//         len++;
//         raw[len - 1].term() = x.term();
//         ASS_EQ(raw[len - 1].power, 1);
//
//       } else if (raw[len - 1].term() == x.term()) {
//         raw[len - 1].power++;
//
//       } else {
//         len++;
//         raw[len - 1].term() = x.term();
//         ASS_EQ(raw[len - 1].power, 1)
//
//       }
//     }
//     raw.truncate(len);
//     ASS_EQ(raw.size(), len)
//     x.integrity();
//     return Monom(num, std::move(x));
//   }
// };
//
// /** a struct that turns any alternative of a NormalizationResult into a PolyNf */
// struct RenderPolyNf {
//
//   PolyNf operator()(PolyNf&& x) const 
//   { return std::move(x); }
//
//   template<class NumTraits>
//   PolyNf operator()(Polynom<NumTraits>&& x) const 
//   { 
//     std::sort(x.raw().begin(), x.raw().end());
//     x.integrity();
//     return PolyNf(std::move(x)); 
//   }
//
//   template<class NumTraits>
//   PolyNf operator()(MonomFactors<NumTraits>&& facs) const 
//   { 
//     auto poly = Polynom<NumTraits>(RenderMonom{}(std::move(facs)));
//     poly.integrity();
//     return (*this)(std::move(poly));
//   }
//
// };
//
//
//
// template<class NumTraits>
// NormalizationResult normalizeAdd(NormalizationResult& lhs, NormalizationResult& rhs) {
//   CALL("normalizeAdd")
//   using Polynom = Polynom<NumTraits>;
//   using Monom = Monom<NumTraits>;
//   using MonomFactors = MonomFactors<NumTraits>;
//   ASS(lhs.is<Polynom>() || lhs.is<MonomFactors>())
//   ASS(rhs.is<Polynom>() || rhs.is<MonomFactors>())
//
//   if (lhs.is<MonomFactors>() && rhs.is<Polynom>())  {
//     auto& l = lhs.unwrap<MonomFactors>();
//     auto& r = rhs.unwrap<Polynom>();
//     /* Monom(...) + Polynom(x0, x1, ...) ==> Polynom(x0, x1, ..., Monom(...)) */
//     r.raw().push(RenderMonom{}(std::move(l)));
//     return std::move(rhs);
//
//   } else if (rhs.is<MonomFactors>() && lhs.is<Polynom>()){
//     auto& r = rhs.unwrap<MonomFactors>();
//     auto& l = lhs.unwrap<Polynom>();
//
//     /*  Polynom(x0, x1, ...) + Monom(...) ==> Polynom(x0, x1, ..., Monom(...)) */
//     l.raw().push(RenderMonom{}(std::move(r)));
//     return std::move(lhs);
//
//   } else if (lhs.is<MonomFactors>() && rhs.is<MonomFactors>()) {
//     auto& l = lhs.unwrap<MonomFactors>();
//     auto& r = rhs.unwrap<MonomFactors>();
//
//     /* Monom(x0, x1, ...) + Monom(y0, y1, ...) ==> Polynom(Monom(x0, x1, ...), Polynom(y0, y1, ...)) */
//     Stack<Monom> summands(2);
//     summands.push(RenderMonom{}(std::move(l)));
//     summands.push(RenderMonom{}(std::move(r)));
//     return NormalizationResult(Polynom(std::move(summands)));
//
//   } else{
//     ASS(lhs.is<Polynom>())
//     ASS(rhs.is<Polynom>())
//     auto& l = lhs.unwrap<Polynom>();
//     auto& r = rhs.unwrap<Polynom>();
//
//     /* Polynom(x0, x1, ...) + Polynom(y0, y1, ...) ==> Polynom(x0, x1, ..., y0, y1, ...) */
//     l.raw().reserve(l.raw().size() + r.raw().size());
//     l.raw().loadFromIterator(r.raw().iter());
//     return std::move(lhs);
//   }
// }
//
//
// template<class NumTraits>
// NormalizationResult normalizeMul(NormalizationResult& lhs, NormalizationResult& rhs) {
//   CALL("normalizeMul")
//   using Polynom = Polynom<NumTraits>;
//   using MonomFactors = MonomFactors<NumTraits>;
//   using MonomFactor = MonomFactor<NumTraits>;
//   ASS(lhs.is<Polynom>() || lhs.is<MonomFactors>())
//   ASS(rhs.is<Polynom>() || rhs.is<MonomFactors>())
//
//   if (lhs.is<MonomFactors>() && rhs.is<Polynom>())  {
//     auto& l = lhs.unwrap<MonomFactors>();
//     auto& r = rhs.unwrap<Polynom>();
//     /* Monom(x0, x1, ...) * Polynom(...) ==> Monom(x0, x1, ..., Polynom(...)) */
//     l.raw().push(MonomFactor(RenderPolyNf{}(std::move(r)), 1));
//     return std::move(lhs);
//
//     // rhs.raw().push(toNormalizedMonom(std::move(lhs)));
//     // return std::move(rhs);
//
//   } else if (rhs.is<MonomFactors>() && lhs.is<Polynom>()){
//     auto& r = rhs.unwrap<MonomFactors>();
//     auto& l = lhs.unwrap<Polynom>();
//     /* Polynom(...) * Monom(x0, x1, ...) ==> Monom(x0, x1, ..., Polynom(...)) */
//     r.raw().push(MonomFactor(RenderPolyNf{}(std::move(l)), 1));
//     return std::move(rhs);
//
//     // lhs.raw().push(toNormalizedMonom(std::move(rhs)));
//     // return std::move(lhs);
//
//   } else if (lhs.is<MonomFactors>() && rhs.is<MonomFactors>()) {
//     auto& l = lhs.unwrap<MonomFactors>();
//     auto& r = rhs.unwrap<MonomFactors>();
//
//     /* Monom(x0, x1, ...) * Monom(y0, y1, ...) ==> Monom(x0, x1, ..., y0, y1, ...) */
//     l.raw().reserve(l.raw().size() + r.raw().size());
//     l.raw().loadFromIterator(r.raw().iter());
//     return std::move(lhs);
//
//   } else{
//     ASS(lhs.is<Polynom>())
//     ASS(rhs.is<Polynom>())
//     auto l = RenderPolyNf{}(std::move(lhs.unwrap<Polynom>()));
//     auto r = RenderPolyNf{}(std::move(rhs.unwrap<Polynom>()));
//
//     /* Polynom(x0, x1, ...) * Polynom(y0, y1, ...) ==> Monom(Polynom(x0, x1, ...), Polynom(y0, y1, ...)) */
//
//     return NormalizationResult(MonomFactors({
//         MonomFactor(l,1),
//         MonomFactor(r,1)
//     }));
//   }
// }
// template<class NumTraits>
// Option<NormalizationResult> normalizeSpecialized(Interpretation i, NormalizationResult* ts);
//
// template<class NumTraits>
// Option<NormalizationResult> normalizeDiv(NormalizationResult& lhs, NormalizationResult& rhs);
//
// template<class NumTraits>
// Option<NormalizationResult> normalizeSpecializedFractional(Interpretation i, NormalizationResult* ts)
// {
//   switch (i) {
//     case NumTraits::divI:
//       ASS(ts != nullptr);
//       return normalizeDiv<NumTraits>(ts[0], ts[1]);
//     default:
//       return Option<NormalizationResult>();
//   }
// }
//
//
// template<>
// Option<NormalizationResult> normalizeSpecialized<IntTraits>(Interpretation i, NormalizationResult* ts) 
// { return Option<NormalizationResult>(); }
//
// template<>
// Option<NormalizationResult> normalizeSpecialized<RatTraits>(Interpretation i, NormalizationResult* ts) 
// { return normalizeSpecializedFractional< RatTraits>(i,ts); }
//
// template<>
// Option<NormalizationResult> normalizeSpecialized<RealTraits>(Interpretation i, NormalizationResult* ts) 
// { return normalizeSpecializedFractional<RealTraits>(i,ts); }
//
// template<class NumTraits>
// struct TryNumeral {
//   using Numeral = typename NumTraits::ConstantType;
//
//   Option<Numeral> operator()(PolyNf& term) const
//   { 
//     ASSERTION_VIOLATION
//     // return term.tryNumeral<NumTraits>(); 
//   }
//
//   Option<Numeral> operator()(MonomFactors<NumTraits>& term) const
//   { 
//     if (term.raw().size() == 1)  {
//       auto fac = term.raw()[0];
//       ASS_EQ(fac.power, 1);
//       return fac.term().template tryNumeral<NumTraits>();
//     } else {
//       return Option<Numeral>();
//     }
//   }
//
//   template<class C> Option<Numeral> operator()(C& term) const
//   { return Option<Numeral>(); }
//
//
// };
//
// template<class ConstantType>
// NormalizationResult wrapNumeral(ConstantType c) 
// { 
//   using NumTraits = NumTraits<ConstantType>;
//   PolyNf numPolyNf(PolyNf(FuncTerm(FuncId::symbolOf(NumTraits::constantT(c)), nullptr)));
//   return NormalizationResult(MonomFactors<NumTraits>(numPolyNf));
// }
//
// template<class NumTraits>
// Option<NormalizationResult> normalizeDiv(NormalizationResult& lhs, NormalizationResult& rhs) {
//   using Numeral = typename NumTraits::ConstantType;
//
//   auto num = rhs.apply(TryNumeral<NumTraits>{});
//   if (num.isSome() && num.unwrap() != Numeral(0)) {
//     auto inv = wrapNumeral(Numeral(1) / num.unwrap());
//     return Option<NormalizationResult>(normalizeMul<NumTraits>(inv, lhs)); 
//   } else {
//     return Option<NormalizationResult>();
//   }
// }
//
//
// template<class NumTraits>
// NormalizationResult normalizeMinus(NormalizationResult& x) {
//   using Numeral = typename NumTraits::ConstantType;
//
//   auto minusOne = wrapNumeral(Numeral(-1));
//   return normalizeMul<NumTraits>(minusOne, x); 
// }
//
// template<class NumTraits>
// NormalizationResult normalizeNumSort(TermList t, NormalizationResult* ts) 
// {
//   CALL("normalizeNumSort(TermList,NormalizationResult)")
//   auto singletonProduct = [](PolyNf t) -> NormalizationResult {
//     return NormalizationResult(MonomFactors<NumTraits>(t));
//   };
//
//   if (t.isVar()) {
//     return singletonProduct(PolyNf(Variable(t.var())));
//
//   } else {
//     auto term = t.term();
//     auto fn = FuncId::symbolOf(term);
//     if (fn.isInterpreted()) {
//       switch(fn.interpretation()) {
//         case NumTraits::mulI:
//           ASS(ts != nullptr);
//           return normalizeMul<NumTraits>(ts[0], ts[1]);
//         case NumTraits::addI:
//           ASS(ts != nullptr);
//           return normalizeAdd<NumTraits>(ts[0], ts[1]);
//         case NumTraits::minusI:
//           ASS(ts != nullptr);
//           return normalizeMinus<NumTraits>(ts[0]);
//         default:
//         {
//           auto out = normalizeSpecialized<NumTraits>(fn.interpretation(), ts);
//           if (out.isSome()) {
//             return out.unwrap();
//           }
//         }
//       }
//     }
//
//     return singletonProduct(PolyNf(FuncTerm(
//         fn, 
//         Stack<PolyNf>::fromIterator(
//             iterTraits(getArrayishObjectIterator<mut_ref_t>(ts, fn.numTermArguments()))
//             .map( [](NormalizationResult& r) -> PolyNf { return std::move(r).apply(RenderPolyNf{}); }))
//       )
//     ));
//   }
// }
//
// #define PRINT_AND_RETURN(...)                                                                                 \
//   auto f = [&](){ __VA_ARGS__ };                                                                              \
//   auto out = f();                                                                                             \
//   DBG("out : ", out);                                                                                         \
//   return out;                                                                                                 \
//

PolyNf normalizeTerm(TypedTermList t, bool& evaluated)
{
  CALL("PolyNf::normalize")
  TIME_TRACE("PolyNf::normalize")
  DEBUG("normalizing ", t)
  Memo::None<PolyNormTerm,NormalizationResult> memo;
  struct Eval
  {
    bool& evaluated;

    using Arg    = PolyNormTerm;
    using Result = NormalizationResult;

    NormalizationResult operator()(PolyNormTerm t_, NormalizationResult* ts, unsigned nTs) const
    { 
      // ASSERTION_VIOLATION_REP("unimplemented")
      auto t = t_._self;
      DBG("normalizing ", t)
      if (t.isVar()) {
        return PolyNf(Variable(t.var()));
      } else {
        auto term = t.term();
        auto f = term->functor();
        auto poly = tryNumTraits([&](auto n) -> Option<PolyNf> {
            using NumTraits = decltype(n);
            using Numeral = typename NumTraits::ConstantType;

            if (NumTraits::addF() == f) {
              auto summands = range(0, nTs)
                .map([&](auto i) { return Monom<NumTraits>::fromNormalized(ts[i].denormalize()); })
                .template collect<Stack<Monom<NumTraits>>>();
              std::sort(summands.begin(), summands.end());
              auto out = some(PolyNf(AnyPoly(Polynom<NumTraits>(std::move(summands)))));
              return out;
            } else if (GetProductArgs<NumTraits>::isAcTerm(t)) {
              Stack<pair<PolyNf, unsigned>> facs = range(0, nTs)
                .map([&](auto i) { return make_pair(ts[i], unsigned(0)); })
                .template collect<Stack>();

              std::sort(facs.begin(), facs.end(),
                  [](auto& l, auto& r) { return l.first < r.first; });

              Numeral numeral(1);
              unsigned offs = 0;
              unsigned i = 0;
              unsigned nums = 0;
              while (i < facs.size()) {
                auto n = NumTraits::tryNumeral(facs[i].first.denormalize());
                if (n.isSome()) {
                  nums++;
              DBGE(numeral)
                  numeral = numeral * n.unwrap();
                  i++;
                } else {
                  facs[offs].first = facs[i].first;
                  while (i < facs.size() && facs[i].first == facs[offs].first) {
                    facs[offs].second++;
                    i++;
                  }
                  offs++;
                }
              }
              if (nums > 1 || (nums == 1 && numeral == Numeral(1))) 
                this->evaluated = true;

              return some(PolyNf(AnyPoly(Polynom<NumTraits>(Monom<NumTraits>(
                          numeral, 
                          // TODO  pass straght a reverse sorted iterator
                          MonomFactors<NumTraits>::fromIterator(
                            range(0, offs)
                              .map([&](auto i) { return MonomFactor<NumTraits>(facs[i].first, facs[i].second); })
                            )
                          )))));
            } else {
              return Option<PolyNf>();
            }
        });
        return poly || PolyNf(FuncTerm(FuncId::symbolOf(t.term()), ts));
      }
    }
  };
  NormalizationResult r = evaluateBottomUp(PolyNormTerm(t), Eval{.evaluated = evaluated}, memo);
  return r;
}

} // namespace Kernel

namespace Lib {

template<>
struct BottomUpChildIter<Kernel::PolyNormTerm>
{
  template<class GetAcArgs>
  struct AcIter_ {

    AcIter_(AcIter_ &&) = default;

    unsigned _symbol;
    PolyNormTerm _self;
    Stack<TermList> _next;
    unsigned _idx;

    AcIter_(PolyNormTerm self) : _self(self), _next{ TermList(self._self) } {
      // ASS(self->numTermArguments() == 2)
      // ASS(SortHelper::getTermArgSort(self, 0) == SortHelper::getResultSort(self))
      // ASS(SortHelper::getTermArgSort(self, 1) == SortHelper::getResultSort(self))
    }

    Kernel::PolyNormTerm self() { return _self; }
    Kernel::PolyNormTerm next() 
    { 
      auto val = _next.pop();
      
      // while (val.isTerm() && val.term()->functor() == _self->functor()) {
      while (GetAcArgs::isAcTerm(val)) {
        _next.push(GetAcArgs::getAcArg(val, 1));
        val = GetAcArgs::getAcArg(val, 0);
      }
      return TypedTermList(val, _self._self.sort()); 
    }

    bool hasNext() { return _next.isNonEmpty(); }
  };

  struct AcIter {
    AcIter(AcIter &&) = default;
    unsigned _symbol;
    Term* _self;
    Stack<TermList> _next;
    unsigned _idx;
    AcIter(Term* self) : _self(self), _next{ TermList(self) } {
      ASS(self->numTermArguments() == 2)
      ASS(SortHelper::getTermArgSort(self, 0) == SortHelper::getResultSort(self))
      ASS(SortHelper::getTermArgSort(self, 1) == SortHelper::getResultSort(self))
    }

    Kernel::PolyNormTerm self() { return PolyNormTerm(TypedTermList(_self)); }
    Kernel::PolyNormTerm next() 
    { 
      auto val = _next.pop();
      while (val.isTerm() && val.term()->functor() == _self->functor()) {
        _next.push(val.term()->termArg(1));
        val = val.term()->termArg(0);
      }
      return TypedTermList(val, TypedTermList(_self).sort()); 
    }

    bool hasNext() { return _next.isNonEmpty(); }
  };

  struct Uninter {
    PolyNormTerm _self;
    unsigned _idx;
    Uninter(PolyNormTerm self) : _self(std::move(self)), _idx(0) {}

    Kernel::PolyNormTerm self() { return _self; }

    Kernel::PolyNormTerm next() 
    { 
      auto out = TypedTermList(_self._self.term()->termArg(_idx), SortHelper::getTermArgSort(_self._self.term(), _idx)); 
      _idx++; 
      return out; 
    }

    bool hasNext() 
    { return _self._self.isTerm() && _idx < _self._self.term()->numTermArguments(); }

    // unsigned nChildren()
    // { return _self._self.isVar() ? 0 : _self._self.term()->numTermArguments(); }
  };

  static bool isSum (unsigned functor) { return forAnyNumTraits([=](auto n) { return n.addF() == functor; }); }
  static bool isProd(unsigned functor) { return forAnyNumTraits([=](auto n) { return n.mulF() == functor; }); }
  static bool isSum (TermList t) { return t.isTerm() && isSum (t.term()->functor()); }
  static bool isProd(TermList t) { return t.isTerm() && isProd(t.term()->functor()); }
  //
  // Coproduct<AcIter, Uninter> _self;
  // BottomUpChildIter(Kernel::PolyNormTerm t) 
  //   : _self((isSum(t._self) || isProd(t._self))
  //                      ? decltype(_self)(AcIter(t._self.term()))
  //                      : decltype(_self)(Uninter(t))) 
  //   {}


  using Inner = Coproduct< Uninter
                         , AcIter_<GetProductArgs< IntTraits>>
                         , AcIter_<GetProductArgs< RatTraits>>
                         , AcIter_<GetProductArgs<RealTraits>>
                         , AcIter_<GetSumArgs    < IntTraits>>
                         , AcIter_<GetSumArgs    < RatTraits>>
                         , AcIter_<GetSumArgs    <RealTraits>>
                         >;
  Inner _self;
  BottomUpChildIter(Kernel::PolyNormTerm t) 
    : _self( GetProductArgs< IntTraits>::isAcTerm(t._self) ? Inner(AcIter_<GetProductArgs< IntTraits>>(t))
           : GetProductArgs< RatTraits>::isAcTerm(t._self) ? Inner(AcIter_<GetProductArgs< RatTraits>>(t))
           : GetProductArgs<RealTraits>::isAcTerm(t._self) ? Inner(AcIter_<GetProductArgs<RealTraits>>(t))
           : GetSumArgs    < IntTraits>::isAcTerm(t._self) ? Inner(AcIter_<GetSumArgs    < IntTraits>>(t))
           : GetSumArgs    < RatTraits>::isAcTerm(t._self) ? Inner(AcIter_<GetSumArgs    < RatTraits>>(t))
           : GetSumArgs    <RealTraits>::isAcTerm(t._self) ? Inner(AcIter_<GetSumArgs    <RealTraits>>(t))
           : Inner(Uninter(t)) )
           {}

  Kernel::PolyNormTerm next() 
  { return _self.apply([](auto& x) { return x.next(); }); }

  bool hasNext()
  { return _self.apply([](auto& x) { return x.hasNext(); }); }

  Kernel::PolyNormTerm self()
  { return _self.apply([](auto& x) { return x.self(); }); }
};

} // namespace Lib
