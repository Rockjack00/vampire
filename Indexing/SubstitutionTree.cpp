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
 * @file SubstitutionTree.cpp
 * Implements class SubstitutionTree.
 *
 * @since 16/08/2008 flight Sydney-San Francisco
 */

namespace Indexing {

/**
 * Initialise the substitution tree.
 * @since 16/08/2008 flight Sydney-San Francisco
 */

template<class LeafData_>
SubstitutionTree<LeafData_>::SubstitutionTree(int nodes)
  : _nextVar(0), _nodes(nodes)
{
  CALL("SubstitutionTree::SubstitutionTree");

#if VDEBUG
  _iteratorCnt=0;
#endif
} // SubstitutionTree::SubstitutionTree

/**
 * Destroy the substitution tree.
 * @warning does not destroy nodes yet
 * @since 16/08/2008 flight Sydney-San Francisco
 */
template<class LeafData_>
SubstitutionTree<LeafData_>::~SubstitutionTree()
{
  CALL("SubstitutionTree::~SubstitutionTree");
  ASS_EQ(_iteratorCnt,0);

  for (unsigned i = 0; i<_nodes.size(); i++) {
    if(_nodes[i]!=0) {
      delete _nodes[i];
    }
  }
} // SubstitutionTree::~SubstitutionTree

/**
 * Store initial bindings of term @b t into @b bq.
 *
 * This method is used for insertions and deletions.
 */
template<class LeafData_>
void SubstitutionTree<LeafData_>::getBindings(Term* t, BindingMap& svBindings)
{
  TermList* args=t->args();

  int nextVar = 0;
  while (! args->isEmpty()) {
    if (_nextVar <= nextVar) {
      ASS_EQ(_iteratorCnt,0);
      _nextVar = nextVar+1;
    }
    svBindings.insert(nextVar++, *args);
    args = args->next();
  }
}

struct UnresolvedSplitRecord
{
  UnresolvedSplitRecord() {}
  UnresolvedSplitRecord(unsigned var, TermList original)
  : var(var), original(original) {}

  unsigned var;
  TermList original;
};

template<class LeafData_>
struct BindingComparator
{
  static Comparison compare(const UnresolvedSplitRecord& r1, const UnresolvedSplitRecord& r2)
  {
    bool r1HasSpecVars=r1.original.isTerm() && !r1.original.term()->shared();
    bool r2HasSpecVars=r2.original.isTerm() && !r2.original.term()->shared();
    if( r1HasSpecVars && !r2HasSpecVars ) {
      return GREATER;
    }
    if( r2HasSpecVars && !r1HasSpecVars ) {
      return LESS;
    }
    return Int::compare(r2.var,r1.var);
  }
  static Comparison compare(const typename SubstitutionTree<LeafData_>::Binding& b1, const typename SubstitutionTree<LeafData_>::Binding& b2)
  {
#if REORDERING
    return Int::compare(b2.var,b1.var);
#else
    return Int::compare(b1.var,b2.var);
#endif
  }
};


/**
 * Insert an entry to the substitution tree.
 *
 * @b pnode is pointer to root of tree corresponding to
 * top symbol of the term/literal being inserted, and
 * @b bh contains its arguments.
 */
template<class LeafData_>
void SubstitutionTree<LeafData_>::insert(Node** pnode,BindingMap& svBindings,LeafData ld)
{
  CALL("SubstitutionTree::insert/3");
  ASS_EQ(_iteratorCnt,0);

  if(*pnode == 0) {
    if(svBindings.isEmpty()) {
      *pnode=createLeaf();
    } else {
      *pnode=createIntermediateNode(svBindings.getOneKey());
    }
  }
  if(svBindings.isEmpty()) {
    ASS((*pnode)->isLeaf());
    ensureLeafEfficiency(reinterpret_cast<Leaf**>(pnode));
    static_cast<Leaf*>(*pnode)->insert(ld);
    return;
  }

  typedef BinaryHeap<UnresolvedSplitRecord, BindingComparator<LeafData_>> SplitRecordHeap;
  static SplitRecordHeap unresolvedSplits;
  unresolvedSplits.reset();

  ASS((*pnode));
  ASS(!(*pnode)->isLeaf());

start:

#if REORDERING
  ASS(!(*pnode)->isLeaf() || !unresolvedSplits.isEmpty());
  bool canPostponeSplits=false;
  if((*pnode)->isLeaf() || (*pnode)->algorithm()!=UNSORTED_LIST) {
    canPostponeSplits=false;
  } else {
    UArrIntermediateNode* inode = static_cast<UArrIntermediateNode*>(*pnode);
    canPostponeSplits = inode->size()==1;
    if(canPostponeSplits) {
      unsigned boundVar=inode->childVar;
      Node* child=inode->_nodes[0];
      bool removeProblematicNode=false;
      if(svBindings.find(boundVar)) {
	TermList term=svBindings.get(boundVar);
	bool wouldDescendIntoChild = inode->childByTop(term,false)!=0;
	ASS_EQ(wouldDescendIntoChild, TermList::sameTop(term, child->term));
	if(!wouldDescendIntoChild) {
	  //if we'd have to perform all postponed splitting due to
	  //node with a single child, we rather remove that node
	  //from the tree and deal with the binding, it represented,
	  //later.
	  removeProblematicNode=true;
	}
      } else if(!child->term.isTerm() || child->term.term()->shared()) {
	//We can remove nodes binding to special variables undefined in our branch
	//of the tree, as long as we're sure, that during split resolving we put these
	//binding nodes below nodes that define spec. variables they bind.
	removeProblematicNode=true;
      } else {
	canPostponeSplits = false;
      }
      if(removeProblematicNode) {
	unresolvedSplits.insert(UnresolvedSplitRecord(inode->childVar, child->term));
	child->term=inode->term;
	*pnode=child;
	inode->makeEmpty();
	delete inode;
	goto start;
      }
    }
  }
  canPostponeSplits|=unresolvedSplits.isEmpty();
  if(!canPostponeSplits) {

    while(!unresolvedSplits.isEmpty()) {
      UnresolvedSplitRecord urr=unresolvedSplits.pop();

      Node* node=*pnode;
      IntermediateNode* newNode = createIntermediateNode(node->term, urr.var);
      node->term=urr.original;

      *pnode=newNode;

      Node** nodePosition=newNode->childByTop(node->term, true);
      ASS(!*nodePosition);
      *nodePosition=node;
    }
  }
#endif
  ASS(!(*pnode)->isLeaf());

  IntermediateNode* inode = static_cast<IntermediateNode*>(*pnode);
  ASS(inode);

  unsigned boundVar=inode->childVar;
  TermList term=svBindings.get(boundVar);
  svBindings.remove(boundVar);

  //Into pparent we store the node, we might be inserting into.
  //So in the case we do insert, we might check whether this node
  //needs expansion.
  Node** pparent=pnode;
  pnode=inode->childByTop(term,true);

  if (*pnode == 0) {
    BindingMap::Iterator svit(svBindings);
    BinaryHeap<Binding, BindingComparator<LeafData_>> remainingBindings;
    while (svit.hasNext()) {
      unsigned var;
      TermList term;
      svit.next(var, term);
      remainingBindings.insert(Binding(var, term));
    }
    while (!remainingBindings.isEmpty()) {
      Binding b=remainingBindings.pop();
      IntermediateNode* inode = createIntermediateNode(term, b.var);
      term=b.term;

      *pnode = inode;
      pnode = inode->childByTop(term,true);
    }
    Leaf* lnode=createLeaf(term);
    *pnode=lnode;
    lnode->insert(ld);

    ensureIntermediateNodeEfficiency(reinterpret_cast<IntermediateNode**>(pparent));
    return;
  }


  TermList* tt = &term;
  TermList* ss = &(*pnode)->term;

  ASS(TermList::sameTop(*ss, *tt));


  // ss is the term in node, tt is the term to be inserted
  // ss and tt have the same top symbols but are not equal
  // create the common subterm of ss,tt and an alternative node
  Stack<TermList*> subterms(64);
  for (;;) {
    if (*tt!=*ss && TermList::sameTop(*ss,*tt)) {
      // ss and tt have the same tops and are different, so must be non-variables
      ASS(! ss->isVar());
      ASS(! tt->isVar());

      Term* s = ss->term();
      Term* t = tt->term();

      ASS(s->arity() > 0);
      ASS(s->functor() == t->functor());

      if (s->shared()) {
        // create a shallow copy of s
        s = Term::cloneNonShared(s);
        ss->setTerm(s);
      }

      ss = s->args();
      tt = t->args();
      if (ss->next()->isEmpty()) {
        continue;
      }
      subterms.push(ss->next());
      subterms.push(tt->next());
    } else {
      if (! TermList::sameTop(*ss,*tt)) {
        unsigned x;
        if(!ss->isSpecialVar()) {
          x = _nextVar++;
        #if REORDERING
          unresolvedSplits.insert(UnresolvedSplitRecord(x,*ss));
          ss->makeSpecialVar(x);
        #else
          Node::split(pnode,ss,x);
        #endif
        } else {
          x=ss->var();
        }
        svBindings.set(x,*tt);
      }

      if (subterms.isEmpty()) {
        break;
      }
      tt = subterms.pop();
      ss = subterms.pop();
      if (! ss->next()->isEmpty()) {
        subterms.push(ss->next());
        subterms.push(tt->next());
      }
    }
  }

  if (svBindings.isEmpty()) {
    ASS((*pnode)->isLeaf());
    ensureLeafEfficiency(reinterpret_cast<Leaf**>(pnode));
    Leaf* leaf = static_cast<Leaf*>(*pnode);
    leaf->insert(ld);
    return;
  }

  goto start;
} // // SubstitutionTree::insert

/*
 * Remove an entry from the substitution tree.
 *
 * @b pnode is pointer to root of tree corresponding to
 * top symbol of the term/literal being removed, and
 * @b bh contains its arguments.
 *
 * If the removal results in a chain of nodes containing
 * no terms/literals, all those nodes are removed as well.
 */
template<class LeafData_>
void SubstitutionTree<LeafData_>::remove(Node** pnode,BindingMap& svBindings,LeafData ld)
{
  CALL("SubstitutionTree::remove-2");
  ASS_EQ(_iteratorCnt,0);

  ASS(*pnode);

  static Stack<Node**> history(1000);
  history.reset();

  while (! (*pnode)->isLeaf()) {
    history.push(pnode);

    IntermediateNode* inode=static_cast<IntermediateNode*>(*pnode);

    unsigned boundVar=inode->childVar;
    TermList t = svBindings.get(boundVar);

    pnode=inode->childByTop(t,false);
    ASS(pnode);


    TermList* s = &(*pnode)->term;
    ASS(TermList::sameTop(*s,t));

    if(*s==t) {
      continue;
    }

    ASS(! s->isVar());
    TermList* ss = s->term()->args();
    ASS(!ss->isEmpty());

    // computing the disagreement set of the two terms
    Stack<TermList*> subterms(120);

    subterms.push(ss);
    subterms.push(t.term()->args());
    while (! subterms.isEmpty()) {
      TermList* tt = subterms.pop();
      ss = subterms.pop();
      if (tt->next()->isEmpty()) {
	ASS(ss->next()->isEmpty());
      }
      else {
	subterms.push(ss->next());
	subterms.push(tt->next());
      }
      if (*ss==*tt) {
	continue;
      }
      if (ss->isVar()) {
	ASS(ss->isSpecialVar());
	svBindings.set(ss->var(),*tt);
	continue;
      }
      ASS(! tt->isVar());
      ASS(ss->term()->functor() == tt->term()->functor());
      ss = ss->term()->args();
      if (! ss->isEmpty()) {
	ASS(! tt->term()->args()->isEmpty());
	subterms.push(ss);
	subterms.push(tt->term()->args());
      }
    }
  }

  ASS ((*pnode)->isLeaf());


  Leaf* lnode = static_cast<Leaf*>(*pnode);
  lnode->remove(ld);
  ensureLeafEfficiency(reinterpret_cast<Leaf**>(pnode));

  while( (*pnode)->isEmpty() ) {
    TermList term=(*pnode)->term;
    if(history.isEmpty()) {
      delete *pnode;
      *pnode=0;
      return;
    } else {
      Node* node=*pnode;
      IntermediateNode* parent=static_cast<IntermediateNode*>(*history.top());
      parent->remove(term);
      delete node;
      pnode=history.pop();
      ensureIntermediateNodeEfficiency(reinterpret_cast<IntermediateNode**>(pnode));
    }
  }
} // SubstitutionTree::remove

/**
 * Return a pointer to the leaf that contains term specified by @b svBindings.
 * If no such leaf exists, return 0.
 */
template<class LeafData_>
typename SubstitutionTree<LeafData_>::Leaf* SubstitutionTree<LeafData_>::findLeaf(Node* root, BindingMap& svBindings)
{
  CALL("SubstitutionTree::findLeaf");
  ASS(root);

  Node* node=root;

  while (! node->isLeaf()) {
    IntermediateNode* inode=static_cast<IntermediateNode*>(node);

    unsigned boundVar=inode->childVar;
    TermList t = svBindings.get(boundVar);

    Node** child=inode->childByTop(t,false);
    if(!child) {
      return 0;
    }
    node=*child;


    TermList s = node->term;
    ASS(TermList::sameTop(s,t));

    if(s==t) {
      continue;
    }

    ASS(! s.isVar());
    TermList* ss = s.term()->args();
    ASS(!ss->isEmpty());

    // computing the disagreement set of the two terms
    Stack<TermList*> subterms(120);

    subterms.push(ss);
    subterms.push(t.term()->args());
    while (! subterms.isEmpty()) {
      TermList* tt = subterms.pop();
      ss = subterms.pop();
      if (tt->next()->isEmpty()) {
	ASS(ss->next()->isEmpty());
      }
      else {
	subterms.push(ss->next());
	subterms.push(tt->next());
      }
      if (*ss==*tt) {
	continue;
      }
      if (ss->isSpecialVar()) {
	svBindings.set(ss->var(),*tt);
	continue;
      }
      if(ss->isVar() || tt->isVar() || ss->term()->functor()!=tt->term()->functor()) {
	return 0;
      }
      ss = ss->term()->args();
      if (! ss->isEmpty()) {
	ASS(! tt->term()->args()->isEmpty());
	subterms.push(ss);
	subterms.push(tt->term()->args());
      }
    }
  }
  ASS(node->isLeaf());
  return static_cast<Leaf*>(node);
}


#if VDEBUG

inline vstring getIndentStr(int n)
{
  vstring res;
  for(int indCnt=0;indCnt<n;indCnt++) {
	res+="  ";
  }
  return res;
}

template<class LeafData_>
vstring SubstitutionTree<LeafData_>::nodeToString(Node* topNode)
{
  CALL("SubstitutionTree::nodeToString");

  vstring res;
  Stack<int> indentStack(10);
  Stack<Node*> stack(10);

  stack.push(topNode);
  indentStack.push(1);

  while(stack.isNonEmpty()) {
    Node* node=stack.pop();
    int indent=indentStack.pop();

    if(!node) {
      continue;
    }
    if(!node->term.isEmpty()) {
      res+=getIndentStr(indent)+node->term.toString()+"  "+
	  Int::toHexString(reinterpret_cast<size_t>(node))+"\n";
    }

    if(node->isLeaf()) {
      Leaf* lnode = static_cast<Leaf*>(node);
      LDIterator ldi(lnode->allChildren());

      while(ldi.hasNext()) {
        LeafData ld=ldi.next();
        res+=getIndentStr(indent) + "Lit: " + ld.literal->toString() + "\n";
        if(ld.clause){
          res+=ld.clause->toString()+"\n";
        }
      }
    } else {
      IntermediateNode* inode = static_cast<IntermediateNode*>(node);
      res+=getIndentStr(indent) + " S" + Int::toString(inode->childVar)+":\n";
      NodeIterator noi(inode->allChildren());
      while(noi.hasNext()) {
        stack.push(*noi.next());
        indentStack.push(indent+1);
      }
    }
  }
  return res;
}

template<class LeafData_>
vstring SubstitutionTree<LeafData_>::toString() const
{
  CALL("SubstitutionTree::toString");

  vstring res;

  for(unsigned tli=0;tli<_nodes.size();tli++) {
    res+=Int::toString(tli);
    res+=":\n";

    Stack<int> indentStack(10);
    Stack<Node*> stack(10);

    res+=nodeToString(_nodes[tli]);
  }
  return res;
}

#endif

template<class LeafData_>
SubstitutionTree<LeafData_>::Node::~Node()
{
  CALL("SubstitutionTree::Node::~Node");

  if(term.isTerm()) {
    term.term()->destroyNonShared();
  }
}


template<class LeafData_>
void SubstitutionTree<LeafData_>::Node::split(Node** pnode, TermList* where, int var)
{
  CALL("SubstitutionTree::Node::split");

  Node* node=*pnode;

  IntermediateNode* newNode = createIntermediateNode(node->term, var);
  node->term=*where;
  *pnode=newNode;

  where->makeSpecialVar(var);

  Node** nodePosition=newNode->childByTop(node->term, true);
  ASS(!*nodePosition);
  *nodePosition=node;
}

template<class LeafData_>
void SubstitutionTree<LeafData_>::IntermediateNode::loadChildren(NodeIterator children)
{
  CALL("SubstitutionTree::IntermediateNode::loadChildren");

  while(children.hasNext()) {
    Node* ext=*children.next();
    Node** own=childByTop(ext->term, true);
    ASS(! *own);
    *own=ext;
  }
}

template<class LeafData_>
void SubstitutionTree<LeafData_>::Leaf::loadChildren(LDIterator children)
{
  CALL("SubstitutionTree::Leaf::loadClauses");

  while(children.hasNext()) {
    LeafData ld=children.next();
    insert(ld);
  }
}

template<class LeafData_>
bool SubstitutionTree<LeafData_>::LeafIterator::hasNext()
{
  CALL("SubstitutionTree::Leaf::hasNext");
  for(;;) {
    while(!_nodeIterators.isEmpty() && !_nodeIterators.top().hasNext()) {
      _nodeIterators.pop();
    }
    if(_nodeIterators.isEmpty()) {
      do {
	if(_nextRootPtr==_afterLastRootPtr) {
	  return false;
	}
	_curr=*(_nextRootPtr++);
      } while(_curr==0);
    } else {
      _curr=*_nodeIterators.top().next();
    }
    if(_curr->isLeaf()) {
      return true;
    } else {
      _nodeIterators.push(static_cast<IntermediateNode*>(_curr)->allChildren());
    }
  }
}

template<class LeafData_>
SubstitutionTree<LeafData_>::UnificationsIterator::UnificationsIterator(SubstitutionTree* parent,
	Node* root, Term* query, bool retrieveSubstitution, bool reversed, 
  bool withoutTop, MismatchHandler* hndlr)
: 
svStack(32), retrieveSubstitution(retrieveSubstitution), inLeaf(false),
ldIterator(LDIterator::getEmpty()), nodeIterators(8), bdStack(8),
clientBDRecording(false), handler(hndlr)
#if VDEBUG
  , tree(parent)
#endif
{
  CALL("SubstitutionTree::UnificationsIterator::UnificationsIterator");

#if VDEBUG
  tree->_iteratorCnt++;
#endif

  if(!root) {
    return;
  }

  queryNormalizer.normalizeVariables(query);
  Term* queryNorm=queryNormalizer.apply(query);

  if(handler){
    subst.setHandler(handler);
    // replace subterms by very special variables
    // For example f($sum(X,Y), b) ---> f(#, b)
    queryNorm = handler->transform(queryNorm);
  }

  if(withoutTop){
    subst.bindSpecialVar(0,TermList(queryNorm),NORM_QUERY_BANK);
  }else{
    if(reversed) {
      createReversedInitialBindings(queryNorm);
    } else {
      createInitialBindings(queryNorm);
    }
  }

  BacktrackData bd;
  enter(root, bd);
  bd.drop();
}

template<class LeafData_>
SubstitutionTree<LeafData_>::UnificationsIterator::~UnificationsIterator()
{
  if(clientBDRecording) {
    subst.bdDone();
    clientBDRecording=false;
    clientBacktrackData.backtrack();
  }
  while(bdStack.isNonEmpty()) {
    bdStack.pop().backtrack();
  }

#if VDEBUG
  tree->_iteratorCnt--;
#endif
}

template<class LeafData_>
void SubstitutionTree<LeafData_>::UnificationsIterator::createInitialBindings(Term* t)
{
  CALL("SubstitutionTree::UnificationsIterator::createInitialBindings");

  TermList* args=t->args();
  int nextVar = 0;
  while (! args->isEmpty()) {
    unsigned var = nextVar++;
    subst.bindSpecialVar(var,*args,NORM_QUERY_BANK);
    args = args->next();
  }
}

template<class LeafData_>
void SubstitutionTree<LeafData_>::UnificationsIterator::createReversedInitialBindings(Term* t)
{
  CALL("SubstitutionTree::UnificationsIterator::createReversedInitialBindings");
  ASS(t->isLiteral());
  ASS(t->commutative());
  ASS_EQ(t->arity(),2);

  subst.bindSpecialVar(1,*t->nthArgument(0),NORM_QUERY_BANK);
  subst.bindSpecialVar(0,*t->nthArgument(1),NORM_QUERY_BANK);
}

template<class LeafData_>
bool SubstitutionTree<LeafData_>::UnificationsIterator::hasNext()
{
  CALL("SubstitutionTree::UnificationsIterator::hasNext");

  if(clientBDRecording) {
    subst.bdDone();
    clientBDRecording=false;
    clientBacktrackData.backtrack();
  }

  while(!ldIterator.hasNext() && findNextLeaf()) {}
  return ldIterator.hasNext();
}

template<class LeafData_>
typename SubstitutionTree<LeafData_>::QueryResult SubstitutionTree<LeafData_>::UnificationsIterator::next()
{
  CALL("SubstitutionTree::UnificationsIterator::next");

  while(!ldIterator.hasNext() && findNextLeaf()) {}
  ASS(ldIterator.hasNext());

  ASS(!clientBDRecording);

  LeafData& ld=ldIterator.next();

  if(retrieveSubstitution) {
    Renaming normalizer;
    normalizer.normalizeVariables(ld.key());

    ASS(clientBacktrackData.isEmpty());
    subst.bdRecord(clientBacktrackData);
    clientBDRecording=true;

    subst.denormalize(normalizer,NORM_RESULT_BANK,RESULT_BANK);
    subst.denormalize(queryNormalizer,NORM_QUERY_BANK,QUERY_BANK);

    return QueryResult(&ld, ResultSubstitution::fromSubstitution(
	    &subst, QUERY_BANK, RESULT_BANK)); 
  } else {
    return QueryResult(&ld, ResultSubstitutionSP());
  }
}


template<class LeafData_>
bool SubstitutionTree<LeafData_>::UnificationsIterator::findNextLeaf()
{
  CALL("SubstitutionTree::UnificationsIterator::findNextLeaf");

  if(nodeIterators.isEmpty()) {
    //There are no node iterators in the stack, so there's nowhere
    //to look for the next leaf.
    //This shouldn't hapen during the regular retrieval process, but it
    //can happen when there are no literals inserted for a predicate,
    //or when predicates with zero arity are encountered.
    ASS(bdStack.isEmpty());
    return false;
  }

  if(inLeaf) {
    ASS(!clientBDRecording);
    //Leave the current leaf
    bdStack.pop().backtrack();
    inLeaf=false;
  }

  ASS(!clientBDRecording);
  ASS(bdStack.length()+1==nodeIterators.length());

  do {
    while(!nodeIterators.top().hasNext() && !bdStack.isEmpty()) {
      //backtrack undos everything that enter(...) method has done,
      //so it also pops one item out of the nodeIterators stack
      bdStack.pop().backtrack();
      svStack.pop();
    }
    if(!nodeIterators.top().hasNext()) {
      return false;
    }
    Node* n=*nodeIterators.top().next();

    BacktrackData bd;
    bool success=enter(n,bd);
    if(!success) {
      bd.backtrack();
      continue;
    } else {
      bdStack.push(bd);
    }
  } while(!inLeaf);
  return true;
}

template<class LeafData_>
bool SubstitutionTree<LeafData_>::UnificationsIterator::enter(Node* n, BacktrackData& bd)
{
  CALL("SubstitutionTree::UnificationsIterator::enter");

  bool success=true;
  bool recording=false;
  if(!n->term.isEmpty()) {
    //n is proper node, not a root

    TermList qt(svStack.top(), true);

    recording=true;
    subst.bdRecord(bd);
    success=associate(qt,n->term);
  }
  if(success) {
    if(n->isLeaf()) {
      ldIterator=static_cast<Leaf*>(n)->allChildren();
      inLeaf=true;
    } else {
      IntermediateNode* inode=static_cast<IntermediateNode*>(n);
      svStack.push(inode->childVar);
      NodeIterator nit=getNodeIterator(inode);
      nodeIterators.backtrackablePush(nit, bd);
    }
  }
  if(recording) {
    subst.bdDone();
  }
  return success;
}

/**
 * TODO: explain properly what associate does
 * called from enter(...)
 */
template<class LeafData_>
bool SubstitutionTree<LeafData_>::UnificationsIterator::associate(TermList query, TermList node)
{
  CALL("SubstitutionTree::UnificationsIterator::associate");

  return subst.unifyConstraintProcessed(query,NORM_QUERY_BANK,node,NORM_RESULT_BANK);
}

//TODO I think this works for VSpcialVars as well. Since .isVar() will return true 
//for them
template<class LeafData_>
typename SubstitutionTree<LeafData_>::NodeIterator
  SubstitutionTree<LeafData_>::UnificationsIterator::getNodeIterator(IntermediateNode* n)
{
  CALL("SubstitutionTree::UnificationsIterator::getNodeIterator");

  unsigned specVar=n->childVar;
  TermList qt=subst.getSpecialVarTop(specVar);
  if(qt.isVar()) {
    return n->allChildren();
  } else {
    Node** match=n->childByTop(qt, false);
    if(match) {
      return pvi( 
        getConcatenatedIterator(
	   getSingletonIterator(match),
	   n->variableChildren() 
       ));
    } else {
      return n->variableChildren();
    }
  }
}


} // namespace Indexing

