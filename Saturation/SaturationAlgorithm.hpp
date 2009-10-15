/**
 * @file SaturationAlgorithm.hpp
 * Defines class SaturationAlgorithm
 *
 */

#ifndef __SaturationAlgorithm__
#define __SaturationAlgorithm__

#include "../Forwards.hpp"

#include "../Kernel/Clause.hpp"

#include "../Lib/DHMap.hpp"
#include "../Lib/Event.hpp"
#include "../Lib/List.hpp"

#include "../Indexing/IndexManager.hpp"

#include "../Inferences/InferenceEngine.hpp"
#include "../Inferences/PropositionalToBDDISE.hpp"

#include "Limits.hpp"
#include "SaturationResult.hpp"
#include "Splitter.hpp"

#if VDEBUG
#include<iostream>
#endif

namespace Saturation
{

using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Inferences;

class SaturationAlgorithm
{
public:
  SaturationAlgorithm(PassiveClauseContainerSP passiveContainer, LiteralSelectorSP selector);
  virtual ~SaturationAlgorithm();

  void setGeneratingInferenceEngine(GeneratingInferenceEngineSP generator);
  void setImmediateSimplificationEngine(ImmediateSimplificationEngineSP immediateSimplifier);


  void setFwDemodulator(ForwardSimplificationEngineSP fwDemodulator);
  void addForwardSimplifierToFront(ForwardSimplificationEngineSP fwSimplifier);
  void addBackwardSimplifierToFront(BackwardSimplificationEngineSP bwSimplifier);

  virtual SaturationResult saturate() = 0;

  void addInputClauses(ClauseIterator cit);

  virtual ClauseContainer* getSimplificationClauseContainer() = 0;
  virtual ClauseContainer* getGenerationClauseContainer() = 0;

  Limits* getLimits() { return &_limits; }
  IndexManager* getIndexManager() { return &_imgr; }

  static SaturationAlgorithmSP createFromOptions();

protected:
  virtual void addInputSOSClause(Clause*& cl);

  void addUnprocessedClause(Clause* cl);

  bool isRefutation(Clause* c);
  bool forwardSimplify(Clause* c);
  void backwardSimplify(Clause* c);
  void addToPassive(Clause* c);
  void reanimate(Clause* c);
  void activate(Clause* c);

  void onActiveAdded(Clause* c);
  virtual void onActiveRemoved(Clause* c);
  void onPassiveAdded(Clause* c);
  virtual void onPassiveRemoved(Clause* c);
  void onPassiveSelected(Clause* c);
  void onUnprocessedAdded(Clause* c);
  void onUnprocessedRemoved(Clause* c);
  void onUnprocessedSelected(Clause* c);
  void onNewClause(Clause* c);
  void onNewUsefulPropositionalClause(Clause* c);
  void onSymbolElimination(Color eliminated, Clause* c, bool nonRedundant=false);
  void onClauseRewrite(Clause* from, Clause* to, bool forward=true, Clause* premise=0);
  void onNonRedundantClause(Clause* c);

  void outputSymbolElimination(Color eliminated, Clause* c);

  void onAllProcessed();

  void handleSaturationStart();
  int elapsedTime();


private:
  void passiveRemovedHandler(Clause* cl);
  void activeRemovedHandler(Clause* cl);

  void addInputClause(Clause* cl);
  void addUnprocessedFinalClause(Clause* cl);
  Clause* handleEmptyClause(Clause* cl);

  Clause* doImmediateSimplification(Clause* cl);

  void performEmptyClauseSubsumption(Clause* cl, BDDNode* emptyClauseProp);

  void removeBackwardSimplifiedClause(Clause* cl);

  void checkForPreprocessorSymbolElimination(Clause* cl);

  Limits _limits;
  IndexManager _imgr;

  class TotalSimplificationPerformer;
  class PartialSimplificationPerformer;
protected:

  int _startTime;
  bool _performSplitting;
  bool _clauseActivationInProgress;

  Stack<Clause*> _postponedClauseRemovals;

  UnprocessedClauseContainer* _unprocessed;
  PassiveClauseContainerSP _passive;
  ActiveClauseContainer* _active;

  GeneratingInferenceEngineSP _generator;
  ImmediateSimplificationEngineSP _immediateSimplifier;

  typedef List<ForwardSimplificationEngineSP> FwSimplList;
  FwSimplList* _fwSimplifiers;
  ForwardSimplificationEngineSP _fwDemodulator;

  typedef List<BackwardSimplificationEngineSP> BwSimplList;
  BwSimplList* _bwSimplifiers;

  LiteralSelectorSP _selector;

  Splitter _splitter;
  PropositionalToBDDISE _propToBDD;

  DHMap<Clause*,Clause*> _symElRewrites;
  DHMap<Clause*,Color> _symElColors;

  SubscriptionData _passiveContRemovalSData;
  SubscriptionData _activeContRemovalSData;
};


};

#endif /*__SaturationAlgorithm__*/
