#include "Shell/TimeTracing.hpp"
#include "Lib/Environment.hpp"
#include "Shell/Statistics.hpp"

namespace Shell {

using namespace Lib;

TimeTrace::TimeTrace() 
  : _root("[root]")
  , _stack({ {&_root, Clock::now(), }, }) 
{  }

TimeTrace::ScopedTimer::ScopedTimer(const char* name)
  : ScopedTimer(env.statistics->timeTrace, name)
{ }

TimeTrace::ScopedTimer::ScopedTimer(TimeTrace& trace, const char* name)
  : _trace(trace)
#if VDEBUG
  , _start()
  , _name(name)
#endif
{
  // DBG("ScopedTimer() ", name)
  auto& children = std::get<0>(trace._stack.top())->children;
  auto node = iterTraits(children.iter())
    .map([](auto& x) { return &*x; })
    .find([&](Node* n) { return n->name == name; })
    .unwrapOrElse([&]() { 
        children.push(Lib::make_unique<Node>(name));
        return &*children.top();
    });
  auto start = Clock::now();
#if VDEBUG
  _start = start;
#endif 

  _trace._stack.push(std::make_pair(node, start));
}

TimeTrace::ScopedTimer::~ScopedTimer()
{
  auto now = Clock::now();
  auto cur = _trace._stack.pop();
  auto node = get<0>(cur);
  auto start = get<1>(cur);
  node->measurements.push(now  - start);
  ASS_EQ(node->name, _name);
  ASS(start == _start);
}


TimeTrace::ScopedChangeRoot::ScopedChangeRoot()
  : ScopedChangeRoot(env.statistics->timeTrace)
{ }

TimeTrace::ScopedChangeRoot::ScopedChangeRoot(TimeTrace& trace)
  : _trace(trace)
{
  _trace._tmpRoots.push(get<0>(trace._stack.top()));
}

TimeTrace::ScopedChangeRoot::~ScopedChangeRoot()
{
  _trace._tmpRoots.pop();
}

TimeTrace::Duration TimeTrace::Node::totalDuration() const
{ return iterTraits(measurements.iter())
           .fold(Duration::zero(), 
                 [](Duration l, Duration r){ return l + r; }); }

const char* TimeTrace::Groups::PREPROCESSING = "preprocessing";
const char* TimeTrace::Groups::PARSING = "parsing";
const char* TimeTrace::Groups::LITERAL_ORDER_AFTERCHECK = "literal order aftercheck";


  
std::ostream& operator<<(std::ostream& out, TimeTrace::Duration const& self)
{ 
  using namespace std::chrono;
  if(self > 1s) {
    return out << duration_cast<seconds>(self).count() << " s"; 
  } else if (self > 1ms) {
    return out << duration_cast<milliseconds>(self).count() << " ms"; 
  } else if (self > 1us) {
    return out << duration_cast<microseconds>(self).count() << " μs"; 
  } else {
    return out << duration_cast<nanoseconds>(self).count() << " ns"; 
  }
// << duration_cast<microseconds>(total / cnt).count() << " μs"
}

struct TimeTrace::Node::NodeFormatOpts {
  Kernel::Stack<const char*>& indent;
  Lib::Option<Duration> parentDuration;
  bool last;
  Lib::Option<unsigned> nameWidth;

  static NodeFormatOpts child(decltype(indent) indent, Node& parent) 
  { return { .indent = indent, 
             .parentDuration = some(parent.totalDuration()), 
             .last = false, 
             .nameWidth = iterTraits(parent.children.iter())
               .map([](auto& c) { return unsigned(strlen(c->name)); })
               .max(),
           }; }

  static NodeFormatOpts root(decltype(indent) indent) 
  { return { .indent = indent, 
             .parentDuration = Option<Duration>(), 
             .last = true, 
             .nameWidth = none<unsigned>(),
           }; }
};

void TimeTrace::Node::printPretty(std::ostream& out, NodeFormatOpts& opts)
{
  const char* indentBeforeLast = "  │  ";
  const char* internalChild    = "  ├──";
  const char* lastChild        = "  └──";
  const char* indentAfterLast  = "     ";
  auto& indent = opts.indent;
  for (int i = 0; i < int(indent.size()) - 1; i++) {
    out << indent[i];
  }
  if (indent.size() > 0) {
    out << (opts.last ? lastChild : internalChild);
  }
  auto percent = [](Duration a, Duration b) {
    return 100 * a / b;
    // auto prec = 100;
    // return double(100 * prec * a / b) / prec;
  };
  // auto percent = [](Duration a, Duration b) {
  //   auto prec = 100;
  //   return double(100 * prec * a / b) / prec;
  // };
  auto total = totalDuration();
  auto cnt = measurements.size();
  if (opts.parentDuration.isSome()) {
    out << "[" << setw(2) << percent(total, opts.parentDuration.unwrap()) << "%] ";
  }
  out << name;
  // if (opts.nameWidth.isSome()) {
  //   for (unsigned i = 0; i < opts.nameWidth.unwrap() - strlen(name); i++) {
  //     out << " ";
  //   }
  // }
  if (opts.parentDuration.isSome()) {
    out << " " << percent(total, opts.parentDuration.unwrap()) << " % ";
  }
  out << " (total: " << total
      << ", cnt: " << cnt 
      << ", avg: " <<  total / cnt
      << ")"
      << std::endl;
  std::sort(children.begin(), children.end(), [](auto& l, auto& r) { return l->totalDuration() > r->totalDuration(); });
  indent.push(indentBeforeLast);
  auto copts = NodeFormatOpts::child(indent, *this);
  for (unsigned i = 0; i < children.size(); i++) {
    copts.last = i == children.size() - 1;
    if (copts.last) {
      indent.top() = indentAfterLast;
    }
    children[i]->printPretty(out, copts);
  }
  indent.pop();
}

void TimeTrace::printPretty(std::ostream& out)
{
  out << "===== start of time trace =====" << std::endl;
  auto now = Clock::now();
  for (auto& x : _stack) {
    auto node = get<0>(x);
    auto start = get<1>(x);
    node->measurements.push(now - start);
  }
  Stack<const char*> indent;
  auto opts = Node::NodeFormatOpts::root(indent);
  auto& root = _tmpRoots.size() == 0 ? _root : *_tmpRoots.top();
  root.printPretty(out, opts);
  for (auto& x : _stack) {
    get<0>(x)->measurements.pop();
  }
  out << "===== end of time trace =====" << std::endl;
}

} // namespace Shell
