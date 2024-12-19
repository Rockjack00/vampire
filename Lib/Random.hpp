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
 * @file Random.hpp
 * Implements random number generation.
 *
 * @since 10/08/1999 Uppsala
 * @since 20/09/1999 Manchester: random bit generator added for efficiency
 * @since 18/02/2000 Manchester
 */


#ifndef __RANDOM__
#  define __RANDOM__


#include <cstdlib>
#include <ctime>
#include <random>

namespace Lib
{

/**
 * A fully static class for random number generation. Optimized to generate
 * random bits.
 */
class Random
{
  /*
   * An entertaining talk on why using the c++11 approach is an improvement
   * over the old C style via rand():
   *
   * https://channel9.msdn.com/Events/GoingNative/2013/rand-Considered-Harmful
   *
   * Still, this is not reproducible across platforms
   * as e.g. uniform_int_distribution is implementation dependent!
   */

  static std::mt19937 _eng[2]; // Standard mersenne_twister_engine(s)
  // the second, invisible engine should be used for in functions that should be side-effect free (such as sorting an array)
  // the use case is making two different paths trough the code the same if one is only adding effectively read only operations on top of the other

  /** the seed we got (last) seeded with */
  static unsigned _seed;

public:
  static inline int getInteger(int modulus, std::size_t invisible=0) {
    return std::uniform_int_distribution<int>(0,modulus-1)(_eng[invisible]);
  }

  static double getDouble(double min, double max, std::size_t invisible=0) {
    return std::uniform_real_distribution<double>(min,max)(_eng[invisible]);
  }

  static float getFloat(float min, float max, std::size_t invisible=0) {
    return std::uniform_real_distribution<float>(min,max)(_eng[invisible]);
  }

  /**
   * Return a random bit.
   */
  static inline bool getBit(std::size_t invisible=0)
  {
    return std::uniform_int_distribution<int>(0,1)(_eng[invisible]);
  } // Random::getBit

  // sets the random seed to s
  /** Set random seed to s */
  inline static void setSeed(unsigned s)
  {
    _seed = s;
    _eng[0].seed(_seed);
    _eng[1].seed(_seed);
  }

  /** Return the current value of the random seed. */
  inline static unsigned seed() { return _seed; }

  /** Try hard to set the seed to something non-deterministic random. */
  inline static void resetSeed ()
  {
    setSeed(std::random_device()());
  }
}; // class Random


} // namespace Lib
#endif


