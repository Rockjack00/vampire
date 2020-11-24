
/*
 * File DPTester.hpp.
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
 * @file DPTester.hpp
 * Defines class DPTester.
 */

#ifndef __DPTester__
#define __DPTester__

#include "Forwards.hpp"



namespace VUtils {

using namespace Lib;
using namespace Kernel;
using namespace Shell;
using namespace DP;

class DPTester {
public:
  int perform(int argc, char** argv);
};

}

#endif // __DPTester__
