/**
 * @file IntNameTable.cpp
 * Implements the class IntNameTable for a table of names.
 */

#include <string>


#include "IntNameTable.hpp"
#include "Hash.hpp"
#include "Exception.hpp"


namespace Lib {


/**
 * Initialise the name table by allocating buckets and
 * setting each bucket to the empty list.
 */
IntNameTable::IntNameTable ()
  : //_names(64),
    _nextNumber(0)
{
} // IntNameTable::IntNameTable


/**
 * Insert an element in the table and return its number.
 */
int IntNameTable::insert (const string& str)
{
#if VDEBUG
  int result = 0;
#else  
  int result;
#endif
  if (_map.find(str,result)) {
    return result;
  }
  _map.insert(str,_nextNumber);
  return _nextNumber++;
} // IntNameTable::insert


}
