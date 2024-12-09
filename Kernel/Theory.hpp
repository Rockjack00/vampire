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
 * @file Theory.hpp
 * Defines class Theory.
 */

#ifndef __Theory__
#define __Theory__

#include <cmath>

#include "Forwards.hpp"

#include "Lib/DHMap.hpp"
#include "Lib/Exception.hpp"

#include "Shell/TermAlgebra.hpp"

#include "OperatorType.hpp"
#include "Term.hpp"

#define WITH_GMP 1
#if WITH_GMP
#include <gmpxx.h>
#endif

namespace Kernel {

class IntegerConstantType;
struct RationalConstantType;
class RealConstantType;

#define MK_CAST_OP(Type, OP, ToCast) \
  friend auto operator OP(Type l, ToCast const& r) { return l OP Type(r); } \
  friend auto operator OP(ToCast const& l, Type r) { return Type(l) OP r; } \

#define MK_CAST_OPS(Type, ToCast) \
  MK_CAST_OP(Type, *, ToCast) \
  MK_CAST_OP(Type, +, ToCast) \
  MK_CAST_OP(Type, -, ToCast) \
  MK_CAST_OP(Type, <, ToCast) \
  MK_CAST_OP(Type, >, ToCast) \
  MK_CAST_OP(Type, <=,ToCast) \
  MK_CAST_OP(Type, >=,ToCast) \
  MK_CAST_OP(Type, ==,ToCast) \
  MK_CAST_OP(Type, !=,ToCast) \


/**
 * Exception to be thrown when the requested operation cannot be performed,
 * e.g. because of overflow of a native type.
 */
class ArithmeticException : public Exception {
protected:
  ArithmeticException(std::string msg) : Exception(msg) {}
};

class MachineArithmeticException : public ArithmeticException 
{ 
public:
  MachineArithmeticException() : ArithmeticException("machine arithmetic exception"){} 
  MachineArithmeticException(std::string msg) : ArithmeticException("machine arithmetic exception: " + msg){} 
};

class DivByZeroException         : public ArithmeticException 
{ 
public:
  DivByZeroException() : ArithmeticException("divided by zero"){} 
};

enum class Sign : uint8_t {
  Zero = 0,
  Pos = 1,
  Neg = 2,
};

std::ostream& operator<<(std::ostream& out, Sign const& self);

class IntegerConstantType
{
public:
  static TermList getSort() { return AtomicSort::intSort(); }

#if WITH_GMP
  using InnerType = mpz_class;
#else // !WITH_GMP
  typedef int InnerType;
#endif // WITH_GMP

  IntegerConstantType() {}
  IntegerConstantType(IntegerConstantType&&) = default;
  IntegerConstantType(const IntegerConstantType&) = default;
  IntegerConstantType& operator=(const IntegerConstantType&) = default;
#if WITH_GMP
  explicit IntegerConstantType(InnerType v) : _val(v) {}
  explicit IntegerConstantType(int v) : _val(v) {}
#else // !WITH_GMP
  IntegerConstantType(int v) : _val(v) {} // <- not explicit to support legacy code from Theory_int.cpp
#endif // WITH_GMP
  explicit IntegerConstantType(const std::string& str);

  IntegerConstantType operator+(const IntegerConstantType& num) const;
  IntegerConstantType operator-(const IntegerConstantType& num) const;
  IntegerConstantType operator-() const;
  IntegerConstantType operator*(const IntegerConstantType& num) const;
  IntegerConstantType operator++() { return IntegerConstantType(++_val); }
  IntegerConstantType operator--() { return IntegerConstantType(--_val); }
  IntegerConstantType operator++(int) { return IntegerConstantType(_val++); }
  IntegerConstantType operator--(int) { return IntegerConstantType(_val--); }

  IntegerConstantType& operator*=(IntegerConstantType const& r) { _val *= r._val; return *this; }
  IntegerConstantType& operator+=(IntegerConstantType const& r) { _val += r._val; return *this; }
  IntegerConstantType& operator-=(IntegerConstantType const& r) { _val -= r._val; return *this; }

  // true if this divides num
  bool divides(const IntegerConstantType& num) const ;
#if !WITH_GMP
  float realDivide(const IntegerConstantType& num) const { 
    if(num._val==0) throw DivByZeroException();
    return ((float)_val)/num._val; 
  }
#endif // WITH_GMP
  IntegerConstantType inverseModulo(IntegerConstantType const& modulus) const;  
  IntegerConstantType intDivide(const IntegerConstantType& num) const ;  
  IntegerConstantType remainderE(const IntegerConstantType& num) const; 
  IntegerConstantType quotientE(const IntegerConstantType& num) const; 
  IntegerConstantType quotientT(const IntegerConstantType& num) const;
  IntegerConstantType quotientF(const IntegerConstantType& num) const; 
  static IntegerConstantType gcd(IntegerConstantType const& lhs, IntegerConstantType const& rhs);
  static IntegerConstantType lcm(IntegerConstantType const& lhs, IntegerConstantType const& rhs);
  IntegerConstantType gcd(IntegerConstantType const& rhs) const { return IntegerConstantType::gcd(*this, rhs); }
  IntegerConstantType lcm(IntegerConstantType const& rhs) const { return IntegerConstantType::lcm(*this, rhs); }

  IntegerConstantType remainderT(const IntegerConstantType& num) const;
  IntegerConstantType remainderF(const IntegerConstantType& num) const;

  bool operator==(const IntegerConstantType& num) const;
  bool operator>(const IntegerConstantType& num) const;

  bool operator!=(const IntegerConstantType& num) const { return !((*this)==num); }
  bool operator<(const IntegerConstantType& o) const { return o>(*this); }
  bool operator>=(const IntegerConstantType& o) const { return !(o>(*this)); }
  bool operator<=(const IntegerConstantType& o) const { return !((*this)>o); }

  InnerType const& toInner() const { return _val; }

  bool isZero()     const { return _val == 0; }
  bool isNegative() const { return _val  < 0; }
  bool isPositive() const { return _val  > 0; }

  Sign sign() const;

  static IntegerConstantType floor(RationalConstantType rat);
  static IntegerConstantType floor(IntegerConstantType rat);

  static IntegerConstantType ceiling(RationalConstantType rat);
  static IntegerConstantType ceiling(IntegerConstantType rat);
  IntegerConstantType abs() const;
  IntegerConstantType log2() const;

  // might throw an exception
  int unwrapInt() const;

  static Comparison comparePrecedence(IntegerConstantType n1, IntegerConstantType n2);
  size_t hash() const;

  std::string toString() const;
  friend std::ostream& operator<<(std::ostream& out, const IntegerConstantType& val);
private:
  InnerType _val;
  IntegerConstantType operator/(const IntegerConstantType& num) const;
  IntegerConstantType operator%(const IntegerConstantType& num) const;
#if VZ3
  friend class SAT::Z3Interfacing;
#endif
  MK_CAST_OPS(IntegerConstantType, int)
};

/**
 * A class for representing rational numbers
 *
 * The class uses IntegerConstantType to store the numerator and denominator.
 * This ensures that if there is an overflow in the operations, an exception
 * will be raised by the IntegerConstantType methods.
 */
struct RationalConstantType {
  typedef IntegerConstantType InnerType;

  static TermList getSort() { return AtomicSort::rationalSort(); }

  RationalConstantType() {}
  RationalConstantType(RationalConstantType&&) = default;
  RationalConstantType(const RationalConstantType&) = default;
  RationalConstantType& operator=(const RationalConstantType&) = default;

  RationalConstantType(const std::string& num, const std::string& den);
  explicit RationalConstantType(int n);
  explicit RationalConstantType(IntegerConstantType num);
  RationalConstantType(int num, int den);
  RationalConstantType(IntegerConstantType num, IntegerConstantType den);

  RationalConstantType operator+(const RationalConstantType& num) const;
  RationalConstantType operator-(const RationalConstantType& num) const;
  RationalConstantType operator-() const;
  RationalConstantType operator*(const RationalConstantType& num) const;
  RationalConstantType operator/(const RationalConstantType& num) const;

  RationalConstantType& operator*=(RationalConstantType const& r) { _num *= r._num; _den *= r._den;  cannonize(); return *this; }
  RationalConstantType& operator+=(RationalConstantType const& r) { *this = *this + r; return *this; }
  RationalConstantType& operator-=(RationalConstantType const& r) { *this = *this - r; return *this; }
  RationalConstantType& operator/=(RationalConstantType const& r) { _num *= r._den; _den *= r._num;  cannonize(); return *this; }

  RationalConstantType inverse() const { return RationalConstantType(1) / *this; }
  IntegerConstantType floor() const { return IntegerConstantType::floor(*this); }
  RationalConstantType floorRat() const { return RationalConstantType(floor()); }
  RationalConstantType ceiling() const { 
    return RationalConstantType(IntegerConstantType::ceiling(*this));
  }
  RationalConstantType truncate() const { 
    return RationalConstantType(_num.quotientT(_den));
  }

  bool isInt() const;

  bool operator==(const RationalConstantType& num) const;
  bool operator>(const RationalConstantType& num) const;

  bool operator!=(const RationalConstantType& num) const { return !((*this)==num); }
  bool operator<(const RationalConstantType& o) const { return o>(*this); }
  bool operator>=(const RationalConstantType& o) const { return !(o>(*this)); }
  bool operator<=(const RationalConstantType& o) const { return !((*this)>o); }


  bool isZero() const { return _num.toInner()==0; } 
  // relies on the fact that cannonize ensures that _den>=0
  bool isNegative() const { ASS(_den >= IntegerConstantType(0)); return _num.toInner() < 0; }
  bool isPositive() const { ASS(_den >= IntegerConstantType(0)); return _num.toInner() > 0; }

  RationalConstantType abs() const;

  std::string toString() const;

  const InnerType& numerator() const { return _num; }
  const InnerType& denominator() const { return _den; }
  size_t hash() const;
  Sign sign() const;

  static Comparison comparePrecedence(RationalConstantType n1, RationalConstantType n2);

  friend std::ostream& operator<<(std::ostream& out, const RationalConstantType& val); 

  MK_CAST_OPS(RationalConstantType, int)
  MK_CAST_OPS(RationalConstantType, IntegerConstantType)
  MK_CAST_OP(RationalConstantType, /, int)

#if !WITH_GMP
protected:
  void init(InnerType num, InnerType den);
#endif

private:
  void cannonize();

  InnerType _num;
  InnerType _den;
};

std::ostream& operator<<(std::ostream& out, const IntegerConstantType& val); 


class RealConstantType : public RationalConstantType
{
public:
  static TermList getSort() { return AtomicSort::realSort(); }

  RealConstantType() {}
  RealConstantType(RealConstantType&&) = default;
  RealConstantType(const RealConstantType&) = default;
  RealConstantType& operator=(const RealConstantType&) = default;

  explicit RealConstantType(const std::string& number);
  explicit RealConstantType(const RationalConstantType& rat) : RationalConstantType(rat) {}
  RealConstantType(IntegerConstantType num) : RationalConstantType(num) {}
  RealConstantType(int num, int den) : RationalConstantType(num, den) {}
  explicit RealConstantType(int number) : RealConstantType(RationalConstantType(number)) {}
  RealConstantType(typename RationalConstantType::InnerType  num, typename RationalConstantType::InnerType den) : RealConstantType(RationalConstantType(num, den)) {}

  RealConstantType operator+(const RealConstantType& num) const
  { return RealConstantType(RationalConstantType::operator+(num)); }
  RealConstantType operator-(const RealConstantType& num) const
  { return RealConstantType(RationalConstantType::operator-(num)); }
  RealConstantType operator-() const
  { return RealConstantType(RationalConstantType::operator-()); }
  RealConstantType operator*(const RealConstantType& num) const
  { return RealConstantType(RationalConstantType::operator*(num)); }
  RealConstantType operator/(const RealConstantType& num) const
  { return RealConstantType(RationalConstantType::operator/(num)); }

  IntegerConstantType floor() const { return RationalConstantType::floor(); }
  RealConstantType floorRat() const { return RealConstantType(floor()); }
  RealConstantType truncate() const { return RealConstantType(RationalConstantType::truncate()); }
  RealConstantType ceiling() const { return RealConstantType(RationalConstantType::ceiling()); }


  bool isZero()     const { return RationalConstantType::isZero(); }
  bool isNegative() const { return RationalConstantType::isNegative(); }
  bool isPositive() const { return RationalConstantType::isPositive(); }
  Sign sign() const;

  RealConstantType abs() const;

  std::string toNiceString() const;

  size_t hash() const;
  static Comparison comparePrecedence(RealConstantType n1, RealConstantType n2);

  /** 
   * returns the internal represenation of this RealConstantType. 
   * 
   * Currently we represent Reals as Rationals. We might
   * change this representation in the future in order to represent numerals other algebraic numbers (e.g.  sqrt(2)). 
   * In order to make this future proof this function is called in places where we rely on the representation of reals,
   * so we get a compiler error if we change the underlying datatype.
   */
  RationalConstantType representation() const;
  RealConstantType inverse() const { return RealConstantType(1) / *this; }

  friend std::ostream& operator<<(std::ostream& out, const RealConstantType& val);
  MK_CAST_OPS(RealConstantType, int)
  MK_CAST_OPS(RealConstantType, IntegerConstantType)
  MK_CAST_OP(RealConstantType, /, int)

private:
  static bool parseDouble(const std::string& num, RationalConstantType& res);
};

inline bool operator<(const RealConstantType& lhs ,const RealConstantType& rhs) { 
  return static_cast<const RationalConstantType&>(lhs) < static_cast<const RationalConstantType&>(rhs);
}
inline bool operator>(const RealConstantType& lhs, const RealConstantType& rhs) {
  return static_cast<const RationalConstantType&>(lhs) > static_cast<const RationalConstantType&>(rhs);
}
inline bool operator<=(const RealConstantType& lhs, const RealConstantType& rhs) {
  return static_cast<const RationalConstantType&>(lhs) <= static_cast<const RationalConstantType&>(rhs);
}
inline bool operator>=(const RealConstantType& lhs, const RealConstantType& rhs) {
  return static_cast<const RationalConstantType&>(lhs) >= static_cast<const RationalConstantType&>(rhs);
}

/**
 * A singleton class handling tasks related to theory symbols in Vampire
 */
class Theory
{
public:
  /**
   * Interpreted symbols and predicates
   *
   * If interpreted_evaluation is enabled, predicates GREATER_EQUAL,
   * LESS and LESS_EQUAL should not appear in the run of the
   * SaturationAlgorithm (they'll be immediately simplified by the
   * InterpretedEvaluation simplification).
   */
  enum Interpretation
  {
    //predicates
    EQUAL,

    INT_IS_INT,
    INT_IS_RAT,
    INT_IS_REAL,
    INT_GREATER,
    INT_GREATER_EQUAL,
    INT_LESS,
    INT_LESS_EQUAL,
    INT_DIVIDES,

    RAT_IS_INT,
    RAT_IS_RAT,
    RAT_IS_REAL,
    RAT_GREATER,
    RAT_GREATER_EQUAL,
    RAT_LESS,
    RAT_LESS_EQUAL,

    REAL_IS_INT,
    REAL_IS_RAT,
    REAL_IS_REAL,
    REAL_GREATER,
    REAL_GREATER_EQUAL,
    REAL_LESS,
    REAL_LESS_EQUAL,

    //numeric functions

    INT_SUCCESSOR,
    INT_UNARY_MINUS,
    INT_PLUS,  // sum in TPTP
    INT_MINUS, // difference in TPTP
    INT_MULTIPLY,
    INT_QUOTIENT_E,
    INT_QUOTIENT_T,
    INT_QUOTIENT_F,
    INT_REMAINDER_E,
    INT_REMAINDER_T,
    INT_REMAINDER_F,
    INT_FLOOR,
    INT_CEILING,
    INT_TRUNCATE,
    INT_ROUND,
    INT_ABS,

    RAT_UNARY_MINUS,
    RAT_PLUS, // sum in TPTP
    RAT_MINUS,// difference in TPTP
    RAT_MULTIPLY,
    RAT_QUOTIENT,
    RAT_QUOTIENT_E,
    RAT_QUOTIENT_T,
    RAT_QUOTIENT_F,
    RAT_REMAINDER_E,
    RAT_REMAINDER_T,
    RAT_REMAINDER_F,
    RAT_FLOOR,
    RAT_CEILING,
    RAT_TRUNCATE,
    RAT_ROUND,

    REAL_UNARY_MINUS,
    REAL_PLUS,  // plus in TPTP
    REAL_MINUS, // difference in TPTP
    REAL_MULTIPLY,
    REAL_QUOTIENT,
    REAL_QUOTIENT_E,
    REAL_QUOTIENT_T,
    REAL_QUOTIENT_F,
    REAL_REMAINDER_E,
    REAL_REMAINDER_T,
    REAL_REMAINDER_F,
    REAL_FLOOR,
    REAL_CEILING,
    REAL_TRUNCATE,
    REAL_ROUND,

    //conversion functions
    INT_TO_INT,
    INT_TO_RAT,
    INT_TO_REAL,
    RAT_TO_INT,
    RAT_TO_RAT,
    RAT_TO_REAL,
    REAL_TO_INT,
    REAL_TO_RAT,
    REAL_TO_REAL,

    // array functions
    ARRAY_SELECT,
    ARRAY_BOOL_SELECT,
    ARRAY_STORE,

    INVALID_INTERPRETATION // LEAVE THIS AS THE LAST ELEMENT OF THE ENUM
  };

  enum IndexedInterpretation {
    FOR_NOW_EMPTY
  };

  typedef std::pair<IndexedInterpretation, unsigned> ConcreteIndexedInterpretation;

  /**
   * Interpretations represent the abstract concept of an interpreted operation vampire knows about.
   *
   * Some of them are polymorphic, such the ones for ARRAYs, and only become a concrete operation when supplied with OperatorType*.
   * To identify these, MonomorphisedInterpretation (see below) can be used. However, notice that the appropriate Symbol always carries
   * an Interpretation (if interpreted) and an OperatorType*.
   *
   * Other operations might be, in fact, indexed families of operations, and need an additional index (unsigned) to be specified.
   * To keep the Symbol structure from growing for their sake, these IndexedInterpretations are instantiated to a concrete member of a family on demand
   * and we keep track of this instantiation in _indexedInterpretations (see below). Members of _indexedInterpretations
   * implicitly "inhabit" a range of values in Interpretation after INVALID_INTERPRETATION, so that again an
   * Interpretation (this time >= INVALID_INTERPRETATION) and an OperatorType* are enough to identify a member of an indexed family.
   */

  typedef std::pair<Interpretation, OperatorType*> MonomorphisedInterpretation;

private:
  DHMap<ConcreteIndexedInterpretation,Interpretation> _indexedInterpretations;

public:

  static unsigned numberOfFixedInterpretations() {
    return INVALID_INTERPRETATION;
  }

  Interpretation interpretationFromIndexedInterpretation(IndexedInterpretation ii, unsigned index)
  {
    ConcreteIndexedInterpretation cii = std::make_pair(ii,index);

    Interpretation res;
    if (!_indexedInterpretations.find(cii, res)) {
      res = static_cast<Interpretation>(numberOfFixedInterpretations() + _indexedInterpretations.size());
      _indexedInterpretations.insert(cii, res);
    }
    return res;
  }

  static bool isPlus(Interpretation i){
    return i == INT_PLUS || i == RAT_PLUS || i == REAL_PLUS;
  }

  static std::string getInterpretationName(Interpretation i);
  static unsigned getArity(Interpretation i);
  static bool isFunction(Interpretation i);
  static bool isInequality(Interpretation i);
  static OperatorType* getNonpolymorphicOperatorType(Interpretation i);

  static OperatorType* getArrayOperatorType(TermList arraySort, Interpretation i);

  static bool hasSingleSort(Interpretation i);
  static TermList getOperationSort(Interpretation i);
  static bool isConversionOperation(Interpretation i);
  static bool isLinearOperation(Interpretation i);
  static bool isNonLinearOperation(Interpretation i);
  bool isPartiallyInterpretedFunction(Term* t);
  bool partiallyDefinedFunctionUndefinedForArgs(Term* t);
  // static bool isPartialFunction(Interpretation i);

  static bool isPolymorphic(Interpretation i);

  unsigned getArrayExtSkolemFunction(TermList sort);

  static Theory theory_obj;
  static Theory* instance();

  void defineTupleTermAlgebra(unsigned arity, TermList* sorts);

  /** Returns true if the argument is an interpreted constant
   */
  bool isInterpretedConstant(unsigned func);
  bool isInterpretedConstant(Term* t);
  bool isInterpretedConstant(TermList t);
  /** Returns true if the argument is an interpreted number
   */
  bool isInterpretedNumber(Term* t);
  bool isInterpretedNumber(TermList t);

  /** Returns false if pred is equality.  Returns true if the argument is any
      other interpreted predicate.
   */
  bool isInterpretedPredicate(unsigned pred);

  bool isInterpretedEquality(Literal* lit);
  bool isInterpretedPredicate(Literal* lit, Interpretation itp);
  bool isInterpretedPredicate(unsigned pred, Interpretation itp);
  bool isInterpretedPredicate(Literal* lit);

  bool isInterpretedFunction(unsigned func);
  bool isInterpretedFunction(Term* t);
  bool isInterpretedFunction(TermList t);
  bool isInterpretedFunction(unsigned func, Interpretation itp);
  bool isInterpretedFunction(Term* t, Interpretation itp);
  bool isInterpretedFunction(TermList t, Interpretation itp);

  // bool isInterpretedPartialFunction(unsigned func);
  bool isZero(TermList t);

  Interpretation interpretFunction(unsigned func);
  Interpretation interpretFunction(Term* t);
  Interpretation interpretFunction(TermList t);
  Interpretation interpretPredicate(unsigned pred);
  Interpretation interpretPredicate(Literal* t);

  void registerLaTeXPredName(unsigned func, bool polarity, std::string temp);
  void registerLaTeXFuncName(unsigned func, std::string temp);
  std::string tryGetInterpretedLaTeXName(unsigned func, bool pred,bool polarity=true);

private:
  // For recording the templates for predicate and function symbols
  DHMap<unsigned,std::string> _predLaTeXnamesPos;
  DHMap<unsigned,std::string> _predLaTeXnamesNeg;
  DHMap<unsigned,std::string> _funcLaTeXnames;

public:

  /**
   * Try to interpret the term list as an integer constant. If it is an
   * integer constant, return true and save the constant in @c res, otherwise
   * return false.
   */
  bool tryInterpretConstant(TermList trm, IntegerConstantType& res)
  {
    if (!trm.isTerm()) {
      return false;
    }
    return tryInterpretConstant(trm.term(),res);
  }
  bool tryInterpretConstant(const Term* t, IntegerConstantType& res);
  bool tryInterpretConstant(unsigned functor, IntegerConstantType& res);
  Option<IntegerConstantType> tryInterpretConstant(unsigned functor);
  /**
   * Try to interpret the term list as an rational constant. If it is an
   * rational constant, return true and save the constant in @c res, otherwise
   * return false.
   */
  bool tryInterpretConstant(TermList trm, RationalConstantType& res);
  bool tryInterpretConstant(const Term* t, RationalConstantType& res);
  bool tryInterpretConstant(unsigned functor, RationalConstantType& res);
  /**
   * Try to interpret the term list as an real constant. If it is an
   * real constant, return true and save the constant in @c res, otherwise
   * return false.
   */
  bool tryInterpretConstant(TermList trm, RealConstantType& res)
  {
    if (!trm.isTerm()) {
      return false;
    }
    return tryInterpretConstant(trm.term(),res);
  }
  bool tryInterpretConstant(const Term* t, RealConstantType& res);
  bool tryInterpretConstant(unsigned functor, RealConstantType& res);

  Term* representConstant(const IntegerConstantType& num);
  Term* representConstant(const RationalConstantType& num);
  Term* representConstant(const RealConstantType& num);

  Term* representIntegerConstant(std::string str);
  Term* representRealConstant(std::string str);
private:
  Theory();
  static OperatorType* getConversionOperationType(Interpretation i);

  DHMap<TermList,unsigned> _arraySkolemFunctions;

public:
  class Tuples {
  public:
    bool isFunctor(unsigned functor);
    unsigned getFunctor(unsigned arity, TermList sorts[]);
    unsigned getFunctor(TermList tupleSort);
    unsigned getProjectionFunctor(unsigned proj, TermList tupleSort);
    bool findProjection(unsigned projFunctor, bool isPredicate, unsigned &proj);
  };

  static Theory::Tuples tuples_obj;
  static Theory::Tuples* tuples();
};

#define ANY_INTERPRETED_PREDICATE                                                                             \
         Kernel::Theory::EQUAL:                                                                               \
    case Kernel::Theory::INT_IS_INT:                                                                          \
    case Kernel::Theory::INT_IS_RAT:                                                                          \
    case Kernel::Theory::INT_IS_REAL:                                                                         \
    case Kernel::Theory::INT_GREATER:                                                                         \
    case Kernel::Theory::INT_GREATER_EQUAL:                                                                   \
    case Kernel::Theory::INT_LESS:                                                                            \
    case Kernel::Theory::INT_LESS_EQUAL:                                                                      \
    case Kernel::Theory::INT_DIVIDES:                                                                         \
    case Kernel::Theory::RAT_IS_INT:                                                                          \
    case Kernel::Theory::RAT_IS_RAT:                                                                          \
    case Kernel::Theory::RAT_IS_REAL:                                                                         \
    case Kernel::Theory::RAT_GREATER:                                                                         \
    case Kernel::Theory::RAT_GREATER_EQUAL:                                                                   \
    case Kernel::Theory::RAT_LESS:                                                                            \
    case Kernel::Theory::RAT_LESS_EQUAL:                                                                      \
    case Kernel::Theory::REAL_IS_INT:                                                                         \
    case Kernel::Theory::REAL_IS_RAT:                                                                         \
    case Kernel::Theory::REAL_IS_REAL:                                                                        \
    case Kernel::Theory::REAL_GREATER:                                                                        \
    case Kernel::Theory::REAL_GREATER_EQUAL:                                                                  \
    case Kernel::Theory::REAL_LESS:                                                                           \
    case Kernel::Theory::ARRAY_BOOL_SELECT:                                                                   \
    case Kernel::Theory::REAL_LESS_EQUAL

#define ANY_INTERPRETED_FUNCTION                                                                              \
         Kernel::Theory::INT_SUCCESSOR:                                                                       \
    case Kernel::Theory::INT_UNARY_MINUS:                                                                     \
    case Kernel::Theory::INT_PLUS:                                                                            \
    case Kernel::Theory::INT_MINUS:                                                                           \
    case Kernel::Theory::INT_MULTIPLY:                                                                        \
    case Kernel::Theory::INT_QUOTIENT_E:                                                                      \
    case Kernel::Theory::INT_QUOTIENT_T:                                                                      \
    case Kernel::Theory::INT_QUOTIENT_F:                                                                      \
    case Kernel::Theory::INT_REMAINDER_E:                                                                     \
    case Kernel::Theory::INT_REMAINDER_T:                                                                     \
    case Kernel::Theory::INT_REMAINDER_F:                                                                     \
    case Kernel::Theory::INT_FLOOR:                                                                           \
    case Kernel::Theory::INT_CEILING:                                                                         \
    case Kernel::Theory::INT_TRUNCATE:                                                                        \
    case Kernel::Theory::INT_ROUND:                                                                           \
    case Kernel::Theory::INT_ABS:                                                                             \
    case Kernel::Theory::RAT_UNARY_MINUS:                                                                     \
    case Kernel::Theory::RAT_PLUS:                                                                            \
    case Kernel::Theory::RAT_MINUS:                                                                           \
    case Kernel::Theory::RAT_MULTIPLY:                                                                        \
    case Kernel::Theory::RAT_QUOTIENT:                                                                        \
    case Kernel::Theory::RAT_QUOTIENT_E:                                                                      \
    case Kernel::Theory::RAT_QUOTIENT_T:                                                                      \
    case Kernel::Theory::RAT_QUOTIENT_F:                                                                      \
    case Kernel::Theory::RAT_REMAINDER_E:                                                                     \
    case Kernel::Theory::RAT_REMAINDER_T:                                                                     \
    case Kernel::Theory::RAT_REMAINDER_F:                                                                     \
    case Kernel::Theory::RAT_FLOOR:                                                                           \
    case Kernel::Theory::RAT_CEILING:                                                                         \
    case Kernel::Theory::RAT_TRUNCATE:                                                                        \
    case Kernel::Theory::RAT_ROUND:                                                                           \
    case Kernel::Theory::REAL_UNARY_MINUS:                                                                    \
    case Kernel::Theory::REAL_PLUS:                                                                           \
    case Kernel::Theory::REAL_MINUS:                                                                          \
    case Kernel::Theory::REAL_MULTIPLY:                                                                       \
    case Kernel::Theory::REAL_QUOTIENT:                                                                       \
    case Kernel::Theory::REAL_QUOTIENT_E:                                                                     \
    case Kernel::Theory::REAL_QUOTIENT_T:                                                                     \
    case Kernel::Theory::REAL_QUOTIENT_F:                                                                     \
    case Kernel::Theory::REAL_REMAINDER_E:                                                                    \
    case Kernel::Theory::REAL_REMAINDER_T:                                                                    \
    case Kernel::Theory::REAL_REMAINDER_F:                                                                    \
    case Kernel::Theory::REAL_FLOOR:                                                                          \
    case Kernel::Theory::REAL_CEILING:                                                                        \
    case Kernel::Theory::REAL_TRUNCATE:                                                                       \
    case Kernel::Theory::REAL_ROUND:                                                                          \
    case Kernel::Theory::INT_TO_INT:                                                                          \
    case Kernel::Theory::INT_TO_RAT:                                                                          \
    case Kernel::Theory::INT_TO_REAL:                                                                         \
    case Kernel::Theory::RAT_TO_INT:                                                                          \
    case Kernel::Theory::RAT_TO_RAT:                                                                          \
    case Kernel::Theory::RAT_TO_REAL:                                                                         \
    case Kernel::Theory::REAL_TO_INT:                                                                         \
    case Kernel::Theory::REAL_TO_RAT:                                                                         \
    case Kernel::Theory::REAL_TO_REAL:                                                                        \
    case Kernel::Theory::ARRAY_SELECT:                                                                        \
    case Kernel::Theory::ARRAY_STORE

typedef Theory::Interpretation Interpretation;

/**
 * Pointer to the singleton Theory instance
 */
extern Theory* theory;

std::ostream& operator<<(std::ostream& out, Kernel::Theory::Interpretation const& self);

}

template<>
struct std::hash<Kernel::IntegerConstantType>
{
  size_t operator()(Kernel::IntegerConstantType const& self) const noexcept 
  { return self.hash(); }
};

template<>
struct std::hash<Kernel::RationalConstantType>
{
  size_t operator()(Kernel::RationalConstantType const& self) const noexcept 
  { return self.hash(); }
};


template<>
struct std::hash<Kernel::RealConstantType>
{
  size_t operator()(Kernel::RealConstantType const& self) const noexcept 
  { return self.hash(); }
};


#endif // __Theory__
