#include "SMTSubsumptionImpl3.hpp"
#include "Kernel/Matcher.hpp"
#include "Debug/RuntimeStatistics.hpp"
#include <iostream>

using namespace Indexing;
using namespace Kernel;
using namespace SMTSubsumption;

// TODO: early exit in case time limit hits, like in MLMatcher which checks every 50k iterations if time limit has been exceeded

SMTSubsumptionImpl3::SMTSubsumptionImpl3()
{
  solver.reserve_variables(64);
  solver.reserve_clause_storage(512);
  solver.theory().reserve(64, 2, 16);
  mcs.reserve(16);
  instance_constraints.reserve(16);
}



SMTSubsumptionImpl3::Token SMTSubsumptionImpl3::setupMainPremise(Kernel::Clause* new_instance)
{
  // std::cerr << "\n\n\nINSTANCE " << new_instance->number() << " " << new_instance->length() << std::endl;
  instance = new_instance;
  next_mc = 0;
  Kernel::Clause::requestAux();
  // TODO:
  // Copy the literals into a vvector, std::sort them (like LiteralMiniIndex; by header).
  // Then use std::binary_search to find the first one in setupSubsumption?
  return {*this};
}



void SMTSubsumptionImpl3::endMainPremise()
{
  // std::cerr << "END" << std::endl;
  Kernel::Clause::releaseAux();
  instance = nullptr;
}



SMTSubsumptionImpl3::Token::~Token()
{
  impl.endMainPremise();
}


void SMTSubsumptionImpl3::fillMatches(MatchCache& mc, Kernel::Clause* base)
{
  // std::cerr << "F  " << &mc << " " << instance->number() << " " << base->number() << std::endl;
  ASS(mc.empty());
  ASS_EQ(mc.bli.size(), 0);
  ASS_EQ(mc.inst_match_count.size(), 0);

  uint32_t const base_len = base->length();
  uint32_t const inst_len = instance->length();

#ifndef NDEBUG
  ASS_EQ(mc.base, nullptr);
  ASS_EQ(mc.inst, nullptr);
  mc.base = base;
  mc.inst = instance;
#endif

  // std::cerr << "              " << instance->number() << " " << inst_len << " " << base->number() << " " << base_len << std::endl;
  // std::cerr << "    vec at " << &mc.inst_match_count << std::endl;
  mc.inst_match_count.resize(2*inst_len+1, 0);
  ASS_EQ(mc.inst_match_count.size(), 2*inst_len+1);

  uint32_t nextVarIndex = 0;
  for (unsigned bi = 0 /*mc.bli.size()*/; bi < base_len; ++bi) {
    Literal* base_lit = base->literals()[bi];
    subsat::Var const first_var{nextVarIndex};
    uint32_t match_count = 0;

    for (unsigned j = 0; j < inst_len; ++j) {
      Literal* inst_lit = instance->literals()[j];

      if (!Literal::headersMatch(base_lit, inst_lit, false)) {
        continue;
      }

      {
        auto binder = mc.bm.start_binder();
        if (base_lit->arity() == 0 || MatchingUtils::matchArgs(base_lit, inst_lit, binder)) {
          subsat::Var b{nextVarIndex++};
          match_count += 1;
          mc.inst_match_count[j] += 1;
          mc.bm.commit_bindings(binder, b, bi, j);
        }
      }

      if (base_lit->commutative()) {
        ASS_EQ(base_lit->arity(), 2);
        ASS_EQ(inst_lit->arity(), 2);
        auto binder = mc.bm.start_binder();
        if (MatchingUtils::matchReversedArgs(base_lit, inst_lit, binder)) {
          subsat::Var b{nextVarIndex++};
          match_count += 1;
          mc.inst_match_count[j] += 1;
          mc.bm.commit_bindings(binder, b, bi, j);
        }
      }
    }
    mc.bli.push_back({first_var, match_count, subsat::Var::invalid(), 0});
    if (!match_count) {
      if (!mc.zero_match_count) {
        // the first base literal without any matches
        // this means for SR, this literal must be (complementary-)matched to the resolved literal
        mc.zero_match_header = base_lit->header();
      } else {
        ASS_NEQ(mc.zero_match_header, std::numeric_limits<uint32_t>::max());
        if (base_lit->header() != mc.zero_match_header) {
          // TODO: exit early, neither S nor SR is possible in this case.
        }
      }
      mc.zero_match_count += 1;
    }
  }

  ASS_EQ(mc.bli.size(), base_len);
  ASS(!mc.empty());
}

/// Set up the subsumption problem. Must have called setupMainPremise first.
/// Returns false if no solution is possible.
/// Otherwise, solve() needs to be called.
bool SMTSubsumptionImpl3::setupSubsumption(Kernel::Clause* base)
{
  CALL("SMTSubsumptionImpl3::setupSubsumption");

  if (base->hasAux()) {
    // if we encounter a clause twice then the result must be false.
    return false;
  }

  uint32_t const base_len = base->length();
  uint32_t const inst_len = instance->length();

  // good check for subsumption alone.
  // but we won't save any matching cost since the same base will appear for SR too.
  // but still, let's keep it. it saves us some cache memory since those cases will use shared_mc.
  if (base_len > inst_len) {
    return false;
  }

  solver.clear();
  ASS(solver.empty());
  ASS(solver.theory().empty());

  if (next_mc == mcs.size()) {
    mcs.push_back(new MatchCache());
  }
  ASS_L(next_mc, mcs.size());
  MatchCache& mc = *mcs[next_mc++];
  mc.clear();
  ASS(mc.empty());
  base->setAux(&mc);
  auto& bm = mc.bm;
  solver.theory().setBindings(&bm);

  // std::cerr << "S  " << &mc << " " << instance->number() << " " << base->number() << std::endl;
 fillMatches(mc, base);
  // std::cerr << "    vec at " << &mc.inst_match_count << std::endl;
  ASS_EQ(mc.inst_match_count.size(), 2*inst_len+1);
 if (mc.zero_match_count) {
   return false;
 }


  // Build clauses stating that base_lit must be matched to at least one corresponding instance literal.
  /*
  ASS_EQ(base_clauses.size(), base_len);
  for (unsigned bi = 0; bi < base_len; ++bi) {
    uint32_t match_count = base_clauses[bi];
    solver.constraint_start();
    while (match_count--) {
      subsat::Var b = solver.new_variable(bi);
      solver.constraint_push_literal(b);
    }
    auto handle = solver.constraint_end();
    solver.add_clause_unsafe(handle);
  }
  */

  ASS_EQ(mc.bli.size(), base_len);
  for (unsigned bi = 0; bi < base_len; ++bi) {
    uint32_t n = mc.bli[bi].match_count;
    solver.constraint_start();
    while (n--) {
      subsat::Var b = solver.new_variable(bi);
      ASS_LE(mc.bli[bi].first.index(), b.index());
      ASS_L(b.index(), mc.bli[bi].first.index() + mc.bli[bi].match_count);
      solver.constraint_push_literal(b);
    }
    auto handle = solver.constraint_end();
    solver.add_clause_unsafe(handle);
  }

  // Here we store the AtMostOne constraints saying that each instance literal may be matched at most once.
  // Each instance literal can be matched by at most 2 boolean vars per base literal (two orientations of equalities).
  // NOTE: instance constraints cannot be packed densely because we only know their shape at the end.
  uint32_t const instance_constraint_maxsize = 2 * base_len;
  instance_constraints.clear();
  ASS(instance_constraints.empty());
  for (size_t j = 0; j < inst_len; ++j) {
    instance_constraints.push_back(solver.alloc_constraint(mc.inst_match_count[j]));
  }
  for (unsigned v = 0; v < bm.size(); ++v) {
    subsat::Var b{v};
    auto& bref = bm.get_bindings(b);
    solver.handle_push_literal(instance_constraints[bref.extra_j], b);
  }
  for (auto handle : instance_constraints) {
    auto built = solver.handle_build(handle);
    solver.add_atmostone_constraint_unsafe(built);
  }

  return !solver.inconsistent();
}  // setupSubsumption



bool SMTSubsumptionImpl3::setupSubsumptionResolution(Kernel::Clause* base)
{
  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution");
  solver.clear();
  ASS(solver.empty());
  ASS(solver.theory().empty());

  MatchCache* mcp = nullptr;
  if (base->hasAux()) {
    mcp = base->getAux<MatchCache>();
    if (!mcp) {
      // SR already checked for this clause
      // (so the answer must be false, or we wouldn't have continued)
      return false;
    }
  } else {
    shared_mc.clear();
    fillMatches(shared_mc, base);
    mcp = &shared_mc;
  }
  base->setAux(nullptr);  // mark clause as already-processed
  ASS(mcp);
  MatchCache& mc = *mcp;
  solver.theory().setBindings(&mc.bm);

  // std::cerr << "SR " << &mc << " " << instance->number() << " " << base->number() << std::endl;
  ASS(!mc.empty());

  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution 1");

  uint32_t const base_len = base->length();
  uint32_t const inst_len = instance->length();

#ifndef NDEBUG
  ASS_EQ(mc.base, base);
  ASS_EQ(mc.inst, instance);
#endif

  // std::cerr << "              " << instance->number() << " " << inst_len << " " << base->number() << " " << base_len << std::endl;
  // std::cerr << "    vec at " << &mc.inst_match_count << std::endl;
  ASS_EQ(mc.inst_match_count.size(), 2*inst_len+1);

  // fill in the complementary matches
  if (mc.zero_match_count) {
    ASS_NEQ(mc.zero_match_header, std::numeric_limits<uint32_t>::max());
  } else {
    ASS_EQ(mc.zero_match_header, std::numeric_limits<uint32_t>::max());
  }
  ASS_EQ(mc.bli.size(), base_len);
  uint32_t nextVarIndex = mc.bli[base_len-1].var_end().index();
  uint32_t total_compl_matches = 0;
  for (unsigned i = 0; i < base_len; ++i) {
    Literal* base_lit = base->literals()[i];
    mc.bli[i].compl_first = subsat::Var{nextVarIndex};
    uint32_t compl_match_count = 0;

  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution X");
    // we have base literals without any non-complementary matches.
    // all of these must be complementary-matched to the same instance literal to obtain SR.
    // of course other base literals may participate in complementary matches as well.
    // but in this case we only need to consider literals with mc.zero_match_header
    if (mc.zero_match_count && base_lit->header() != mc.zero_match_header) {
      continue;
    }

    for (unsigned j = 0; j < inst_len; ++j) {
      Literal* inst_lit = instance->literals()[j];

      if (!Literal::headersMatch(base_lit, inst_lit, true)) {
        continue;
      }

      // TODO:
      {
        auto binder = mc.bm.start_binder();
        if (base_lit->arity() == 0 || MatchingUtils::matchArgs(base_lit, inst_lit, binder)) {
          subsat::Var b{nextVarIndex++};
          mc.bm.commit_bindings(binder, b, i, j);
          // solver.constraint_push_literal(b);
          // complementary_matches.push_back({b, j});
          // inst_compl_matches[j].push_back(b);
          mc.inst_match_count[inst_len+j] += 1;
          compl_match_count += 1;
        }
      }
      if (base_lit->commutative()) {
        auto binder = mc.bm.start_binder();
        if (MatchingUtils::matchReversedArgs(base_lit, inst_lit, binder)) {
          subsat::Var b{nextVarIndex++};
          mc.bm.commit_bindings(binder, b, i, j);
          // solver.constraint_push_literal(b);
          // complementary_matches.push_back({b, j});
          // inst_compl_matches[j].push_back(b);
          mc.inst_match_count[inst_len+j] += 1;
          compl_match_count += 1;
        }
      }
    }

    if (!mc.bli[i].match_count && !compl_match_count) {
      return false;
    }
    mc.bli[i].compl_match_count = compl_match_count;  // TODO: could compute this from nextVarIndex as well.
    total_compl_matches += compl_match_count;
  }

  if (!total_compl_matches) {
    return false;
  }

  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution 2");

  // create solver variables in the right order
  ASS_EQ(mc.bli.size(), base_len);
  for (unsigned i = 0; i < base_len; ++i) {
    uint32_t n = mc.bli[i].match_count;
    while (n--) {
      subsat::Var b = solver.new_variable(i);
      ASS_LE(mc.bli[i].first.index(), b.index());
      ASS_L(b.index(), mc.bli[i].var_end().index());
    }
  }
  for (unsigned i = 0; i < base_len; ++i) {
    uint32_t n = mc.bli[i].compl_match_count;
    while (n--) {
      subsat::Var b = solver.new_variable(i);
      ASS_LE(mc.bli[i].compl_first.index(), b.index());
      ASS_L(b.index(), mc.bli[i].compl_var_end().index());
    }
  }

  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution 3");

  // nextVarIndex here is the total number of match variables
  ASS_EQ(nextVarIndex, std::accumulate(mc.inst_match_count.begin(), mc.inst_match_count.end(), 0));
  // convert counts into (end) indices
  for (unsigned j = 1; j < mc.inst_match_count.size(); ++j) {
    mc.inst_match_count[j] += mc.inst_match_count[j-1];
  }
  mc.inst_match_count[inst_len+inst_len] = nextVarIndex;  // for uniformity of access
  m_inst_matches.resize(nextVarIndex);
  // now we can write match variable for inst[j] at m_inst_matches[--mc.inst_match_count[j]].
  // afterwards, the match variables for inst[j] will be in m_inst_matches from indices mc.inst_match_count[j] to mc.inst_match_count[j+1] (excl. end).

  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution 4");

  // Ensure at least one complementary match
  // NOTE: this clause is required. Without it, we may get a false subsumption
  //       (because subsumption resolution uses set-matching and not multiset-matching)
  auto ensure_compl_match = solver.alloc_constraint(total_compl_matches);

  // matching clauses - each base literal needs at least one match
  uint32_t v1 = mc.bli[0].first.index();
  uint32_t v2 = mc.bli[0].compl_first.index();
  for (unsigned i = 0; i < base_len; ++i) {
    solver.constraint_start();
    uint32_t n = mc.bli[i].match_count;
    uint32_t m = mc.bli[i].compl_match_count;
    while (n--) {
      subsat::Var const b{v1++};
      ASS_LE(mc.bli[i].first.index(), b.index());
      ASS_L(b.index(), mc.bli[i].var_end().index());
      solver.constraint_push_literal(b);
      uint32_t const j = mc.bm.get_bindings(b).extra_j;
      m_inst_matches[--mc.inst_match_count[j]] = b.index();
    }
    while (m--) {
      subsat::Var const b{v2++};
      ASS_LE(mc.bli[i].compl_first.index(), b.index());
      ASS_L(b.index(), mc.bli[i].compl_var_end().index());
      solver.constraint_push_literal(b);
      solver.handle_push_literal(ensure_compl_match, b);
      uint32_t const j = mc.bm.get_bindings(b).extra_j;
      m_inst_matches[--mc.inst_match_count[inst_len+j]] = b.index();
    }
    auto handle = solver.constraint_end();
    solver.add_clause_unsafe(handle);
  }

  {
    auto handle = solver.handle_build(ensure_compl_match);
    solver.add_clause_unsafe(handle);
  }


  // TODO

  CALL("SMTSubsumptionImpl3::setupSubsumptionResolution 5");

  // At most one instance literal is complementary-matched.
  // But note that this instance literal may be complementary-matched by multiple base literals!
  auto amo_inst_compl_matched = solver.alloc_constraint(inst_len);

  for (unsigned j = 0; j < instance->length(); ++j) {
    uint32_t const compl_match_begin = mc.inst_match_count[inst_len+j];
    uint32_t const compl_match_end = mc.inst_match_count[inst_len+j+1];
    ASS_LE(compl_match_begin, compl_match_end);
    if (compl_match_begin == compl_match_end) {
      continue;
    }
    // b_is_matched is true if instance[j] is complementary-matched by one or more base literals
    // (other direction not required, but we could use it instead of the "at least one complementary match" above)
    subsat::Var const b_is_matched = solver.new_variable();
    solver.handle_push_literal(amo_inst_compl_matched, b_is_matched);

    for (uint32_t k = compl_match_begin; k != compl_match_end; ++k) {
      subsat::Var const b_compl{m_inst_matches[k]};
      solver.constraint_start();
      solver.constraint_push_literal(~b_compl);
      solver.constraint_push_literal(b_is_matched);
      auto handle = solver.constraint_end();
      solver.add_clause_unsafe(handle);
    }
  }

  {
    auto handle = solver.handle_build(amo_inst_compl_matched);
    solver.add_atmostone_constraint_unsafe(handle);
  }


  // NOTE: these constraints are necessary because:
  // 1) when an inst_lit is complementary-matched, then we cannot match anything else to it.
  // 2) but when it is not complementary-matched, then we may match multiple base literals to it.
  // The reason 2) is why we can't simply use instance-AtMostOne constraints like we do for subsumption.
  // Naive solution: use binary clauses "~compl \/ ~normal", more sophisticated: use a helper variable that just means "instance literal is complementary-matched".
  //
  // Example of wrong inference without these constraints:
  // % ***WRONG RESULT OF SUBSUMPTION RESOLUTION***
  // %    base:       ~p(X0,X1,X2,X3,X4) | p(X5,X1,X2,X3,X4)
  // %    instance:   ~neq(X10,X11) | ~neq(X10,s0) | ~neq(X12,X11) | ~neq(X10,X12) | ~neq(X10,X13) | ~neq(X12,s0) | ~neq(X13,X14) | ~neq(X13,X11) | ~neq(X10,X14) | p(X10,X13,X14,s0,s0)
  // % Should NOT be possible but found the following result:
  // %    conclusion: ~neq(X10,X11) | ~neq(X10,s0) | ~neq(X12,X11) | ~neq(X10,X12) | ~neq(X10,X13) | ~neq(X12,s0) | ~neq(X13,X14) | ~neq(X13,X11) | ~neq(X10,X14)
  for (unsigned j = 0; j < instance->length(); ++j) {
    uint32_t const normal_match_begin = mc.inst_match_count[j];
    uint32_t const normal_match_end = mc.inst_match_count[j+1];
    uint32_t const compl_match_begin = mc.inst_match_count[inst_len+j];
    uint32_t const compl_match_end = mc.inst_match_count[inst_len+j+1];
    // uint32_t const nnormal = normal_match_end - normal_match_begin;
    // uint32_t const ncompl = compl_match_end - compl_match_begin;
    // if (nnormal >= 2 && ncompl >= 2 && nnormal + ncompl > 4) {
    //   // TODO: more sophisticated encoding with helper variable? instead of the 'matrix' encoding below
    //   RSTAT_CTR_INC("would do SR sophisticated encoding");
    // }
    // Idea: instance literal is complementary-matched => cannot be normal-matched
    // basic implementation using binary clauses.
    for (uint32_t kc = compl_match_begin; kc != compl_match_end; ++kc) {
      for (uint32_t kn = normal_match_begin; kn != normal_match_end; ++kn) {
        subsat::Var const b_compl{m_inst_matches[kc]};
        subsat::Var const b_normal{m_inst_matches[kn]};
        solver.constraint_start();
        solver.constraint_push_literal(~b_compl);
        solver.constraint_push_literal(~b_normal);
        auto handle = solver.constraint_end();
        solver.add_clause_unsafe(handle);
      }
    }
  }

  return !solver.inconsistent();
}  // setupSubsumptionResolution



bool SMTSubsumptionImpl3::solve()
{
  return solver.solve() == subsat::Result::Sat;
}
