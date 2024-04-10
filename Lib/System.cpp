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
 * @file System.cpp
 * Wrappers of some system functions and methods that take care of the
 * system stuff and don't fit anywhere else (handling signals etc...)
 */

#include "Portability.hpp"

#include <csignal>

// TODO these should probably be guarded
// for getpid, _exit
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include "Debug/Tracer.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/UIHelper.hpp"

#include "Environment.hpp"

#include "System.hpp"

namespace Lib {

using namespace std;

const char* System::s_argv0 = 0;

const char* signalToString (int sigNum)
{
  switch (sigNum)
    {
    case SIGTERM:
      return "SIGTERM";
# ifndef _MSC_VER
    case SIGQUIT:
      return "SIGQUIT";
    case SIGHUP:
      return "SIGHUP";
    case SIGXCPU:
      return "SIGXCPU";
    case SIGBUS:
      return "SIGBUS";
    case SIGTRAP:
      return "SIGTRAP";
# endif
    case SIGINT:
      return "SIGINT";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGSEGV:
      return "SIGSEGV";
    case SIGABRT:
      return "SIGABRT";
    default:
      return "UNKNOWN SIGNAL";
    }
} // signalToString


/**
 * Signal handling function. Rewritten from the kernel standalone.
 *
 * @param sigNum signal number
 * @since 28/06/2003 Manchester, statistics result registration added
 */
void handleSignal (int sigNum)
{
  // true if a terminal signal has been handled already.
  // to avoid catching signals over and over again
  static bool handled = false;
  static bool haveSigInt = false;
  const char* signalDescription = signalToString(sigNum);

  switch (sigNum)
    {
    case SIGTERM:

# ifndef _MSC_VER
    case SIGQUIT:
      if (handled) {
	System::terminateImmediately(haveSigInt ? VAMP_RESULT_STATUS_SIGINT : VAMP_RESULT_STATUS_OTHER_SIGNAL);
      }
      handled = true;
      if(Shell::outputAllowed(true)) {
	if(env.options) {
    cout << "Aborted by signal " << signalDescription << " on " << env.options->inputFile() << "\n";
	} else {
	  cout << "Aborted by signal " << signalDescription << "\n";
	}
      }
      return;
    case SIGXCPU:
      if(Shell::outputAllowed(true)) {
	if(env.options) {
    cout << "External time out (SIGXCPU) on " << env.options->inputFile() << "\n";
	} else {
	  cout << "External time out (SIGXCPU)\n";
	}
      }
      System::terminateImmediately(VAMP_RESULT_STATUS_OTHER_SIGNAL);
      break;
# endif

    case SIGINT:
      haveSigInt=true;
      System::terminateImmediately(VAMP_RESULT_STATUS_SIGINT);
//      exit(0);
//      return;

    case SIGHUP:
    case SIGILL:
    case SIGFPE:
    case SIGSEGV:

# ifndef _MSC_VER
    case SIGBUS:
    case SIGTRAP:
# endif
    case SIGABRT:
      {
	if (handled) {
	  System::terminateImmediately(haveSigInt ? VAMP_RESULT_STATUS_SIGINT : VAMP_RESULT_STATUS_OTHER_SIGNAL);
	}
	Shell::reportSpiderFail();
	handled = true;
	if(Shell::outputAllowed()) {
	  if(env.options && env.statistics) {
      cout << getpid() << " Aborted by signal " << signalDescription << " on " << env.options->inputFile() << "\n";
	    env.statistics->print(cout);
	    Debug::Tracer::printStack(cout);
	  } else {
	    cout << getpid() << "Aborted by signal " << signalDescription << "\n";
	    Debug::Tracer::printStack(cout);
	  }
	}
	System::terminateImmediately(haveSigInt ? VAMP_RESULT_STATUS_SIGINT : VAMP_RESULT_STATUS_OTHER_SIGNAL);
      }

    default:
      break;
    }
} // handleSignal

void System::setSignalHandlers()
{
  signal(SIGTERM,handleSignal);
  signal(SIGINT,handleSignal);
  signal(SIGILL,handleSignal);
  signal(SIGFPE,handleSignal);
  signal(SIGSEGV,handleSignal);
  signal(SIGABRT,handleSignal);

#ifndef _MSC_VER
  signal(SIGQUIT,handleSignal);
  signal(SIGHUP,handleSignal);
  signal(SIGXCPU,handleSignal);
  signal(SIGBUS,handleSignal);
  signal(SIGTRAP,handleSignal);
#endif

  errno=0;
  // ensure that termination handlers are created _before_ the atexit() call
  // C++ then guarantees that the array is destructed _after_ onTermination
  terminationHandlersArray();
  int res=atexit(onTermination);
  if(res==-1) {
    SYSTEM_FAIL("Call of atexit() function in System::setSignalHandlers failed.", errno);
  }
  ASS_EQ(res,0);
}

/**
 * Function that returns a reference to an array that contains
 * lists of termination handlers
 *
 * Using a function with a static variable inside is a way to ensure
 * that no matter how early we want to register a termination
 * handler, the array will be constructed.
 */
ZIArray<List<VoidFunc>*>& System::terminationHandlersArray()
{
  static ZIArray<List<VoidFunc>*> arr(2);
  return arr;
}

/**
 * Ensure that @b proc will be called before termination of the process.
 * Functions added with lower @b priority will be called first.
 *
 * We try to cover all possibilities how the process may terminate, but
 * some are probably impossible (such as receiving the signal 9). In these
 * cases the @b proc function is not called.
 */
void System::addTerminationHandler(VoidFunc proc, unsigned priority)
{
  VoidFuncList::push(proc, terminationHandlersArray()[priority]);
}

/**
 * This function should be called as the last thing on every path that leads
 * to a process termination.
 */
void System::onTermination()
{
  static bool called=false;
  if(called) {
    return;
  }
  called=true;

  auto handlers = terminationHandlersArray();
  size_t sz=handlers.size();
  for(size_t i=0;i<sz;i++) {
    VoidFuncList::Iterator thIter(handlers[i]);
    while(thIter.hasNext()) {
      VoidFunc func=thIter.next();
      func();
    }
  }
}

void System::terminateImmediately(int resultStatus)
{
  onTermination();
  _exit(resultStatus);
}

/**
 * Make sure that the process will receive the SIGHUP signal
 * when its parent process dies
 *
 * This setting is not passed to the child processes created by fork().
 */
void System::registerForSIGHUPOnParentDeath()
{
#ifdef __linux__
  prctl(PR_SET_PDEATHSIG, SIGHUP);
#endif
}

/**
 * If directory name can be extracted from @c path, assign it into
 * @c dir and return true; otherwise return false.
 *
 * The directory name is extracted without the final '/'.
 */
bool System::extractDirNameFromPath(vstring path, vstring& dir)
{
  size_t index=path.find_last_of("\\/");
  if(index==vstring::npos) {
    return false;
  }
  dir = path.substr(0, index);
  return true;
}

};
