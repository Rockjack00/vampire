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
 * @file FlatTerm.cpp

 * Implements class FlatTerm.
 */

#include <cstring>

#include "Lib/Allocator.hpp"
#include "Lib/DArray.hpp"

#include "Term.hpp"
#include "TermIterators.hpp"

#include "FlatTerm.hpp"

namespace Kernel
{

using namespace Lib;

// /**
//  * Allocate a FlatTerm object having @b num entries.
//  */
// void* FlatTerm::operator new(size_t sz,unsigned num)
// {
//   ASS_GE(num,1);
//   ASS_EQ(sz, sizeof(FlatTerm));

//   //one entry is already accounted for in the size of the FlatTerm object
//   size_t size=sizeof(FlatTerm)+(num-1)*sizeof(Entry);

//   return ALLOC_KNOWN(size,"FlatTerm");
// }

/**
 * Destroy the FlatTerm object
 */
void FlatTerm::destroy()
{
  ASS_GE(_length,1);

  if (_ownsData) {
    DEALLOC_KNOWN(_data, _length*sizeof(Entry), "FlatTerm");
  }
  DEALLOC_KNOWN(this, sizeof(FlatTerm), "FlatTerm");
}

FlatTerm::FlatTerm(size_t length)
: _length(length), _ownsData(false), _data(nullptr)
{
}

size_t FlatTerm::getEntryCount(Term* t)
{
  //functionEntryCount entries per function and one per variable
  return t->weight()*functionEntryCount-(functionEntryCount-1)*t->numVarOccs();
}

FlatTerm* FlatTerm::create(Term* t)
{
  size_t entries=getEntryCount(t);

  FlatTerm* res=new FlatTerm(entries);
  void* mem = ALLOC_KNOWN(entries*sizeof(Entry), "FlatTerm");
  res->_data = array_new<Entry>(mem, entries);
  res->_ownsData = true;

  size_t fti=0;
  res->_data[fti++]=Entry(FUN,
      t->isLiteral() ? static_cast<Literal*>(t)->header() : t->functor());
  res->_data[fti++]=Entry(t);
  res->_data[fti++]=Entry(FUN_RIGHT_OFS, getEntryCount(t));

  SubtermIterator sti(t);
  while(sti.hasNext()) {
    ASS_L(fti, entries);
    TermList s=sti.next();
    if(s.isVar()) {
      ASS(s.isOrdinaryVar());
      res->_data[fti++]=Entry(VAR, s.var());
    }
    else {
      ASS(s.isTerm());
      res->_data[fti++]=Entry(FUN, s.term()->functor());
      res->_data[fti++]=Entry(s.term());
      res->_data[fti++]=Entry(FUN_RIGHT_OFS, getEntryCount(s.term()));
    }
  }
  ASS_EQ(fti, entries);

  return res;
}

FlatTerm* FlatTerm::create(TermList t)
{
  if(t.isTerm()) {
    return create(t.term());
  }
  ASS(t.isOrdinaryVar());

  FlatTerm* res=new FlatTerm(1);
  void* mem = ALLOC_KNOWN(sizeof(Entry), "FlatTerm");
  res->_data = array_new<Entry>(mem, 1);
  res->_ownsData = true;
  res->_data[0]=Entry(VAR, t.var());

  return res;
}

FlatTerm* FlatTerm::create(Term* t, Entry* data)
{
  size_t entries=getEntryCount(t);

  FlatTerm* res=new FlatTerm(entries);
  res->_data = data;
  res->_ownsData = false;

#if VDEBUG
  ASS(res->_data[0].isFun())
  ASS_EQ(res->_data[1]._ptr,t);
#endif

  return res;
}

FlatTerm* FlatTerm::copy(const FlatTerm* ft)
{
  size_t entries=ft->_length;
  FlatTerm* res=new FlatTerm(entries);
  void* mem = ALLOC_KNOWN(entries*sizeof(Entry), "FlatTerm");
  res->_data = array_new<Entry>(mem, entries);
  res->_ownsData = true;
  memcpy(res->_data, ft->_data, entries*sizeof(Entry));
  return res;
}

void FlatTerm::swapCommutativePredicateArguments()
{
  ASS_EQ((*this)[0].tag(), FUN);
  ASS_EQ((*this)[0].number()|1, 1); //as for now, the only commutative predicate is equality

  size_t firstStart=3;
  size_t firstLen;
  if((*this)[firstStart].tag()==FUN) {
    ASS_EQ((*this)[firstStart+2].tag(), FUN_RIGHT_OFS);
    firstLen=(*this)[firstStart+2].number();
  }
  else {
    ASS_EQ((*this)[firstStart].tag(), VAR);
    firstLen=1;
  }

  size_t secStart=firstStart+firstLen;
  size_t secLen;

  if((*this)[secStart].tag()==FUN) {
    ASS_EQ((*this)[secStart+2].tag(), FUN_RIGHT_OFS);
    secLen=(*this)[secStart+2].number();
  }
  else {
    ASS_EQ((*this)[secStart].tag(), VAR);
    secLen=1;
  }
  ASS_EQ(secStart+secLen,_length);

  static DArray<Entry> buf;
  if(firstLen>secLen) {
    buf.ensure(firstLen);
    memcpy(buf.array(), &_data[firstStart], firstLen*sizeof(Entry));
    memcpy(&_data[firstStart], &_data[secStart], secLen*sizeof(Entry));
    memcpy(&_data[firstStart+secLen], buf.array(), firstLen*sizeof(Entry));
  }
  else {
    buf.ensure(secLen);
    memcpy(buf.array(), &_data[secStart], secLen*sizeof(Entry));
    memcpy(&_data[firstStart+secLen], &_data[firstStart], firstLen*sizeof(Entry));
    memcpy(&_data[firstStart], buf.array(), secLen*sizeof(Entry));
  }
}

};

