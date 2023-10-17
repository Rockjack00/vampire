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
 * @file InductionRewriting.hpp
 * Defines class InductionRewriting
 *
 */

#ifndef __InductionRewriting__
#define __InductionRewriting__

#include "Forwards.hpp"

#include "InferenceEngine.hpp"
#include "Induction.hpp"

#include "Kernel/EqHelper.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/TermTransformer.hpp"

#include "Lib/ScopedPtr.hpp"
#include "Lib/SharedSet.hpp"

namespace Inferences
{

using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

TermList replaceOccurrence(Term* t, Term* orig, TermList repl, const Position& pos);
vstring posToString(const Position& pos);
VirtualIterator<std::pair<Term*,Position>> getPositions(TermList t, Term* st);
bool shouldChain(Term* lhs);
VirtualIterator<TypedTermList> lhsIterator(Literal* lit);
VirtualIterator<TypedTermList> orderedLhsIterator(Literal* lit, const Ordering& ord, bool reverse);

class PositionalNonVariableNonTypeIterator
  : public IteratorCore<std::pair<Term*,Position>>
{
public:
  PositionalNonVariableNonTypeIterator(const PositionalNonVariableNonTypeIterator&);

  PositionalNonVariableNonTypeIterator(Term* term) : _stack(8)
  {
    _stack.push(std::make_pair(term,Position()));
  }

  /** true if there exists at least one subterm */
  bool hasNext() { return !_stack.isEmpty(); }
  std::pair<Term*,Position> next();
private:
  /** available non-variable subterms */
  Stack<std::pair<Term*,Position>> _stack;
}; // PositionalNonVariableNonTypeIterator

class InductionRewriting
: public GeneratingInferenceEngine
{
public:
  CLASS_NAME(InductionRewriting);
  USE_ALLOCATOR(InductionRewriting);

  void attach(SaturationAlgorithm* salg) override;
  void detach() override;
  ClauseIterator generateClauses(Clause* premise) override;

private:
  Clause* perform(Clause* rwClause, Literal* rwLit, Term* rwSide, Term* rwTerm, Position&& pos,
    Clause* eqClause, Literal* eqLit, TermList eqLhs, ResultSubstitution* subst, bool eqIsResult);

  TermIndex* _lhsIndex;
  TermIndex* _subtermIndex;
};

}

#endif /*__InductionRewriting__*/