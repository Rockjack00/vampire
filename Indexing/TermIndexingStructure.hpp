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
 * @file TermIndexingStructure.hpp
 * Defines class TermIndexingStructure.
 */


#ifndef __TermIndexingStructure__
#define __TermIndexingStructure__

#include "Index.hpp"

namespace Indexing {

template<class Data>
class TermIndexingStructure {
  using TermQueryResultIterator = Indexing::TermQueryResultIterator<Data>;
public:
  virtual ~TermIndexingStructure() {}

  virtual void insert(Data data) = 0;
  virtual void remove(Data data) = 0;

  // virtual void remove(TermList t, Literal* lit, Clause* cls) = 0;

  virtual TermQueryResultIterator getUnifications(TermList t,
	  bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }
  virtual TermQueryResultIterator getUnificationsUsingSorts(TermList t, TermList sort,
    bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }  
  virtual TermQueryResultIterator getGeneralizations(TermList t,
	  bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }
  virtual TermQueryResultIterator getInstances(TermList t,
	  bool retrieveSubstitutions = true) { NOT_IMPLEMENTED; }

  virtual bool generalizationExists(TermList t) { NOT_IMPLEMENTED; }

  virtual std::ostream& output(std::ostream& out) const = 0;

  friend std::ostream& operator<<(std::ostream& out, TermIndexingStructure const& self) 
  { return self.output(out); }
};

};

#endif /* __TermIndexingStructure__ */
