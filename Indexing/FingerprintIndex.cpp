#include "Kernel/RobSubstitution.hpp"
#include "Lib/Metaiterators.hpp"
#include "TermIndexingStructure.hpp"
#include "FingerprintIndex.hpp"
#include <iostream>

static const signed A = -1, B = -2, N = -4;

namespace Indexing {
std::array<signed, FingerprintIndex::FINGERPRINT_SIZE> FingerprintIndex::fingerprint(TermList p)
{
  CALL("FingerprintIndex::fingerprint");
  std::array<signed, FINGERPRINT_SIZE> result{N};
  if (p.isVar()) {
    result[0] = A;
    result[1] = B;
    return result;
  }
  Term *t = p.term();
  result[0] = t->functor();

  if (t->arity() == 0) {
    result[1] = N;
    return result;
  }
  TermList *p1 = t->nthArgument(0);
  if (p1->isVar()) {
    result[1] = A;
    return result;
  }

  Term *t1 = p1->term();
  result[1] = t1->functor();
  return result;
}

FingerprintIndex::FingerprintIndex() : _root(new Branch()), _fresh_bucket(0) {}
FingerprintIndex::~FingerprintIndex()
{
  CALL("FingerprintIndex::~FingerprintIndex()");
  delete _root;
}

unsigned FingerprintIndex::makeBucket(TermList t)
{
  CALL("FingerprintIndex::make");
  auto fp = fingerprint(t);
  return _root->makeBucket(fp, _fresh_bucket, 0);
}

void FingerprintIndex::getUnifications(Stack<unsigned> &results, TermList t)
{
  CALL("FingerprintIndex::insert");
  auto fp = fingerprint(t);
  _root->getUnifications(results, fp, 0);
}

FingerprintIndex::Branch::~Branch()
{
  CALL("FingerprintIndex::Branch::~Branch");
  _children.deleteAll();
}

FingerprintIndex::Leaf::Leaf(unsigned bucket) : _bucket(bucket) {}

unsigned FingerprintIndex::Leaf::makeBucket(const std::array<signed, FINGERPRINT_SIZE> &fingerprint, unsigned &fresh, unsigned index)
{
  CALL("FingerprintIndex::Leaf::makeBucket");
  return _bucket;
}

void FingerprintIndex::Leaf::getUnifications(Stack<unsigned> &results, const std::array<signed, FingerprintIndex::FINGERPRINT_SIZE> &fingerprint, unsigned index)
{
  CALL("FingerprintIndex::Leaf::getUnifications");
  results.push(_bucket);
}

unsigned FingerprintIndex::Branch::makeBucket(const std::array<signed, FINGERPRINT_SIZE> &fingerprint, unsigned &fresh, unsigned index)
{
  CALL("FingerprintIndex::Branch::makeBucket");
  Node *next;
  Node **next_ptr = _children.getPtr(fingerprint[index]);
  if (next_ptr) {
    next = *next_ptr;
  }
  else {
    if (index + 1 == FINGERPRINT_SIZE) {
      next = new Leaf(fresh++);
    }
    else {
      next = new Branch();
    }
    _children.insert(fingerprint[index], next);
  }
  return next->makeBucket(fingerprint, fresh, index + 1);
}

void FingerprintIndex::Branch::getUnifications(Stack<unsigned> &results, const std::array<signed, FingerprintIndex::FINGERPRINT_SIZE> &fingerprint, unsigned index)
{
  CALL("FingerprintIndex::Branch::getUnifications");
  signed value = fingerprint[index];

  auto node = [&](signed n) {
    if (Node **next_ptr = _children.getPtr(n)) {
      (*next_ptr)->getUnifications(results, fingerprint, index + 1);
    }
  };
  auto nodes_if = [&](bool (*condition)(signed)) {
    decltype(_children)::Iterator it(_children);
    signed key;
    Node *next;
    while (it.hasNext()) {
      it.next(key, next);
      if (condition(key)) {
        next->getUnifications(results, fingerprint, index + 1);
      }
    }
  };
  switch (value) {
    case N:
      node(B);
      node(N);
      break;
    case B:
      nodes_if([](signed key) { return true; });
      break;
    case A:
      nodes_if([](signed key) { return key != N; });
      break;
    default:
      ASS_GE(value, 0);
      node(value);
      node(A);
      node(B);
      break;
  }
}

bool TermFingerprintIndex::Entry::operator==(const Entry &other) const {
  return cls == other.cls && lit == other.lit && term == other.term;
}

bool TermFingerprintIndex::Entry::operator!=(const Entry &other) const {
  return cls != other.cls || lit != other.lit || term != other.term;
}

TermFingerprintIndex::ResultIterator::ResultIterator(
  TermFingerprintIndex *index,
  Stack<unsigned> &&buckets
) :
  _index(index),
  _buckets(buckets),
  _entryIt()
{}

bool TermFingerprintIndex::ResultIterator::hasNext() {
  CALL("TermFingerprintIndex::ResultIterator::hasNext");
  while(!_entryIt.hasNext()) {
    if(_buckets.isEmpty()) {
      return false;
    }
    unsigned bucket = _buckets.pop();
    _entryIt = Set<Entry>::Iterator(_index->_buckets[bucket]);
  }
  return true;
}

TermQueryResult TermFingerprintIndex::ResultIterator::next() {
  CALL("TermFingerprintIndex::ResultIterator::next");
  Entry entry = _entryIt.next();
  return TermQueryResult(entry.term, entry.lit, entry.cls);
}

TermFingerprintIndex::UnificationIterator::UnificationIterator(
  ResultIterator it,
  TermList query
) :
  _it(it),
  _query(query),
  _subst(new RobSubstitution()),
  _next(),
  _hasNext(false)
{}

bool TermFingerprintIndex::UnificationIterator::hasNext() {
  CALL("TermFingerprintIndex::UnificationIterator::hasNext");
  if(_hasNext) {
    return true;
  }
  //std::cout << "candidates for " << _query << std::endl;
  while(_it.hasNext()) {
    _next = _it.next();
    //std::cout << _next.term << std::endl;
    _subst->reset();
    if(_subst->unify(_query, 0, _next.term, 1)) {
      _next.substitution =
        ResultSubstitution::fromSubstitution(_subst.ptr(), 0, 1);
      _hasNext = true;
      return true;
    }
  }
  //std::cout << "done" << std::endl;
  return false;
}

TermQueryResult TermFingerprintIndex::UnificationIterator::next() {
  CALL("TermFingerprintIndex::UnificationIterator::next");
  //std::cout << _query << " unifies with " << _next.term << std::endl;
  //std::cout << _subst->toString() << std::endl;
  _hasNext = false;
  return _next;
}

void TermFingerprintIndex::insert(TermList trm, Literal *lit, Clause *cls)
{
  CALL("TermFingerprintIndex::insert");
  //std::cout << "insert: " << trm << " in " << *lit << std::endl;
  _buckets[_index.makeBucket(trm)].insert(Entry{cls, lit, trm});
}

void TermFingerprintIndex::remove(TermList trm, Literal *lit, Clause *cls)
{
  CALL("TermFingerprintIndex::remove");
  //std::cout << "remove: " << trm << " in " << *lit << std::endl;
  _buckets[_index.makeBucket(trm)].remove(Entry{cls, lit, trm});
}

TermQueryResultIterator TermFingerprintIndex::getUnifications(TermList t, bool retrieveSubstitutions)
{
  CALL("TermFingerprintIndex::getUnifications");
  Stack<unsigned> buckets;
  _index.getUnifications(buckets, t);
  return
    pvi(UnificationIterator(ResultIterator(this, std::move(buckets)), t));
}
} // namespace Indexing
