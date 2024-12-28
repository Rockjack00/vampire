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
 * @file NeuralPassiveClauseContainer.cpp
 * Implements class NeuralPassiveClauseContainer and co.
 * @since 30/12/2007 Manchester
 */

#define USING_LIBTORCH // see Lib/Output.hpp

#include <cmath>
#include <climits>

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Environment.hpp"
#include "Lib/Int.hpp"
#include "Lib/Timer.hpp"
#include "Lib/Random.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/SoftmaxClauseQueue.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/Options.hpp"

#include "SaturationAlgorithm.hpp"
#include "Splitter.hpp"

#if VDEBUG
#include <iostream>
#endif

#include "NeuralPassiveClauseContainers.hpp"

#define DEBUG_MODEL 0
#include <torch/utils.h>

namespace Saturation
{
using namespace std;
using namespace Lib;
using namespace Kernel;

NeuralClauseEvaluationModel::NeuralClauseEvaluationModel(const std::string clauseEvalModelFilePath,
  // const std::string& tweak_str,
  uint64_t random_seed, unsigned num_cl_features, float temperature) :
  _numFeatures(num_cl_features), _temp(temperature)
{
  TIME_TRACE("neural model warmup");

#if DEBUG_MODEL
  auto start = env.timer->elapsedMilliseconds();
#endif

  // seems to be making this nicely single-threaded
  at::set_num_threads(1);
  at::set_num_interop_threads(1);

  torch::manual_seed(random_seed);

  _model = torch::jit::load(clauseEvalModelFilePath);
  _model.eval();

  /*
  if (!tweak_str.empty()) {
    if (auto m = _model.find_method("eatMyTweaks")) { // if the model is not interested in tweaks, it will get none!
      std::vector<float> tweaks;

      std::size_t i=0,j;
      while (true) {
        j = tweak_str.find_first_of(',',i);

        auto t = tweak_str.substr(i,j-i);
        if (t.empty()) {
          break;
        }

        float nextVal;
        ALWAYS(Int::stringToFloat(t.c_str(),nextVal));
        tweaks.push_back(nextVal);

        if (j == std::string::npos) {
          break;
        }

        i = j+1;
      }

      std::vector<torch::jit::IValue> inputs;
      inputs.push_back(torch::jit::IValue(torch::from_blob(tweaks.data(), {static_cast<long long>(tweaks.size())}, torch::TensorOptions().dtype(torch::kFloat32))));
      (*m)(std::move(inputs));
    }
  }
  */

#if DEBUG_MODEL
  cout << "Model loaded in " << env.timer->elapsedMilliseconds() - start << " ms" << endl;
  cout << at::get_parallel_info() << endl;
#endif

  _useSimpleFeatures = useSimpleFeatures();
  if (!_useSimpleFeatures) {
    _numFeatures = 0;
  }

  _gageEmbeddingSize = gageEmbeddingSize();
  _gageRuleEmbed = _model.attr("gage_rule_embed").toModule().attr("weight").toTensor();
  _gageCombine = _model.attr("gage_combine").toModule();

  _gweightEmbeddingSize = gweightEmbeddingSize();
  _gweightVarEmbed = _model.attr("gweight_var_embed").toModule().attr("weight").toTensor();
  _gweightTermCombine = _model.attr("gweight_term_combine").toModule();

  _evalClauses = _model.find_method("eval_clauses");
}

float NeuralClauseEvaluationModel::tryGetScore(Clause* cl) {
  float* someVal = _scores.findPtr(cl->number());
  if (someVal) {
    return *someVal;
  }
  // a very optimistic constant (since large is good)
  return std::numeric_limits<float>::max();
}

float NeuralClauseEvaluationModel::evalClause(Clause* cl) {
  float* someVal = _scores.findPtr(cl->number());
  if (someVal) {
    return *someVal;
  }

  float logit;
  {
    TIME_TRACE("neural model evaluation");

    std::vector<torch::jit::IValue> inputs;

    std::vector<float> features(_numFeatures);
    unsigned i = 0;
    Clause::FeatureIterator it(cl);
    while (i < _numFeatures && it.hasNext()) {
      features[i] = it.next();
      i++;
    }
    ASS_EQ(features.size(),_numFeatures);
    inputs.push_back(torch::from_blob(features.data(), {_numFeatures}, torch::TensorOptions().dtype(torch::kFloat32)));

    logit = _model.forward(std::move(inputs)).toTensor().item().toDouble();
  }

  if (_temp > 0.0) {
    // adding the gumbel noise
    logit += -_temp*log(-log(Random::getFloat(0.0,1.0)));
  }

  // cout << "New clause has " << res << " with number " << cl->number() << endl;
  _scores.insert(cl->number(),logit);
  return logit;
}

void NeuralClauseEvaluationModel::gageEmbedPending()
{
  torch::NoGradGuard no_grad; // TODO: check if this is necessary here
  /*
  for todos in self.gage_todo_layers:
    ruleIdxs: list[int] = [] # into gage_rule_embed
    mainPrems = []
    otherPrems = []
    for clNum,infRule,parents in todos:
      ruleIdxs.append(infRule)
      mainPrems.append(self.gage_embed_store[parents[0]])
      if len(parents) == 1:
        otherPrems.append(torch.zeros(HP.GAGE_EMBEDDING_SIZE))
      elif len(parents) == 2:
        otherPrems.append(self.gage_embed_store[parents[1]])
      else:
        # this would work even in the binary case, but let's not invoke the monster if we don't need to
        otherPrem = torch.sum(torch.stack([self.gage_embed_store[parents[p]] for p in parents[1:]]),dim=0)/(len(parents)-1)
        otherPrems.append(otherPrem)
    ruleEbeds = self.gage_rule_embed(torch.tensor(ruleIdxs))
    mainPremEbeds = torch.stack(mainPrems)
    otherPremEbeds = torch.stack(otherPrems)
  */
  for (int64_t i = 0; i < static_cast<int64_t>(_gageTodoLayers.size()); i++) {
    Stack<std::tuple<Clause*,std::vector<int64_t>>>& todos = _gageTodoLayers[i];
    auto rect = torch::empty({static_cast<int64_t>(todos.size()), 3*_gageEmbeddingSize}, torch::TensorOptions().dtype(torch::kFloat32));
    {
      auto it = todos.iterFifo();
      int64_t j = 0;
      while (it.hasNext()) {
        const auto& [c,parents] = it.next();
        auto ruleIdx = (int64_t)toNumber(c->inference().rule());
        rect.index_put_({j, torch::indexing::Slice(0, _gageEmbeddingSize)}, _gageRuleEmbed.index({ruleIdx}));
        rect.index_put_({j, torch::indexing::Slice(1*_gageEmbeddingSize, 2*_gageEmbeddingSize)}, _gageEmbedStore.get(parents[0]));
        int64_t k = 1;
        auto remainingPremisesEmbedSum = torch::zeros({_gageEmbeddingSize});
        while (k < parents.size()) {
          remainingPremisesEmbedSum += _gageEmbedStore.get(parents[k++]);
        }
        k--; // now it reflect the number of parents actually summed up in remainingPremisesEmbedSum
        if (k > 1) {
          rect.index_put_({j, torch::indexing::Slice(2*_gageEmbeddingSize, 3*_gageEmbeddingSize)}, remainingPremisesEmbedSum/k);
        } else {
          rect.index_put_({j, torch::indexing::Slice(2*_gageEmbeddingSize, 3*_gageEmbeddingSize)}, remainingPremisesEmbedSum);
        }

        j++;
      }
    }
    /*
      res = self.gage_combine(torch.cat((ruleEbeds, mainPremEbeds, otherPremEbeds), dim=1))
      for j,(clNum,_,_) in enumerate(todos):
        self.gage_embed_store[clNum] = res[j]
    */
    auto res = _gageCombine.forward({rect}).toTensor();
    {
      auto it = todos.iterFifo();
      int64_t j = 0;
      while (it.hasNext()) {
        _gageEmbedStore.insert(std::get<0>(it.next())->number(),res.index({j}));
      }
      j++;
    }
    List<torch::Tensor>::push(res, _laterGageResults); // just to prevent garbage collector from deleting too early
  }
  /*
    self.gage_cur_base_layer += len(self.gage_todo_layers)
    empty_todo_layers: list[list[Tuple[int,int,list[int]]]] = []
    self.gage_todo_layers = empty_todo_layers
  */
  _gageCurBaseLayer += _gageTodoLayers.size();
  _gageTodoLayers.reset();
}

torch::Tensor NeuralClauseEvaluationModel::getSubtermEmbed(int64_t id) {
  /*
  if id < 0:
    return self.gweight_var_embed(torch.tensor([id % HP.GWEIGHT_NUM_VAR_EMBEDS]))[0]
  else:
    return self.gweight_term_embed_store[id]
  */
  if (id < 0) {
    return _gweightVarEmbed[0]; // only using 1 var embed now
  } else {
    return _gweightTermEmbedStore.get(id);
  }
}

void NeuralClauseEvaluationModel::gweightEmbedPending() {
  torch::NoGradGuard no_grad; // TODO: check if this is necessary here

  /*
  # first like what gage does with clause, but here with terms
  for todos in self.gweight_todo_layers:
    functors = []
    signs = []
    first_args = []
    other_args = []
    for id,functor,sign,args in todos:
      functors.append(self.gweight_symbol_embeds[functor])
      signs.append(torch.tensor([sign]))
      if len(args) == 0:
        first_args.append(torch.zeros(HP.GWEIGHT_EMBEDDING_SIZE))
        other_args.append(torch.zeros(HP.GWEIGHT_EMBEDDING_SIZE))
      else:
        first_args.append(self.get_subterm_embed(args[0]))
        if len(args) == 1:
          other_args.append(torch.zeros(HP.GWEIGHT_EMBEDDING_SIZE))
        else:
          other_arg = torch.sum(torch.stack([self.get_subterm_embed(a) for a in args[1:]]),dim=0)/(len(args)-1)
          other_args.append(other_arg)
  */
  for (int64_t i = 0; i < static_cast<int64_t>(_gweightTodoLayers.size()); i++) {
    Stack<std::tuple<int64_t,unsigned,float,std::vector<int64_t>>>& todos = _gweightTodoLayers[i];
    auto rect = torch::empty({static_cast<int64_t>(todos.size()), 1+3*_gweightEmbeddingSize}, torch::TensorOptions().dtype(torch::kFloat32));

    auto it = todos.iterFifo();
    int64_t j = 0;
    while (it.hasNext()) {
      const auto& [id,functor,sign,args] = it.next();
      rect.index_put_({j, torch::indexing::Slice(0, _gweightEmbeddingSize)}, _gweightSymbolEmbeds.index({(int64_t)functor}));
      rect.index_put_({j, _gweightEmbeddingSize}, sign);
      if (args.size() == 0) {
        rect.index_put_({j, torch::indexing::Slice(1+_gweightEmbeddingSize, 1+3*_gweightEmbeddingSize)}, torch::zeros({2*_gweightEmbeddingSize}));
      } else {
        rect.index_put_({j, torch::indexing::Slice(1+_gweightEmbeddingSize, 1+2*_gweightEmbeddingSize)}, getSubtermEmbed(args[0]));
        int64_t k = 1;
        auto remainingArgsEmbedSum = torch::zeros({_gweightEmbeddingSize});
        while (k < args.size()) {
          remainingArgsEmbedSum += getSubtermEmbed(args[k++]);
        }
        k--; // now it reflect the number of args actually summed up in remainingArgsEmbedSum
        if (k > 1) {
          rect.index_put_({j, torch::indexing::Slice(1+2*_gweightEmbeddingSize, 1+3*_gweightEmbeddingSize)}, remainingArgsEmbedSum/k);
        } else {
          rect.index_put_({j, torch::indexing::Slice(1+2*_gweightEmbeddingSize, 1+3*_gweightEmbeddingSize)}, remainingArgsEmbedSum);
        }
      }
      j++;
    }
    /*
      res = self.gweight_term_combine(torch.cat((torch.stack(functors), torch.stack(signs), torch.stack(first_args), torch.stack(other_args)), dim=1))
      for j,(id,_,_,_) in enumerate(todos):
      self.gweight_term_embed_store[id] = res[j]
    */
    auto res = _gweightTermCombine.forward({rect}).toTensor();
    {
      auto it = todos.iterFifo();
      int64_t j = 0;
      while (it.hasNext()) {
        _gweightTermEmbedStore.insert(std::get<0>(it.next()),res.index({j}));
      }
      j++;
    }
    List<torch::Tensor>::push(res, _gweightResults); // just to prevent garbage collector from deleting too early
  }
  /*
    self.gweight_cur_base_layer += len(self.gweight_todo_layers)
    empty_todo_layers: list[list[Tuple[int,int,float,list[int]]]] = []
    self.gweight_todo_layers = empty_todo_layers
  */
  _gweightCurBaseLayer += _gweightTodoLayers.size();
  _gweightTodoLayers.reset();

  /*
    # second, do the clauses part
    for j,(cl_num,lits) in enumerate(self.gweight_clause_todo):
      lit_embeds = torch.stack([self.gweight_term_embed_store[lit] for lit in lits])
      # TODO: try: avg over lits, max over lits, attention over lits, extra non-linearity level, ...
      self.gweight_clause_embeds[cl_num] = torch.sum(lit_embeds,dim=0)
    empty_clause_todo: List[Tuple[int, List[int]]] = []
    self.gweight_clause_todo = empty_clause_todo
  */
  {
    auto it = _gweightClauseTodo.iterFifo();
    while (it.hasNext()) {
      Clause* c = it.next();
      auto clauseEmbed = torch::zeros(_gweightEmbeddingSize);
      for (Literal* lit : c->iterLits()) {
        // using negative indices for literals (otherwise might overlap with term ids!)
        int64_t litId = -1-(int64_t)lit->getId(); // an ugly copy-paste from SaturationAlgorithm.cpp
        clauseEmbed += _gweightTermEmbedStore.get(litId);
      }
      _gweightClauseEmbeds.insert(c->number(),clauseEmbed);
    }
    _gweightClauseTodo.reset();
  }
}

void NeuralClauseEvaluationModel::evalClauses(Stack<Clause*>& clauses, bool justRecord) {
  int64_t sz = clauses.size();
  if (sz == 0) return;

  torch::NoGradGuard no_grad; // TODO: check if this is necessary here

  auto gageRect = torch::empty({sz, _gageEmbeddingSize}, torch::TensorOptions().dtype(torch::kFloat32));
  auto gweightRect = torch::empty({sz, _gweightEmbeddingSize}, torch::TensorOptions().dtype(torch::kFloat32));

  std::vector<int64_t> clauseNums;
  std::vector<float> features(_numFeatures*sz);
  {
    int64_t j = 0;
    unsigned idx = 0;
    auto uIt = clauses.iter();
    while (uIt.hasNext()) {
      unsigned i = 0;
      Clause* cl = uIt.next();
      clauseNums.push_back((int64_t)cl->number());
      Clause::FeatureIterator cIt(cl);
      while (i++ < _numFeatures && cIt.hasNext()) {
        features[idx] = cIt.next();
        idx++;
      }

      if (_computing) { // could as well be (!justRecord) here
        gageRect.index_put_({j}, _gageEmbedStore.get(cl->number()));
        gweightRect.index_put_({j}, _gweightClauseEmbeds.get(cl->number()));
        j++;
      }
    }
  }

  auto result = (*_evalClauses)({
    std::move(clauseNums),
    torch::from_blob(features.data(), {sz,_numFeatures}, torch::TensorOptions().dtype(torch::kFloat32)),
    gageRect, gweightRect
  });

  if (justRecord) {
    return;
  }

  auto logits = result.toTensor();

  // cout << "Eval clauses for " << sz << " requires " << logits.requires_grad() << endl;

  {
    auto uIt = clauses.iter();
    unsigned idx = 0;
    while (uIt.hasNext()) {
      Clause* cl = uIt.next();
      float logit = logits[idx++].item().toDouble();
      if (_temp > 0.0) {
        // adding the gumbel noise
        logit += -_temp*log(-log(Random::getFloat(0.0,1.0)));
      }

      float* score;
      // only overwrite, if not present
      if (_scores.getValuePtr(cl->number(),score)) {
        *score = logit;
      }
    }
  }
}

NeuralPassiveClauseContainer::NeuralPassiveClauseContainer(bool isOutermost, const Shell::Options& opt,
  NeuralClauseEvaluationModel& model, std::function<void(Clause*)> makeReadyForEval)
  : LRSIgnoringPassiveClauseContainer(isOutermost, opt),
  _model(model), _queue(_model.getScores()),
  _makeReadyForEval(makeReadyForEval),
  _size(0), _reshuffleAt(opt.reshuffleAt())
{
  ASS(_isOutermost);
}

void NeuralPassiveClauseContainer::evalAndEnqueueDelayed()
{
  TIME_TRACE(TimeTrace::DEEP_STUFF);

  if (!_delayedInsertionBuffer.size())
    return;

  {
    auto it = _delayedInsertionBuffer.iter();
    while (it.hasNext()) {
      _makeReadyForEval(it.next());
    }
  }

  _model.gageEmbedPending();
  _model.gweightEmbedPending();
  _model.evalClauses(_delayedInsertionBuffer);

  // cout << "evalAndEnqueueDelayed for " << _delayedInsertionBuffer.size() << endl;
  {
    auto it = _delayedInsertionBuffer.iter();
    while (it.hasNext()) {
      _queue.insert(it.next());
    }
  }
  _delayedInsertionBuffer.reset();
}

void NeuralPassiveClauseContainer::add(Clause* cl)
{
  _delayedInsertionBuffer.push(cl);

  // cout << "Inserting " << cl->number() << endl;
  _size++;

  ASS(cl->store() == Clause::PASSIVE);
  addedEvent.fire(cl);
}

void NeuralPassiveClauseContainer::remove(Clause* cl)
{
  ASS(cl->store()==Clause::PASSIVE);

  // cout << "Removing " << cl->number() << endl;
  if (!_delayedInsertionBuffer.remove(cl)) {
    _queue.remove(cl);
  }
  _size--;

  removedEvent.fire(cl);
  ASS(cl->store()!=Clause::PASSIVE);
}

Clause* NeuralPassiveClauseContainer::popSelected()
{
  ASS(_size);

  evalAndEnqueueDelayed();

  static unsigned popCount = 0;
  if (++popCount == _reshuffleAt) {
    // cout << "reshuffled at "<< popCount << endl;
    Random::resetSeed();
  }

  // cout << "About to pop" << endl;
  Clause* cl = _queue.pop();
  // cout << "Got " << cl->number() << endl;
  // cout << "popped from " << _size << " got " << cl->toString() << endl;
  _size--;

  if (popCount == _reshuffleAt) {
    cout << "s: " << cl->number() << '\n';
  }

  selectedEvent.fire(cl);
  return cl;
}

bool NeuralPassiveClauseContainer::setLimits(float newLimit)
{
  bool tighened = newLimit > _curLimit;
  _curLimit = newLimit;
  return tighened;
}

void NeuralPassiveClauseContainer::simulationInit()
{
  evalAndEnqueueDelayed();

  _simulationIt = new ClauseQueue::Iterator(_queue);
}

bool NeuralPassiveClauseContainer::simulationHasNext()
{
  return _simulationIt->hasNext();
}

void NeuralPassiveClauseContainer::simulationPopSelected()
{
  _simulationIt->next();
}

bool NeuralPassiveClauseContainer::setLimitsFromSimulation()
{
  if (_simulationIt->hasNext()) {
    return setLimits(_model.getScores().get(_simulationIt->next()->number()));
  } else {
    return setLimitsToMax();
  }
}

void NeuralPassiveClauseContainer::onLimitsUpdated()
{
  static Stack<Clause*> toRemove(256);
  simulationInit(); // abused to setup fresh _simulationIt
  while (_simulationIt->hasNext()) {
    Clause* cl = _simulationIt->next();
    if (exceedsLimit(cl)) {
      toRemove.push(cl);
    }
  }

  // cout << "Will remove " << toRemove.size() << " from passive through LRS update" << endl;

  while (toRemove.isNonEmpty()) {
    Clause* removed=toRemove.pop();
    RSTAT_CTR_INC("clauses discarded from passive on limit update");
    env.statistics->discardedNonRedundantClauses++;
    remove(removed);
  }
}


LearnedPassiveClauseContainer::LearnedPassiveClauseContainer(bool isOutermost, const Shell::Options& opt)
  : LRSIgnoringPassiveClauseContainer(isOutermost, opt), _scores(), _queue(_scores), _size(0), _temperature(opt.npccTemperature())
{
  ASS(_isOutermost);
}

void LearnedPassiveClauseContainer::add(Clause* cl)
{
  float* score;
  if (_scores.getValuePtr(cl->number(),score)) {
    *score = scoreClause(cl);
  }
  _queue.insert(cl);
  _size++;

  // cout << "Added " << cl->toString() << " size " << _size << endl;

  ASS(cl->store() == Clause::PASSIVE);
  addedEvent.fire(cl);
}

void LearnedPassiveClauseContainer::remove(Clause* cl)
{
  // we never delete from _scores, maybe that's not the best?
  _queue.remove(cl);

  _size--;

  // cout << "Removed " << cl->toString() << " size " << _size << endl;

  removedEvent.fire(cl);
  ASS(cl->store()!=Clause::PASSIVE);
}

Clause* LearnedPassiveClauseContainer::popSelected()
{
  // TODO: here it will get trickier with the temperature and softmax sampling!

  // we never delete from _scores, maybe that's not the best?
  Clause* cl = _queue.pop();
  _size--;

  // cout << "Popped " << cl->toString() << " size " << _size << endl;

  selectedEvent.fire(cl);
  return cl;
}

float LearnedPassiveClauseContainerExperNF12cLoop5::scoreClause(Clause* cl)
{
  Clause::FeatureIterator fit(cl);

  float features[12] = {(float)fit.next(),(float)fit.next(),(float)fit.next(),(float)fit.next(),(float)fit.next(),(float)fit.next(),
                        (float)fit.next(),(float)fit.next(),(float)fit.next(),(float)fit.next(),(float)fit.next(),(float)fit.next()};

  float weight[] = {
    -2.0405941009521484, 0.12202191352844238, 0.20660847425460815, 0.8350633978843689, -0.14192698895931244, 0.6823735237121582, 0.8786749839782715, -0.11922553181648254, 0.5346186757087708, 0.2527293562889099, -0.48670780658721924, -1.396571397781372,
    0.34327173233032227, -0.11386033892631531, 0.3851318657398224, -1.944481372833252, 0.47715431451797485, -0.8444045782089233, -1.3999969959259033, 0.23372626304626465, -0.9005630612373352, 0.9059399962425232, 0.07302427291870117, -1.581055998802185,
    0.5451248288154602, 0.23543480038642883, 0.039707571268081665, -0.2643747329711914, -0.08209452033042908, 0.9222909212112427, -0.3640296459197998, 0.08987753093242645, -0.9831720590591431, -0.4468047320842743, -0.11443955451250076, 1.5496660470962524,
    -3.107799530029297, 0.22115907073020935, -0.2641993761062622, 0.3595792055130005, -0.5342901349067688, 0.5067926645278931, -0.03309682756662369, 0.19077888131141663, -0.46792128682136536, -1.739579439163208, -0.6812117695808411, -1.1918081045150757,
    0.8465003371238708, 0.042243655771017075, -0.1746903508901596, 0.24819599092006683, -0.32844430208206177, 0.8037562966346741, 0.1972443014383316, 0.18607524037361145, -0.5450467467308044, 0.05763491243124008, 0.0818820521235466, 1.1643238067626953,
    -0.05943622067570686, 0.09342581033706665, 0.34915491938591003, -0.10326356440782547, 0.7751635909080505, 0.6140362024307251, 0.5045745372772217, -0.9581993818283081, 0.9414848685264587, 1.5846697092056274, -0.026700519025325775, -1.7046382427215576,
    0.6129408478736877, -0.4079468548297882, -0.09116656333208084, 0.5605136752128601, -1.721616268157959, 2.0208377838134766, -0.2280556708574295, 0.06740489602088928, 0.8718560934066772, -0.7919328808784485, 0.03510770574212074, 0.15992459654808044,
    0.5424445271492004, 0.10199402272701263, -0.021819917485117912, 0.37965983152389526, -0.12451092153787613, 0.7016618847846985, 0.019443033263087273, 0.15037991106510162, -0.8367310166358948, 0.12205961346626282, 0.3608677387237549, 1.4494743347167969,
    0.39824023842811584, -0.0651693046092987, 0.15712572634220123, 0.4916081726551056, -0.08553516864776611, -0.17163175344467163, 0.18713459372520447, 0.12873928248882294, -0.746273398399353, -0.4054866135120392, 0.2539588510990143, 1.3716002702713013,
    0.8778604865074158, 0.056522175669670105, 0.16329514980316162, 0.11627950519323349, 0.032977260649204254, -0.11529311537742615, 0.03956061974167824, -0.037985362112522125, -0.9197039604187012, -1.4825650453567505, 0.37275660037994385, 1.1955711841583252,
    0.5749868750572205, 0.04442526772618294, 0.047122370451688766, 0.35504409670829773, 0.05695868656039238, 0.898934006690979, -0.1719825714826584, -0.0007673741201870143, -0.5014393329620361, -0.04191356524825096, 0.31047967076301575, 1.0618921518325806,
    -0.10317326337099075, -0.07561460137367249, -0.04910855367779732, -0.14195069670677185, -0.153847798705101, -0.26410049200057983, -0.14690853655338287, -0.21531906723976135, -0.22774572670459747, -0.194924458861351, 0.09902256727218628, -0.011355039663612843,
    0.0247220229357481, -0.49687010049819946, 0.8304696679115295, 0.09509161114692688, 0.5466886162757874, 0.184383362531662, 0.471223384141922, -0.015821756795048714, -1.1008623838424683, -0.31359875202178955, 0.0646572932600975, 1.4368337392807007,
    0.518570065498352, 0.1785249412059784, 0.13946658372879028, 0.3568970859050751, -0.17607930302619934, 0.4906843602657318, -0.333568811416626, -0.14993613958358765, -0.19920840859413147, -0.07193896174430847, 0.37689778208732605, 1.3621294498443604,
    -0.6101843118667603, 0.024073515087366104, 0.24759799242019653, -0.7292666435241699, 0.16373802721500397, -1.8925291299819946, 1.141858696937561, 0.139650359749794, -0.33725234866142273, 0.4965920150279999, -0.42264172434806824, -1.4773523807525635,
    0.5868123769760132, -0.3106329143047333, -0.20227579772472382, -0.09633610397577286, 0.4186137616634369, -0.41743332147598267, -0.4262687861919403, 0.31165263056755066, 1.8063807487487793, -0.40551140904426575, -0.16047526895999908, 0.3483814299106598};

  float bias[] = {2.8044779300689697, -1.3988730907440186, -0.034629229456186295, 1.1336582899093628, 1.174654483795166, 0.8624619841575623, 0.8874326348304749, -0.28390437364578247, 0.003475602250546217, -0.671423614025116, 0.42329445481300354, -0.15679511427879333, 0.30384835600852966, -0.05644775182008743, 1.1080713272094727, -0.08055964857339859};
  float kweight[] = {0.37144598364830017, 0.5145484805107117, -0.2039152830839157, 0.2875518500804901, -0.31656408309936523, 0.4513503313064575, 0.9311041831970215, -0.21673251688480377, -0.032943692058324814, -0.498897910118103, -0.21648238599300385, -0.036208927631378174, -1.37989342212677, -0.21697357296943665, 0.07956060022115707, 0.7410840392112732};

  float res = 0.0;
  for (int i = 0; i < 16; i++) {
      float tmp = bias[i];
      for (int j = 0; j < 12; j++) {
          tmp += features[j]*weight[12*i+j];
      }
      if (tmp < 0.0) {
          tmp = 0.0;
      }
      res += tmp*kweight[i];
  }
  return res;
}

} // namespace Saturation
