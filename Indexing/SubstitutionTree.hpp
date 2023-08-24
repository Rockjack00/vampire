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
 * @file SubstitutionTree.hpp
 * Defines class SubstitutionTree.
 *
 * @since 16/08/2008 flight Sydney-San Francisco
 */

#ifndef __SubstitutionTree__
#define __SubstitutionTree__

#include <utility>

#include "Forwards.hpp"

#include "Kernel/MismatchHandler.hpp"
#include "Lib/Exception.hpp"
#include "Lib/Reflection.hpp"
#include "Lib/VirtualIterator.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/Comparison.hpp"
#include "Lib/Int.hpp"
#include "Lib/Stack.hpp"
#include "Lib/List.hpp"
#include "Lib/SkipList.hpp"
#include "Lib/BinaryHeap.hpp"
#include "Lib/Backtrackable.hpp"
#include "Lib/ArrayMap.hpp"
#include "Lib/Array.hpp"
#include "Lib/BiMap.hpp"
#include "Lib/Recycled.hpp"

#include "Kernel/RobSubstitution.hpp"
#include "Kernel/Renaming.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/OperatorType.hpp"
#include "Lib/Option.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/ApplicativeHelper.hpp"
#include "Indexing/ResultSubstitution.hpp"

#include "Lib/Allocator.hpp"

#include "Index.hpp"

#if VDEBUG
#include <iostream>
#endif


  // TODO where should these go?
  static constexpr int QUERY_BANK=0;
  static constexpr int RESULT_BANK=1;
  static constexpr int NORM_RESULT_BANK=3;
  using namespace Lib;
  using namespace Kernel;

#define UARR_INTERMEDIATE_NODE_MAX_SIZE 4

#define REORDERING 1

  namespace Indexing {

    
  class SubstitutionTree;
  std::ostream& operator<<(std::ostream& out, SubstitutionTree const& self);
  std::ostream& operator<<(std::ostream& out, OutputMultiline<SubstitutionTree> const& self);

  template<class Key> struct SubtitutionTreeConfig;

  /** a counter that is compiled away in release mode */
  struct Cntr {
#if VDEBUG
    Cntr() : self(0) {}
    int self;
    operator int() const { return self; }
#endif 
  };

  /** a reference to a Cntr that increments the counter when it is created and decrements it when it goes out of scope
   * This can be used to count the number of instances when an object of this type is added as a member field to the class 
   * that should be counted */
  class InstanceCntr {
  public:
#if VDEBUG
    Cntr& _cntr;

    InstanceCntr& operator=(InstanceCntr&& other) 
    { std::swap(other._cntr, _cntr); return *this; }

    InstanceCntr(InstanceCntr&& other) 
      : _cntr(other._cntr)
    { other._cntr.self++; }

    InstanceCntr(Cntr& cntr) : _cntr(cntr) 
    { _cntr.self++; }
    ~InstanceCntr() 
    { _cntr.self--; }
#else // VDEBUG
    InstanceCntr(Cntr& parent) {}
#endif 
  };

  /**
   * Class of substitution trees. 
   *
   * We can either store typed terms, or literals in a subtitution tree.
   * Classically we'd think of inserting/removing only one term t into a substitution tree. 
   * This can be understood as inserting the substitution { S0 -> t } into the tree.
   *
   * In general we can insertt a substitution with more than just this one binding. 
   * This is what we do in order to store the sort of variables, and in order to insert all the arguments of a literal:
   * - For a term t of sort s we insert { S0 -> t; S1 -> s }
   * - For literals (~)P(t0..tn) we insert { S0 -> t0 .. Sn -> tn }.
   * (Note that we do not check the predicate or the polarity of literals here. This happens in LiteralSubstitutionTree)
   */
  class SubstitutionTree
  {

  public:
    static constexpr int QRS_QUERY_BANK = 0;
    static constexpr int QRS_RESULT_BANK = 1;
    CLASS_NAME(SubstitutionTree);
    USE_ALLOCATOR(SubstitutionTree);

    SubstitutionTree();
    SubstitutionTree(SubstitutionTree const&) = delete;
    SubstitutionTree& operator=(SubstitutionTree const& other) = delete;
    SubstitutionTree(SubstitutionTree&& other)
    : SubstitutionTree()
    {
      std::swap(_nextVar, other._nextVar);
      std::swap(_root, other._root);
    }

    virtual ~SubstitutionTree();

    friend std::ostream& operator<<(std::ostream& out, SubstitutionTree const& self);
    friend std::ostream& operator<<(std::ostream& out, OutputMultiline<SubstitutionTree> const& self);

    struct LeafData {
      LeafData() {}

      LeafData(Clause* cls, Literal* literal, TypedTermList term, TermList extraTerm)
      : clause(cls), literal(literal), term(term), sort(term.sort()), extraTerm(extraTerm) {}
      LeafData(Clause* cls, Literal* literal, TypedTermList term)
      : clause(cls), literal(literal), term(term), sort(term.sort()) { extraTerm.makeEmpty();}

      LeafData(Clause* cls, Literal* literal, TermList term, TermList extraTerm)
      : clause(cls), literal(literal), term(term), extraTerm(extraTerm) { sort.makeEmpty();}
      LeafData(Clause* cls, Literal* literal, TermList term)
      : clause(cls), literal(literal), term(term) { extraTerm.makeEmpty(); sort.makeEmpty(); }

      LeafData(Clause* cls, Literal* literal)
      : clause(cls), literal(literal) { term.makeEmpty(); sort.makeEmpty(), extraTerm.makeEmpty(); }
      inline
      bool operator==(const LeafData& o)
      { return clause==o.clause && literal==o.literal && term==o.term; }

      Clause* clause;
      Literal* literal;
      TermList term;
      TermList sort;
      // In some higher-order use cases, we want to store a different term 
      // in the leaf to the indexed term. extraTerm is used for this purpose.
      // In all other situations it is empty
      TermList extraTerm;

    };
    typedef VirtualIterator<LeafData*> LDIterator;

    template<class Unifier>
    struct QueryResult {
      LeafData const* data; 
      Unifier unif;

      QueryResult(LeafData const* ld, Unifier unif) : data(ld), unif(std::move(unif)) {}
    };
    template<class Unifier>
    static QueryResult<Unifier>  queryResult(LeafData const* ld, Unifier unif) 
    { return QueryResult<Unifier>(ld, std::move(unif)); }

    template<class I> using QueryResultIter = VirtualIterator<QueryResult<typename I::Unifier>>;
    // TODO get rid of me
    using RSQueryResult = QueryResult<ResultSubstitutionSP>;
    // TODO get rid of me
    using RSQueryResultIter = VirtualIterator<QueryResult<ResultSubstitutionSP>>;
    // TODO make const function
    template<class I, class TermOrLit, class... Args> 
    inline auto iterator(TermOrLit query, bool retrieveSubstitutions, bool reversed, Args... args)
    { return ifElseIter(_root == nullptr, 
        [&](){ return EmptyIter<ELEMENT_TYPE(I)>{}; },
        [&](){ return iterPointer(std::make_unique<I>(this, _root, query, retrieveSubstitutions, reversed, std::move(args)...)); }); }

    class LDComparator
    {
    public:
      inline
      static Comparison compare(const LeafData& ld1, const LeafData& ld2)
      {
        if(ld1.clause && ld2.clause && ld1.clause!=ld2.clause) {
          ASS_NEQ(ld1.clause->number(), ld2.clause->number());
          return (ld1.clause->number()<ld2.clause->number()) ? LESS : GREATER;
        }
        Comparison res;
        if(ld1.literal && ld2.literal && ld1.literal!=ld2.literal) {
          res = (ld1.literal->getId()<ld2.literal->getId())? LESS : GREATER;
        } else {
          ASS_EQ(ld1.clause,ld2.clause);
          ASS_EQ(ld1.literal,ld2.literal);

          if (ld1.term.isEmpty()) {
            ASS(ld2.term.isEmpty());
            res = EQUAL;
          } else {
            if (ld1.term.isVar()) {
              if (ld2.term.isVar()) {
                unsigned var1 = ld1.term.var();
                unsigned var2 = ld2.term.var();
                res=(var1<var2)? LESS : (var1>var2)? GREATER : EQUAL;
              }
              else{
                res = LESS;
              }
            } else {
              if (ld2.term.isVar()) {
                res = GREATER;
              } else {
                unsigned id1 = ld1.term.term()->getId();
                unsigned id2 = ld2.term.term()->getId();
                res=(id1<id2)? LESS : (id1>id2)? GREATER : EQUAL;
              }
            }
          }
        }
        return res;
      }
    };

    enum NodeAlgorithm
    {
      UNSORTED_LIST=1,
      SKIP_LIST=2,
      SET=3
    };

    class Node {

      /** term at this node */
      TermList _term;
#define CACHE_FUNCTOR 0
#if CACHE_FUNCTOR
      /** if _term is a Term* we cache the functor so we are faster than calling _term->term(); */
      unsigned _functor;
#endif // CACHE_FUNCTOR

    public:
      friend std::ostream& operator<<(std::ostream& out, OutputMultiline<Node> const& self) 
      { self.self.output(out, /* multiline = */ true, self.indent); return out; }
      friend std::ostream& operator<<(std::ostream& out, Node const& self) 
      { self.output(out, /* multiline = */ false, /* indent */ 0); return out; }
      inline Node() : _term(TermList::empty()) {}
      inline Node(TermList ts) : Node() { setTerm(ts); }
      virtual ~Node();
      /** True if a leaf node */
      virtual bool isLeaf() const = 0;
      virtual bool isEmpty() const = 0;
      /**
       * Return number of elements held in the node.
       *
       * Descendant classes should override this method.
       */
      virtual int size() const { NOT_IMPLEMENTED; }
      virtual NodeAlgorithm algorithm() const = 0;

      /**
       * Remove all referenced structures without destroying them.
       *
       * This is used when the implementation of a node is being changed.
       * The current node will be deleted, but we don't want to destroy
       * structures, that are taken over by the new node implementation.
       */
      virtual void makeEmpty() { _term.makeEmpty(); }
      static void split(Node** pnode, TermList* where, int var);

#if VDEBUG
      virtual void assertValid() const {};
#endif

      void setTerm(TermList t) { 
        _term = t; 
#if CACHE_FUNCTOR
        if (t.isTerm()) { _functor =  t.term()->functor(); } 
#endif
      }
      inline TermList::Top top() { 
#if CACHE_FUNCTOR
        auto out = _term.isTerm() ? TermList::Top::functor(_functor) : _term.top(); 
        ASS_EQ(_term.top(), out); 
        return out; 
#else // !CACHE_FUNCTOR
        return _term.top(); 
#endif
      }
      // if there is a write to this reference, a term with the same top() as the current one has to be written. otherwise use setTerm instead
      TermList& term() { return _term; }
      TermList const& term() const { return _term; }
      virtual void output(std::ostream& out, bool multiline, int indent) const = 0;
    };


    typedef VirtualIterator<Node**> NodeIterator;
    class IntermediateNode;
      
    class IntermediateNode
          : public Node
    {
    public:
      /** Build a new intermediate node which will serve as the root*/
      inline
      IntermediateNode(unsigned childVar) : childVar(childVar) {}

      /** Build a new intermediate node */
      inline
      IntermediateNode(TermList ts, unsigned childVar) : Node(ts), childVar(childVar) {}

      inline
      bool isLeaf() const final override { return false; };

      virtual NodeIterator allChildren() = 0;
      virtual NodeIterator variableChildren() = 0;
      /**
       * Return pointer to pointer to child node with top symbol
       * of @b t. This pointer to node can be changed.
       *
       * If canCreate is true and such child node does
       * not exist, pointer to null pointer is returned, and it's
       * assumed, that pointer to newly created node with given
       * top symbol will be put there.
       *
       * If canCreate is false, null pointer is returned in case
       * suitable child does not exist.
       */
      virtual Node** childByTop(TermList::Top t, bool canCreate) = 0;


      /**
       * Remove child which points to node with top symbol of @b t.
       * This node has to still exist in time of the call to remove method.
       */
      virtual void remove(TermList::Top t) = 0;
      /**
       * Remove all children of the node without destroying them.
       */
      virtual void removeAllChildren() = 0;

      void destroyChildren();

      void makeEmpty() final override
      {
        Node::makeEmpty();
        removeAllChildren();
      }

      virtual void mightExistAsTop(TermList::Top t) {
      }

      void loadChildren(NodeIterator children);

      const unsigned childVar;

      virtual void output(std::ostream& out, bool multiline, int indent) const override;
    }; // class SubstitutionTree::IntermediateNode

      struct NotTop
      {
          NotTop(unsigned t) : top(t) {};
          bool operator()(TermList t){
              return t.term()->functor()!=top;
          }
      private:
          unsigned top;
      };
      

    class Leaf
    : public Node
    {
    public:
      /** Build a new leaf which will serve as the root */
      inline
      Leaf()
      {}
      /** Build a new leaf */
      inline
      Leaf(TermList ts) : Node(ts) {}

      inline
      bool isLeaf() const final override { return true; };
      virtual LDIterator allChildren() = 0;
      virtual void insert(LeafData ld) = 0;
      virtual void remove(LeafData ld) = 0;
      void loadChildren(LDIterator children);
      virtual void output(std::ostream& out, bool multiline, int indent) const override;
    };

    //These classes and methods are defined in SubstitutionTree_Nodes.cpp
    class UListLeaf;
    class SListIntermediateNode;
    class SListLeaf;
    class SetLeaf;
    static Leaf* createLeaf();
    static Leaf* createLeaf(TermList ts);
    static void ensureLeafEfficiency(Leaf** l);
    static IntermediateNode* createIntermediateNode(unsigned childVar);
    static IntermediateNode* createIntermediateNode(TermList ts, unsigned childVar);
    static void ensureIntermediateNodeEfficiency(IntermediateNode** inode);

    struct IsPtrToVarNodeFn
    {
      bool operator()(Node** n)
      {
        return (*n)->term().isVar();
      }
    };

    class UArrIntermediateNode
    : public IntermediateNode
    {
    public:
      inline
      UArrIntermediateNode(unsigned childVar) : IntermediateNode(childVar), _size(0)
      {
        _nodes[0]=0;
      }
      inline
      UArrIntermediateNode(TermList ts, unsigned childVar) : IntermediateNode(ts, childVar), _size(0)
      {
        _nodes[0]=0;
      }

      ~UArrIntermediateNode()
      {
        if(!isEmpty()) {
          destroyChildren();
        }
      }

      void removeAllChildren()
      {
        _size=0;
        _nodes[0]=0;
      }

      NodeAlgorithm algorithm() const { return UNSORTED_LIST; }
      bool isEmpty() const { return !_size; }
      int size() const { return _size; }
      NodeIterator allChildren()
      { return pvi( PointerPtrIterator<Node*>(&_nodes[0],&_nodes[_size]) ); }

      NodeIterator variableChildren()
      {
        return pvi( getFilteredIterator(PointerPtrIterator<Node*>(&_nodes[0],&_nodes[_size]),
              IsPtrToVarNodeFn()) );
      }
      virtual Node** childByTop(TermList::Top t, bool canCreate);
      void remove(TermList::Top t);

#if VDEBUG
      virtual void assertValid() const
      {
        ASS_ALLOC_TYPE(this,"SubstitutionTree::UArrIntermediateNode");
      }
#endif

      CLASS_NAME(SubstitutionTree::UArrIntermediateNode);
      USE_ALLOCATOR(UArrIntermediateNode);

      int _size;
      Node* _nodes[UARR_INTERMEDIATE_NODE_MAX_SIZE+1];
    };

    class SListIntermediateNode
    : public IntermediateNode
    {
    public:
      SListIntermediateNode(unsigned childVar) : IntermediateNode(childVar) {}
      SListIntermediateNode(TermList ts, unsigned childVar) : IntermediateNode(ts, childVar) {}

      ~SListIntermediateNode()
      {
        if(!isEmpty()) {
          destroyChildren();
        }
      }

      void removeAllChildren()
      {
        while(!_nodes.isEmpty()) {
          _nodes.pop();
        }
      }

      static IntermediateNode* assimilate(IntermediateNode* orig);

      inline
      NodeAlgorithm algorithm() const { return SKIP_LIST; }
      inline
      bool isEmpty() const { return _nodes.isEmpty(); }
      int size() const { return _nodes.size(); }
#if VDEBUG
      virtual void assertValid() const
      {
        ASS_ALLOC_TYPE(this,"SubstitutionTree::SListIntermediateNode");
      }
#endif
      inline
      NodeIterator allChildren()
      {
        return pvi( NodeSkipList::PtrIterator(_nodes) );
      }
      inline
      NodeIterator variableChildren()
      {
        return pvi( getWhileLimitedIterator(
                      NodeSkipList::PtrIterator(_nodes),
                      IsPtrToVarNodeFn()) );
      }
      virtual Node** childByTop(TermList::Top t, bool canCreate)
      {
        Node** res;
        bool found=_nodes.getPosition(t,res,canCreate);
        if(!found) {
          if(canCreate) {
            mightExistAsTop(t);
            *res=0;
          } else {
            res=0;
          }
        }
        return res;
      }

      inline void remove(TermList::Top t)
      { _nodes.remove(t); }

      CLASS_NAME(SubstitutionTree::SListIntermediateNode);
      USE_ALLOCATOR(SListIntermediateNode);

      class NodePtrComparator
      {
      public:
        inline static Comparison compare(TermList::Top& l, Node* r)
        { 
          if(l.var()) {
            return r->term().isVar() ? Int::compare(*l.var(), r->term().var())
                                     : LESS;
          } else {
            return r->term().isVar() ? GREATER
                                     : Int::compare(*l.functor(), r->term().term()->functor());
          }
        }
      };
      typedef SkipList<Node*,NodePtrComparator> NodeSkipList;
      NodeSkipList _nodes;
    };


    class Binding {
    public:
      /** Number of the variable at this node */
      unsigned var;
      /** term at this node */
      TermList term;
      /** Create new binding */
      Binding(int v,TermList t) : var(v), term(t) {}

      struct Comparator
      {
        inline
        static Comparison compare(Binding& b1, Binding& b2)
        {
          return Int::compare(b2.var, b1.var);
        }
      };
    }; // class SubstitutionTree::Binding

    struct SpecVarComparator
    {
      inline
      static Comparison compare(unsigned v1, unsigned v2)
      { return Int::compare(v2, v1); }
      inline
      static unsigned max()
      { return 0u; }
    };

    typedef DHMap<unsigned,TermList,IdentityHash,DefaultHash> BindingMap;
    //Using BinaryHeap as a BindingQueue leads to about 30% faster insertion,
    //that when SkipList is used.
    typedef BinaryHeap<Binding,Binding::Comparator> BindingQueue;
    typedef BinaryHeap<unsigned,SpecVarComparator> SpecVarQueue;
    typedef Stack<unsigned> VarStack;

    void getBindingsArgBindings(Term* t, BindingMap& binding);

    Leaf* findLeaf(BindingMap& svBindings)
    { ASS(!_root || !_root->isLeaf() )
      return _root ? findLeaf(_root, svBindings) : nullptr; }

    Leaf* findLeaf(Node* root, BindingMap& svBindings);

    void setSort(TypedTermList const& term, LeafData& ld)
    {
      ASS_EQ(ld.term, term)
      ld.sort = term.sort();
    }

    void setSort(TermList const& term, LeafData& ld)
    {
      ASS_EQ(ld.term, term)
      if (term.isTerm()) {
        ld.sort = SortHelper::getResultSort(term.term());
      }
    }


    void setSort(Literal* literal, LeafData &ld)
    { 
      ASS_EQ(ld.literal, literal); 
      if (literal->isEquality()) {
        ld.sort = SortHelper::getEqualityArgumentSort(literal);
      }
    }


    template<class Key>
    void handle(Key const& key, LeafData ld, bool doInsert)
    {
      auto norm = Renaming::normalize(key);
      Recycled<BindingMap> bindings;
      setSort(key, ld);
      createBindings(norm, /* reversed */ false,
          [&](auto var, auto term) { 
            bindings->insert(var, term);
            _nextVar = std::max(_nextVar, (int)var + 1);
          });
      if (doInsert) insert(*bindings, ld);
      else          remove(*bindings, ld);
    }

  private:
    void insert(BindingMap& binding,LeafData ld);
    void remove(BindingMap& binding,LeafData ld);

    /** Number of the next variable */
    int _nextVar;
    Node* _root;
  public:

    class RenamingSubstitution 
    : public ResultSubstitution 
    {
    public:
      Recycled<Renaming> _query;
      Recycled<Renaming> _result;
      RenamingSubstitution(): _query(), _result() {}
      virtual ~RenamingSubstitution() override {}
      virtual TermList applyToQuery(TermList t) final override { return _query->apply(t); }
      virtual Literal* applyToQuery(Literal* l) final override { return _query->apply(l); }
      virtual TermList applyToResult(TermList t) final override { return _result->apply(t); }
      virtual Literal* applyToResult(Literal* l) final override { return _result->apply(l); }

      virtual TermList applyTo(TermList t, unsigned index) final override { ASSERTION_VIOLATION; }
      virtual Literal* applyTo(Literal* l, unsigned index) final override { NOT_IMPLEMENTED; }

      virtual size_t getQueryApplicationWeight(TermList t) final override { return t.weight(); }
      virtual size_t getQueryApplicationWeight(Literal* l) final override  { return l->weight(); }
      virtual size_t getResultApplicationWeight(TermList t) final override { return t.weight(); }
      virtual size_t getResultApplicationWeight(Literal* l) final override { return l->weight(); }

      void output(std::ostream& out) const final override
      { out << "{ _query: " << _query << ", _result: " << _result << " }"; }
    };

    template<class Query>
    bool generalizationExists(Query query)
    {
      return _root == nullptr 
        ? false
        : FastGeneralizationsIterator(this, _root, query, /* retrieveSubstitutions */ false, /* reversed */ false).hasNext();
    }

    template<class Query>
    RSQueryResultIter getVariants(Query query, bool retrieveSubstitutions)
    {
      auto renaming = retrieveSubstitutions ? std::make_unique<RenamingSubstitution>() : std::unique_ptr<RenamingSubstitution>(nullptr);
      ResultSubstitutionSP resultSubst = retrieveSubstitutions ? ResultSubstitutionSP(&*renaming) : ResultSubstitutionSP();

      Query normQuery;
      if (retrieveSubstitutions) {
        renaming->_query->normalizeVariables(query);
        normQuery = renaming->_query->apply(query);
      } else {
        normQuery = Renaming::normalize(query);
      }

      Recycled<BindingMap> svBindings;
      createBindings(normQuery, /* reversed */ false,
          [&](auto v, auto t) { {
            _nextVar = std::max<int>(_nextVar, v + 1); // TODO do we need this line?
            svBindings->insert(v, t);
          } });
      Leaf* leaf = findLeaf(*svBindings);
      if(leaf==0) {
        return RSQueryResultIter::getEmpty();
      } else {
        return pvi(iterTraits(leaf->allChildren())
          .map([retrieveSubstitutions, renaming = std::move(renaming), resultSubst](LeafData* ld) 
            {
              ResultSubstitutionSP subs;
              if (retrieveSubstitutions) {
                renaming->_result->reset();
                renaming->_result->normalizeVariables(SubtitutionTreeConfig<Query>::getKey(*ld));
                subs = resultSubst;
              }
              return queryResult(ld, subs);
            }));
      }
    }

    class LeafIterator
    {
    public:
      LeafIterator(LeafIterator&&) = default;
      LeafIterator& operator=(LeafIterator&&) = default;
      DECL_ELEMENT_TYPE(Leaf*);
      LeafIterator(SubstitutionTree* st);
      bool hasNext();
      Leaf* next();
    private:
      void skipToNextLeaf();
      Node* _curr;
      Stack<NodeIterator> _nodeIterators;
    };



     /**
     * Class that supports matching operations required by
     * retrieval of generalizations in substitution trees.
     */
    class GenMatcher
    {
      static unsigned weight(Literal* l) { return l->weight(); }
      static unsigned weight(TermList t) { return  t.weight(); }
    public:
      GenMatcher(GenMatcher&&) = default;
      GenMatcher& operator=(GenMatcher&&) = default;

      /**
       * @b nextSpecVar Number higher than any special variable present in the tree.
       * 	It's used to determine size of the array that stores bindings of
       * 	special variables.
       */
      template<class TermOrLit>
      GenMatcher(TermOrLit query, unsigned nextSpecVar)
        : _maxVar(weight(query) - 1)
      {
        if(_specVars->size()<nextSpecVar) {
          //_specVars can get really big, but it was introduced instead of hash table
          //during optimizations, as it raised performance by abour 5%.
          _specVars->ensure(std::max(static_cast<unsigned>(_specVars->size()*2), nextSpecVar));
        }
        _bindings->ensure(weight(query));
      }



      CLASS_NAME(SubstitutionTree::GenMatcher);
      USE_ALLOCATOR(GenMatcher);

      /**
       * Bind special variable @b var to @b term. This method
       * should be called only before any calls to @b matchNext()
       * and @b backtrack().
       */
      void bindSpecialVar(unsigned var, TermList term)
      {
        (*_specVars)[var]=term;
      }
      /**
       * Return term bound to special variable @b specVar
       */
      TermList getSpecVarBinding(unsigned specVar)
      { return (*_specVars)[specVar]; }

      bool matchNext(unsigned specVar, TermList nodeTerm, bool separate=true);
      bool matchNextAux(TermList queryTerm, TermList nodeTerm, bool separate=true);
      void backtrack();
      bool tryBacktrack();

      ResultSubstitutionSP getSubstitution(Renaming* resultNormalizer);

      int getBSCnt()
      {
        int res=0;
        VarStack::Iterator vsit(*_boundVars);
        while(vsit.hasNext()) {
      if(vsit.next()==BACKTRACK_SEPARATOR) {
        res++;
      }
        }
        return res;
      }

    protected:
      static const unsigned BACKTRACK_SEPARATOR=0xFFFFFFFF;

      struct Binder;
      struct Applicator;
      class Substitution;

      Recycled<VarStack> _boundVars;
      Recycled<DArray<TermList>, NoReset> _specVars;
      //                         ^^^^^^^ all values that will be read, will be overridden anyways so we can safe time by not resetting.

      /**
       * Inheritors must assign the maximal possible number of an ordinary
       * variable that can be bound during the retrievall process.
       */
      unsigned _maxVar;

      /**
       * Inheritors must ensure that the size of this map will
       * be at least @b _maxVar+1
       */
      Recycled<ArrayMap<TermList>> _bindings;
    };

    // TODO document
    template<class BindingFunction>
    void createBindings(TypedTermList term, bool reversed, BindingFunction bindSpecialVar)
    {
      bindSpecialVar(0, term);
      bindSpecialVar(1, term.sort());
    }

    template<class BindingFunction>
    void createBindings(Literal* lit, bool reversed, BindingFunction bindSpecialVar)
    {
      if (lit->isEquality()) {

        if (reversed) {
          bindSpecialVar(1,*lit->nthArgument(0));
          bindSpecialVar(0,*lit->nthArgument(1));
        } else {
          bindSpecialVar(0,*lit->nthArgument(0));
          bindSpecialVar(1,*lit->nthArgument(1));
        }

        bindSpecialVar(2, SortHelper::getEqualityArgumentSort(lit));

      } else if(reversed) {
        ASS(lit->commutative());
        ASS_EQ(lit->arity(),2);

        bindSpecialVar(1,*lit->nthArgument(0));
        bindSpecialVar(0,*lit->nthArgument(1));

      } else {

        TermList* args=lit->args();
        int nextVar = 0;
        while (! args->isEmpty()) {
          unsigned var = nextVar++;
          bindSpecialVar(var,*args);
          args = args->next();
        }
      }
    }

    /**
     * Iterator, that yields generalizations of given term/literal.
     */
    class FastGeneralizationsIterator
    {
    public:

      FastGeneralizationsIterator(FastGeneralizationsIterator&&) = default;
      FastGeneralizationsIterator& operator=(FastGeneralizationsIterator&&) = default;
      DECL_ELEMENT_TYPE(RSQueryResult);
      using Unifier = ResultSubstitutionSP;
      /**
       * If @b reversed If true, parameters of supplied binary literal are
       * 	reversed. (useful for retrieval commutative terms)
       */
      template<class TermOrLit>
      FastGeneralizationsIterator(SubstitutionTree* parent, Node* root, TermOrLit query, bool retrieveSubstitution, bool reversed)
        : _literalRetrieval(std::is_same<TermOrLit, Literal*>::value)
        , _retrieveSubstitution(retrieveSubstitution)
        , _inLeaf(root->isLeaf())
        , _subst(query,parent->_nextVar)
        , _ldIterator(_inLeaf ? static_cast<Leaf*>(root)->allChildren() : LDIterator::getEmpty())
        , _resultNormalizer()
        , _root(root)
        , _alternatives()
        , _specVarNumbers()
        , _nodeTypes()
        , _iterCntr(parent->_iterCnt)
      {
        ASS(root);

        parent->createBindings(query, reversed,
            [&](unsigned var, TermList t) { _subst.bindSpecialVar(var, t); });
      }

      RSQueryResult next();
      bool hasNext();
    protected:

      bool findNextLeaf();
      bool enterNode(Node*& node);

      /** We are retrieving generalizations of a literal */
      bool _literalRetrieval;
      /** We should include substitutions in the results */
      bool _retrieveSubstitution;
      /** The iterator is currently in a leaf
       *
       * This is false in the beginning when it is in the root */
      bool _inLeaf;

      GenMatcher _subst;

      LDIterator _ldIterator;

      Recycled<Renaming> _resultNormalizer;

      Node* _root;

      Recycled<Stack<void*>> _alternatives;
      Recycled<Stack<unsigned>> _specVarNumbers;
      Recycled<Stack<NodeAlgorithm>> _nodeTypes;
      InstanceCntr _iterCntr;
    };


    /**
     * Class that supports matching operations required by
     * retrieval of generalizations in substitution trees.
     */
    class InstMatcher 
    {
    public:

      CLASS_NAME(SubstitutionTree::InstMatcher);
      USE_ALLOCATOR(InstMatcher);

      struct TermSpec
      {
        TermSpec() : q(false) {
        #if VDEBUG
          t.makeEmpty();
        #endif
        }
        TermSpec(bool q, TermList t)
        : q(q), t(t)
        {
          //query does not contain special vars
          ASS(!q || !t.isTerm() || t.term()->shared());
          ASS(!q || !t.isSpecialVar());
        }

        vstring toString()
        { return (q ? "q|" : "n|")+t.toString(); }

        /**
         * Return true if the @b t field can be use as a binding for a query
         * term variable in the retrieved substitution
         */
        bool isFinal()
        {
          //the fact that a term is shared means it does not contain any special variables
          return q
        ? (t.isTerm() && t.term()->ground())
        : (t.isOrdinaryVar() || (t.isTerm() && t.term()->shared()) );
        }

        bool q;
        TermList t;
      };

      /**
       * Bind special variable @b var to @b term
       *
       * This method should be called only before any calls to @b matchNext()
       * and @b backtrack().
       */
      void bindSpecialVar(unsigned var, TermList term)
      {
        ASS_EQ(getBSCnt(), 0);

        ALWAYS(_bindings->insert(TermList(var,true),TermSpec(true,term)));
      }

      bool isSpecVarBound(unsigned specVar)
      {
        return _bindings->find(TermList(specVar,true));
      }

      /** Return term bound to special variable @b specVar */
      TermSpec getSpecVarBinding(unsigned specVar)
      {
        TermSpec res=_bindings->get(TermList(specVar,true));

        return res;
      }

      bool findSpecVarBinding(unsigned specVar, TermSpec& res)
      {
        return _bindings->find(TermList(specVar,true), res);
      }

      bool matchNext(unsigned specVar, TermList nodeTerm, bool separate=true);
      bool matchNextAux(TermList queryTerm, TermList nodeTerm, bool separate=true);

      void backtrack();
      bool tryBacktrack();
      ResultSubstitutionSP getSubstitution(Renaming* resultDenormalizer);

      int getBSCnt()
      {
        int res=0;
        TermStack::Iterator vsit(*_boundVars);
        while(vsit.hasNext()) {
          if(vsit.next().isEmpty()) {
      res++;
          }
        }
        return res;
      }

      void onLeafEntered()
      {
        _derefBindings->reset();
      }

    private:

      class Substitution;

      TermList derefQueryBinding(unsigned var);

      bool isBound(TermList var)
      {
        ASS(var.isVar());

        return _bindings->find(var);
      }
      void bind(TermList var, TermSpec trm)
      {
        ASS(!var.isOrdinaryVar() || !trm.q); //we do not bind ordinary vars to query terms

        ALWAYS(_bindings->insert(var, trm));
        _boundVars->push(var);
      }

      TermSpec deref(TermList var);

      typedef DHMap<TermList, TermSpec> BindingMap;
      typedef Stack<TermList> TermStack;

      /** Stacks of bindings made on each backtrack level. Backtrack
       * levels are separated by empty terms. */
      Recycled<TermStack> _boundVars;

      Recycled<BindingMap> _bindings;

      /**
       * A cache for bindings of variables to result terms
       *
       * The map is reset whenever we enter a new leaf
       */
      Recycled<DHMap<TermList,TermList>> _derefBindings;

      struct DerefTask
      {
        DerefTask(TermList var) : var(var) { trm.t.makeEmpty(); }
        DerefTask(TermList var, TermSpec trm) : var(var), trm(trm) {}
        TermList var;
        TermSpec trm;
        bool buildDerefTerm() { return trm.t.isNonEmpty(); };
      };

      struct DerefApplicator
      {
        DerefApplicator(InstMatcher* im, bool query) : query(query), im(im) {}
        TermList apply(unsigned var)
        {
          if(query) {
            return im->_derefBindings->get(TermList(var, false));
          }
          else {
      return TermList(var, false);
          }
        }
        TermList applyToSpecVar(unsigned specVar)
        {
          ASS(!query);

          return im->_derefBindings->get(TermList(specVar, true));
        }
      private:
        bool query;
        InstMatcher* im;
      };
    };

    /**
     * Iterator, that yields generalizations of given term/literal.
     */
    class FastInstancesIterator
    {
    public:
      FastInstancesIterator(FastInstancesIterator&&) = default;
      FastInstancesIterator& operator=(FastInstancesIterator&&) = default;
      DECL_ELEMENT_TYPE(RSQueryResult);
      using Unifier = ResultSubstitutionSP;

      /**
       * If @b reversed If true, parameters of supplied binary literal are
       * 	reversed. (useful for retrieval commutative terms)
       */
      template<class TermOrLit>
      FastInstancesIterator(SubstitutionTree* parent, Node* root, TermOrLit query, bool retrieveSubstitution, bool reversed)
        : _literalRetrieval(std::is_same<TermOrLit, Literal*>::value)
        , _retrieveSubstitution(retrieveSubstitution)
        , _inLeaf(root->isLeaf())
        , _ldIterator(_inLeaf ? static_cast<Leaf*>(root)->allChildren() : LDIterator::getEmpty())
        , _root(root)
        , _alternatives()
        , _specVarNumbers()
        , _nodeTypes()
        , _iterCntr(parent->_iterCnt)
      {
        ASS(root);

        parent->createBindings(query, reversed,
            [&](unsigned var, TermList t) { _subst.bindSpecialVar(var, t); });

        if (_inLeaf) {
          _subst.onLeafEntered(); //we reset the bindings cache
        }
      }

      bool hasNext();
      RSQueryResult next();
    protected:
      bool findNextLeaf();

      bool enterNode(Node*& node);

    private:

      bool _literalRetrieval;
      bool _retrieveSubstitution;
      bool _inLeaf;
      LDIterator _ldIterator;

      InstMatcher _subst;

      Renaming _resultDenormalizer;
      Node* _root;

      Recycled<Stack<void*>> _alternatives;
      Recycled<Stack<unsigned>> _specVarNumbers;
      Recycled<Stack<NodeAlgorithm>> _nodeTypes;
      InstanceCntr _iterCntr;
    };

    /** * A generic iterator over a substitution tree that can be used to retrieve elements stored in the tree that match a certain retrieval condition R.
     * The most simple retrieval condition would be `R(x) <=> x unifies with query`.
     * Similarly we could have retrieval conditions that, find instances, generalizations, unification with 
     * abstraction etc. 
     * 
     * The retrieval condition is computed by objects of the type RetrievalAlgorithm. All commonly used ones can be 
     * found in Indexing::RetrievalAlgorithms, which also documents the interface of these objects. 
     *
     * Notes:
     * - currently instantiation and generalization don't use this generic approach, but the optimized iterators
     *   `Fast*Iterator`, which we hopefully can refactor away in the future without any loss in performance.
     * - We do not use subtyping but parametric polymorphism for them, as subtyping polymorphsim would require us to 
     *   have the same element type for all of them, which is not what we want.
     */
    template<class RetrievalAlgorithm>
    class Iterator final
    {
    public:
      Iterator(Iterator&&) = default;
      Iterator& operator=(Iterator&&) = default;
      using Unifier = typename RetrievalAlgorithm::Unifier;
      DECL_ELEMENT_TYPE(QueryResult<Unifier>);

      template<class TermOrLit, class...AlgoArgs>
      Iterator(SubstitutionTree* parent, Node* root, TermOrLit query, bool retrieveSubstitution, bool reversed, AlgoArgs... args)
        : _algo(std::move(args)...)
        , _svStack()
        , _literalRetrieval(std::is_same<TermOrLit, Literal*>::value)
        , _retrieveSubstitution(retrieveSubstitution)
        , _inLeaf(false)
        , _ldIterator(LDIterator::getEmpty())
        , _nodeIterators()
        , _bdStack()
        , _clientBDRecording(false)
        , _iterCntr(parent->_iterCnt)
      {
#define DEBUG_QUERY(...) // DBG(__VA_ARGS__)
        if(!root) {
          return;
        }

        parent->createBindings(query, reversed, 
            [&](unsigned var, TermList t) { _algo.bindQuerySpecialVar(var, t); });
        DEBUG_QUERY("query: ", _abstractingUnifier.subs())


        BacktrackData bd;
        enter(root, bd);
        bd.drop();
      }


      ~Iterator()
      {
        if(_clientBDRecording) {
          _algo.bdDone();
          _clientBDRecording=false;
          _clientBacktrackData.backtrack();
        }
        if (_bdStack.alive()) 
          while(_bdStack->isNonEmpty()) {
            _bdStack->pop().backtrack();
          }
      }

      bool hasNext()
      {
        if(_clientBDRecording) {
          _algo.bdDone();
          _clientBDRecording=false;
          _clientBacktrackData.backtrack();
        }

        while(!_ldIterator.hasNext() && findNextLeaf()) {}
        return _ldIterator.hasNext();
      }

      QueryResult<Unifier> next()
      {
        while(!_ldIterator.hasNext() && findNextLeaf()) {}
        ASS(_ldIterator.hasNext());

        ASS(!_clientBDRecording);

        auto ld = _ldIterator.next();
        // TODO resolve this kinda messy bit
        if (_retrieveSubstitution) {
            Renaming normalizer;
            if(_literalRetrieval) {
              normalizer.normalizeVariables(ld->literal);
            } else {
              normalizer.normalizeVariables(ld->term);
              if (ld->sort.isNonEmpty()) {
                normalizer.normalizeVariables(ld->sort);
              }
            }

            ASS(_clientBacktrackData.isEmpty());
            _algo.bdRecord(_clientBacktrackData);
            _clientBDRecording=true;

            _algo.denormalize(normalizer);
        }

        return queryResult(ld, _algo.unifier());
      }

    private:

      bool findNextLeaf()
      {
        if(_nodeIterators->isEmpty()) {
          //There are no node iterators in the stack, so there's nowhere
          //to look for the next leaf.
          //This shouldn't hapen during the regular retrieval process, but it
          //can happen when there are no literals inserted for a predicate,
          //or when predicates with zero arity are encountered.
          ASS(_bdStack->isEmpty());
          return false;
        }

        if(_inLeaf) {
          ASS(!_clientBDRecording);
          //Leave the current leaf
          _bdStack->pop().backtrack();
          _inLeaf=false;
        }

        ASS(!_clientBDRecording);
        ASS(_bdStack->length()+1==_nodeIterators->length());

        do {
          while(!_nodeIterators->top().hasNext() && !_bdStack->isEmpty()) {
            //backtrack undos everything that enter(...) method has done,
            //so it also pops one item out of the nodeIterators stack
            _bdStack->pop().backtrack();
            _svStack->pop();
          }
          if(!_nodeIterators->top().hasNext()) {
            return false;
          }
          Node* n=*_nodeIterators->top().next();

          BacktrackData bd;
          bool success=enter(n,bd);
          if(!success) {
            bd.backtrack();
            continue;
          } else {
            _bdStack->push(bd);
          }
        } while(!_inLeaf);
        return true;
      }

      bool enter(Node* n, BacktrackData& bd)
      {
        bool success=true;
        bool recording=false;
        if(!n->term().isEmpty()) {
          //n is proper node, not a root

          recording=true;
          _algo.bdRecord(bd);
          success = _algo.associate(_svStack->top(),n->term());
          
        }
        if(success) {
          if(n->isLeaf()) {
            _ldIterator=static_cast<Leaf*>(n)->allChildren();
            _inLeaf=true;
          } else {
            IntermediateNode* inode=static_cast<IntermediateNode*>(n);
            _svStack->push(inode->childVar);
            
            backtrackablePush(*_nodeIterators, _algo.selectPotentiallyUnifiableChildren(inode), bd);
          }
        }
        if(recording) {
          _algo.bdDone();
        }
        return success;
      }


      RetrievalAlgorithm _algo;
      Recycled<VarStack> _svStack;
      bool _literalRetrieval;
      bool _retrieveSubstitution;
      bool _inLeaf;
      LDIterator _ldIterator;
      Recycled<Stack<NodeIterator>> _nodeIterators;
      Recycled<Stack<BacktrackData>> _bdStack;
      bool _clientBDRecording;
      BacktrackData _clientBacktrackData;
      InstanceCntr _iterCntr;
    };


  public:
    bool isEmpty() const { return _root == nullptr || _root->isEmpty(); }
    friend std::ostream& operator<<(std::ostream& out, SubstitutionTree const& self);

    Cntr _iterCnt;
  }; // class SubstiutionTree

  /* This namespace defines classes to be used as type parameter for SubstitutionTree::Iterator. 
   * 
   * They implement different retrieval operations, which determine which elements from the tree shall 
   * be returned.  The most classic retrieval operations are retrieving all key terms that unify with 
   * a query term, or that a generalization/instance of a query term.
   *
   * The only non-classic one implemented currently is UnificaitonWithAbstraction, but one could also think 
   * of implementing finding variable variants, equal terms (modulo some theory), etc using the same interface.
   *
   * The interface all RetrievalAlgorithms must conform to is documented at the example of class RobUnification.
   * Retrieval from a SubstitutionTree is preformed incrementally. We start first by inserting some query terms. 
   * This is done by Algorithm::bindQuerySpecialVar.
   *
   */ 
  namespace RetrievalAlgorithms {

      class RobUnification { 
        Recycled<RobSubstitution> _subs;
      public:
        RobUnification() : _subs() {}
        /** a witness that the returned term matches the retrieval condition 
         * (could be a substitution, variable renaming, etc.) 
         */
        using Unifier = ResultSubstitutionSP;  
        
        /** before starting to retrieve terms from the tree we insert some query terms, which we are going to 
         *  match the terms in the tree with. This is done using this function. */
        void bindQuerySpecialVar(unsigned var, TermList term)
        { _subs->bindSpecialVar(var, term, QUERY_BANK); }

        /** we intrementally traverse the tree, and at every code we call this retrieval algorithm to check 
         * whether it is okay to bind a new special variable to some term in the tree.
         * This function returns true if the retrieval condition (like in this case unifyability) can still 
         * be achieved, or false if not. Depending on that the iterator will backtrack or continue to traverse 
         * deeper into the tree.
         *
         * On insert into a substitution tree the inserted terms are first normalized (the names of the variables)
         * Therefore the namespace of the variables passed here is different from the ones of the actually inserted 
         * terms. 
         * Matching them up again is done by the function denormalize.
         */
        bool associate(unsigned specialVar, TermList node)
        { return _subs->unify(TermList(specialVar, /* special */ true), QUERY_BANK, node, NORM_RESULT_BANK); }


        /** @see associate */
        void denormalize(Renaming& norm)
        { _subs->denormalize(norm, NORM_RESULT_BANK,RESULT_BANK); }

        /** whenever we arrieve at a leave we return the currrent witness for the current leave term to unify
         * with the query term. The unifier is queried using this function.  */
        Unifier unifier() { return ResultSubstitution::fromSubstitution(&*_subs, QUERY_BANK, RESULT_BANK); }

        /** same as in @Backtrackable */
        void bdRecord(BacktrackData& bd) { _subs->bdRecord(bd); }

        /** same as in @Backtrackable */
        void bdDone() { _subs->bdDone(); }

        /** 
         * Returns an iterator over all child nodes of n that should be attempted for unification.
         * This is only an optimization. One could allways return n->allChildren(), but in the case 
         * of syntactic unificaiton we can use SkipList (i.e. n->childByTop(...)) in order to skip 
         * unnecessary unification attempts.
         */
        SubstitutionTree::NodeIterator selectPotentiallyUnifiableChildren(SubstitutionTree::IntermediateNode* n)
        { return selectPotentiallyUnifiableChildren(n, *_subs); }

        static SubstitutionTree::NodeIterator selectPotentiallyUnifiableChildren(SubstitutionTree::IntermediateNode* n, RobSubstitution& subs)
        {
          unsigned specVar=n->childVar;
          auto top = subs.getSpecialVarTop(specVar);
          if(top.var()) {
            return n->allChildren();
          } else {
            SubstitutionTree::Node** match = n->childByTop(top, /* canCreate */ false);
            if(match) {
              return pvi(concatIters(
                           getSingletonIterator(match),
                           n->variableChildren()));
            } else {
              return n->variableChildren();
            }
          }
        }

      };

      class UnificationWithAbstraction { 
        AbstractingUnifier _unif;
      public:
        UnificationWithAbstraction(MismatchHandler handler) : _unif(AbstractingUnifier::empty(handler)) {}
        using Unifier = AbstractingUnifier*;

        bool associate(unsigned specialVar, TermList node)
        { return _unif.unify(TermList(specialVar, /* special */ true), QUERY_BANK, node, NORM_RESULT_BANK); }

        Unifier unifier()
        { return &_unif; }

        void bindQuerySpecialVar(unsigned var, TermList term)
        { _unif.subs().bindSpecialVar(var, term, QUERY_BANK); }

        void bdRecord(BacktrackData& bd)
        { _unif.subs().bdRecord(bd); }

        void bdDone()
        { _unif.subs().bdDone(); }

        void denormalize(Renaming& norm)
        { _unif.subs().denormalize(norm, NORM_RESULT_BANK,RESULT_BANK); }

        // TODO document
        static SubstitutionTree::NodeIterator selectPotentiallyUnifiableChildren(SubstitutionTree::IntermediateNode* n, AbstractingUnifier& unif)
        {
          if (unif.usesUwa()) {
            unsigned specVar = n->childVar;
            auto top = unif.subs().getSpecialVarTop(specVar);

            if(top.var()) {
              return n->allChildren();
            } else {
              auto syms = unif.unifiableSymbols(top.functor());
              if (syms) {
                return pvi(concatIters(
                      arrayIter(std::move(*syms))
                        .map   ([=](auto f) { return n->childByTop(top, /* canCreate */ false); })
                        .filter([ ](auto n) { return n != nullptr; }),
                      n->variableChildren()
                      ));
              } else {
                return n->allChildren();
              }
            }
          } else {
            return RobUnification::selectPotentiallyUnifiableChildren(n, unif.subs());
          }
        }

        SubstitutionTree::NodeIterator selectPotentiallyUnifiableChildren(SubstitutionTree::IntermediateNode* n)
        { return selectPotentiallyUnifiableChildren(n, _unif); }
      };

      class UnificationWithAbstractionWithPostprocessing 
      { 
        AbstractingUnifier _unif;
        Option<bool> _fpRes;
      public:
        class NotFinalized { 
          AbstractingUnifier* _unif; 
          Option<bool>* _result;
        public:
          explicit NotFinalized(AbstractingUnifier* unif, Option<bool>* result) 
            : _unif(unif)
            , _result(result) 
          { }

          Option<AbstractingUnifier*> fixedPointIteration() 
          {
            if (_result->isNone()) {
              *_result = some(bool(_unif->fixedPointIteration()));
              if (_unif->isRecording()) {
                _unif->bdGet().addClosure([res = _result]() { *res = {}; });
              }
            }
            return someIf(**_result, [&](){ return _unif;  });
          }

          friend std::ostream& operator<<(std::ostream& out, NotFinalized const& self)
          { return out << *self._unif << " (fixedPointIteration: " << *self._result << " )"; }
        };

        using Unifier = NotFinalized;

        UnificationWithAbstractionWithPostprocessing(MismatchHandler handler) 
          : _unif(AbstractingUnifier::empty(handler)) 
          , _fpRes()
        {}

        bool associate(unsigned specialVar, TermList node)
        { return _unif.unify(TermList(specialVar, /* special */ true), QUERY_BANK, node, NORM_RESULT_BANK); }

        Unifier unifier()
        { return NotFinalized(&_unif, &_fpRes); }

        void bindQuerySpecialVar(unsigned var, TermList term)
        { _unif.subs().bindSpecialVar(var, term, QUERY_BANK); }

        void bdRecord(BacktrackData& bd)
        { _unif.subs().bdRecord(bd); }

        void bdDone()
        { _unif.subs().bdDone(); }

        void denormalize(Renaming& norm)
        { _unif.subs().denormalize(norm, NORM_RESULT_BANK,RESULT_BANK); }


        SubstitutionTree::NodeIterator selectPotentiallyUnifiableChildren(SubstitutionTree::IntermediateNode* n)
        { return UnificationWithAbstraction::selectPotentiallyUnifiableChildren(n, _unif); }
      };
    };


  template<> 
  struct SubtitutionTreeConfig<Literal*> 
  {
    static Literal* const& getKey(SubstitutionTree::LeafData const& ld)
    { return ld.literal;  }
  };


  template<> 
  struct SubtitutionTreeConfig<TermList> 
  {
    static TermList const& getKey(SubstitutionTree::LeafData const& ld)
    { return ld.term;  }
  };

} // namespace Indexing

#endif
