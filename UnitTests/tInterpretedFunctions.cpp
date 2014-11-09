

#include "Kernel/Clause.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Problem.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Term.hpp"

#include "Kernel/InterpretedLiteralEvaluator.hpp"

#include "Test/UnitTesting.hpp"

#define UNIT_ID interpFunc
UT_CREATE;

using namespace std;
using namespace Lib;
using namespace Kernel;
using namespace Shell;


void interpret(Literal* lit)
{

  cout << endl;
  cout << "Start with: " << lit->toString() << endl;

  InterpretedLiteralEvaluator* eval = new InterpretedLiteralEvaluator();

  bool constant;
  Literal* res;
  bool constantTrue;

  eval->evaluate(lit,constant,res,constantTrue);
  
  cout << "constant="<<constant<<",constantTrue="<<constantTrue<<endl;
  if(res){
     cout << "res= " << res->toString() << endl;
  }else{
     cout << "res not defined" << endl;
  }

}

// Interpret x*2=5
TEST_FUN(interpFunc1)
{
  unsigned mult = theory->getFnNum(Theory::REAL_MULTIPLY);
  TermList two(theory->representConstant(RealConstantType("2")));
  TermList five(theory->representConstant(RealConstantType("5")));
  TermList x(1,false);
  TermList multTwoX(Term::create2(mult, two, x));
  Literal* lit = Literal::createEquality(true, multTwoX, five, Sorts::SRT_REAL);

  interpret(lit);
}

// Interpret 2.5*2=5
TEST_FUN(interpFunc2)
{
  unsigned mult = theory->getFnNum(Theory::REAL_MULTIPLY);
  TermList two(theory->representConstant(RealConstantType("2")));
  TermList twoHalf(theory->representConstant(RealConstantType("2.5")));
  TermList five(theory->representConstant(RealConstantType("5")));
  TermList multTwoTwoH(Term::create2(mult, two, twoHalf));
  Literal* lit = Literal::createEquality(true, multTwoTwoH, five, Sorts::SRT_REAL);

  interpret(lit);
} 

// Interpret 3*2 > 5
TEST_FUN(interpFunc3)
{
  unsigned mult = theory->getFnNum(Theory::REAL_MULTIPLY);
  TermList two(theory->representConstant(RealConstantType("2")));
  TermList three(theory->representConstant(RealConstantType("3")));
  TermList five(theory->representConstant(RealConstantType("5")));
  TermList multTwoThree(Term::create2(mult, two, three));
  unsigned greater = theory->getPredNum(Theory::REAL_GREATER);
  Literal* lit = Literal::create2(greater,true, multTwoThree, five);

  interpret(lit);
}
