/**
 * @file Z3MainLoop.cpp
 * Defines class Z3MainLoop.
 */

#if VZ3

#include "Forwards.hpp"

#include "Shell/Statistics.hpp"

#include "Z3MainLoop.hpp"

namespace SAT
{

using namespace Shell;
using namespace Lib;

Z3MainLoop::Z3MainLoop(Problem& prb, const Options& opt)
: MainLoop(prb,opt)
{
  CALL("Z3MainLoop::Z3MainLoop");

}

void Z3MainLoop::init()
{
  CALL("Z3MainLoop::init");

  
}

MainLoopResult Z3MainLoop::runImpl()
{
  CALL("Z3MainLoop::runImpl");

  if(!_prb.getProperty()->allNonTheoryClausesGround()){
    return MainLoopResult(Statistics::INAPPROPRIATE);
  }

  SAT2FO s2f;
  Z3Interfacing solver(_opt,s2f);

 ClauseIterator cit(_prb.clauseIterator());
 while(cit.hasNext()){
   Clause* cl = cit.next();

   if(cl->varCnt() > 0){
     ASS(cl->inference()->rule()==Inference::THEORY || cl->inference()->rule()==Inference::FOOL_AXIOM);
     continue;
   }

   Clause::Iterator lit(*cl);
   unsigned len = cl->size();
   SATClause* sc = new(len) SATClause(len, true);
   unsigned i=0;
   while(lit.hasNext()){
     Literal* l = lit.next();
     SATLiteral sl = s2f.toSAT(l);
     (*sc)[i++] = sl;
   }
   solver.addClause(sc);
 }

 SATSolver::Status status = solver.solve(UINT_MAX);

 Statistics::TerminationReason reason = Statistics::UNKNOWN; 
                
 if(status == SATSolver::Status::UNSATISFIABLE){ reason = Statistics::REFUTATION; }
 if(status == SATSolver::Status::SATISFIABLE){ reason = Statistics::SATISFIABLE; }

 return MainLoopResult(reason);
}

}

#endif
