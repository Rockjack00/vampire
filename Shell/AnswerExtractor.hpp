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
 * @file AnswerExtractor.hpp
 * Defines class AnswerExtractor.
 */

#ifndef __AnswerExtractor__
#define __AnswerExtractor__

#include "Forwards.hpp"

#include "Lib/DHMap.hpp"

#include "Kernel/Formula.hpp"
#include "Kernel/InferenceStore.hpp"
#include "Kernel/RCClauseStack.hpp"
#include "Kernel/TermTransformer.hpp"



namespace Shell {

using namespace Lib;
using namespace Kernel;
using namespace Indexing;

class AnswerExtractor {
public:
  virtual ~AnswerExtractor() {}

  static void tryOutputAnswer(Clause* refutation);

  virtual bool tryGetAnswer(Clause* refutation, Stack<TermList>& answer) = 0;

  virtual void tryOutputInputUnits();

  void addInputUnit(Unit* unit) { UnitList::push(unit, _inputs); }
protected:
  void getNeededUnits(Clause* refutation, ClauseStack& premiseClauses, Stack<Unit*>& conjectures, DHSet<Unit*>& allProofUnits);

  UnitList* _inputs = nullptr;
};

class ConjunctionGoalAnswerExractor : public AnswerExtractor {
public:
  virtual bool tryGetAnswer(Clause* refutation, Stack<TermList>& answer);

private:
  class SubstBuilder;
};


class AnswerLiteralManager : public AnswerExtractor
{
public:
  static AnswerLiteralManager* getInstance();

  virtual bool tryGetAnswer(Clause* refutation, Stack<TermList>& answer);

  void addAnswerLiterals(Problem& prb);
  bool addAnswerLiterals(UnitList*& units);

  void onNewClause(Clause* cl);

  Clause* recordAnswerAndReduce(Clause* cl);

  void bindSkolemToVar(Term* skolem, unsigned var);

  static unsigned getITEFunctionSymbol(TermList sort) {
    vstring name = "$ite_" + sort.toString();
    bool added = false;
    unsigned fn = env.signature->addFunction(name, 3, added);
    if (added) {
      Signature::Symbol* sym = env.signature->getFunction(fn);
      sym->setType(OperatorType::getFunctionType({AtomicSort::defaultSort(), sort, sort}, sort));
    }
    return fn;
  }

  static Literal* makeITEAnswerLiteral(Literal* condition, Literal* thenLit, Literal* elseLit);

private:
  class ConjectureSkolemReplacement : public TermTransformer {
   public:
    ConjectureSkolemReplacement() : _skolemToVar() {}
    void bindSkolemToVar(Term* t, unsigned v);
    TermList transformTermList(TermList tl, TermList sort);
    virtual Literal* transform(Literal* lit) { return TermTransformer::transform(lit); }
   protected:
    virtual TermList transformSubterm(TermList trm);
    virtual TermList transform(TermList ts);
   private:
    vmap<Term*, unsigned> _skolemToVar;
  };

  Literal* getAnswerLiteral(VList* vars,Formula* f);
  Unit* tryAddingAnswerLiteral(Unit* unit);

  Clause* getResolverClause(unsigned pred);
  Clause* getRefutation(Clause* answer);

  Formula* getConditionFromClause(Clause* cl);

  RCClauseStack _answers;

  DHMap<unsigned, Clause*> _resolverClauses;

  ConjectureSkolemReplacement _skolemReplacement;

  List<pair<Clause*, Literal*>>* _answerPairs = nullptr;

  Literal* _lastAnsLit = nullptr;
};

}

#endif // __AnswerExtractor__
