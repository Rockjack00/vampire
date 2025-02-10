/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#ifndef __TEST__GENERATION_TESTER_HPP__
#define __TEST__GENERATION_TESTER_HPP__

/**
 * This file provides macros and classes used to write nice tests for generating inference rules.
 *
 * \see UnitTests/tEqualityResolution.cpp, for usage a example
 *
 * Don't rely on any part of the interface, but the things containted in the examples,
 * because it's rather unstable.
 */

#include "Forwards.hpp"
#include "Inferences/InferenceEngine.hpp"
#include "Test/TestUtils.hpp"
#include "Kernel/Clause.hpp"
#include "Lib/Coproduct.hpp"
#include "Test/ClausePattern.hpp"
#include "Saturation/Otter.hpp"
#include "Kernel/Problem.hpp"
#include "Shell/Options.hpp"
#include "Lib/STL.hpp"
#include "Test/MockedSaturationAlgorithm.hpp"
#include "Test/SyntaxSugar.hpp"
#include <type_traits>

namespace Test {

#define TEST_FN_ASS_EQ(VAL1, VAL2)                                                        \
  [] (std::string& s1, std::string& s2) {                                                 \
    bool res = (VAL1 == VAL2);                                                            \
    if (!res) {                                                                           \
      s1 = Int::toString(VAL1);                                                           \
      s1.append(" != ");                                                                  \
      s1.append(Int::toString(VAL2));                                                     \
      s2 = std::string(#VAL1);                                                            \
      s2.append(" == ");                                                                  \
      s2.append(#VAL2);                                                                   \
    }                                                                                     \
    return res;                                                                           \
  }

namespace Generation {
template<class R>
class GenerationTester;
}

class ContainsStackMatcher {
  Stack<ClausePattern> _patterns;

public:
  ContainsStackMatcher(Stack<ClausePattern> self) : _patterns(self) {}

  template<class Rule>
  bool matches(Stack<Kernel::Clause*> sRes, Generation::GenerationTester<Rule>& simpl) 
  { 
    return iterTraits(_patterns.iter())
      .all([&](auto& p) {
          return iterTraits(sRes.iter())
             .any([&](Kernel::Clause* cl) { return p.matches(simpl, cl); });
      });
  }

  template<class F>
  ContainsStackMatcher mapClauses(F fun) const {
    return ContainsStackMatcher(arrayIter(_patterns)
        .map([&](auto& c) { return c.mapClauses(fun); })
        .template collect<Stack>()); }

  friend std::ostream& operator<<(std::ostream& out, ContainsStackMatcher const& self)
  { return out << "contains: " << self._patterns; }
};


class StackMatcher;
std::ostream& operator<<(std::ostream& out, StackMatcher const& self);

class ExactlyStackMatcher {
  Stack<ClausePattern> _patterns;

public:
  ExactlyStackMatcher(Stack<ClausePattern> self) : _patterns(self) {}

  template<class F>
  ExactlyStackMatcher mapClauses(F fun) const {
    return ExactlyStackMatcher(arrayIter(_patterns)
        .map([&](auto& c) { return c.mapClauses(fun); })
        .template collect<Stack>()); }

  template<class Rule>
  bool matches(Stack<Kernel::Clause*> sRes, Generation::GenerationTester<Rule>& simpl) 
  { return TestUtils::permEq(_patterns, sRes, [&](auto exp, auto res) { return exp.matches(simpl, res); }); }

  friend std::ostream& operator<<(std::ostream& out, ExactlyStackMatcher const& self)
  { return out << "exactly: " << self._patterns; }
};

class TodoStackMatcher {

public:
  TodoStackMatcher() {}

  template<class F>
  TodoStackMatcher mapClauses(F fun) const 
  { return TodoStackMatcher(); }

  template<class Rule>
  bool matches(Stack<Kernel::Clause*> sRes, Generation::GenerationTester<Rule>& simpl) 
  { return false; }

  friend std::ostream& operator<<(std::ostream& out, TodoStackMatcher const& self)
  { return out << "TODO"; }
};

class WithoutDuplicatesMatcher {
  std::shared_ptr<StackMatcher> _inner;

public:
  WithoutDuplicatesMatcher(std::shared_ptr<StackMatcher> m) : _inner(std::move(m)) {}

  template<class F>
  WithoutDuplicatesMatcher mapClauses(F fun) const;

  template<class Rule>
  bool matches(Stack<Kernel::Clause*> sRes, Generation::GenerationTester<Rule>& simpl);

  friend std::ostream& operator<<(std::ostream& out, WithoutDuplicatesMatcher const& self)
  { return out << "without duplicates: " << *self._inner; }
};


using AnyStackMatcher = Coproduct< ContainsStackMatcher
                                 , WithoutDuplicatesMatcher
                                 , ExactlyStackMatcher
                                 , TodoStackMatcher>;

class StackMatcher
  : public AnyStackMatcher {
public:
  StackMatcher(std::initializer_list<ClausePattern> clauses) : StackMatcher(ExactlyStackMatcher(Stack<ClausePattern>(clauses))) {}
  template<class C> StackMatcher(C c) : AnyStackMatcher(std::move(c)) {}

  template<class F>
  StackMatcher mapClauses(F fun) const 
  { return apply([&](auto x) { return StackMatcher(x.mapClauses(fun)); }); }

  template<class Rule>
  bool matches(Stack<Kernel::Clause*> sRes, Generation::GenerationTester<Rule>& simpl) 
  { return apply([&](auto& self) { return self.matches(sRes, simpl); }); }

  friend std::ostream& operator<<(std::ostream& out, StackMatcher const& self)
  { return self.apply([&](auto& inner) -> decltype(auto) { return out << inner; }); }
};

template<class F>
WithoutDuplicatesMatcher WithoutDuplicatesMatcher::mapClauses(F fun) const 
{ return make_shared(_inner->mapClauses(fun)); }


template<class Rule>
bool WithoutDuplicatesMatcher::matches(Stack<Kernel::Clause*> sRes, Generation::GenerationTester<Rule>& simpl)
{ 
  Stack<Stack<Literal*>> clauses;
  for (auto c : sRes) {
    clauses.push(c->iterLits().collect<Stack<Literal*>>().sorted());
  }
  clauses.sort();
  clauses.dedup();
  Stack<Kernel::Clause*> newRes;
  for (auto& c : clauses) {
    newRes.push(Clause::fromStack(c, Inference(FromInput(UnitInputType::ASSUMPTION))));
  }

  return _inner->matches(std::move(newRes), simpl); 
}

template<class... As>
StackMatcher exactly(As... as) 
{ return ExactlyStackMatcher(Stack<ClausePattern>({ as... })); }

inline StackMatcher EXPECTED_TODO()
{ return TodoStackMatcher(); }

inline StackMatcher withoutDuplicates(StackMatcher inner) 
{ return WithoutDuplicatesMatcher(std::shared_ptr<StackMatcher>(move_to_heap(std::move(inner)))); }


template<class... As>
StackMatcher contains(As... as) 
{ return ContainsStackMatcher(Stack<ClausePattern>({ as... })); }

inline StackMatcher none() 
{ return ExactlyStackMatcher(Stack<ClausePattern>()); }

namespace Generation {
class AsymmetricTest;
class SymmetricTest;

template<class Rule>
class GenerationTester
{
protected:
  std::unique_ptr<NewGeneratingInference> _rule;

public:

  GenerationTester(std::unique_ptr<NewGeneratingInference> rule) : _rule(std::move(rule)) {}


  template<class T>
  GenerationTester(T rule) 
    : GenerationTester(makePtr(move_to_heap(std::move(rule))))
  {  }

  static std::unique_ptr<NewGeneratingInference> makePtr(NewGeneratingInference* inf) { return std::unique_ptr<NewGeneratingInference>(inf); }
  static std::unique_ptr<NewGeneratingInference> makePtr(SimplifyingGeneratingInference* inf) 
  { return NewGeneratingInference::fromSGI(inf); }

  // template<class T,
  //   std::enable_if_t<std::is_base_of_v<NewGeneratingInference, Rule>, bool> = 0>
  // GenerationTester(T rule) 
  //   : _rule(std::unique_ptr<NewGeneratingInference>(static_cast<NewGeneratingInference*>(move_to_heap(std::move(rule))))) 
  // {  }
  //
  // template<class T,
  //   std::enable_if_t<std::is_base_of_v<SimplifyingGeneratingInference, Rule>, bool> = 0>
  // GenerationTester(T rule) 
  //   : _rule(std::unique_ptr<NewGeneratingInference>(NewGeneratingInference::fromSGI(move_to_heap(std::move(rule))))) 
  // {  }

  virtual Clause* normalize(Kernel::Clause* c)
  { return c; }

  virtual bool eq(Kernel::Clause* lhs, Kernel::Clause* rhs)
  { return TestUtils::eqModACRect(lhs, rhs); }

  friend class AsymmetricTest;
  friend class SymmetricTest;
};

class AsymmetricTest
{
  using Clause = Kernel::Clause;
  using OptionMap = Stack<std::pair<std::string,std::string>>;
  using Condition = std::function<bool(std::string&, std::string&)>;
  Option<NewGeneratingInference*> _rule;
  Clause* _input;
  Option<StackMatcher> _expected;
  Stack<Clause*> _context;
  Option<bool> _premiseRedundant;
  Stack<std::function<Indexing::Index*()>> _indices;
  std::function<void(SaturationAlgorithm&)> _setup = [](SaturationAlgorithm&){};
  bool _selfApplications;
  OptionMap _options;
  Stack<Condition> _preConditions;
  Stack<Condition> _postConditions;

  template<class Is, class Expected>
  void testFail(Is const& is, Expected const& expected) {
      std::cout  << std::endl;
      std::cout << "[  context ]: " << pretty(_context) << std::endl;
      std::cout << "[  options ]: " << pretty(_options) << std::endl;
      std::cout << "[     case ]: " << pretty(*_input) << std::endl;
      std::cout << "[       is ]: " << pretty(is) << std::endl;
      std::cout << "[ expected ]: " << pretty(expected) << std::endl;
      ASSERTION_VIOLATION
  }

public:

  AsymmetricTest() : _rule(), _input(NULL), _expected(), _premiseRedundant(false), _selfApplications(true), _options() {}

#define BUILDER_METHOD(type, field)                                                       \
  AsymmetricTest field(type field)                                                        \
  {                                                                                       \
    this->_##field = decltype(_##field)(std::move(field));                                \
    return *this;                                                                         \
  }                                                                                       \

  BUILDER_METHOD(Clause*, input)
  BUILDER_METHOD(ClauseStack, context)
  BUILDER_METHOD(StackMatcher, expected)
  BUILDER_METHOD(Option<bool>, premiseRedundant)
  BUILDER_METHOD(bool, premiseRedundant)
  BUILDER_METHOD(bool, selfApplications)
  BUILDER_METHOD(NewGeneratingInference*, rule)
  BUILDER_METHOD(Stack<std::function<Indexing::Index*()>>, indices)
  BUILDER_METHOD(std::function<void(SaturationAlgorithm&)>, setup)
  BUILDER_METHOD(OptionMap, options)
  BUILDER_METHOD(Stack<Condition>, preConditions)
  BUILDER_METHOD(Stack<Condition>, postConditions)

  auto rule(SimplifyingGeneratingInference* sgi) {
    _rule = some(NewGeneratingInference::fromSGI(sgi).release());
    return *this;
  }

  template<class Rule>
  void run(GenerationTester<Rule>& simpl) {

    for (auto& c : _context) {
      c = simpl.normalize(c);
    }
    _input = simpl.normalize(_input);

    // set up saturation algorithm
    auto container = ActiveClauseContainer();

    // init problem
    Problem p;
    auto ul = UnitList::empty();
    UnitList::pushFromIterator(ClauseStack::Iterator(_context), ul);
    p.addUnits(ul);
    env.setMainProblem(&p);

    delete env.options;
    env.options = new Options;
    for (const auto& kv : _options) {
      env.options->set(kv.first, kv.second);
    }
    
    auto& alg = *SaturationAlgorithm::createFromOptions(p, *env.options);
    // MockedSaturationAlgorithm alg(p, *env.options);
    _setup(alg);
    auto& rule = *_rule.unwrapOrElse([&](){ return simpl._rule.get(); });
    rule.attach(&alg);
    Stack<Indexing::Index*> indices;
    for (auto i : _indices) {
      indices.push(i());
    }

    rule.setTestIndices(indices);
    for (auto i : indices) {
      i->attachContainer(&container);
    }

    // add the clauses to the index
    for (auto c : _context) {
      c->setStore(Clause::ACTIVE);
      container.add(c);
    }

    // check that the preconditions hold
    std::string s1, s2;
    for (auto c : _preConditions) {
      if (!c(s1, s2)) {
        s2.append(" (precondition)");
        testFail(s1, s2);
      }
    }

    // run rule
    if (_selfApplications) {
      _input->setStore(Clause::ACTIVE);
      container.add(_input);
    }

    auto res = rule.apply(_input);

    // run checks
    auto sExp = this->_expected.unwrap();
    Set<Clause*> redundant;
    Stack<Clause*> generated;
    for (auto r : iterTraits(res)) {
      redundant.loadFromIterator(r.redundant);
      generated.loadFromIterator(r.generated);
    }

    if (!sExp.matches(generated, simpl)) {
      testFail(generated, sExp);
    }

    if (_premiseRedundant.isSome()) {
      auto premiseRedundant = redundant.contains(_input);
      if (*_premiseRedundant != premiseRedundant) {
        auto wrapStr = [](bool b) -> std::string { return b ? "premise is redundant" : "premise is not redundant"; };
        testFail( wrapStr(premiseRedundant), wrapStr(_premiseRedundant));
      }
    }


    // check that the postconditions hold
    for (auto c : _postConditions) {
      if (!c(s1, s2)) {
        s2.append(" (postcondition)");
        testFail(s1, s2);
      }
    }


    // add the clauses to the index
    for (auto c : _context) {
      // c->setStore(Clause::ACTIVE);
      container.remove(c);
    }

    // tear down saturation algorithm
    rule.detach();

    Ordering::unsetGlobalOrdering();
    delete &alg;
    SaturationAlgorithm::unsetGlobalInstance();
  }
};

class SymmetricTest
{
  using Clause = Kernel::Clause;
  Option<NewGeneratingInference*> _rule;
  Stack<Clause*> _inputs;
  Option<StackMatcher> _expected;
  Option<bool> _premiseRedundant;
  bool _selfApplications;
  Stack<std::function<Indexing::Index*()>> _indices;

  template<class Is, class Expected>
  void testFail(Is const& is, Expected const& expected) {
      std::cout  << std::endl;
      std::cout << "[     case ]: " << pretty(_inputs) << std::endl;
      std::cout << "[       is ]: " << pretty(is) << std::endl;
      std::cout << "[ expected ]: " << pretty(expected) << std::endl;
      exit(-1);
  }

public:

  SymmetricTest() : _rule(), _expected(), _premiseRedundant(), _selfApplications(true) {}

#define _BUILDER_METHOD(type, field)                                                      \
  SymmetricTest field(type field)                                                         \
  {                                                                                       \
    this->_##field = decltype(_##field)(std::move(field));                                \
    return *this;                                                                         \
  }                                                                                       \

  _BUILDER_METHOD(Stack<Clause*>, inputs)
  _BUILDER_METHOD(StackMatcher, expected)
  _BUILDER_METHOD(bool, premiseRedundant)
  _BUILDER_METHOD(Option<bool>, premiseRedundant)
  _BUILDER_METHOD(bool, selfApplications)
  _BUILDER_METHOD(NewGeneratingInference*, rule)
  _BUILDER_METHOD(Stack<std::function<Indexing::Index*()>>, indices)

  auto rule(SimplifyingGeneratingInference* sgi) {
    _rule = some(NewGeneratingInference::fromSGI(sgi).release());
    return *this;
  }


  template<class Rule>
  void run(GenerationTester<Rule>& simpl) {
    for (unsigned i = 0; i < _inputs.size(); i++) {
      Stack<Clause*> context;
      auto input = _inputs[i];
      for (unsigned j = 0; j < _inputs.size(); j++) 
        if (i != j) 
          context.push(_inputs[j]);
      run(simpl, input, context);
    }
  }

  template<class Rule>
  void run(GenerationTester<Rule>& simpl, Clause* input, Stack<Clause*> context) {
    auto rule = _rule.unwrapOrElse([&](){ return simpl._rule.get(); });
    AsymmetricTest()
      .input(input)
      .context(context)
      .expected(_expected.unwrap())
      .premiseRedundant(_premiseRedundant)
      .selfApplications(_selfApplications)
      .rule(rule)
      .indices(_indices)
      .run(simpl);
  }
};

#define __CREATE_GEN_TESTER CAT(__createGenTester_, UNIT_ID)

#define REGISTER_GEN_TESTER(t) const auto __CREATE_GEN_TESTER = []()  { return t; };

#define TEST_GENERATION(name, ...)                                                        \
        TEST_GENERATION_WITH_SUGAR(name, MY_SYNTAX_SUGAR, __VA_ARGS__) 

#define TEST_GENERATION_WITH_SUGAR(name, syntax_sugar, ...)                               \
  TEST_FUN(name) {                                                                        \
    auto tester = __CREATE_GEN_TESTER();                                                  \
    __ALLOW_UNUSED(syntax_sugar)                                                          \
    auto test = __VA_ARGS__;                                                              \
    test.run(tester);                                                                     \
  }                                                                                       \

} // namespace Simplification

} // namespace Test

#endif // __TEST__GENERATION_TESTER_HPP__
