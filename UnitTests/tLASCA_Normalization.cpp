/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "Test/UnitTesting.hpp"
#include "Test/SyntaxSugar.hpp"
#include "Indexing/TermSharing.hpp"
#include "Inferences/LASCA/FourierMotzkin.hpp"
#include "Inferences/InterpretedEvaluation.hpp"
#include "Kernel/Ordering.hpp"
#include "Inferences/PolynomialEvaluation.hpp"
#include "Inferences/Cancellation.hpp"

#include "Test/SyntaxSugar.hpp"
#include "Test/TestUtils.hpp"
#include "Lib/Coproduct.hpp"
#include "Test/SimplificationTester.hpp"
#include "Test/GenerationTester.hpp"
#include "Kernel/KBO.hpp"
#include "Indexing/TermSubstitutionTree.hpp"
#include "Inferences/PolynomialEvaluation.hpp"

using namespace std;
using namespace Kernel;
using namespace Inferences;
using namespace Test;
using namespace Indexing;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////// TEST CASES 
/////////////////////////////////////

#define SUGAR(Num)                                                                                  \
  __ALLOW_UNUSED(                                                                                   \
    NUMBER_SUGAR(Num)                                                                               \
    DECL_DEFAULT_VARS                                                                               \
    DECL_FUNC(f, {Num}, Num)                                                                        \
    DECL_FUNC(g, {Num, Num}, Num)                                                                   \
    DECL_CONST(a, Num)                                                                              \
    DECL_CONST(b, Num)                                                                              \
    DECL_CONST(c, Num)                                                                              \
    DECL_PRED(r, {Num,Num})                                                                         \
    DECL_PRED(p, {Num})                                                                             \
  )                                                                                                 \


#define SIMPL_MUL_ZERO 1

/////////////////////////////////////////////////////////
// Basic tests
//////////////////////////////////////

struct TestCase 
{
  Literal* in;
  Stack<Stack<Literal*>> out;
  bool strong;

  template<class NumTraits>
  void run() {
    auto norm = InequalityNormalizer(strong);
    auto results_ = norm.normalizeLasca<NumTraits>(in).unwrap();
    auto results = Stack<Literal*>();
    for (auto& r : results_.value) {
      results.push(r.denormalize());
    }
    if (!iterTraits(out.iterFifo()).any([&](auto const& out){ return TestUtils::eqModACRect(out, results); })) {
      std::cout << "\r" << endl;
      std::cout << "\r[    input ]" << pretty(in) << endl;
      std::cout << "\r[ expected ]" << pretty(out) << endl;
      std::cout << "\r[  results ]" << pretty(results) << endl;
      exit(-1);
    }
  }
};
#define TEST_CASE(Num, name, ...)                                                                   \
  TEST_FUN(name ## _ ## Num) {                                                                      \
    SUGAR(Num)                                                                                      \
    __VA_ARGS__.run<Num ## Traits>();                                                               \
  }                                                                                                 \

#define TEST_FRAC(...)                                                                              \
    TEST_CASE(Rat , __VA_ARGS__)                                                                    \
    TEST_CASE(Real, __VA_ARGS__)                                                                    \

#define TEST_INT(...)                                                                               \
    TEST_CASE(Int, __VA_ARGS__)                                                                     \

#define TEST_ALL(...)                                                                               \
    TEST_CASE(Int , __VA_ARGS__)                                                                    \
    TEST_CASE(Rat , __VA_ARGS__)                                                                    \
    TEST_CASE(Real, __VA_ARGS__)                                                                    \


TEST_ALL(strict_01, 
    TestCase {
      .in  =      f(a) < 0    ,
      .out = { { -f(a) > 0 } },
      .strong = true,
    })

TEST_ALL(strict_02, 
    TestCase {
      .in  =      0 > x,
      .out = { { -x > 0 } },
      .strong = true,
    })

TEST_ALL(strict_03, 
    TestCase {
      .in  =          a > b    ,
      .out = { { a + -b > 0 } },
      .strong = true,
    })

TEST_ALL(strict_04, 
    TestCase {
      .in  =     a + b > 0    ,
      .out = { { a + b > 0 } },
      .strong = true,
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_ALL(eq_01, 
    TestCase {
      .in  =     f(a) == 0                    , // TODO
      .out = { { f(a) == 0 }, { -f(a) == 0 } },
      .strong = true,
    })

TEST_ALL(eq_02, 
    TestCase {
      .in  =      0 == x                 ,
      .out =  { { 0 == x }, { -x == 0 } }, // TODO
      .strong = true,
    })

TEST_ALL(eq_03, 
    TestCase {
      .in  =         a == b                    ,
      .out = { { a - b == 0 }, { b - a == 0 } }, // TODO ?
      .strong = true,
    })

TEST_ALL(eq_04, 
    TestCase {
      .in  =     a + b == 0                     ,
      .out = { { a + b == 0 }, { -a - b == 0 } },
      .strong = true,
    })


///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_ALL(neq_01a, 
    TestCase {
      .in  =     f(a) != 0,
      .out = { { f(a) != 0 }, { -f(a) != 0 } }, // TODO
      .strong = false,
    })

TEST_ALL(neq_01b, 
    TestCase {
      .in  = f(a) != 0,
      .out = { { f(a) > 0, -f(a) > 0, }, },
      .strong = true,
    })

TEST_ALL(neq_02a, 
    TestCase {
      .in  =      0 != x,
      .out =  { { 0 != x }, { -x != 0 } },
      .strong = false,
    })

TEST_ALL(neq_02b, 
    TestCase {
      .in  =      0 != x,
      .out =  { {  x > 0, -x > 0 } },
      .strong = true,
    })

TEST_ALL(neq_03a, 
    TestCase {
      .in  =         a != b,
      .out = { { a - b != 0 }, { b - a != 0 } },
      .strong = false,
    })

TEST_ALL(neq_03b, 
    TestCase {
      .in  =         a != b,
      .out = { { a - b > 0, b - a > 0 } },
      .strong = true,
    })

TEST_ALL(neq_04, 
    TestCase {
      .in  =     a + b != 0,
      .out = { { a + b != 0 }, { -a - b != 0 } },
      .strong = false,
    })

TEST_ALL(neq_05, 
    TestCase {
      .in  =      7 * a + b != a,
      .out = { {  6 * a + b != 0 }
             , { -6 * a - b != 0 } },
      .strong = false,
    })

TEST_ALL(neq_06, 
    TestCase {
      .in  =      7 * a + b      != a - 3,
      .out = { {  6 * a + b + 3  != 0 }
             , { -6 * a - b + -3 != 0 } },
      .strong = false,
    })


///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_INT(lax_01, 
    TestCase {
      .in  =   f(a) <= 0, // TODO theory: we normalize to something bigger here...
      .out = { { 1 + -f(a) > 0 } },
      .strong = false,
    })

TEST_INT(lax_02, 
    TestCase {
      .in  =    0 >= x, // TODO theory: we normalize to something bigger here...
      .out = { { -x + 1 > 0 } },
      .strong = false,
    })

TEST_INT(lax_03, 
    TestCase {
      .in  =   a >= b, // TODO theory: we normalize to something bigger here...
      .out = { { a - b + 1 > 0 } },
      .strong = false,
    })

TEST_INT(lax_04, 
    TestCase {
      .in  =   a + b >= 0, // TODO theory: we normalize to something bigger here...
      .out = { { a + b + 1 > 0 } },
      .strong = false,
    })


///////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_FRAC(lax_01a,
    TestCase {
      .in  =      f(a) <= 0,
      .out = { { -f(a) >= 0 } },
      .strong = false,
    })

TEST_FRAC(lax_01b, 
    TestCase {
      .in  =      f(a) <= 0,
      .out = { { -f(a) == 0, -f(a) > 0 } },
      .strong = true,
    })

TEST_FRAC(lax_02a, 
    TestCase {
      .in  =      0 >= x,
      .out = { { -x >= 0 } },
      .strong = false,
    })

TEST_FRAC(lax_02b, 
    TestCase {
      .in  =      0 >= x,
      .out = { { -x == 0, -x > 0  } },
      .strong = true,
    })

TEST_FRAC(lax_03a, 
    TestCase {
      .in  =         a >= b,
      .out = { { a - b >= 0 } },
      .strong = false,
    })

TEST_FRAC(lax_03b, 
    TestCase {
      .in  =         a >= b,
      .out = { { a - b == 0, a - b > 0 } },
      .strong = true,
    })

TEST_FRAC(lax_04a,
    TestCase {
      .in  =     a + b >= 0    ,
      .out = { { a + b >= 0 } },
      .strong = false,
    })


TEST_FRAC(lax_04b,
    TestCase {
      .in  =     a + b >= 0               ,
      .out = { { a + b == 0, a + b > 0 } },
      .strong = true,
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

TEST_ALL(gcd_01, 
    TestCase {
      .in  =   2 * a + 2 * b > 0,
      .out = { {     a +     b > 0 } },
      .strong = true,
    })

TEST_ALL(gcd_02, 
    TestCase {
      .in  =     2 * a + 4 * b + -6 * c > 0,
      .out = { {     a + 2 * b + -3 * c > 0 } },
      .strong = true,
    })

TEST_FRAC(gcd_03, 
    TestCase {
      .in  =     frac(1, 2) * a + frac(1, 4) * b + -frac(1, 6) * c > 0,
      .out = { {         6  * a +         3  * b + -        2  * c > 0 } },
      .strong = true,
    })

TEST_FRAC(gcd_04, 
    TestCase {
      .in  =     frac(9, 2) * a + frac(6, 4) * b + -frac(3, 6) * c > 0,
      .out = { {         9  * a +         3  * b + -             c > 0 } },
      .strong = true,
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

#if WITH_GMP

TEST_ALL(bug_01, 
    TestCase {
      .in  =     0 * num(-1) + 2 * a * 1073741824 > 0,
#if SIMPL_MUL_ZERO
      .out = { {              a > 0 } },
#else
      .out = { {         0  + a > 0 } },
#endif // SIMPL_MUL_ZERO
      .strong = true,
    })


#else // !WITH_GMP

// due to overflow we can't compute the gcd
TEST_ALL(bug_01, 
    TestCase {
      .in  =     0 * num(-1) + 2 * a * 1073741824 > 0,
      .out = { {     num(0)  +     a * 1073741824 > 0 } },
      .strong = true,
    })

#endif // WITH_GMP

TEST_INT(bug_02, 
    TestCase {
      .in  =     ~(x <  0),
      //          (x >= 0),
      .out = { { x + 1 > 0 } },
      .strong = true,
    })

TEST_ALL(bug_03, 
    TestCase {
      .in  =     g(a, x) + -2 * b * y > 0,
      .out = { { g(a, x) + -2 * b * y > 0 } },
      .strong = true,
   })
 
TEST_FRAC(bug_04, 
    TestCase {
      .in  =     a + b + c >= 0,
      .out = { { a + b + c >= 0 } },
      .strong = false,
    })

TEST_FRAC(bug_05, 
    TestCase {
      .in  =     a * b * c >= 0,
      .out = { { a * b * c >= 0 } },
      .strong = false,
    })


TEST_FRAC(bug_06, 
    TestCase {
      .in  =     num(-4) + 0 >= 0  ,
      .out = { { num(-1)     >= 0 } },
      .strong = false,
    })

#if WITH_GMP

TEST_ALL(bug_07, 
    TestCase {
      .in  =   -600335 * (-a * 251886) + 251886 * (-a * 600335) == 0 ,
      //       -600335 * ( a * 251886) + 251886 * ( a * 600335) == 0
      //       -600335 *   a           +            a * 600335  == 0
      //                  -a           +            a           == 0
#if SIMPL_MUL_ZERO
      .out = { { num(0)== 0 } },
#else
      .out = { { 0 * a == 0 } },
#endif // SIMPL_MUL_ZERO
      .strong = false,
    })

#else // !WITH_GMP

TEST_ALL(bug_07, 
    TestCase {
      .in  =   -600335 * (-a * 251886) + 251886 * (-a * 600335) == 0 ,
      //       -600335 * ( a * 251886) + 251886 * ( a * 600335) == 0
      //       -600335 *   a           +            a * 600335  == 0
      //                  -a           +            a           == 0
#if SIMPL_MUL_ZERO
      .out = { { num(0) == 0 } },
#else
      .out = { { 0 * (600335 * a) == 0 } },
#endif // SIMPL_MUL_ZERO
      .strong = false,
    })

#endif // WITH_GMP

TEST_INT(bug_08, 
    TestCase {
      .in  =   0 * f(x) > 0,
#if SIMPL_MUL_ZERO
      .out = { {   num(0) > 0 } },
#else
      .out = { { 0 * f(x) > 0 } },
#endif // SIMPL_MUL_ZERO
      .strong = false,
    })



TEST_ALL(misc_01, 
    TestCase {
      .in  =     0 > f(a)     ,
      .out = { { -f(a) > 0 } },
      .strong = false,
    })

TEST_FRAC(misc_02, 
    TestCase {
      .in  =     ~(0 > -f(a))   ,
      .out = { { -f(a) >= 0  } },
      .strong = false,
    })

TEST_INT(misc_02, 
    TestCase {
      .in  =     ~(0 > -f(a))      ,
      .out = { { -f(a) + 1 > 0  } },
      .strong = false,
    })

TEST_ALL(misc_03, 
    TestCase {
      .in  =     a + 3 * ( 0 * f(a) +     b + 0 ) > 0  ,
#if SIMPL_MUL_ZERO
      .out = { { a +                  3 * b       > 0  } },
#else
      .out = { { a +       0 * f(a) + 3 * b + 0   > 0  } },
#endif // SIMPL_MUL_ZERO
      .strong = false,
    })


TEST_ALL(misc_04, 
    TestCase {
      .in  =      (((2 * (b * (c + (b + -c)))) + -(b * b)))  > 0,
      .out = { {                  b * b                       > 0, } },
      .strong = false,
    })

TEST_ALL(misc_05, 
    TestCase {
      .in  =     (((a * a) + ((2 * (b * (c + -(-b + c)))) + -(b * b)))) > 0,
      //     { {    a * a  +   2*b *  c + 2*b * b + -2*b * c  + -(b * b)    > 0, } },
      //     { {    a * a  +   2*b *  c +   b * b + -2*b * c                > 0, } },
      .out = { {    a * a  +                b * b                           > 0, } },
      .strong = false,
    })
