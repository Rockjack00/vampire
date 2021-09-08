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
 * @file GeneralInduction.cpp
 * Implements class GeneralInduction.
 */

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Environment.hpp"
#include "Lib/Set.hpp"
#include "Lib/Array.hpp"
#include "Lib/ScopedPtr.hpp"

#include "Kernel/TermIterators.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Sorts.hpp"
#include "Kernel/Theory.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/FormulaVarIterator.hpp"
#include "Kernel/Connective.hpp"
#include "Kernel/RobSubstitution.hpp"

#include "Saturation/SaturationAlgorithm.hpp"
#include "Saturation/Splitter.hpp"

#include "Shell/InductionSchemeGenerator.hpp"
#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/NewCNF.hpp"
#include "Shell/NNF.hpp"
#include "Shell/Rectify.hpp"
#include "Shell/Skolem.hpp"

#include "Indexing/Index.hpp"
#include "Indexing/ResultSubstitution.hpp"
#include "Inferences/BinaryResolution.hpp"

#include "GeneralInduction.hpp"

namespace Inferences
{

using namespace Kernel;
using namespace Lib;
using namespace Shell;

TermList TermOccurrenceReplacement::transformSubterm(TermList trm)
{
  CALL("TermOccurrenceReplacement::transformSubterm");

  if (trm.isVar()) {
    return trm;
  }
  auto rIt = _r.find(trm.term());
  if (rIt != _r.end()) {
    auto oIt = _o._m.find(make_pair(_lit, trm.term()));
    ASS(oIt != _o._m.end());
    // if current bit is one, replace
    if (oIt->second.pop_last()) {
      return TermList(rIt->second, false);
    }
  }
  return trm;
}

TermList TermMapReplacement::transformSubterm(TermList trm)
{
  CALL("TermMapReplacement::transformSubterm");

  if (trm.isVar()) {
    return trm;
  }
  auto t = trm.term();
  ASS(!t->isLiteral());
  auto rIt = _r.find(t);
  if (rIt != _r.end()) {
    // if term needs to be replaced, get its sort and map it
    // to the next replacement term within that sort
    TermList srt = env.signature->getFunction(t->functor())->fnType()->result();
    auto oIt = _ord.find(t);
    if (oIt == _ord.end()) {
      oIt = _ord.insert(make_pair(t, _curr.at(srt)++)).first;
    }
    return TermList(_m.get(srt)[oIt->second]);
  }
  return trm;
}

ClauseIterator GeneralInduction::generateClauses(Clause* premise)
{
  CALL("GeneralInduction::generateClauses");

  InductionClauseIterator res;
  if (InductionHelper::isInductionClause(premise)) {
    for (unsigned i = 0; i < premise->length(); i++) {
      process(res, premise, (*premise)[i]);
    }
  }

  return pvi(res);
}

void filterSides(const InductionScheme& scheme, const vset<InductionPremise>& sides, bool allowOnlyBounds,
    OccurrenceMap& occMap, vset<pair<Literal*, Clause*>>& filteredSides) {
  // Retain side and bound literals for further processing if:
  // (1) they contain some induction term from the current scheme
  // (2) they are used as bounds, or they have either induction depth 0 or they contain some
  //     complex induction term (this has been partly checked in selectMainSidePairs but there
  //     we did not know yet whether there is a complex induction term)
  // (3) if the scheme is for integer induction, they are either valid induction literals,
  //     or bounds used by the scheme.
  // TODO(mhajdu,hzzv): experiment with relaxing constraint (2) for datatypes and tightening
  //                    it for integers.
  const bool isIntScheme = scheme.isInteger();
  for (const InductionPremise& s : sides) {
    bool filtered = true;
    const bool isBound = isIntScheme &&
        ((s.literal == scheme.bound1()) || (s.literal == scheme.optionalBound2()));
    for (const auto& kv : scheme.inductionTerms()) {
      TermList tl(kv.first);
      if (s.literal->containsSubterm(tl) &&
          (isBound || ((!skolem(kv.first) || !s.clause->inference().inductionDepth()) &&
                       (!isIntScheme || (!allowOnlyBounds && InductionHelper::isIntInductionTermListInLiteral(tl, s.literal)))))) {
        filteredSides.insert(make_pair(s.literal, s.clause));
        filtered = false;
        break;
      }
    }
    // update occurrence map
    if (filtered) {
      for (auto it = occMap._m.begin(); it != occMap._m.end();) {
        if (it->first.first == s.literal) {
          it = occMap._m.erase(it);
        } else {
          it++;
        }
      }
    }
  }
}

InferenceRule getGeneralizedRule(InferenceRule rule) {
  switch (rule) {
    case InferenceRule::INDUCTION_AXIOM:
    case InferenceRule::GEN_INDUCTION_AXIOM:
      return InferenceRule::GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INDUCTION_AXIOM:
    case InferenceRule::MC_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_INF_UP_INDUCTION_AXIOM:
    case InferenceRule::INT_INF_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::INT_INF_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INT_INF_UP_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_INF_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_INF_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_INF_DOWN_INDUCTION_AXIOM:
    case InferenceRule::INT_INF_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::INT_INF_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INT_INF_DOWN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_INF_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_INF_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_FIN_UP_INDUCTION_AXIOM:
    case InferenceRule::INT_FIN_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::INT_FIN_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INT_FIN_UP_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_FIN_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_FIN_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_FIN_DOWN_INDUCTION_AXIOM:
    case InferenceRule::INT_FIN_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::INT_FIN_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INT_FIN_DOWN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_FIN_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_FIN_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_DB_UP_INDUCTION_AXIOM:
    case InferenceRule::INT_DB_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::INT_DB_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INT_DB_UP_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_DB_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_DB_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_DB_DOWN_INDUCTION_AXIOM:
    case InferenceRule::INT_DB_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::INT_DB_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::MC_INT_DB_DOWN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_DB_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_DB_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::STRUCTURAL_INDUCTION_AXIOM:
    case InferenceRule::GEN_STRUCTURAL_INDUCTION_AXIOM:
      return InferenceRule::GEN_STRUCTURAL_INDUCTION_AXIOM;
    case InferenceRule::MC_STRUCTURAL_INDUCTION_AXIOM:
    case InferenceRule::MC_GEN_STRUCTURAL_INDUCTION_AXIOM:
      return InferenceRule::MC_GEN_STRUCTURAL_INDUCTION_AXIOM;
    case InferenceRule::RECURSION_INDUCTION_AXIOM:
    case InferenceRule::GEN_RECURSION_INDUCTION_AXIOM:
      return InferenceRule::GEN_RECURSION_INDUCTION_AXIOM;
    case InferenceRule::MC_RECURSION_INDUCTION_AXIOM:
    case InferenceRule::MC_GEN_RECURSION_INDUCTION_AXIOM:
      return InferenceRule::MC_GEN_RECURSION_INDUCTION_AXIOM;
    default:
      ASSERTION_VIOLATION;
  }
}

InferenceRule getMultiClauseRule(InferenceRule rule) {
  switch (rule) {
    case InferenceRule::INDUCTION_AXIOM:
    case InferenceRule::MC_INDUCTION_AXIOM:
      return InferenceRule::MC_INDUCTION_AXIOM;
    case InferenceRule::GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_INF_UP_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_INF_UP_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_INF_UP_INDUCTION_AXIOM;
    case InferenceRule::INT_INF_UP_GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_INF_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_INF_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_INF_DOWN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_INF_DOWN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_INF_DOWN_INDUCTION_AXIOM;
    case InferenceRule::INT_INF_DOWN_GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_INF_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_INF_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_FIN_UP_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_FIN_UP_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_FIN_UP_INDUCTION_AXIOM;
    case InferenceRule::INT_FIN_UP_GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_FIN_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_FIN_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_FIN_DOWN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_FIN_DOWN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_FIN_DOWN_INDUCTION_AXIOM;
    case InferenceRule::INT_FIN_DOWN_GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_FIN_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_FIN_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_DB_UP_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_DB_UP_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_DB_UP_INDUCTION_AXIOM;
    case InferenceRule::INT_DB_UP_GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_DB_UP_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_DB_UP_GEN_INDUCTION_AXIOM;
    case InferenceRule::INT_DB_DOWN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_DB_DOWN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_DB_DOWN_INDUCTION_AXIOM;
    case InferenceRule::INT_DB_DOWN_GEN_INDUCTION_AXIOM:
    case InferenceRule::MC_INT_DB_DOWN_GEN_INDUCTION_AXIOM:
      return InferenceRule::MC_INT_DB_DOWN_GEN_INDUCTION_AXIOM;
    case InferenceRule::STRUCTURAL_INDUCTION_AXIOM:
    case InferenceRule::MC_STRUCTURAL_INDUCTION_AXIOM:
      return InferenceRule::MC_STRUCTURAL_INDUCTION_AXIOM;
    case InferenceRule::GEN_STRUCTURAL_INDUCTION_AXIOM:
    case InferenceRule::MC_GEN_STRUCTURAL_INDUCTION_AXIOM:
      return InferenceRule::MC_GEN_STRUCTURAL_INDUCTION_AXIOM;
    case InferenceRule::RECURSION_INDUCTION_AXIOM:
    case InferenceRule::MC_RECURSION_INDUCTION_AXIOM:
      return InferenceRule::MC_RECURSION_INDUCTION_AXIOM;
    case InferenceRule::GEN_RECURSION_INDUCTION_AXIOM:
    case InferenceRule::MC_GEN_RECURSION_INDUCTION_AXIOM:
      return InferenceRule::MC_GEN_RECURSION_INDUCTION_AXIOM;
    default:
      ASSERTION_VIOLATION;
  }
}

void GeneralInduction::process(InductionClauseIterator& res, Clause* premise, Literal* literal)
{
  CALL("GeneralInduction::process");

  if(env.options->showInduction()){
    env.beginOutput();
    env.out() << "[Induction] process " << *literal << " in " << *premise << endl;
    env.endOutput();
  }

  vmap<InductionPremise, InductionPremises> premisePairs = selectPremises(literal, premise);

  for (auto& gen : _gen) {
    for (const auto& kv : premisePairs) {
      auto& ips = kv.second;
      auto main = ips.main();
      ASS(main.originalPremise || ips.sidesHaveOriginalPremise() || ips.boundsHaveOriginalPremise());
      ASS(!(main.originalPremise && ips.sidesHaveOriginalPremise()) &&
          !(main.originalPremise && ips.boundsHaveOriginalPremise()) &&
          !(ips.sidesHaveOriginalPremise() && ips.boundsHaveOriginalPremise()));
      if (!gen->usesBounds() && !main.originalPremise && !ips.sidesHaveOriginalPremise()) {
        // 'premise' is neither the main premise from 'ips' nor one of 'ips.sides'.
        // Since 'gen' does not use bounds, 'ips' is not valid for 'gen'.
        continue;
      }
      static vvector<pair<InductionScheme, OccurrenceMap>> schOccMap;
      schOccMap.clear();
      gen->generate(ips, schOccMap);

      vvector<pair<Literal*, vset<Literal*>>> schLits;
      for (auto& kv : schOccMap) {
        vset<pair<Literal*, Clause*>> sidesFiltered;
        filterSides(kv.first, ips.sides(), /*allowOnlyBounds=*/false, kv.second, sidesFiltered);
        if (!ips.bounds().empty()) filterSides(kv.first, ips.bounds(), /*allowOnlyBounds=*/true, kv.second, sidesFiltered);
        // Check whether we done this induction before. Since there can
        // be other induction schemes and literals that produce the same,
        // we add the new ones at the end
        schLits.emplace_back(nullptr, vset<Literal*>());
        if (alreadyDone(literal, sidesFiltered, kv.first, schLits.back())) {
          continue;
        }
        static const bool generalize = env.options->inductionGen();
        ScopedPtr<IteratorCore<OccurrenceMap>> g;
        if (generalize) {
          static const bool heuristic = env.options->inductionGenHeur();
          g = new GeneralizationIterator(kv.second, heuristic, gen->setsFixOccurrences());
        } else {
          g = new NoGeneralizationIterator(kv.second);
        }
        while (g->hasNext()) {
          auto eg = g->next();
          auto rule = kv.first.rule();
          if (g->hasNext()) {
            // except for the last generalization (always no
            // generalization), we mark every formula generalized
            rule = getGeneralizedRule(rule);
          }
          // create the generalized literals by replacing the current
          // set of occurrences of induction terms by the variables
          TermOccurrenceReplacement tr(kv.first.inductionTerms(), eg, main.literal);
          auto mainLitGen = tr.transformLit();
          ASS_NEQ(mainLitGen, main.literal); // main literal should be inducted on
          vvector<pair<Literal*, SLQueryResult>> sidesGeneralized;
          for (const auto& kv2 : sidesFiltered) {
            TermOccurrenceReplacement tr(kv.first.inductionTerms(), eg, kv2.first);
            auto sideLitGen = tr.transformLit();
            if (sideLitGen != kv2.first) { // side literals may be discarded if they contain no induction term occurrence
              sidesGeneralized.push_back(make_pair(sideLitGen, SLQueryResult(kv2.first, kv2.second)));
            }
          }
          if (!sidesGeneralized.empty()) {
            rule = getMultiClauseRule(rule);
          }
          generateClauses(kv.first, mainLitGen, SLQueryResult(main.literal, main.clause), std::move(sidesGeneralized), res._clauses, rule);
        }
      }
      for (const auto& schLit : schLits) {
        // if the pattern is already contained but we have a superset of its
        // side literals, we add the superset to cover as many as possible
        if (!_done.insert(schLit.first, schLit.second)) {
          auto curr = _done.get(schLit.first);
          if (includes(schLit.second.begin(), schLit.second.end(), curr.begin(), curr.end())) {
            _done.set(schLit.first, schLit.second);
          }
          // TODO(mhajdu): there can be cases where the current set of side literals
          // is not a superset of the already inducted on ones, in this case the new
          // ones are not added
        }
      }
    }
  }
}

void GeneralInduction::attach(SaturationAlgorithm* salg)
{
  CALL("GeneralInduction::attach");

  GeneratingInferenceEngine::attach(salg);
  _splitter=_salg->getSplitter();
  _index = static_cast<TermIndex *>(
      _salg->getIndexManager()->request(INDUCTION_SIDE_LITERAL_TERM_INDEX));
  // Indices for integer induction
  if (InductionHelper::isIntInductionOn()) {
    _comparisonIndex = static_cast<LiteralIndex*>(_salg->getIndexManager()->request(UNIT_INT_COMPARISON_INDEX));
  }
  if (InductionHelper::isIntInductionTwoOn()) {
    _inductionTermIndex = static_cast<TermIndex*>(_salg->getIndexManager()->request(INDUCTION_TERM_INDEX));
  }
  if (_comparisonIndex || _inductionTermIndex) {
    _helper = InductionHelper(_comparisonIndex, _inductionTermIndex, _splitter);
  }
}

void GeneralInduction::detach()
{
  CALL("GeneralInduction::detach");

  _index = 0;
  _salg->getIndexManager()->release(INDUCTION_SIDE_LITERAL_TERM_INDEX);
  if (InductionHelper::isIntInductionOn()) {
    _comparisonIndex = 0;
    _salg->getIndexManager()->release(UNIT_INT_COMPARISON_INDEX);
  }
  if (InductionHelper::isIntInductionTwoOn()) {
    _inductionTermIndex = 0;
    _salg->getIndexManager()->release(INDUCTION_TERM_INDEX);
  }
  _splitter=0;
  GeneratingInferenceEngine::detach();
}

// creates implication of the form (L1\theta & ... & Ln\theta) => ~L\theta
// where L1, ..., Ln are the side literals, L is the main literal and \theta is the substitution
Formula* createImplication(Literal* mainLit, const vvector<pair<Literal*, SLQueryResult>>& sideLitQrPairs, Substitution subst = Substitution()) {
  FormulaList* ll = FormulaList::empty();
  for (const auto& kv : sideLitQrPairs) {
    FormulaList::push(new AtomicFormula(kv.first->apply(subst)), ll);
  }
  Formula* left = 0;
  if (FormulaList::isNonEmpty(ll)) {
    left = JunctionFormula::generalJunction(Connective::AND, ll);
  }
  Formula* right = new AtomicFormula(Literal::complementaryLiteral(mainLit->apply(subst)));
  return left ? new BinaryFormula(Connective::IMP, left, right) : right;
}

void GeneralInduction::generateClauses(
  const Shell::InductionScheme& scheme,
  Literal* mainLit, SLQueryResult mainQuery,
  vvector<pair<Literal*, SLQueryResult>> sideLitQrPairs,
  ClauseStack& clauses, InferenceRule rule)
{
  CALL("GeneralInduction::generateClauses");

  static const bool indhrw = env.options->inductionHypRewriting();
  static const bool indmc = env.options->inductionMultiClause();
  static const unsigned less = env.signature->getInterpretingSymbol(Theory::INT_LESS);
  const bool intind = scheme.isInteger();

  if (env.options->showInduction()){
    env.beginOutput();
    env.out() << "[Induction] generating from scheme " << scheme
              << " with generalized literals " << *mainLit << ", ";
    for (const auto& kv : sideLitQrPairs) {
      env.out() << *kv.first << ", ";
    }
    env.out() << endl;
    env.endOutput();
  }

  vvector<pair<Literal*, SLQueryResult>> regularSideLitQrPairs;
  vvector<pair<Literal*, SLQueryResult>> boundLitQrPairs;
  for (const auto& p : sideLitQrPairs) {
    if (intind &&
        ((p.second.literal == scheme.bound1()) || (p.second.literal == scheme.optionalBound2()))) {
      boundLitQrPairs.push_back(p);
    } else {
      regularSideLitQrPairs.push_back(p);
    }
  }
  ASS(!intind || (scheme.bound1() != nullptr));
  if (intind && scheme.isDefaultBound() && boundLitQrPairs.empty()) {
    ASS(scheme.optionalBound2() == nullptr);
    // Create the bound literal for the default bound (as given in scheme.bound1()).
    const bool upward = scheme.isUpward();
    static TermList v0(0, false);
    TermList zero = *scheme.bound1()->nthArgument(upward ? 1 : 0);
    boundLitQrPairs.emplace_back(Literal::create2(less, /*polarity=*/false, upward ? v0 : zero, upward ? zero : v0),
        SLQueryResult(scheme.bound1(), /*clause=*/nullptr));
  } 

  vset<unsigned> hypVars;
  FormulaList* cases = FormulaList::empty();
  TermList t;
  for (const auto& c : scheme.cases()) {
    FormulaList* ll = FormulaList::empty();
    for (auto& r : c._recursiveCalls) {
      Formula* f = createImplication(mainLit, regularSideLitQrPairs, r);
      FormulaList::push(f, ll);
      // save all free variables of the hypotheses -- these are used
      // to mark the clauses as hypotheses and corresponding conclusion
      if ((indhrw && mainLit->isEquality()) || (indmc && !mainLit->isEquality())) {
        FormulaVarIterator fvit(f);
        while (fvit.hasNext()) {
          hypVars.insert(fvit.next());
        }
      }
    }
    Formula* right = createImplication(mainLit, regularSideLitQrPairs, c._step);
    Formula* left = 0;
    if (FormulaList::isNonEmpty(ll)) {
      left = JunctionFormula::generalJunction(Connective::AND, ll);
    }
    auto caseFormula = left ? new BinaryFormula(Connective::IMP, left, right) : right;
    FormulaList* cl = FormulaList::empty();
    Formula* caseBound = 0;
    if (intind && (c._recursiveCalls.size() == 1)) {
      // Integer induction schemes require the non-base cases to be guarded by the case bounds.
      Substitution sub(c._recursiveCalls[0]);
      for (const auto& p : boundLitQrPairs) {
        Literal* l = p.first->apply(sub);
        Literal* l2;
        // TODO(hzzv): use more sophisticated logic for creating bounds (later)
        if (p.second.literal == scheme.bound1()) {
          if (l->isNegative()) l2 = l;
          else l2 = Literal::create2(less, /*polarity=*/false, *l->nthArgument(1), *l->nthArgument(0));
        } else {
          ASS_EQ(p.second.literal, scheme.optionalBound2());
          if (l->isPositive()) l2 = l;
          else l2 = Literal::create2(less, /*polarity=*/true, *l->nthArgument(1), *l->nthArgument(0));
        }
        ASS(l2 != nullptr);
        FormulaList::push(new AtomicFormula(l2), cl);
      }
    }
    if (FormulaList::isNonEmpty(cl)) caseBound = JunctionFormula::generalJunction(Connective::AND, cl);
    Formula* f = caseBound ? new BinaryFormula(Connective::IMP, caseBound, caseFormula) : caseFormula;
    FormulaList::push(Formula::quantify(f), cases);
  }

  // create the substitution that will be used for binary resolution
  // this is basically the reverse of the induction term map
  ASS(FormulaList::isNonEmpty(cases));
  RobSubstitution subst;
  for (const auto& kv : scheme.inductionTerms()) {
    ALWAYS(subst.match(TermList(kv.second, false), 0, TermList(kv.first), 1));
  }
  Formula* conclusionBound = 0;
  if (intind && scheme.isDefaultBound()) {
    // If the scheme uses default bound, we need to add it to the conclusion manually
    // (it is not included in the sideLitQrPairs).
    ASS(boundLitQrPairs.size() == 1)
    conclusionBound = new AtomicFormula(boundLitQrPairs[0].first);
  }
  Formula* conclusion = createImplication(mainLit, sideLitQrPairs);
  Formula* hypothesis = new BinaryFormula(Connective::IMP,
    JunctionFormula::generalJunction(Connective::AND, cases),
    Formula::quantify(conclusionBound ? new BinaryFormula(Connective::IMP, conclusionBound, conclusion) : conclusion));
  // cout << *hypothesis << endl;

  NewCNF cnf(0);
  cnf.setForInduction();
  Stack<Clause*> hyp_clauses;
  Inference inf = NonspecificInference0(UnitInputType::AXIOM,rule);
  unsigned maxDepth = mainQuery.clause->inference().inductionDepth();
  for (const auto& kv : sideLitQrPairs) {
    maxDepth = max(maxDepth, kv.second.clause->inference().inductionDepth());
  }
  inf.setInductionDepth(maxDepth+1);
  auto fu = new FormulaUnit(hypothesis,inf);
  cnf.clausify(NNF::ennf(fu), hyp_clauses);
  DHMap<unsigned,unsigned> rvs;
  if ((indhrw && mainLit->isEquality()) || (indmc && !mainLit->isEquality())) {
    // NewCNF creates a mapping from newly introduced Skolem symbols
    // to the variables before Skolemization. We need the reverse of
    // this, but we double check that it is a bijection.
    // In other cases it may be non-bijective, but here it should be.
    rvs.loadFromInverted(cnf.getSkFunToVarMap());
  }
  DHSet<unsigned> info;
  for (const auto& v : hypVars) {
    info.insert(rvs.get(v));
  }
  vset<unsigned> oldSk = InductionHelper::collectInductionSkolems(mainQuery.literal, mainQuery.clause);
  for (const auto& kv : sideLitQrPairs) {
    auto oldSkSide = InductionHelper::collectInductionSkolems(kv.second.literal, kv.second.clause);
    oldSk.insert(oldSkSide.begin(), oldSkSide.end());
  }

  // Resolve all induction clauses with the main and side literals
  auto resSubst = ResultSubstitution::fromSubstitution(&subst, 0, 1);
  mainQuery.substitution = resSubst;
  // Be aware that we change mainLit and sideLitQrPairs here irreversibly
  mainLit = Literal::complementaryLiteral(mainLit);
  for (auto& kv : sideLitQrPairs) {
    ASS(kv.second.clause != nullptr);
    kv.first = Literal::complementaryLiteral(subst.apply(kv.first, 0));
    kv.second.substitution = resSubst;
  }
  ClauseStack::Iterator cit(hyp_clauses);
  while(cit.hasNext()){
    Clause* c = cit.next();
    for (unsigned i = 0; i < c->length(); i++) {
      auto sk = InductionHelper::collectInductionSkolems((*c)[i], c, &info);
      for (const auto& v : sk) {
        c->inference().addToInductionInfo(v);
      }
    }
    c = applyBinaryResolutionAndCallSplitter(c, mainLit, mainQuery,
            /*splitterCondition=*/ !sideLitQrPairs.empty());
    unsigned i = 0;
    for (const auto& kv : sideLitQrPairs) {
      c = applyBinaryResolutionAndCallSplitter(c, kv.first, kv.second,
            /*splitterCondition=*/ ++i < sideLitQrPairs.size());
    }
    if(env.options->showInduction()){
      env.beginOutput();
      env.out() << "[Induction] generate " << c->toString() << endl;
      env.endOutput();
    }
    for (const auto& v : oldSk) {
      c->inference().removeFromInductionInfo(v);
    }
    clauses.push(c);
  }
  env.statistics->induction++;
}

Clause* GeneralInduction::applyBinaryResolutionAndCallSplitter(Clause* c, Literal* l, const SLQueryResult& slqr, bool splitterCondition) {
  Clause* res = BinaryResolution::generateClause(c, l, slqr, *env.options);
  ASS(res);
  if (_splitter && splitterCondition) {
    _splitter->onNewClause(res);
  }
  return res;
}

void reserveBlanksForScheme(const InductionScheme& sch, DHMap<TermList, vvector<Term*>>& blanks)
{
  vmap<TermList, unsigned> srts;
  // count sorts in induction terms
  for (const auto& kv : sch.inductionTerms()) {
    TermList srt = env.signature->getFunction(kv.first->functor())->fnType()->result();
    auto res = srts.insert(make_pair(srt,1));
    if (!res.second) {
      res.first->second++;
    }
  }
  // introduce as many blanks for each sort as needed
  for (const auto kv : srts) {
    if (!blanks.find(kv.first)) {
      blanks.insert(kv.first, vvector<Term*>());
    }
    auto& v = blanks.get(kv.first);
    v.reserve(kv.second);
    while (v.size() < kv.second) {
      unsigned fresh = env.signature->addFreshFunction(0, "blank");
      env.signature->getFunction(fresh)->setType(OperatorType::getConstantsType(kv.first));
      v.push_back(Term::createConstant(fresh));
    }
  }
}

bool GeneralInduction::alreadyDone(Literal* mainLit, const vset<pair<Literal*,Clause*>>& sides,
  const InductionScheme& sch, pair<Literal*,vset<Literal*>>& res)
{
  CALL("GeneralInduction::alreadyDone");

  // Instead of relying on the order within the induction term set, we map induction terms
  // to blanks based on their first occurrences within the literal to avoid creating different
  // blanks for the same literal pattern. E.g. if we have a saved pattern leq(blank0,blank1)
  // and a new literal leq(sk1,sk0) should be inducted upon with induction terms { sk0, sk1 },
  // instead of using the order within the set to get the different leq(blank1,blank0) essentially
  // for the same pattern, since sk1 is the first within the literal, we map this to
  // leq(blank0,blank1) and we detect that it was already inducted upon in this form

  // introduce the blanks
  static DHMap<TermList, vvector<Term*>> blanks;
  reserveBlanksForScheme(sch, blanks);

  // place the blanks in main literal
  TermMapReplacement cr(blanks, sch.inductionTerms());
  res.first = cr.transform(mainLit);

  // place the blanks in sides (using the now fixed order from main literal)
  for (const auto& kv : sides) {
    res.second.insert(cr.transform(kv.first));
  }
  // check already existing pattern for main literal
  if (!_done.find(res.first)) {
    return false;
  }
  auto s = _done.get(res.first);
  // check if the sides for the new pattern are included in the existing one
  // TODO(hzzv): add conditions for integer induction (later)
  if (includes(s.begin(), s.end(), res.second.begin(), res.second.end())) {
    if (env.options->showInduction()) {
      env.beginOutput();
      env.out() << "[Induction] already inducted on " << *mainLit << " in " << *res.first << " form" << endl;
      env.endOutput();
    }
    return true;
  }
  return false;
}

// Returns a vector of InductionPremises, where each InductionPremises contains the main
// premise, side premises and bounds.
// It is guaranteed that the main premise is not contained in either side premises or bounds,
// and that sides and bounds are disjoint. However, that means that literals useable as bounds
// might be contained in sides (if indmc is on).
vmap<InductionPremise, InductionPremises> GeneralInduction::selectPremises(Literal* literal, Clause* premise)
{
  CALL("GeneralInduction::selectPremises");

  vmap<InductionPremise, InductionPremises> res;
  static const bool indmc = env.options->inductionMultiClause();
  static const bool intInd = InductionHelper::isIntInductionOn();
  static const bool finInterval = InductionHelper::isInductionForFiniteIntervalsOn();
  const bool isPremiseComparison = InductionHelper::isIntegerComparison(premise); 

  // TODO(mhajdu): is there a way to duplicate these iterators?
  TermQueryResultIterator sidesIt = TermQueryResultIterator::getEmpty();
  TermQueryResultIterator boundsIt = TermQueryResultIterator::getEmpty();
  if ((indmc || intInd) && InductionHelper::isSideLiteral(literal, premise))
  {
    NonVariableIterator nvi(literal);
    DHSet<TermList> skolems;
    DHSet<TermList> ints;
    while (nvi.hasNext()) {
      auto st = nvi.next();
      auto fn = st.term()->functor();
      if (indmc && InductionHelper::isStructInductionFunctor(fn)) skolems.insert(st);
      if (intInd && env.signature->getFunction(fn)->fnType()->result() == Term::intSort()) ints.insert(st);
    }
    DHSet<TermList>::Iterator skit(skolems);
    while (skit.hasNext()) {
      auto st = skit.next();
      sidesIt = pvi(getConcatenatedIterator(sidesIt, _index->getGeneralizations(st)));
    }
    DHSet<TermList>::Iterator iit(ints);
    while (iit.hasNext()) {
      auto st = iit.next();
      if (!indmc && isPremiseComparison && InductionHelper::isIntInductionTwoOn() &&
          InductionHelper::isIntegerBoundLiteral(st, literal)) {
        // Fetch integer induction literals for 'st' (bounded by 'premise').
        // (If indmc is on, these literals were already fetched above.)
        sidesIt = pvi(getConcatenatedIterator(sidesIt, _helper.getTQRsForInductionTerm(st)));
      }
      if (InductionHelper::isIntInductionOneOn() && InductionHelper::isIntInductionTermListInLiteral(st, literal)) {
        // Fetch bounds for the term st for integer induction.
        Term* t = st.term();
        boundsIt = pvi(getConcatenatedIterator(boundsIt,
            getConcatenatedIterator(getConcatenatedIterator(_helper.getLess(t), _helper.getLessEqual(t)),
                                    getConcatenatedIterator(_helper.getGreater(t), _helper.getGreaterEqual(t)))));
      }
    }
  }

  // pair current literal as main literal with possible side literals
  // this results in any number of side literals
  const bool indLit = InductionHelper::isInductionLiteral(literal);
  InductionPremise mainPremise(literal, premise, /*originalPremise=*/true);
  if (indLit) {
    // first InductionPremises in result always uses the current premise as the main literal
    res.insert(make_pair(mainPremise, InductionPremises(mainPremise)));
  }
  while (sidesIt.hasNext()) {
    auto qr = sidesIt.next();
    // query is side literal
    if (indLit && indmc) {
      res.at(mainPremise).addSidePremise(qr.literal, qr.clause);
    }
    if ((qr.literal == literal && qr.clause == premise) || !InductionHelper::isInductionLiteral(qr.literal)) {
      continue;
    }
    // query is main literal
    TermList& st = qr.term;
    const bool premiseIsLeftBound = isPremiseComparison && (st == *literal->nthArgument(0));
    const bool premiseIsRightBound = isPremiseComparison && (st == *literal->nthArgument(1));
    const bool intIndPair = intInd && InductionHelper::isIntegerBoundLiteral(st, literal) &&
        (premiseIsLeftBound || premiseIsRightBound) && InductionHelper::isIntInductionTermListInLiteral(st, qr.literal);
    const bool indmcPair = indmc && InductionHelper::isMainSidePair(qr.literal, qr.clause, literal, premise);
    if (intIndPair || indmcPair)
    {
      InductionPremise qrPremise(qr.literal, qr.clause);
      auto resIt = res.find(qrPremise);
      if (resIt == res.end()) {
        resIt = res.insert(make_pair(qrPremise, InductionPremises(qrPremise))).first;
      }
      auto& premises = resIt->second;
      if (indmcPair) {
        if (premises.addSidePremise(literal, premise, /* originalPremise= */true)) {
          // add side literals other than the input
          TermQueryResultIterator sideIt2 = _index->getGeneralizations(st);
          while (sideIt2.hasNext()) {
            auto qrSide = sideIt2.next();
            premises.addSidePremise(qrSide.literal, qrSide.clause);
          }
        }
      } else { // intIndPair must be true
        // in case that literal/premise wasn't already added as side, add it as bound
        premises.addBound(literal, premise, true);
      }
      if (intIndPair && finInterval) {
        // add bound literals other than the input and side literals
        TermQueryResultIterator boundIt2 = TermQueryResultIterator::getEmpty();
        // We use the premise as a bound for integer induction. Fetch other bounds:
        Term* t = st.term();
        if (literal->isPositive() == premiseIsLeftBound) {
          // 'st' is smaller than the bound (the bound is upper). Fetch the lower bound for 'st'.
          boundIt2 = pvi(getConcatenatedIterator(_helper.getLess(t), _helper.getLessEqual(t)));
        } else {
          // 'st' is greater than the bound (the bound is lower). Fetch the upper bound for 'st'.
          boundIt2 = pvi(getConcatenatedIterator(_helper.getGreater(t), _helper.getGreaterEqual(t)));
        }
        while (boundIt2.hasNext()) {
          auto qrSide = boundIt2.next();
          premises.addBound(qrSide.literal, qrSide.clause);
        }
      }
    }
  }
  // Finally, add bounds to the first InductionPremises (if they aren't already present as sides).
  if (indLit) {
    while (boundsIt.hasNext()) {
      auto qr = boundsIt.next();
      res.at(mainPremise).addBound(qr.literal, qr.clause);
    }
  }
  return res;
}

}
