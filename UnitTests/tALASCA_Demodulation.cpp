/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "Inferences/ALASCA/Demodulation.hpp"
#include "Inferences/ALASCA/Normalization.hpp"
#include "Test/SyntaxSugar.hpp"

#include "Test/SyntaxSugar.hpp"
#include "Test/FwdBwdSimplificationTester.hpp"
#include "Test/AlascaTestUtils.hpp"

// TODO rename FwdBwdSimplificationTester to SimplificationTester and SimplificationTester to  ImmediatesSimplificationTester

using namespace std;
using namespace Kernel;
using namespace Inferences;
using namespace Test;
using namespace Indexing;
using namespace Inferences::ALASCA;

#define SUGAR(Num)                                                                        \
  NUMBER_SUGAR(Num)                                                                       \
  DECL_DEFAULT_VARS                                                                       \
  DECL_CONST(a, Num)                                                                      \
  DECL_CONST(b, Num)                                                                      \
  DECL_CONST(c, Num)                                                                      \
  DECL_FUNC(f, {Num}, Num)                                                                \
  DECL_FUNC(g, {Num, Num}, Num)                                                           \
  DECL_PRED(p, {Num})                                                                     \
  DECL_PRED(p0, {})                                                                       \
  DECL_PRED(r, {Num,Num})                                                                 \
  DECL_SORT(s)                                                                            \
  DECL_CONST(aU, s)                                                                       \
  DECL_CONST(bU, s)                                                                       \
  DECL_FUNC(fU, {s}, s)                                                                       \
  DECL_PRED(pU, {s})                                                                      \

#define MY_SYNTAX_SUGAR SUGAR(Rat) mkAlascaSyntaxSugar(Rat ## Traits{});

#define UWA_MODE Options::UnificationWithAbstraction::ALASCA_MAIN

template<class Rule>
inline auto ALASCA_Demod_TestCase()  {
  auto state = testAlascaState();
  auto rule = move_to_heap(BinSimpl<Rule>(state));
  ALASCA::Normalization norm(state);
  return FwdBwdSimplification::TestCase()
    .fwd(rule)
    .bwd(rule)
    .fwdIdx({ rule->testToSimplIdx(), rule->testConditionIdx() })
    .bwdIdx({ rule->testToSimplIdx(), rule->testConditionIdx() })
    .normalize([norm = std::move(norm)](auto c) mutable { return norm.simplify(c); });
}

/////////////////////////////////
// superposition demod tests
/////////////////////////////////

TEST_SIMPLIFICATION(basic01,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(a) - a  }   ) })
      .toSimplify  ({    clause(   { p(f(a))        }   ) })
      .expected(    {    clause(   { p(  a )        }   ) })
    )

TEST_SIMPLIFICATION(basic01b,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == -f(a) + a  }   ) })
      .toSimplify  ({    clause(   { p(f(a))         }   ) })
      .expected(    {    clause(   { p(  a )         }   ) })
    )


TEST_SIMPLIFICATION(basic02,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(a) - a   }   )
                    ,    clause(   { 0 == g(b,a) - b }   ) })
      .toSimplify  ({    clause(   { r(f(a), f(b))   }   ) })
      .expected(    {    clause(   { r(  a , f(b))   }   ) })
      .justifications({  clause(   {  0 == f(a) - a  }   ) })
    )

TEST_SIMPLIFICATION(basic03,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(x) - x      }   ) })
      .toSimplify  ({    clause(   { r(f(a), f(b))      }   ) })
      .expected(    {    clause(   { r(f(a),   b )      }   ) })
    )

TEST_SIMPLIFICATION(basic04,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(x) - x }   ) })
      .toSimplify  ({    clause(   { p(f(a))       }   ) , clause(   { p(f(b)) }   ) })
      .expected(    {    clause(   { p(  a )       }   ) , clause(   { p(  b ) }   ) })
    )

TEST_SIMPLIFICATION(basic05,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(a) - a }   ), clause(   { 0 == f(b) - b }   ) })
      .toSimplify  ({    clause(   { p(f(a)) }         ), clause(   { p(f(b)) }         ) })
      .expected(    {    clause(   { p(  a ) }         ), clause(   { p(  b ) }         ) })
    )

TEST_SIMPLIFICATION(basic06,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(a) - a }   ), clause(   { 0 == f(b) - b }   ) })
      .toSimplify  ({    clause(   { p(f(a)) }         ), clause(   { p(f(f(a))) }         ) })
      .expected(    {    clause(   { p(  a ) }         ), clause(   { p(  f(a) ) }         ) })
      .justifications({  clause(   {  0 == f(a) - a  }   ) })
    )

TEST_SIMPLIFICATION(basic07,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == g(a, x) - x      }   ) })
      .toSimplify  ({    clause(   { p(g(a,b))             }   ) })
      .expected(    {    clause(   { p(    b )             }   ) })
    )

TEST_SIMPLIFICATION(basic08,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == g(a, x) - x      }   ) })
      .toSimplify  ({    clause(   { p(g(y,b))             }   ) })
      .expectNotApplicable()
    )

TEST_SIMPLIFICATION(basic09,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == frac(1,3) * f(g(a,a)) - a  }   ) })
      .toSimplify  ({    clause(   { p( f(g(a,a)))                   }   ) })
      .expected(    {    clause(   { p(3 * a)                        }   ) })
    )

// checking `C[sσ] ≻ (±ks + t ≈ 0)σ`
TEST_SIMPLIFICATION(ordering01,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(x) + g(x,x) }   ) })
      .toSimplify  ({    clause(   { 0 == g(a,a)    }   ) })
      .expectNotApplicable()
    )

// checking `sσ ≻ terms(t)σ`
TEST_SIMPLIFICATION(ordering02,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == f(x) + g(y,y) }       ) })
      .toSimplify  ({    clause(   { 0 == g(a,a) + f(x) + a }   ) })
      .expectNotApplicable()
    )

// checking `sσ ≻ terms(t)σ`
TEST_SIMPLIFICATION(sum01,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == x + g(x,x) + a }       ) })
      .toSimplify  ({    clause(   { p(g(f(f(a)),f(f(a))))  }   ) })
      .expected(    {    clause(   { p(    - a - f(f(a)) )  }   ) })
    )

TEST_SIMPLIFICATION(sum02,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == x + g(x,x) }       ) })
      .toSimplify  ({    clause(   { p(g(f(f(a)),f(f(a))))  }   ) })
      .expected(    {    clause(   { p(    - f(f(a))     )  }   ) })
    )

TEST_SIMPLIFICATION(sum03,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == a + g(x,x) }       ) })
      .toSimplify  ({    clause(   { p(g(f(f(a)),f(f(a))))  }   ) })
      .expected(    {    clause(   { p(    - a           )  }   ) })
    )


TEST_SIMPLIFICATION(bug01,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == g(x, y) - y  }   ) })
      .toSimplify  ({    clause(   { p(g(z,a))         }   ) })
      .expected(    {    clause(   { p(    a )         }   ) })
    )


TEST_SIMPLIFICATION(misc01,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == a  }   ) })
      .toSimplify  ({    clause(   { ~p0(), a == b }   ) })
      .expected(    {    clause(   { ~p0(), b == 0 }   ) })
    )

TEST_SIMPLIFICATION(misc02,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { 0 == b  }   ) })
      .toSimplify  ({    clause(   { ~p0(), a == b }   ) })
      .expected(    {    clause(   { ~p0(), a == 0 }   ) })
    )

TEST_SIMPLIFICATION(bug02,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { x == aU  }   ) })
      .toSimplify  ({    clause(   { pU(bU) }   ) })
      .expected(    {    clause(   { pU(aU) }   ) })
    )

// checking `sσ ≻ tσ` being aware of variable banks. can lead to invalid terms
TEST_SIMPLIFICATION(bug03,
    ALASCA_Demod_TestCase<SuperpositionDemodConf>()
      .simplifyWith({    clause(   { f(x) == y  }   ) })
      .toSimplify  ({    clause(   { f(y) != 0 }   ) })
      .expectNotApplicable()
    )

/////////////////////////////////
// coherence demod tests
/////////////////////////////////

TEST_SIMPLIFICATION(demod_basic_01,
    ALASCA_Demod_TestCase<CoherenceDemodConf<RatTraits>>()
      .simplifyWith({    clause(   { isInt(f(x))  }   ) })
      .toSimplify  ({    clause(   { p(floor(f(a))) }   ) })
      .expected(    {    clause(   { p(f(a)) }   ) })
    )


TEST_SIMPLIFICATION(demod_basic_02,
    ALASCA_Demod_TestCase<CoherenceDemodConf<RatTraits>>()
      .simplifyWith({    clause(   { isInt(f(x))  }   ) })
      .toSimplify  ({    clause(   { floor(f(a)) != a }   ) })
      .expected(    {    clause(   { f(a) != a }   ) })
    )


// checking `C[sσ] ≻ isInt(s + t)σ`
TEST_SIMPLIFICATION(demod_basic_03,
    ALASCA_Demod_TestCase<CoherenceDemodConf<RatTraits>>()
      .simplifyWith({    clause(   { isInt(f(x))  }   ) })
      .toSimplify  ({    clause(   { floor(f(a)) == a }   ) })
      .expectNotApplicable()
    )

TEST_SIMPLIFICATION(demod_basic_04,
    ALASCA_Demod_TestCase<CoherenceDemodConf<RatTraits>>()
      .simplifyWith({    clause(   { isInt(f(a))  }   ) })
      .toSimplify  ({    clause(   { floor(f(x)) != x }   ) })
      .expectNotApplicable()
    )

// checking `sσ ≻ uσ`
TEST_SIMPLIFICATION(demod_basic_05,
    ALASCA_Demod_TestCase<CoherenceDemodConf<RatTraits>>()
      .simplifyWith({    clause(   { isInt(f(x) + x)  }   ) })
      .toSimplify  ({    clause(   { p(floor(f(a) + a)) }   ) })
      .expected    ({    clause(   { p(      f(a) + a ) }   ) })
    )

// checking `sσ ≻ uσ`
TEST_SIMPLIFICATION(demod_basic_06,
    ALASCA_Demod_TestCase<CoherenceDemodConf<RatTraits>>()
      .simplifyWith({    clause(   { isInt(f(x) + f(y))  }   ) })
      .toSimplify  ({    clause(   { p(floor(f(a) + f(b))) }   ) })
      .expectNotApplicable()
    )
