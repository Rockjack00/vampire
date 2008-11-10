/**
 * @file SubstitutionTree.cpp
 * Implements class SubstitutionTree.
 *
 * @since 16/08/2008 flight Sydney-San Francisco
 */

#include "../Kernel/Clause.hpp"
#include "../Kernel/Term.hpp"
#include "../Lib/DHMap.hpp"

#ifdef VDEBUG
#include <iostream>
#include "../Kernel/Signature.hpp"
#include "../Lib/Environment.hpp"
#include "../Lib/Int.hpp"
#include "../Test/Output.hpp"

string SingleTermListToString(const TermList* ts);

#endif

#include "SubstitutionTree.hpp"

using namespace Indexing;

/**
 * Initialise the substitution tree.
 * @since 16/08/2008 flight Sydney-San Francisco
 */
SubstitutionTree::SubstitutionTree(int nodes)
  : _numberOfTopLevelNodes(nodes),
    _nextVar(0)
{
  CALL("SubstitutionTree::SubstitutionTree");
  ASS(nodes > 0);

  _nodes = new(ALLOC_KNOWN(nodes*sizeof(Node*),"SubstitutionTree::Node"))
                Node*[nodes];
  for (int i = nodes-1;i >= 0;i--) {
    _nodes[i] = 0;
  }
} // SubstitutionTree::SubstitutionTree

/**
 * Destroy the substitution tree.
 * @warning does not destroy nodes yet
 * @since 16/08/2008 flight Sydney-San Francisco
 */
SubstitutionTree::~SubstitutionTree()
{
  CALL("SubstitutionTree::~SubstitutionTree");

  DEALLOC_KNOWN(_nodes,
		_numberOfTopLevelNodes*sizeof(Node*),
		"SubstitutionTree::Node");
} // SubstitutionTree::~SubstitutionTree

/**
 * Insert term arguments to the tree.
 * @param nodeNumber the number of the node in the tree
 * @param args the list of arguments to be inserted
 * @param cls the clause to be stored at the leaf.
 * @since 16/08/2008 flight Sydney-San Francisco
 */
void SubstitutionTree::insert(int nodeNumber,TermList* args,Clause* cls)
{
  CALL("SubstitutionTree::insert");
  ASS(nodeNumber < _numberOfTopLevelNodes);
  ASS(nodeNumber >= 0);

  Binding bind;
  BindingHeap bh;

  int nextVar = 0;
  while (! args->isEmpty()) {
    if (_nextVar <= nextVar) {
      _nextVar = nextVar+1;
    }
    bind.var = nextVar++;
    bind.term = args;
    bh.insert(bind);
    args = args->next();
  }
  
  insert(_nodes+nodeNumber,bh,cls);
} // SubstitutionTree::insert

/**
 * Auxiliary function for substitution tree insertion.
 * @since 16/08/2008 flight Sydney-San Francisco
 */
void SubstitutionTree::insert(Node** pnode,BindingHeap& bh,Clause* clause)
{
  CALL("SubstitutionTree::insert/3");
  
  if(*pnode == 0) {
    if(bh.isEmpty()) {
      *pnode=createLeaf();
    } else {
      *pnode=createIntermediateNode();
    }
  }
  if(bh.isEmpty()) {
    ASS((*pnode)->isLeaf());
    ensureLeafEfficiency(reinterpret_cast<Leaf**>(pnode));
    static_cast<Leaf*>(*pnode)->insert(clause);
    return;
  }

  start:
  Binding bind=bh.pop(); 
  TermList* term=bind.term;
  
  ASS(! (*pnode)->isLeaf());
  IntermediateNode* inode = static_cast<IntermediateNode*>(*pnode);

  //Into pparent we store the node, we might be inserting into.
  //So in the case we do insert, we might check whether this node 
  //needs expansion.
  Node** pparent=pnode;
  pnode=inode->childByTop(term,true);
  
  if (*pnode == 0) {
    while (!bh.isEmpty()) {
      IntermediateNode* inode = createIntermediateNode(term);
      *pnode = inode;
      
      bind = bh.pop();
      term=bind.term;
      pnode = inode->childByTop(term,true);
    }
    Leaf* lnode=createLeaf(term);
    *pnode=lnode;
    lnode->insert(clause);
    
    ensureIntermediateNodeEfficiency(reinterpret_cast<IntermediateNode**>(pparent));
    return;
  }

  
  TermList* tt = term;
  TermList* ss = &(*pnode)->term;

  ASS(sameTop(ss, tt));

  if (tt->sameContent(ss)) {
    if (bh.isEmpty()) {
      ASS((*pnode)->isLeaf());
      ensureLeafEfficiency(reinterpret_cast<Leaf**>(pnode));
      Leaf* leaf = static_cast<Leaf*>(*pnode);
      leaf->insert(clause);
      return;
    }
    goto start;
  }

  // ss is the term in inode, tt is the term to be inserted
  // ss and tt have the same top symbols but are not equal
  // create the common subterm of ss,tt and an alternative node
  Stack<TermList*> subterms(64);
  for (;;) {
    if (!ss->sameContent(tt) && sameTop(ss,tt)) {
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
      if (! sameTop(ss,tt)) {
	int x;
	if(!ss->isSpecialVar()) { 
	  x = _nextVar++;
	  Node::split(pnode, ss,x);
	} else {
	  x=ss->var();
	}
	Binding bind(x,tt);
	bh.insert(bind);
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
  
  goto start;
} // // SubstitutionTree::insert

/**
 * Return true if @b ss and @b tt have the same top symbols, that is,
 * either both are the same variable or both are complex terms with the
 * same function symbol. 
 * @since 16/08/2008 flight Sydney-San Francisco
 */
bool SubstitutionTree::sameTop(const TermList* ss,const TermList* tt)
{
  if (ss->isVar()) {
    return ss->sameContent(tt);
  }
  if (tt->isVar()) {
    return false;
  }
  return ss->term()->functor() == tt->term()->functor();
}

/*
 * Remove a clause from the substitution tree.
 * @since 16/08/2008 flight San Francisco-Seattle
 */
void SubstitutionTree::remove(int nodeNumber,TermList* args,Clause* cls)
{
  CALL("SubstitutionTree::remove-1");
  ASS(nodeNumber < _numberOfTopLevelNodes);
  ASS(nodeNumber >= 0);

  Binding bind;
  BindingHeap bh;

  int nextVar = 0;
  while (! args->isEmpty()) {
    ASS(_nextVar > nextVar);

    bind.var = nextVar++;
    bind.term = args;
    bh.insert(bind);
    args = args->next();
  }

  remove(_nodes+nodeNumber,bh,cls);
} // remove

/*
 * Remove a clause from the substitution tree.
 * @since 16/08/2008 flight San Francisco-Seattle
 */
void SubstitutionTree::remove(Node** pnode,BindingHeap& bh,Clause* clause)
{
  CALL("SubstitutionTree::remove-2");
  
  ASS(*pnode);
  
  Stack<Node**> history(1000);

  while (! bh.isEmpty()) {
    history.push(pnode);

    ASS (! (*pnode)->isLeaf());
    IntermediateNode* inode=static_cast<IntermediateNode*>(*pnode);

    Binding bind = bh.pop();
    TermList* t = bind.term;
    
    pnode=inode->childByTop(t,false);
    ASS(pnode);
    
    TermList* s = &(*pnode)->term;
    ASS(sameTop(s,t));

    if (s->content() == t->content()) {
      continue;
    }
    
    ASS(! s->isVar());
    const TermList* ss = s->term()->args();
    ASS(!ss->isEmpty());

    // computing the disagreement set of the two terms
    TermStack subterms(120);

    subterms.push(ss);
    subterms.push(t->term()->args());
    while (! subterms.isEmpty()) {
      const TermList* tt = subterms.pop();
      ss = subterms.pop();
      if (tt->next()->isEmpty()) {
	ASS(ss->next()->isEmpty());
      }
      else {
	subterms.push(ss->next());
	subterms.push(tt->next());
      }
      if (ss->content() == tt->content()) {
	continue;
      }
      if (ss->isVar()) {
	ASS(ss->isSpecialVar());
	Binding b(ss->var(),const_cast<TermList*>(tt));
	bh.insert(b);
	continue;
      }
      ASS(! t->isVar());
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
  lnode->remove(clause);
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
      parent->remove(&term);
      delete node;
      pnode=history.pop();
      ensureIntermediateNodeEfficiency(reinterpret_cast<IntermediateNode**>(pnode));
    }
  }
} // SubstitutionTree::remove


#if VDEBUG

/**
 * Get string representation of just the header of given term.
 * 
 * This us useful when there's something wrong with the term and
 * Test::Output::toString causes segmentation faults.
 */
string SingleTermListToString(const TermList* ts)
{
  if(ts->isOrdinaryVar()) {
    return string("X")+Int::toString(ts->var());
  } else if(ts->isSpecialVar()) {
    return string("S")+Int::toString(ts->var());
  } else if(ts->isEmpty()) {
    return "EMPTY";
  }
  
  //term is REF
  const Term* term=ts->term();
  return Test::Output::toString(term);
}

string getIndentStr(int n)
{
  string res;
  for(int indCnt=0;indCnt<n;indCnt++) {
	res+="  ";
  }
  return res;
}

string SubstitutionTree::toString() const
{
  CALL("SubstitutionTree::toString");

  string res;
  
  for(int tli=0;tli<_numberOfTopLevelNodes;tli++) {
    res+=Int::toString(tli);
    res+=":\n";
    
    Stack<int> indentStack(10);
    Stack<Node*> stack(10);
    
    stack.push(_nodes[tli]);
    indentStack.push(1);
    
    while(stack.isNonEmpty()) {
      Node* node=stack.pop();
      int indent=indentStack.pop();

      if(!node) {
	continue;
      }
      if(node->term.content()) {
	res+=getIndentStr(indent)+SingleTermListToString(&node->term)+"\n";
      }

      if(node->isLeaf()) {
	Leaf* lnode = static_cast<Leaf*>(node);
	ClauseIterator cli(lnode->allCaluses());
	
	while(cli.hasNext()) {
	  res+=getIndentStr(indent) + "Cl: " + Test::Output::toString(cli.next()) + "\n";
	}
      } else {
	IntermediateNode* inode = static_cast<IntermediateNode*>(node);
	NodeIterator noi(inode->allChildren());
	while(noi.hasNext()) {
	  stack.push(*noi.next());
	  indentStack.push(indent+1);
	}
      }
    }
  }
  return res;
}

#endif

SubstitutionTree::Node::~Node()
{
  if(term.tag()!=REF || term.term()->shared()) {
    return;
  }
  TermList* ts=&term;
  Stack<TermList*> stack(4);
  Stack<Term*> deletingStack(8);
  for(;;) {
    if(ts->tag()==REF && !ts->term()->shared()) {
      stack.push(ts->term()->args());
      deletingStack.push(ts->term());
    }
    if(stack.isEmpty()) {
      break;
    }
    ts=stack.pop();
    if(!ts->next()->isEmpty()) {
      stack.push(ts->next());
    }
  }
  while(!deletingStack.isEmpty()) {
    deletingStack.pop()->destroy();
  }
}


void SubstitutionTree::Node::split(Node** pnode, TermList* where, int var)
{
  CALL("SubstitutionTree::Node::split");

  Node* node=*pnode;
  
  IntermediateNode* newNode = createIntermediateNode(&node->term);
  node->term=*where;
  *pnode=newNode;
  
  where->makeSpecialVar(var);
  
  Node** nodePosition=newNode->childByTop(&node->term, true);
  ASS(!*nodePosition);
  *nodePosition=node;
}

void SubstitutionTree::IntermediateNode::loadChildren(NodeIterator children)
{
  CALL("SubstitutionTree::IntermediateNode::loadChildren");
  while(children.hasNext()) {
	Node* ext=*children.next();
	Node** own=childByTop(&ext->term, true);
	ASS(! *own);
	*own=ext;
  }
}

void SubstitutionTree::Leaf::loadClauses(ClauseIterator clauses)
{
  CALL("SubstitutionTree::Leaf::loadClauses");
  while(clauses.hasNext()) {
	Clause* cls=clauses.next();
	insert(cls);
  }
}

SubstitutionTree::Node** SubstitutionTree::UListIntermediateNode::
	childByTop(TermList* t, bool canCreate)
{
  CALL("SubstitutionTree::UListIntermediateNode::childByTop");
  
  NodeList** nl=&_nodes;
  while(*nl && !sameTop(t, &(*nl)->head()->term)) {
	nl=reinterpret_cast<NodeList**>(&(*nl)->tailReference());
  }
  if(!*nl && canCreate) {
  	*nl=new NodeList(0,0);
  	_size++;
  }
  if(*nl) {
	return (*nl)->headPtr(); 
  } else {
	return 0;
  }
}

void SubstitutionTree::UListIntermediateNode::remove(TermList* t)
{
  CALL("SubstitutionTree::UListIntermediateNode::remove");
  
  NodeList** nl=&_nodes;
  while(!sameTop(t, &(*nl)->head()->term)) {
	nl=reinterpret_cast<NodeList**>(&(*nl)->tailReference());
	ASS(*nl);
  }
  NodeList* removedPiece=*nl;
  *nl=static_cast<NodeList*>((*nl)->tail());
  delete removedPiece;
  _size--;
}

/**
 * Take an IntermediateNode, destroy it, and return 
 * SListIntermediateNode with the same content.
 */
SubstitutionTree::SListIntermediateNode* SubstitutionTree::SListIntermediateNode
	::assimilate(IntermediateNode* orig) 
{
  SListIntermediateNode* res=new SListIntermediateNode(&orig->term);
  res->loadChildren(orig->allChildren()); 
  orig->term.makeEmpty();
  delete orig;
  return res;
}

/**
 * Take a Leaf, destroy it, and return SListLeaf 
 * with the same content.
 */
SubstitutionTree::SListLeaf* SubstitutionTree::SListLeaf::assimilate(Leaf* orig) 
{
  SListLeaf* res=new SListLeaf(&orig->term);
  res->loadClauses(orig->allCaluses()); 
  orig->term.makeEmpty();
  delete orig;
  return res;
}

/**
 * Take a Leaf, destroy it, and return SetLeaf 
 * with the same content.
 */
SubstitutionTree::SetLeaf* SubstitutionTree::SetLeaf::assimilate(Leaf* orig) 
{
  SetLeaf* res=new SetLeaf(&orig->term);
  res->loadClauses(orig->allCaluses()); 
  orig->term.makeEmpty();
  delete orig;
  return res;
}


void SubstitutionTree::ensureLeafEfficiency(Leaf** leaf)
{
  CALL("SubstitutionTree::ensureLeafEfficiency");

  if( (*leaf)->algorithm()==UNSORTED_LIST && (*leaf)->size()>5 ) {
    //*leaf=SListLeaf::assimilate(*leaf);
    *leaf=SetLeaf::assimilate(*leaf);
  }
}

void SubstitutionTree::ensureIntermediateNodeEfficiency(IntermediateNode** inode)
{
  CALL("SubstitutionTree::ensureIntermediateNodeEfficiency");

  if( (*inode)->algorithm()==UNSORTED_LIST && (*inode)->size()>3 ) {
    *inode=SListIntermediateNode::assimilate(*inode);
  }
}
