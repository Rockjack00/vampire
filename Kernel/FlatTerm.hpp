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
 * @file FlatTerm.hpp

 * Defines class FlatTerm.
 */

#ifndef __FlatTerm__
#define __FlatTerm__

#include "Forwards.hpp"

namespace Kernel {

class FlatTerm
{
public:
  static FlatTerm* create(Term* t);
  static FlatTerm* create(TermList t);
  /**
   * Similar to @b create but only allocates the flat term,
   * and does not fill out its content. The caller has to
   * make sure @b Entry::expand is called on each flat term
   * entry before traversing its arguments.
   */
  static FlatTerm* createUnexpanded(Term* t);
  static FlatTerm* createUnexpanded(TermList t);
  void destroy();

  static FlatTerm* copy(const FlatTerm* ft);

  static const size_t functionEntryCount=3;

  enum EntryTag {
    FUN_TERM_PTR = 0,
    FUN = 1,
    VAR = 2,
    /**
     * If tag is set to this, @b number() gives offset that needs to be
     * added to the position of the corresponding @b FUN Entry in order
     * to get behind the function
     */
    FUN_RIGHT_OFS = 3,
    FUN_UNEXPANDED = 4,
  };

  struct Entry
  {
    Entry() = default;
    Entry(EntryTag tag, unsigned num) { _info.tag=tag; _info.number=num; }
    Entry(Term* ptr) : _ptr(ptr) { ASS_EQ(tag(), FUN_TERM_PTR); }

    inline EntryTag tag() const { return static_cast<EntryTag>(_info.tag); }
    inline unsigned number() const { return _info.number; }
    inline Term* ptr() const { return _ptr; }
    inline bool isVar() const { return tag()==VAR; }
    inline bool isVar(unsigned num) const { return isVar() && number()==num; }
    inline bool isFun() const { return tag()==FUN || tag()==FUN_UNEXPANDED; }
    inline bool isFun(unsigned num) const { return isFun() && number()==num; }
    /**
     * Should be called when @b isFun() is true.
     * If @b tag()==FUN_UNEXPANDED, it fills out entries for the functions
     * arguments with FUN_UNEXPANDED values. Otherwise does nothing.
     */
    void expand();

    union {
      Term* _ptr;
      struct {
	unsigned tag : 4;
	unsigned number : 28;
      } _info;
    };
  };

  inline Entry& operator[](size_t i) { ASS_L(i,_length); return _data[i]; }
  inline const Entry& operator[](size_t i) const { ASS_L(i,_length); return _data[i]; }

  void swapCommutativePredicateArguments();
  void changeLiteralPolarity()
  { _data[0]._info.number^=1; }

private:
  static size_t getEntryCount(Term* t);

  FlatTerm(size_t length);
  void* operator new(size_t,unsigned length);

  /**
   * The @b operator @b delete is undefined, FlatTerm objects should
   * be destroyed by the @b destroy() function
   */
  void operator delete(void*);

  size_t _length;
  Entry _data[1];
};

};

#endif /* __FlatTerm__ */
