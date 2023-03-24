/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "Forwards.hpp"
#include "Indexing/IndexManager.hpp"
#include "Indexing/LiteralIndex.hpp"
#include "Indexing/LiteralSubstitutionTree.hpp"
#include "Test/UnitTesting.hpp"
#include "Test/SyntaxSugar.hpp"
#include "Inferences/BinaryInferenceEngine.hpp"
#include "Kernel/Ordering.hpp"

#include "Test/GenerationTester.hpp"
#include "Lib/Reflection.hpp"

using namespace Kernel;
using namespace Inferences;
using namespace Test;
using namespace Indexing;


class SimpleSubsumptionResolution {
public:
  static constexpr unsigned DEBUG_LEVEL = 0;

  struct Lhs 
  {
    Clause* cl;

    using Key = Literal*;

    Literal* key() const 
    { return Literal::complementaryLiteral((*cl)[0]); }

    Clause* clause() const 
    { return cl; }

    friend std::ostream& operator<<(std::ostream& out, Lhs const& self)
    { return out << *self.cl; }

    auto asTuple() const -> decltype(auto)
    { return std::tie(cl); }

    IMPL_COMPARISONS_FROM_TUPLE(Lhs);
  };

  struct Rhs 
  {
    Clause* cl;
    unsigned literalIndex;

    using Key = Literal*;

    Literal* key() const 
    { return (*cl)[literalIndex]; }

    Clause* clause() const 
    { return cl; }

    friend std::ostream& operator<<(std::ostream& out, Rhs const& self)
    { return out << *self.cl << "[" << self.literalIndex << "]"; }

    auto asTuple() const -> decltype(auto)
    { return std::tie(cl, literalIndex); }

    IMPL_COMPARISONS_FROM_TUPLE(Rhs);
  };


  using Matching = BinInfMatching::RightInstanceOfLeft<Lhs, Rhs>;

  IndexType indexType() const
  { return Indexing::SIMPLE_SUBSUMPTION_RESOLUTION; }

  VirtualIterator<Lhs> iterLhs(Clause* cl) const
  {
    if (cl->size() == 1 && !(*cl)[0]->isEquality()) {
      return pvi(getSingletonIterator(Lhs { .cl = cl }));
    } else {
      return VirtualIterator<Lhs>::getEmpty();
    }
  }

  VirtualIterator<Rhs> iterRhs(Clause* cl) const
  {
    return pvi(range(0, cl->numSelected())
      .map([cl](auto i) { return Rhs { .cl = cl, .literalIndex = i, }; }));
  }

  RuleApplicationResult apply(
      Lhs const& lhs, bool lRes,
      Rhs const& rhs, bool rRes,
      ResultSubstitution& subs
      ) const
  {

    auto rhsLits = range(0, rhs.clause()->size())
      .filter([&](auto i) { return i != rhs.literalIndex; })
      .map([&](auto i){ 
          auto lit = (*rhs.clause())[i];
          return subs.apply(lit, rRes);
      });

    return Clause::fromIterator(
        rhsLits, 
        Inference(SimplifyingInference2(InferenceRule::SIMPLE_SUBSUMPTION_RESOLUTION, lhs.clause(), rhs.clause())));
  }
};



Stack<std::function<Indexing::Index*()>> simplSubResoIndices()
{ return Stack<std::function<Indexing::Index*()>>{
  []() -> Index* { return new BinInfIndex<SimpleSubsumptionResolution>(); }
  }; }

#define MY_SYNTAX_SUGAR                                                                   \
                                                                                          \
  DECL_VAR(x, 0)                                                                          \
  DECL_VAR(y, 1)                                                                          \
  DECL_VAR(z, 2)                                                                          \
                                                                                          \
  DECL_SORT(s)                                                                            \
                                                                                          \
  DECL_CONST(a, s)                                                                        \
  DECL_CONST(b, s)                                                                        \
  DECL_CONST(c, s)                                                                        \
                                                                                          \
  DECL_FUNC(f, {s}, s)                                                                    \
  DECL_FUNC(g, {s}, s)                                                                    \
  DECL_FUNC(f2, {s,s}, s)                                                                 \
  DECL_FUNC(g2, {s,s}, s)                                                                 \
                                                                                          \
  DECL_PRED(p, {s})                                                                       \
  DECL_PRED(q, {s})                                                                       \
  DECL_PRED(p2, {s, s})                                                                   \
  DECL_PRED(q2, {s,s})                                                                    \
               

REGISTER_GEN_TESTER(Test::Generation::GenerationTester<BinaryInferenceEngine<SimpleSubsumptionResolution>>(BinaryInferenceEngine<SimpleSubsumptionResolution>(SimpleSubsumptionResolution())))

/////////////////////////////////////////////////////////
// Basic tests
//////////////////////////////////////

TEST_GENERATION(basic01,
    Generation::SymmetricTest()
      .indices(simplSubResoIndices())
      .inputs  ({ clause({ selected( p(a)  ), p(b)  }) 
                , clause({ selected( ~p(x) )        }) })
      .expected(exactly(
            clause({ p(b)  }) 
      ))
    )

TEST_GENERATION(basic02,
    Generation::SymmetricTest()
      .indices(simplSubResoIndices())
      .inputs  ({ clause({ selected( ~p(x)  ), p(b)  }) 
                , clause({ selected( p(a) )        }) })
      .expected(exactly(
          /* nothing */
      ))
    )

TEST_GENERATION(basic03,
    Generation::SymmetricTest()
      .indices(simplSubResoIndices())
      .inputs  ({ clause({ selected( ~p(a) ), p(b)  }) 
                , clause({ selected(  p(x) ), p(c)  }) })
      .expected(exactly(     /* nothing */             ))
    )

