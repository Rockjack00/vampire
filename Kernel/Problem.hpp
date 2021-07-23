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
 * @file Kernel/Problem.hpp
 * Defines class Problem.
 */

#ifndef __Kernel_Problem__
#define __Kernel_Problem__

#include "Forwards.hpp"

#include "Lib/DHMap.hpp"
#include "Lib/MaybeBool.hpp"

#include "Shell/SMTLIBLogic.hpp"

// added for the sake of ProofTracer
#include "Parse/TPTP.hpp"
#include "Indexing/ClauseVariantIndex.hpp"

namespace Kernel {

using namespace Lib;
using namespace Shell;

// before I figure out where to put this new class, let me just keep it here:
struct ProofTracer {
  CLASS_NAME(ProofTracer);
  USE_ALLOCATOR(ProofTracer);

  void init(const vstring& traceFileNames);
  void onInputClause(Clause* cl);
  void onInputFinished();

  enum InferenceKind {
    ICP = 0, // INPUT / PREPROCESSING / CLAUSIFICATION anything larger than this should end up in the TracedProof
    TRIVSIMP = 1,
    SIMPLIFYING = 2,  // TODO: let's see if we don't also need to distinguish FWD and BWD!
    GENERATING = 3,
  };

  struct ParsedProof {
    CLASS_NAME(ParsedProof);
    USE_ALLOCATOR(ParsedProof);

    UnitList* units;
    DHMap<unsigned, vstring> names;
    DHMap<Unit*,Parse::TPTP::SourceRecord*> sources;
  };

  // maybe could use Store for this, but let's keep some flexibility
  // this is what we know about the actual runs clause corresponding to this one at the moment
  enum ClauseState {
    NONE = 0,        // the starting state; somehow before it's even born
    NEW = 1,
    UNPRO = 2,
    PASSIVE = 3,
    ACTIVE = 4,
    GONE = 5
  };

  struct TracedClauseInfo {
    CLASS_NAME(TracedClauseInfo);
    USE_ALLOCATOR(TracedClauseInfo);

    vstring _name;
    InferenceKind _ik; // the kind of inference this clause arose by

    TracedClauseInfo(const vstring& name, InferenceKind ik) : _name(name), _ik(ik), _state(NONE) {}

    Stack<Clause*> _parents;  // premises
    Stack<Clause*> _children; // the opposite arrows

    bool isInital() {
      return _parents.size() == 0;
    }

    // should be only the final empty clause
    bool isTerminal() {
      return _children.size() == 0;
    }

    ClauseState _state;

    void makeNew() {
      ASS_EQ(_state,NONE);
      _state = NEW;
    }


  };

  struct TracedProof {
    CLASS_NAME(TracedProof);
    USE_ALLOCATOR(TracedProof);

    TracedProof() : _theEmpty(0), _variantLookup(new Indexing::HashingClauseVariantIndex()), _unbornInitials(0) {}
    ~TracedProof() { delete _variantLookup; }

    void init();
    void onInputFinished();

    void regNewClause(Clause* cl, const vstring& name, InferenceKind ik) {
      CALL("ProofTracer::TracedProof::regNewClause");

      ALWAYS(_clInfo.insert(cl,new TracedClauseInfo(name,ik)));

      _variantLookup->insert(cl);
    }

    void regChildParentPair(Clause* ch, Clause* p) {
      CALL("ProofTracer::TracedProof::regChildParentPair");

      _clInfo.get(ch)->_parents.push(p);
      _clInfo.get(p)->_children.push(ch);
    }

    void setEmpty(Clause* cl) {
      CALL("ProofTracer::TracedProof::setEmpty");
      ASS_EQ(_theEmpty,0); // only set once
      _theEmpty = cl;
    }

    Clause* findVariant(Clause* cl) {
      CALL("ProofTracer::TracedProof::findVariant");

      Clause* res = 0;

      ClauseIterator it = _variantLookup->retrieveVariants(cl);
      if (it.hasNext()) {
        res = it.next();
        ASS(!it.hasNext());
      }
      return res;
    }

    TracedClauseInfo* getClauseInfo(Clause* cl) {
      CALL("ProofTracer::TracedProof::getClauseInfo");
      return _clInfo.get(cl);
    }

    void initalBorn() {
      CALL("ProofTracer::TracedProof::initalBorn");
      _unbornInitials--;
    }

  private:
    Clause* _theEmpty;
    DHMap<Clause*, TracedClauseInfo*> _clInfo;

    Indexing::ClauseVariantIndex* _variantLookup;

    int _unbornInitials;

  };

protected:
  ParsedProof* getParsedProof(const vstring& traceFileNames);
  TracedProof* prepareTracedProof(ParsedProof* pp);
  void initializeTracedProof(TracedProof* tp);

private:
  TracedProof* _tp;

  Clause* unitToClause(Unit* u);
};




/**
 * Class representing a TPTP problem to be solved
 *
 * The main benefit of this class is that it can carry information about
 * all preprocessing performed on a problem. This can be necessary for
 * outputting models.
 *
 * Functions has... answer with certainty whether the problem (in its current state)
 * has certain property.
 *
 * Functions mayHave... provide answer that may err on the positive side --
 * for example mayHaveEquality() may return true for a problem that no longer
 * has equality because it was removed somewhere during preprocessing.
 * These functions are present so that we do not need to keep track of
 * every step performed by the preprocessor, and at the same time we do not
 * need to reevaluate the Property object with each call to such function.
 */
class Problem {
private:
  Problem(const Problem&); //private and undefined copy constructor
  Problem& operator=(const Problem&); //private and undefined assignment operator
public:

  CLASS_NAME(Problem);
  USE_ALLOCATOR(Problem);

  explicit Problem(UnitList* units=0);
  explicit Problem(ClauseIterator clauses, bool copy);
  ~Problem();

  void addUnits(UnitList* newUnits);

  UnitList*& units() { return _units; }
  const UnitList* units() const { return _units; }

  ClauseIterator clauseIterator() const;

  Problem* copy(bool copyClauses=false);
  void copyInto(Problem& tgt, bool copyClauses=false);

  bool hadIncompleteTransformation() const { return _hadIncompleteTransformation; }
  void reportIncompleteTransformation() { _hadIncompleteTransformation = true; }

  typedef DHMap<unsigned,bool> TrivialPredicateMap;
  void addTrivialPredicate(unsigned pred, bool assignment);
  /**
   * Return map of trivial predicates into their assignments.
   *
   * Trivial predicates are the predicates whose all occurrences
   * can be assigned either true or false.
   */
  const TrivialPredicateMap& trivialPredicates() const { return _trivialPredicates; }

  /**
   * Always exactly one of the pair is non-zero, if the literal is specified,
   * it must be ground.
   */
  typedef pair<Literal*,Clause*> BDDMeaningSpec;
  typedef DHMap<unsigned, BDDMeaningSpec> BDDVarMeaningMap;
  void addBDDVarMeaning(unsigned var, BDDMeaningSpec spec);
  const BDDVarMeaningMap& getBDDVarMeanings() const { return _bddVarSpecs; }

  void addEliminatedFunction(unsigned func, Literal* definition);
  void addEliminatedPredicate(unsigned pred, Unit* definition);
  void addPartiallyEliminatedPredicate(unsigned pred, Unit* definition); 
 
  DHMap<unsigned,Literal*> getEliminatedFunctions(){ return _deletedFunctions; }
  DHMap<unsigned,Unit*> getEliminatedPredicates(){ return _deletedPredicates; }
  DHMap<unsigned,Unit*> getPartiallyEliminatedPredicates(){ return _partiallyDeletedPredicates;}
  

  bool isPropertyUpToDate() const { return _propertyValid; }
  Property* getProperty() const;
  void invalidateProperty() { _propertyValid = false; }

  void invalidateByRemoval();
  void invalidateEverything();

  bool hasFormulas() const;
  bool hasEquality() const;
  /** Problem contains an interpreted symbol including equality */
  bool hasInterpretedOperations() const;
  bool hasInterpretedEquality() const;
  /** Problem contains let terms or formulas, or term if-then-else */
  bool hasFOOL() const;
  bool hasCombs() const;
  bool hasLogicalProxy() const;
  bool hasBoolVar() const;
  bool hasApp() const;
  bool hasAppliedVar() const;
  bool hasPolymorphicSym() const;
  bool quantifiesOverPolymorphicVar() const;

  bool mayHaveEquality() const { return _mayHaveEquality; }
  bool mayHaveFormulas() const { return _mayHaveFormulas; }
  bool mayHaveFunctionDefinitions() const { return _mayHaveFunctionDefinitions; }
  bool mayHaveInequalityResolvableWithDeletion() const { return _mayHaveInequalityResolvableWithDeletion; }
  bool mayHaveXEqualsY() const { return _mayHaveXEqualsY; }

  void setSMTLIBLogic(SMTLIBLogic smtLibLogic) { 
    _smtlibLogic = smtLibLogic;
  }
  SMTLIBLogic getSMTLIBLogic() const {
    CALL("Kernel::Problem::getSMTLIBLogic");
    return _smtlibLogic;
  }

  void reportFOOLEliminated()
  {
    invalidateProperty();
    _hasFOOL = false;
  }

  void reportFOOLAdded()
  {
    invalidateProperty();
    _hasFOOL = true;
  }
  
  void reportFormulasAdded()
  {
    invalidateProperty();
    _mayHaveFormulas = true;
    _hasFormulas = true;
  }
  /**
   * Report that equality was added into the problem
   *
   * If @c oneVariable is true, the equality contained at least one variable,
   * if @c twoVariables is true, the equality was between two variables
   */
  void reportEqualityAdded(bool oneVariable, bool twoVariables=false)
  {
    invalidateProperty();
    _hasEquality = true;
    _mayHaveEquality = true;
    if(oneVariable) {
      _mayHaveInequalityResolvableWithDeletion = true;
    }
    if(twoVariables) {
      _mayHaveXEqualsY = true;
    }
  }
  void reportFormulasEliminated()
  {
    invalidateProperty();
    _hasFormulas = false;
    _mayHaveFormulas = false;
  }
  void reportEqualityEliminated()
  {
    invalidateProperty();
    _hasEquality = false;
    _mayHaveEquality = false;
    _mayHaveFunctionDefinitions = false;
    _mayHaveInequalityResolvableWithDeletion = false;
    _mayHaveXEqualsY = false;
  }


  //utility functions

  void collectPredicates(Stack<unsigned>& acc) const;


#if VDEBUG
  //debugging functions
  void assertValid();
#endif

private:

  void initValues();

  void refreshProperty() const;
  void readDetailsFromProperty() const;

  UnitList* _units;
  DHMap<unsigned,Literal*> _deletedFunctions;
  DHMap<unsigned,Unit*> _deletedPredicates;
  DHMap<unsigned,Unit*> _partiallyDeletedPredicates; 

  bool _hadIncompleteTransformation;

  DHMap<unsigned,bool> _trivialPredicates;
  BDDVarMeaningMap _bddVarSpecs;

  mutable bool _mayHaveEquality;
  mutable bool _mayHaveFormulas;
  mutable bool _mayHaveFunctionDefinitions;
  mutable bool _mayHaveInequalityResolvableWithDeletion;
  mutable bool _mayHaveXEqualsY;

  mutable MaybeBool _hasFormulas;
  mutable MaybeBool _hasEquality;
  mutable MaybeBool _hasInterpretedOperations;
  mutable MaybeBool _hasFOOL;
  mutable MaybeBool _hasCombs;
  mutable MaybeBool _hasApp;
  mutable MaybeBool _hasAppliedVar;
  mutable MaybeBool _hasLogicalProxy;
  mutable MaybeBool _hasPolymorphicSym;
  mutable MaybeBool _quantifiesOverPolymorphicVar;
  mutable MaybeBool _hasBoolVar; 
  mutable MaybeBool _hasInterpretedEquality;

  SMTLIBLogic _smtlibLogic;

  mutable bool _propertyValid;
  mutable Property* _property;
};

}

#endif // __Kernel_Problem__
