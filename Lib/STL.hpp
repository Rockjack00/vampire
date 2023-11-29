/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#ifndef STL_HPP
#define STL_HPP

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "Lib/STLAllocator.hpp"

namespace Lib {


template< typename Key
        , typename T
        , typename Compare = std::less<Key>
        >
using vmap = std::map<Key, T, Compare, STLAllocator<std::pair<const Key, T>>>;


template< typename Key
        , typename Compare = std::less<Key>
        >
using vset = std::set<Key, Compare, STLAllocator<Key>>;


template< typename Key
        , typename T
        , typename Hash = std::hash<Key>
        , typename KeyEqual = std::equal_to<Key>
        >
using vunordered_map = std::unordered_map<Key, T, Hash, KeyEqual, STLAllocator<std::pair<const Key, T>>>;


template< typename Key
        , typename Hash = std::hash<Key>
        , typename KeyEqual = std::equal_to<Key>
        >
using vunordered_set = std::unordered_set<Key, Hash, KeyEqual, STLAllocator<Key>>;


template< typename T >
using vvector = std::vector<T, STLAllocator<T>>;

template< typename T >
std::shared_ptr<T> make_shared(T* ptr)
{ return std::shared_ptr<T>(ptr, [](auto ptr) { STLAllocator<std::remove_pointer_t<decltype(ptr)>>{}.destroy(ptr); }, STLAllocator<T>{}); }


template< class T >
inline T* move_to_heap(T&& t) { return new T(std::move(t)); }
                
template< typename T, 
          std::enable_if_t<!std::is_pointer<T>::value, bool> = true  
        >
std::shared_ptr<T> make_shared(T t)
{ return make_shared(move_to_heap(std::move(t))); }

}  // namespace Lib

#endif /* !STL_HPP */
