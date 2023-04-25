#include "QKbo.hpp"
#include "Term.hpp"
#include "NumTraits.hpp"
#include "Kernel/PolynomialNormalizer.hpp"
#include "Lib/Option.hpp"
#include "Lib/Metaiterators.hpp"
#include "Kernel/OrderingUtils.hpp"
#include "Theory.hpp"
#include "Debug/TimeProfiling.hpp"

#define DEBUG(...) // DBG(__VA_ARGS__)

namespace Kernel {

template<class T>
vstring output_to_string(T const& t) 
{
  vstringstream out;
  out << t;
  return out.str();
}

using OU = OrderingUtils2;

// Precedence makeOneSmallest(Precedence p) {
//
// }

QKbo::QKbo(KBO kbo) 
  : _shared(nullptr)
  , _kbo(std::move(kbo))
{
  ASS(_kbo.usesQkboPrecedence())
}

// QKbo::QKbo(Precedence prec) 
//   : _prec(std::move(prec))
//   , _shared(nullptr)
//   , _kbo(
//       KboWeightMap<FuncSigTraits>::dflt(),
//       KboWeightMap<PredSigTraits>::dflt(),
//       _prec.funPrec(),
//       _prec.predPrec(),
//         DArray<int>(), /* <- pred levels will never be used */
//         /* reverseLCM */ false
//       )
// {
// }

// class SummandIter 
// {
//   unsigned _plus;
//   Stack<TermList> _work;
// public:
//   SummandIter(TermList t) 
//     : _plus(t.isVar() ? 0 
//                       : tryNumTraits([&](auto numTraits) { 
//                         if (numTraits.sort() == SortHelper::getResultSort(t.term())) {
//                           return Option<unsigned>(numTraits.addF());
//                         } else {
//                           return Option<unsigned>();
//                         } }).unwrap())
//     , _work({ t }) {  }
//
//   DECL_ELEMENT_TYPE(TermList);
//
//   bool hasNext() const 
//   { return !_work.isEmpty(); }
//
//   TermList next() 
//   {
//     while (_work.top().isTerm() && _work.top().term()->functor() == _plus) {
//       auto t = _work.pop();
//       _work.push(*t.term()->nthArgument(0));
//       _work.push(*t.term()->nthArgument(1));
//     }
//     return _work.pop();
//   }
// };
//
//
// auto iterSummands(TermList t)
// { return iterTraits(SummandIter(t)); }


using MulExtMemo = DArray<Option<Ordering::Result>>;

RationalConstantType rat(int n) { return RationalConstantType(IntegerConstantType(n), IntegerConstantType(1)); };
RationalConstantType rat(IntegerConstantType n) { return RationalConstantType(n, IntegerConstantType(1)); };
template<class T> RationalConstantType rat(T n) { return RationalConstantType(n);    };

QKbo::Result QKbo::compare(Literal* l1, Literal* l2) const 
{
  CALL("QKbo::compare(Literal* l1, Literal* l2) const ")
  if (l1 == l2) 
    return Result::EQUAL;

  auto i1 = _shared->interpretedPred(l1);
  auto i2 = _shared->interpretedPred(l2);
       if ( i1 && !i2) return Result::LESS;
  else if (!i1 &&  i2) return Result::GREATER;
  else if (!i1 && !i2) return TIME_TRACE_EXPR("uninterpreted", OU::lexProductCapture(
        [&]() { return _kbo.comparePrecedence(l1, l2); }
      , [&]() { return OU::lexExt(termArgIter(l1), termArgIter(l2), this->asClosure()); }
      , [&]() { return OU::stdCompare(l1->isNegative(), l2->isNegative()); }
    ));
  else {
    ASS(i1 && i2)
   
    auto a1_ = atomsWithLvl(l1);
    auto a2_ = atomsWithLvl(l2);
    if (!a1_.isSome() || !a2_.isSome())
      return Result::INCOMPARABLE;
    auto& a1 = a1_.unwrap();
    auto& a2 = a2_.unwrap();
    return OU::lexProductCapture(
        [&]() -> Ordering::Result { 
        TIME_TRACE("atoms with levels")
        return OU::weightedMulExt(*std::get<0>(a1), *std::get<0>(a2), 
                          [&](auto const& l, auto const& r)
                          { return OU::lexProductCapture(
                              [&]() { return this->compare(l.term, r.term); }
                            , [&]() { return OU::stdCompare(std::get<1>(a1),std::get<1>(a2)); }
                          );}); }
      , [&]() {
        // the atoms of the two literals are the same. 
        // This means they must be of the same sort
        auto sort =  SortHelper::getTermArgSort(l1,0);
        ASS_EQ(sort, SortHelper::getTermArgSort(l2,0));
        ASS_EQ(l1->isEquality() && l1->isPositive(), l2->isEquality() && l2->isPositive())
        return tryNumTraits([&](auto numTraits) {
          using NumTraits = decltype(numTraits);
          if (NumTraits::sort() != sort) {
            return Option<Ordering::Result>();
          } else {
            if (l1->isEquality() && l2->isEquality()) {
              TIME_TRACE("compare equalities")
              ASS_EQ(l1->isPositive(), l2->isPositive())
              return Option<Ordering::Result>(OU::lexProductCapture(
                  // TODO make use of the constant size of the multiset
                  [&]() { 
                    auto e1 = nfEquality<NumTraits>(l1);
                    auto e2 = nfEquality<NumTraits>(l2);
                    return OU::mulExt(*e1, *e2, this->asClosure()); }
                , [&]() { 
                  Recycled<MultiSet<TermList>> m1; m1->init(l1->termArg(0), l1->termArg(1));
                  Recycled<MultiSet<TermList>> m2; m2->init(l2->termArg(0), l2->termArg(1));
                  // TODO make use of the constant size of the multiset
                  return OU::mulExt(*m1,*m2,this->asClosure()); }
              ));
            } else if ( l1->isEquality() && !l2->isEquality()) {
              ASS(l1->isNegative())
              return Option<Ordering::Result>(Result::LESS);
            } else if (!l1->isEquality() &&  l2->isEquality()) {
              ASS(l2->isNegative())
              return Option<Ordering::Result>(Result::GREATER);
            } else if (l1->functor() == NumTraits::isIntF()) {
              ASS_EQ(l2->functor(), NumTraits::isIntF())
              ASS_EQ(l2->isPositive(), l1->isPositive())
              return some(this->compare(l1->termArg(0), l2->termArg(0)));
            } else {
              TIME_TRACE("compare inequqlities")
              ASS(l1->functor() == numTraits.greaterF() || l1->functor() == numTraits.geqF())
              ASS(l2->functor() == numTraits.greaterF() || l2->functor() == numTraits.geqF())
              ASS(l1->isPositive())
              ASS(l2->isPositive())
              return Option<Ordering::Result>(OU::lexProductCapture(
                  [&]() { return this->compare(l1->termArg(0), l2->termArg(0)); }
                , [&]() { return _kbo.comparePrecedence(l1, l2); }
              ));
            } 
          } 
        }) || [&]() {
          ASS_EQ(l1->isPositive(), l2->isPositive())
          // uninterpreted sort
          return Result::EQUAL;
        };
      }
    );
  }
}

bool interpretedFun(Term* t) {
  auto f = t->functor();
  return forAnyNumTraits([&](auto numTraits) -> bool {
      return f == numTraits.addF()
         || (f == numTraits.mulF() && numTraits.isNumeral(*t->nthArgument(0)))
         || numTraits.isNumeral(t);
  });
}

bool uninterpretedFun(Term* t) 
{ return !interpretedFun(t); }


auto toNumeralMul(TermList t) -> std::tuple<Option<TermList>, RationalConstantType> {
  CALL("toNumeralMul(TermList t)")
  if (t.isVar()) {
    return make_tuple(Option<TermList>(t), rat(1));
  } else {
    auto term = t.term();
    auto f = term->functor();
    auto sort = SortHelper::getResultSort(term);
    return tryNumTraits([&](auto numTraits) {
        if (sort != numTraits.sort()) {
          return Option<std::tuple<Option<TermList>, RationalConstantType>>();

        } else if (f == numTraits.mulF() && numTraits.isNumeral(*term->nthArgument(0))) {
          /* t = k * t' ( for some numeral k ) */
          return some(make_tuple(
                some(*term->nthArgument(1)),  /* <- t' */
                rat(numTraits.tryNumeral(*term->nthArgument(0)).unwrap()) /* <- k */
                ));

        } else if (numTraits.isNumeral(t)) {
          /* t is a numeral */
          return some(make_tuple(
                Option<TermList>(), 
                rat(numTraits.tryNumeral(t).unwrap())
                ));

        } else {
          /* t is uninterpreted */
          return some( make_tuple(Option<TermList>(t), RationalConstantType(1)));
        }
    }).unwrap();
  }
}


Ordering::Result QKbo::compare(TermList s, TermList t) const 
{
  CALL("QKbo::compare(TermList, TermList) const")
  if (s.isVar() && t.isVar()) 
    return s == t ? Ordering::EQUAL : Ordering::INCOMPARABLE;

  if (s.isTerm() && t.isTerm() && _shared->equivalent(s.term(), t.term()))
    return Ordering::EQUAL;

  auto as = abstr(s);
  auto at = abstr(t);
  // TODO subterm modulo Tsigma
  if (as.isNone() || at.isNone()) {
    return Ordering::Result::INCOMPARABLE;

  } else {
    auto cmp = _kbo.compare(as.unwrap(), at.unwrap());
    switch (cmp) {
      case Ordering::GREATER:      return Ordering::GREATER;
      case Ordering::LESS:         return Ordering::LESS;
      case Ordering::INCOMPARABLE: return Ordering::INCOMPARABLE;
      case Ordering::EQUAL: 
        ASS_EQ(as.unwrap(), at.unwrap())
        return cmpNonAbstr(s,t);
      default:;
    }
    ASSERTION_VIOLATION
  }
}


/// case 2. precondition: we know that abstr(t1) == abstr(t2)
Ordering::Result QKbo::cmpNonAbstr(TermList t1, TermList t2) const 
{
  CALL("QKbo::cmpNonAbstr(TermList, TermList) const")
  if (t1 == t2) return Result::EQUAL;
  if (t1.isTerm() && t2.isTerm() 
      && t1.term()->functor() == t2.term()->functor() 
      && uninterpretedFun(t1.term())) {
    // 2.a) LEX
    return OrderingUtils::lexExt(termArgIter(t1.term()), termArgIter(t2.term()), 
          [&](auto l, auto r) { return this->compare(l,r); });

  } else {
    // 2.b) interpreted stuff
    if (t1.isVar() && t2.isVar()) {
      ASS_NEQ(t1, t2);
      return INCOMPARABLE;
    }

    return forAnyNumTraits([&](auto numTraits){
        using NumTraits = decltype(numTraits);
        if (
               ( t1.isTerm() && SortHelper::getResultSort(t1.term()) == numTraits.sort() )
            || ( t2.isTerm() && SortHelper::getResultSort(t2.term()) == numTraits.sort() )
            ) {
          auto a1 = _shared->signedAtoms<NumTraits>(t1);
          auto a2 = _shared->signedAtoms<NumTraits>(t2);
          if (a1.isNone() || a2.isNone()) {
            return Option<Result>(Result::INCOMPARABLE);
          } else {
            return Option<Result>(OU::weightedMulExt(*a1.unwrap(), *a2.unwrap(),
                  [this](auto& l, auto& r) 
                  { return OU::lexProductCapture(
                      [&]() { return this->compare(l.term, r.term); },
                      [&]() { return OU::stdCompare(l.sign, r.sign); }); }));
          }
        } else {
          return Option<Result>();
        }
    }) || []() -> Result { ASSERTION_VIOLATION };
  }
}


Option<TermList> QKbo::abstr(TermList t) const 
{
  CALL("QKbo::abstr(TermList t) const ")
  using Out = Option<TermList>;
  if (t.isVar()) {
    return Option<TermList>(t);
  } else {
    auto term = t.term();
    auto f = term->functor();
    auto res = tryNumTraits([&](auto numTraits) -> Option<Option<TermList>> {
      using NumTraits = decltype(numTraits);
        auto noAbstraction = []() { return Option<Option<TermList>>(Option<TermList>()); };
        if (numTraits.isNumeral(t)) {
          return some(some(NumTraits::one()));

        } else if (   
          /* t = t1 + ... + tn */
               numTraits.addF() == f
          /* t = k * t' */
          || ( numTraits.mulF() == f && numTraits.isNumeral(*term->nthArgument(0)) )
          ) {
          auto norm = _shared->normalize(TypedTermList(term)).wrapPoly<NumTraits>();
          auto abstracted = norm->iterSummands()
            .map([&](Monom<NumTraits> monom) { return abstr(monom.factors->denormalize()); });
          ASS(abstracted.hasNext())
          auto _max = abstracted.next();
          if (_max.isNone()) {
            return noAbstraction();
          }
          auto max = _max.unwrap();
          for (auto a : abstracted) {
            if (a.isNone()) {
              return noAbstraction();
            } else {
              switch(_kbo.compare(max, a.unwrap())) {
                case Result::INCOMPARABLE: return noAbstraction();
                case Result::GREATER: 
                case Result::EQUAL: 
                  break;
                case Result::LESS:
                  max = a.unwrap();
                  break;
                default:
                  ASSERTION_VIOLATION
              }
            }
          }

          return Option<Option<TermList>>(Option<TermList>(max));

        } else {
          // wrong number type or uninterpreted function
          return Option<Option<TermList>>();
        }
    });
    if (res.isSome()) {
      return res.unwrap();
    } else {
      Recycled<Stack<TermList>> args;
      args->loadFromIterator(typeArgIter(term));
      for (auto a : termArgIter(term)) {
        auto abs = abstr(a);
        if (abs.isNone()) {
          return abs;
        } else {
          args->push(abs.unwrap());
        }
      }
      return Out(TermList(Term::create(term, args->begin())));
    }
  }
}

void QKbo::show(ostream& out) const 
{ _kbo.show(out); }

} // Kernel
