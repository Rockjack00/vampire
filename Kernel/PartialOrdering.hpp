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
 * @file PartialOrdering.hpp
 * Defines a partial ordering between elements of some set.
 */

#ifndef __PartialOrdering__
#define __PartialOrdering__

#include <string>

namespace Kernel {

/** This corresponds to the values we can handle between two elements.
 *  Note that incomparability is also possible, namely ≱ (NGEQ),
 *  ≰ (NLEQ) and their conjunction (INCOMPARABLE). */
enum class PoComp : uint8_t {
  UNKNOWN,
  GREATER,
  EQUAL,
  LESS,
  NGEQ,
  NLEQ,
  INCOMPARABLE,
};

bool checkCompatibility(PoComp old, PoComp curr, PoComp& res);
std::string pocompToInfix(PoComp c);

/** 
 * Partial ordering between elements of some set. The set elements
 * are denoted by IDs inside the class, which is given by order of
 * appearance, as explained below. The set elements are abstracted
 * via these IDs to increase sharing among partial ordering objects.
 * Hence, operations modifying the objects are performed through
 * static methods.
 */
class PartialOrdering
{
public:
  /** Get relation between two elements with IDs @b x and @b y. */
  PoComp get(size_t x, size_t y) const;

  /** Get empty partial ordering. */
  static const PartialOrdering* getEmpty();
  /** Add new element to partial ordering. The ID of this
   *  element is set to @b size()-1 of the new partial ordering. */
  static const PartialOrdering* extend(const PartialOrdering* po);
  /** Tries to set relation between two elements with IDs @b x and @b y,
   *  and performs transitive closure over the entire set so far.
   *  If this fails, returns null, otherwise returns a non-null object. */
  static const PartialOrdering* set(const PartialOrdering* po, size_t x, size_t y, PoComp v);

  friend std::ostream& operator<<(std::ostream& str, const PartialOrdering& po);

private:
  PartialOrdering();
  ~PartialOrdering();
  PartialOrdering(const PartialOrdering& other);
  PartialOrdering& operator=(const PartialOrdering&) = delete;

  void extend();

  PoComp getUnsafe(size_t x, size_t y) const;
  bool setRel(size_t x, size_t y, PoComp v, bool& changed);
  bool setRelSafe(size_t x, size_t y, PoComp v, bool& changed);

  bool setInferred(size_t x, size_t y, PoComp result);
  bool setInferredHelper(size_t x, size_t y, PoComp rel);
  bool setInferredHelperInc(size_t x, size_t y, PoComp wkn);
  bool setInferredHelperEq(size_t x, size_t y);

  size_t _size;
  PoComp* _array;
};

};

#endif /* __PartialOrdering__ */