
/*
 * File AnnotationColoring.hpp.
 *
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file AnnotationColoring.hpp
 * Defines class AnnotationColoring.
 */

#ifndef __AnnotationColoring__
#define __AnnotationColoring__

#include "Forwards.hpp"

#include "Lib/DHSet.hpp"
#include "Lib/Stack.hpp"

#include "Shell/SineUtils.hpp"


namespace VUtils {

using namespace Lib;
using namespace Shell;

class AnnotationColoring
{
public:
  int perform(int argc, char** argv);
private:
  typedef SineSymbolExtractor::SymId SymId;
  typedef SineSymbolExtractor::SymIdIterator SymIdIterator;
  typedef DHSet<SymId> SymIdSet;

  void outputColorInfo(ostream& out, SymId sym, vstring color);

  SineSymbolExtractor symEx;
  SymIdSet symbols;
  SymIdSet conjectureSymbols;
  SymIdSet axiomSymbols;

  Stack<Unit*> axiomStack;
  Stack<Unit*> conjectureStack;
};

}

#endif // __AnnotationColoring__
