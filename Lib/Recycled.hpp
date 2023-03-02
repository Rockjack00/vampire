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
 * @file Recycled.hpp
 */

#ifndef __Recycled__
#define __Recycled__

#include "Forwards.hpp"

#include "Stack.hpp"
#include "DArray.hpp"
#include <initializer_list>

namespace Lib
{

struct DefaultReset 
{ template<class T> void operator()(T& t) { t.reset(); } };

struct NoReset 
{ template<class T> void operator()(T& t) {  } };

/** A smart that lets you keep memory allocated, and reuse it.
 * Constructs an object of type T on the heap. When the Recycled<T> is destroyed,
 * the object is not returned, but the object is reset using Reset::operator(), 
 * and returned to a pool of pre-allocated objects. When the next Recycled<T> is
 * constructed an object from the pool is returned instead of allocating heap memory.
 */
template<class T, class Reset = DefaultReset>
class Recycled
{
  T _ptr;
  Reset _reset;

  static Stack<T>& mem() {
    static Stack<T> mem;
    return mem;
  }
public: 

  Recycled()
    : _ptr(mem().isNonEmpty() ? mem().pop() : T()) 
    , _reset()
  { }


  // template<class A>
  // Recycled(std::initializer_list<A> list)
  //   : _ptr(mem().isNonEmpty() ? mem().pop() : T()) 
  //   , _reset()
  // {
  //   _ptr.init(list);
  // }


  template<class A, class... As>
  Recycled(A a, As... as)
    : _ptr(mem().isNonEmpty() ? mem().pop() : T()) 
    , _reset()
  {
    _ptr.init(a, as...);
  }

  Recycled(Recycled&& other) = default;
  Recycled& operator=(Recycled&& other) = default;

  ~Recycled()
  { 
    _reset(_ptr);
    mem().push(std::move(_ptr));
  }

  T const& operator* () const { return  _ptr; }
  T const* operator->() const { return &_ptr; }
  T& operator* () { return  _ptr; }
  T* operator->() { return &_ptr; }

  friend std::ostream& operator<<(std::ostream& out, Recycled const& self)
  { return out << self._ptr; }
};

};

#endif /*__Recycled__*/
