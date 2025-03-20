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
 * @file OrderingComparator.cpp
 * Implements class OrderingComparator.
 */

#include "Lib/Stack.hpp"
#include "Lib/Environment.hpp"

#include "KBO.hpp"
#include "SubstHelper.hpp"

#include "OrderingComparator.hpp"

using namespace std;

namespace Kernel {

// OrderingComparator

static Ordering::Result kGtPtr = Ordering::GREATER;
static Ordering::Result kEqPtr = Ordering::EQUAL;

OrderingComparator* OrderingComparator::createForSingleComparison(const Ordering& ord, TermList lhs, TermList rhs)
{
  static Map<std::pair<TermList,TermList>,OrderingComparator*> cache; // TODO this leaks now

  OrderingComparator** ptr;
  if (cache.getValuePtr({ lhs, rhs }, ptr, nullptr)) {
    *ptr = ord.createComparator().release();
    (*ptr)->_threeValued = true;
    (*ptr)->_source = Branch(lhs, rhs);
    (*ptr)->_source.node()->gtBranch = Branch(&kGtPtr, (*ptr)->_sink);
    (*ptr)->_source.node()->eqBranch = Branch(&kEqPtr, (*ptr)->_sink);
    (*ptr)->_source.node()->ngeBranch = (*ptr)->_sink;
  }
  return *ptr;
}

OrderingComparator::OrderingComparator(const Ordering& ord)
: _ord(ord), _source(nullptr, Branch()), _sink(_source), _curr(&_source), _prev(nullptr), _appl(nullptr)
{
  _sink.node()->ready = true;
}

OrderingComparator::~OrderingComparator() = default;

void OrderingComparator::init(const SubstApplicator* appl)
{
  _curr = &_source;
  _prev = nullptr;
  _appl = appl;
}

void* OrderingComparator::next()
{
  ASS(_appl);
  ASS(_curr);

  for (;;) {
    processCurrentNode();

    auto node = _curr->node();
    ASS(node->ready);

    if (node->tag == Node::T_DATA) {
      if (!node->data) {
        return nullptr;
      }
      _prev = _curr;
      _curr = &node->alternative;
      return node->data;
    }

    Ordering::Result comp =
      (node->tag == Node::T_TERM)
      ? _ord.compareUnidirectional(
          AppliedTerm(node->lhs, _appl, true),
          AppliedTerm(node->rhs, _appl, true))
      : positivityCheck();

    _prev = _curr;
    _curr = &node->getBranch(comp);
  }
  return nullptr;
}

bool OrderingComparator::check(const SubstApplicator* appl, const PrevPoly& prevPoly, const TermPartialOrdering* tpo)
{
  // ASS_NEQ(tpo, TermPartialOrdering::getEmpty(_ord));
  init(appl);

  static Stack<Branch*> btStack;
  btStack.reset();

  for (;;) {
    processCurrentNode();

    auto node = _curr->node();
    ASS(node->ready);

    if (node->tag == Node::T_DATA) {
      if (!node->data) {
        if (btStack.isEmpty()) {
          return false;
        }
        // backtrack
        _prev = btStack.pop();
        _curr = &_prev->node()->getBranch(Ordering::INCOMPARABLE);
        continue;
      }
      return node->data;
    }

    Ordering::Result res =
      (node->tag == Node::T_TERM)
      ? _ord.compareUnidirectional(
          AppliedTerm(node->lhs, _appl, true),
          AppliedTerm(node->rhs, _appl, true))
      : positivityCheck();

    if (res == Ordering::INCOMPARABLE) {
      if (node->tag == Node::T_TERM) {
        TermNodeIterator termIt(_ord, appl, node->lhs, node->rhs, prevPoly, tpo);
        auto val = termIt.get();
        if (val != Ordering::INCOMPARABLE) {
          btStack.push(_curr);
          res = val;
        }
      } else {
        ASS_EQ(node->tag, Node::T_POLY);
        const auto& kbo = static_cast<const KBO&>(_ord);
        auto weight = node->poly->constant;
        ZIArray<int> varDiffs;
        for (const auto& [var, coeff] : node->poly->varCoeffPairs) {
          AppliedTerm tt(TermList::var(var), appl, true);

          VariableIterator vit(tt.term);
          while (vit.hasNext()) {
            auto v = vit.next();
            varDiffs[v.var()] += coeff;
          }
          int64_t w = kbo.computeWeight(tt);
          weight += coeff*w;
        }
        Stack<VarCoeffPair> nonzeros;
        for (unsigned i = 0; i < varDiffs.size(); i++) {
          if (varDiffs[i]==0) {
            continue;
          }
          nonzeros.push({ i, varDiffs[i] });
        }

        PolyNodeIterator polyIt(Polynomial::get(weight, nonzeros), prevPoly, tpo);
        auto val = polyIt.get();
        if (val != Ordering::INCOMPARABLE) {
          btStack.push(_curr);
          res = val;
        }
      }
    }
    _prev = _curr;
    _curr = &node->getBranch(res);
  }
  return false;
}

void OrderingComparator::insert(const Stack<TermOrderingConstraint>& comps, void* data)
{
  ASS(data);
  static Ordering::Result ordVals[] = { Ordering::GREATER, Ordering::EQUAL, Ordering::INCOMPARABLE };
  // we mutate current fail node and add a new one
  auto curr = &_sink;
  Branch newFail(nullptr, Branch());
  newFail.node()->ready = true;

  curr->node()->~Node();
  curr->node()->ready = false;

  if (comps.isNonEmpty()) {
    curr->node()->tag = Node::T_TERM;
    curr->node()->lhs = comps[0].lhs;
    curr->node()->rhs = comps[0].rhs;
    for (unsigned i = 0; i < 3; i++) {
      if (ordVals[i] != comps[0].rel) {
        curr->node()->getBranchUnsafe(ordVals[i]) = newFail;
      }
    }
    curr = &curr->node()->getBranchUnsafe(comps[0].rel);
    for (unsigned i = 1; i < comps.size(); i++) {
      auto [lhs,rhs,rel] = comps[i];
      *curr = OrderingComparator::Branch(lhs, rhs);
      for (unsigned i = 0; i < 3; i++) {
        if (ordVals[i] != rel) {
          curr->node()->getBranchUnsafe(ordVals[i]) = newFail;
        }
      }
      curr = &curr->node()->getBranchUnsafe(rel);
    }
    *curr = Branch(data, newFail);
  } else {
    curr->node()->tag = Node::T_DATA;
    curr->node()->data = data;
    curr->node()->alternative = newFail;
  }

  _sink = newFail;
}

Ordering::Result OrderingComparator::positivityCheck() const
{
  auto node = _curr->node();
  ASS(node->ready);
  ASS_EQ(node->tag, Node::T_POLY);

  const auto& kbo = static_cast<const KBO&>(_ord);
  auto weight = node->poly->constant;
  ZIArray<int> varDiffs;
  for (const auto& [var, coeff] : node->poly->varCoeffPairs) {
    AppliedTerm tt(TermList::var(var), _appl, true);

    VariableIterator vit(tt.term);
    while (vit.hasNext()) {
      auto v = vit.next();
      varDiffs[v.var()] += coeff;
      // since the counts are sorted in descending order,
      // this can only mean we will fail
      if (varDiffs[v.var()]<0) {
        return Ordering::INCOMPARABLE;
      }
    }
    int64_t w = kbo.computeWeight(tt);
    weight += coeff*w;
    // due to descending order of counts,
    // this also means failure
    if (coeff<0 && weight<0) {
      return Ordering::INCOMPARABLE;
    }
  }

  if (weight > 0) {
    return Ordering::GREATER;
  } else if (weight == 0) {
    return Ordering::EQUAL;
  }
  return Ordering::INCOMPARABLE;
}

void OrderingComparator::processCurrentNode()
{
  ASS(_curr->node());
  while (!_curr->node()->ready)
  {
    auto node = _curr->node();

    if (node->tag == Node::T_DATA) {
      ASS(node->data); // no fail nodes here
      // if refcnt > 1 we have to copy the node,
      // otherwise we can mutate the original
      if (node->refcnt > 1) {
        *_curr = Branch(node->data, node->alternative);
      }
      auto trace = getCurrentTrace();
      // this only happens when we try to enumerate all
      // branches of the diagram, including invalid ones
      if (!trace) {
        *_curr = _sink;
      } else {
        _curr->node()->trace = trace;
        _curr->node()->prevPoly = getPrevPoly();
        _curr->node()->ready = true;
      }
      return;
    }
    if (node->tag == Node::T_POLY) {
      processPolyNode();
      continue;
    }

    // Use compare here to filter out as many
    // precomputable comparisons as possible.
    auto comp = _ord.compare(node->lhs,node->rhs);
    if (comp != Ordering::INCOMPARABLE) {
      if (comp == Ordering::LESS) {
        *_curr = node->ngeBranch;
      } else if (comp == Ordering::GREATER) {
        *_curr = node->gtBranch;
      } else {
        *_curr = node->eqBranch;
      }
      continue;
    }
    // If we have a variable, we cannot expand further.
    if (node->lhs.isVar() || node->rhs.isVar()) {
      processVarNode();
      continue;
    }
    processTermNode();
  }
}

void OrderingComparator::processVarNode()
{
  auto node = _curr->node();
  auto trace = getCurrentTrace();

  // this only happens when we try to enumerate all
  // branches of the diagram, including invalid ones
  if (!trace) {
    *_curr = _sink;
    return;
  }

  Ordering::Result val;
  if (trace->get(node->lhs, node->rhs, val)) {
    if (val == Ordering::GREATER) {
      *_curr = node->gtBranch;
    } else if (val == Ordering::EQUAL) {
      *_curr = node->eqBranch;
    } else {
      *_curr = node->ngeBranch;
    }
    return;
  }
  // if refcnt > 1 we have to copy the node,
  // otherwise we can mutate the original
  if (node->refcnt > 1) {
    *_curr = Branch(node->lhs, node->rhs);
    _curr->node()->eqBranch = node->eqBranch;
    _curr->node()->gtBranch = node->gtBranch;
    _curr->node()->ngeBranch = node->ngeBranch;
  }
  _curr->node()->ready = true;
  _curr->node()->trace = trace;
  _curr->node()->prevPoly = getPrevPoly();
}

void OrderingComparator::processPolyNode()
{
  auto node = _curr->node();
  auto trace = getCurrentTrace();
  auto prevPoly = getPrevPoly();

  // this only happens when we try to enumerate all
  // branches of the diagram, including invalid ones
  if (!trace) {
    *_curr = _sink;
    return;
  }

  unsigned pos = 0;
  unsigned neg = 0;

  auto vcs = node->poly->varCoeffPairs;

  for (unsigned i = 0; i < vcs.size();) {
    auto& [var, coeff] = vcs[i];
    for (unsigned j = i+1; j < vcs.size();) {
      auto& [var2, coeff2] = vcs[j];
      Ordering::Result res;
      if (trace->get(TermList::var(var), TermList::var(var2), res) && res == Ordering::EQUAL) {
        coeff += coeff2;
        swap(vcs[j],vcs.top());
        vcs.pop();
        continue;
      }
      j++;
    }
    if (coeff == 0) {
      swap(vcs[i],vcs.top());
      vcs.pop();
      continue;
    } else if (coeff > 0) {
      pos++;
    } else {
      neg++;
    }
    i++;
  }

  auto constant = node->poly->constant;
  if (constant == 0 && pos == 0 && neg == 0) {
    *_curr = node->eqBranch;
    return;
  }
  if (constant >= 0 && neg == 0) {
    *_curr = node->gtBranch;
    return;
  }
  if (constant <= 0 && pos == 0) {
    *_curr = node->ngeBranch;
    return;
  }
  auto poly = Polynomial::get(constant, vcs);

  // check if we have seen this polynomial
  // on the path leading here
  auto polyIt = prevPoly;
  while (polyIt.first) {
    ASS_EQ(polyIt.first->tag, Node::T_POLY);
    switch (polyIt.second) {
      case Ordering::GREATER: {
        if (polyIt.first->poly == poly) {
          *_curr = node->gtBranch;
          return;
        }
        break;
      }
      case Ordering::EQUAL: {
        if (polyIt.first->poly == poly) {
          *_curr = node->eqBranch;
          return;
        }
        break;
      }
      case Ordering::INCOMPARABLE: {
        if (polyIt.first->poly == poly) {
          *_curr = node->ngeBranch;
          return;
        }
        break;
      }
      default:
        break;
    }
    polyIt = polyIt.first->prevPoly;
  }

  // if refcnt > 1 we have to copy the node,
  // otherwise we can mutate the original
  if (node->refcnt > 1) {
    *_curr = Branch(poly);
    _curr->node()->eqBranch = node->eqBranch;
    _curr->node()->gtBranch = node->gtBranch;
    _curr->node()->ngeBranch = node->ngeBranch;
  } else {
    _curr->node()->poly = poly;
  }
  _curr->node()->trace = trace;
  _curr->node()->prevPoly = prevPoly;
  _curr->node()->ready = true;
}

void OrderingComparator::processTermNode()
{
  ASS(_curr->node() && !_curr->node()->ready);
  _curr->node()->ready = true;
  _curr->node()->trace = Trace::getEmpty(_ord);
}

const OrderingComparator::Trace* OrderingComparator::getCurrentTrace()
{
  ASS(!_curr->node()->ready);

  if (!_prev) {
    return Trace::getEmpty(_ord);
  }

  ASS(_prev->node()->ready);
  ASS(_prev->node()->trace);

  switch (_prev->node()->tag) {
    case Node::T_TERM: {
      auto lhs = _prev->node()->lhs;
      auto rhs = _prev->node()->rhs;
      Ordering::Result res;
      if (_curr == &_prev->node()->eqBranch) {
        res = Ordering::EQUAL;
      } else if (_curr == &_prev->node()->gtBranch) {
        res = Ordering::GREATER;
      } else {
        ASS_EQ(_curr, &_prev->node()->ngeBranch);
        res = Ordering::INCOMPARABLE;
      }
      return Trace::set(_prev->node()->trace, { lhs, rhs, res });
    }
    case Node::T_DATA:
    case Node::T_POLY: {
      return _prev->node()->trace;
    }
  }
  ASSERTION_VIOLATION;
}

std::pair<OrderingComparator::Node*, Ordering::Result> OrderingComparator::getPrevPoly()
{
  auto res = make_pair((Node*)nullptr, Ordering::INCOMPARABLE);
  if (_prev) {
    // take value from previous node by default
    res = _prev->node()->prevPoly;

    // override value if the previous is a poly node 
    if (_prev->node()->tag == Node::T_POLY) {
      res.first = _prev->node();
      if (_curr == &_prev->node()->gtBranch) {
        res.second = Ordering::GREATER;
      } else if (_curr == &_prev->node()->eqBranch) {
        res.second = Ordering::EQUAL;
      } else {
        ASS_EQ(_curr, &_prev->node()->ngeBranch);
        res.second = Ordering::INCOMPARABLE;
      }
    }
  }
  return res;
}

// Branch

OrderingComparator::Branch::Branch(void* data, Branch alt)
{
  setNode(new Node(data, alt));
}

OrderingComparator::Branch::Branch(TermList lhs, TermList rhs)
{
  setNode(new Node(lhs, rhs));
}

OrderingComparator::Branch::Branch(const Polynomial* p)
{
  setNode(new Node(p));
}

OrderingComparator::Branch::~Branch()
{
  setNode(nullptr);
}

OrderingComparator::Branch::Branch(const Branch& other)
{
  setNode(other._node);
}

OrderingComparator::Node* OrderingComparator::Branch::node() const
{
  return _node;
}

void OrderingComparator::Branch::setNode(Node* node)
{
  if (node) {
    node->incRefCnt();
  }
  if (_node) {
    _node->decRefCnt();
  }
  _node = node;
}

OrderingComparator::Branch::Branch(Branch&& other)
{
  swap(_node,other._node);
}

OrderingComparator::Branch& OrderingComparator::Branch::operator=(Branch other)
{
  swap(_node,other._node);
  return *this;
}

// Node

OrderingComparator::Node::Node(void* data, Branch alternative)
  : tag(T_DATA), data(data), alternative(alternative) {}

OrderingComparator::Node::Node(TermList lhs, TermList rhs)
  : tag(T_TERM), lhs(lhs), rhs(rhs) {}

OrderingComparator::Node::Node(const Polynomial* p)
  : tag(T_POLY), poly(p) {}

OrderingComparator::Node::~Node()
{
  if (tag==T_DATA) {
    alternative.~Branch();
  }
  ready = false;
}

void OrderingComparator::Node::incRefCnt()
{
  refcnt++;
}

void OrderingComparator::Node::decRefCnt()
{
  ASS(refcnt>=0);
  refcnt--;
  if (refcnt==0) {
    delete this;
  }
}

OrderingComparator::Branch& OrderingComparator::Node::getBranch(Ordering::Result r)
{
  ASS(ready && trace);
  switch (r) {
    case Ordering::EQUAL: return eqBranch;
    case Ordering::GREATER: return gtBranch;
    case Ordering::INCOMPARABLE:
    case Ordering::LESS:
      // no distinction between less and incomparable
      return ngeBranch;
  }
  ASSERTION_VIOLATION;
}

OrderingComparator::Branch& OrderingComparator::Node::getBranchUnsafe(Ordering::Result r)
{
  switch (r) {
    case Ordering::EQUAL: return eqBranch;
    case Ordering::GREATER: return gtBranch;
    case Ordering::INCOMPARABLE:
    case Ordering::LESS:
      // no distinction between less and incomparable
      return ngeBranch;
  }
  ASSERTION_VIOLATION_REP(r);
}

// Polynomial

const OrderingComparator::Polynomial* OrderingComparator::Polynomial::get(int constant, const Stack<VarCoeffPair>& varCoeffPairs)
{
  static Set<Polynomial*, DerefPtrHash<DefaultHash>> polys;

  sort(varCoeffPairs.begin(),varCoeffPairs.end(),[](const auto& vc1, const auto& vc2) {
    auto vc1pos = vc1.second>0;
    auto vc2pos = vc2.second>0;
    return (vc1pos && !vc2pos) || (vc1pos == vc2pos && vc1.first<vc2.first);
  });

  Polynomial poly{ constant, varCoeffPairs };
  // The code below depends on the fact that the first argument
  // is only called when poly is not used anymore, otherwise the
  // move would give undefined behaviour.
  bool unused;
  return polys.rawFindOrInsert(
    [&](){ return new Polynomial(std::move(poly)); },
    poly.defaultHash(),
    [&](Polynomial* p) { return *p == poly; },
    unused);
}

// Iterators

OrderingComparator::TermNodeIterator::TermNodeIterator(
  const Ordering& ord, const SubstApplicator* appl,
    TermList lhs, TermList rhs,
    const PrevPoly& prevPoly, const TermPartialOrdering* tpo)
  : _ord(ord),
    _comp(OrderingComparator::createForSingleComparison(
      ord,AppliedTerm(lhs,appl,true).apply(),AppliedTerm(rhs,appl,true).apply())),
    _prevPoly(prevPoly), _tpo(tpo)
{
}

Ordering::Result OrderingComparator::TermNodeIterator::get()
{
  _comp->_prev = nullptr;
  _comp->_curr = &_comp->_source;

  for (;;) {
    _comp->processCurrentNode();

    auto node = _comp->_curr->node();
    ASS(node->ready);

    if (node->tag == Node::T_DATA) {
      if (node->data) {
        return *static_cast<Result*>(node->data);
      }
      return Ordering::INCOMPARABLE;
    }

    Ordering::Result comp = Ordering::INCOMPARABLE;
    if (node->tag == Node::T_TERM) {

      Ordering::Result val;
      if (_tpo->get(node->lhs, node->rhs, val)) {
        comp = val;
      }

    } else {
      ASS_EQ(node->tag, Node::T_POLY);

      PolyNodeIterator polyIt(node->poly, _prevPoly, _tpo);
      auto val = polyIt.get();
      if (val != Ordering::INCOMPARABLE) {
        comp = val;
      }
    }
    _comp->_prev = _comp->_curr;
    _comp->_curr = &node->getBranch(comp);
  }
  ASSERTION_VIOLATION;
}

OrderingComparator::PolyNodeIterator::PolyNodeIterator(
  const Polynomial* poly, const PrevPoly& prevPoly, const TermPartialOrdering* tpo)
  : _poly(poly), _prevPoly(prevPoly), _tpo(tpo)
{
}

Ordering::Result OrderingComparator::PolyNodeIterator::get()
{
  // check if we have seen this polynomial
  // on the path leading here
  auto polyIt = _prevPoly;
  while (polyIt.first) {
    ASS_EQ(polyIt.first->tag, Node::T_POLY);
    switch (polyIt.second) {
      case Ordering::GREATER: {
        if (polyIt.first->poly == _poly) {
          return Ordering::GREATER;
        }
        break;
      }
      case Ordering::EQUAL: {
        if (polyIt.first->poly == _poly) {
          return Ordering::EQUAL;
        }
        break;
      }
      default:
        break;
    }
    polyIt = polyIt.first->prevPoly;
  }

  unsigned pos = 0;
  unsigned neg = 0;

  auto vcs = _poly->varCoeffPairs;

  for (unsigned i = 0; i < vcs.size();) {
    auto& [var, coeff] = vcs[i];
    for (unsigned j = i+1; j < vcs.size();) {
      auto& [var2, coeff2] = vcs[j];
      Ordering::Result res;
      if (_tpo->get(TermList::var(var), TermList::var(var2), res) && res == Ordering::EQUAL) {
        coeff += coeff2;
        swap(vcs[j],vcs.top());
        vcs.pop();
        continue;
      }
      j++;
    }
    if (coeff == 0) {
      swap(vcs[i],vcs.top());
      vcs.pop();
      continue;
    } else if (coeff > 0) {
      pos++;
    } else {
      neg++;
    }
    i++;
  }

  auto constant = _poly->constant;
  if (constant == 0 && pos == 0 && neg == 0) {
    return Ordering::EQUAL;
  }
  if (constant >= 0 && neg == 0) {
    return Ordering::GREATER;
  }
  return Ordering::INCOMPARABLE;
}

OrderingComparator::GreaterIterator::GreaterIterator(const Ordering& ord, TermList lhs, TermList rhs)
  : _comp(*OrderingComparator::createForSingleComparison(ord, lhs, rhs))
{
  _path.push(&_comp._source);
}

bool OrderingComparator::GreaterIterator::hasNext()
{
  while (_path.isNonEmpty()) {
    auto& curr = _path.top();

    _comp._prev = (_path.size()==1) ? nullptr : _path[_path.size()-2];
    _comp._curr = curr;
    _comp.processCurrentNode();

    auto node = _comp._curr->node();
    ASS(node->ready);

    if (node->tag != Node::T_DATA) {
      // do down
      _path.push(&node->getBranch(Ordering::GREATER));
      continue;
    }

    // push next first
    while (_path.isNonEmpty()) {
      _path.pop();
      if (_path.isEmpty()) {
        break;
      }

      auto prev = _path.top()->node();
      ASS(prev->tag == Node::T_POLY || prev->tag == Node::T_TERM);
      if (_comp._curr == &prev->getBranch(Ordering::GREATER)) {
        _path.push(&prev->getBranch(Ordering::EQUAL));
        break;
      } else if (_comp._curr == &prev->getBranch(Ordering::EQUAL)) {
        _path.push(&prev->getBranch(Ordering::LESS));
        break;
      }
    }
    ASS(_comp._threeValued);
    if (node->data && *static_cast<Result*>(node->data) == Ordering::GREATER) {
      ASS(node->trace);
      _res.first = node->prevPoly;
      _res.second = node->trace;
      return true;
    }
  }
  return false;
}

// Printing

std::ostream& operator<<(std::ostream& out, const OrderingComparator::Node::Tag& t)
{
  using Tag = OrderingComparator::Node::Tag;
  switch (t) {
    case Tag::T_DATA: return out << "d";
    case Tag::T_TERM: return out << "t";
    case Tag::T_POLY: return out << "p";
  }
  ASSERTION_VIOLATION;
}

std::ostream& operator<<(std::ostream& out, const OrderingComparator::Node& node)
{
  using Tag = OrderingComparator::Node::Tag;
  out << (Tag)node.tag << (node.ready?" ":"? ");
  switch (node.tag) {
    case Tag::T_DATA: return out << node.data;
    case Tag::T_POLY: return out << *node.poly;
    case Tag::T_TERM: return out << node.lhs << " " << node.rhs;
  }
  ASSERTION_VIOLATION;
}

std::ostream& operator<<(std::ostream& out, const OrderingComparator::Polynomial& poly)
{
  bool first = true;
  for (const auto& [var, coeff] : poly.varCoeffPairs) {
    if (coeff > 0) {
      out << (first ? "" : " + ");
    } else {
      out << (first ? "- " : " - ");
    }
    first = false;
    auto abscoeff = std::abs(coeff);
    if (abscoeff!=1) {
      out << abscoeff << " * ";
    }
    out << TermList::var(var);
  }
  if (poly.constant) {
    out << (poly.constant<0 ? " - " : " + ");
    out << std::abs(poly.constant);
  }
  return out;
}

std::ostream& operator<<(std::ostream& str, const OrderingComparator& comp)
{
  Stack<std::pair<const OrderingComparator::Branch*, unsigned>> todo;
  todo.push(std::make_pair(&comp._source,0));
  // Note: using this set we get a more compact representation
  DHSet<OrderingComparator::Node*> seen;

  while (todo.isNonEmpty()) {
    auto kv = todo.pop();
    for (unsigned i = 0; i < kv.second; i++) {
      str << ((i+1 == kv.second) ? "  |--" : "  |  ");
    }
    str << *kv.first->node() << std::endl;
    if (seen.insert(kv.first->node())) {
      if (kv.first->node()->tag==OrderingComparator::Node::T_DATA) {
        if (kv.first->node()->data) {
          todo.push(std::make_pair(&kv.first->node()->alternative,kv.second+1));
        }
      } else {
        todo.push(std::make_pair(&kv.first->node()->ngeBranch,kv.second+1));
        todo.push(std::make_pair(&kv.first->node()->eqBranch,kv.second+1));
        todo.push(std::make_pair(&kv.first->node()->gtBranch,kv.second+1));
      }
    }
  }
  return str;
}

}
