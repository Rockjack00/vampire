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
 * @file LiteralIndex.hpp
 * Defines class LiteralIndex.
 */


#ifndef __LiteralIndex__
#define __LiteralIndex__

#include "Lib/DHMap.hpp"

#include "Index.hpp"
#include "LiteralIndexingStructure.hpp"


namespace Indexing {

template<class Data>
class LiteralIndex
: public Index
{
public:
  CLASS_NAME(LiteralIndex);
  USE_ALLOCATOR(LiteralIndex);

  VirtualIterator<DefaultLiteralLeafData> getAll()
  { return _is->getAll(); }

  SLQueryResultIterator getUnifications(Literal* lit, bool complementary, bool retrieveSubstitutions = true)
  { return _is->getUnifications(lit, complementary, retrieveSubstitutions); }

  VirtualIterator<QueryRes<AbstractingUnifier*, Data>> getUwa(Literal* lit, bool complementary, Options::UnificationWithAbstraction uwa, bool fixedPointIteration)
  { return _is->getUwa(lit, complementary, uwa, fixedPointIteration); }

  SLQueryResultIterator getGeneralizations(Literal* lit, bool complementary, bool retrieveSubstitutions = true)
  { return _is->getGeneralizations(lit, complementary, retrieveSubstitutions); }

  SLQueryResultIterator getInstances(Literal* lit, bool complementary, bool retrieveSubstitutions = true)
  { return _is->getInstances(lit, complementary, retrieveSubstitutions); }

  size_t getUnificationCount(Literal* lit, bool complementary)
  { return _is->getUnificationCount(lit, complementary); }


protected:
  LiteralIndex(LiteralIndexingStructure<Data>* is) : _is(is) {}

  void handleLiteral(Literal* lit, Clause* cl, bool add)
  { _is->handle(Data(cl, lit), add); }

  unique_ptr<LiteralIndexingStructure<Data>> _is;
};

class BinaryResolutionIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(BinaryResolutionIndex);
  USE_ALLOCATOR(BinaryResolutionIndex);

  BinaryResolutionIndex(LiteralIndexingStructure<>* is)
  : LiteralIndex<DefaultLiteralLeafData>(is) {};
protected:
  void handleClause(Clause* c, bool adding);
};

class BackwardSubsumptionIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(BackwardSubsumptionIndex);
  USE_ALLOCATOR(BackwardSubsumptionIndex);

  BackwardSubsumptionIndex(LiteralIndexingStructure<>* is)
  : LiteralIndex<DefaultLiteralLeafData>(is) {};
protected:
  void handleClause(Clause* c, bool adding);
};

class FwSubsSimplifyingLiteralIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(FwSubsSimplifyingLiteralIndex);
  USE_ALLOCATOR(FwSubsSimplifyingLiteralIndex);

  FwSubsSimplifyingLiteralIndex(LiteralIndexingStructure<>* is)
    : LiteralIndex<DefaultLiteralLeafData>(is)
  { }

protected:
  void handleClause(Clause* c, bool adding) override;
};

class FSDLiteralIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(FSDLiteralIndex);
  USE_ALLOCATOR(FSDLiteralIndex);

  FSDLiteralIndex(LiteralIndexingStructure<>* is)
    : LiteralIndex<DefaultLiteralLeafData>(is)
  { }

protected:
  void handleClause(Clause* c, bool adding) override;
};

class UnitClauseLiteralIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(UnitClauseLiteralIndex);
  USE_ALLOCATOR(UnitClauseLiteralIndex);

  UnitClauseLiteralIndex(LiteralIndexingStructure<>* is)
  : LiteralIndex<DefaultLiteralLeafData>(is) {};
protected:
  void handleClause(Clause* c, bool adding);
};

class NonUnitClauseLiteralIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(NonUnitClauseLiteralIndex);
  USE_ALLOCATOR(NonUnitClauseLiteralIndex);

  NonUnitClauseLiteralIndex(LiteralIndexingStructure<>* is, bool selectedOnly=false)
  : LiteralIndex<DefaultLiteralLeafData>(is), _selectedOnly(selectedOnly) {};
protected:
  void handleClause(Clause* c, bool adding);
private:
  bool _selectedOnly;
};

class RewriteRuleIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(RewriteRuleIndex);
  USE_ALLOCATOR(RewriteRuleIndex);

  RewriteRuleIndex(LiteralIndexingStructure<>* is, Ordering& ordering);
  ~RewriteRuleIndex();

  Clause* getCounterpart(Clause* c) {
    return _counterparts.get(c);
  }
protected:
  void handleClause(Clause* c, bool adding);
  Literal* getGreater(Clause* c);

private:
  void handleEquivalence(Clause* c, Literal* cgr, Clause* d, Literal* dgr, bool adding);

  LiteralIndexingStructure<>* _partialIndex;
  DHMap<Clause*,Clause*> _counterparts;
  Ordering& _ordering;
};

class DismatchingLiteralIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(DismatchingLiteralIndex);
  USE_ALLOCATOR(DismatchingLiteralIndex);

  DismatchingLiteralIndex(LiteralIndexingStructure<>* is)
  : LiteralIndex<DefaultLiteralLeafData>(is) {};
  void handleClause(Clause* c, bool adding);
  void addLiteral(Literal* c);
};

class UnitIntegerComparisonLiteralIndex
: public LiteralIndex<DefaultLiteralLeafData>
{
public:
  CLASS_NAME(UnitIntegerComparisonLiteralIndex);
  USE_ALLOCATOR(UnitIntegerComparisonLiteralIndex);

  UnitIntegerComparisonLiteralIndex(LiteralIndexingStructure<>* is)
  : LiteralIndex<DefaultLiteralLeafData>(is) {}

protected:
  void handleClause(Clause* c, bool adding);
};

};

#endif /* __LiteralIndex__ */
