//===----------------------------------------------------------------------===//
// Clang Static Analyzer
//===----------------------------------------------------------------------===//

= Library Structure =

The analyzer library has two layers: a (low-level) static analysis
engine (ExprEngine.cpp and friends), and some static checkers
(*Checker.cpp).  The latter are built on top of the former via the
Checker and CheckerVisitor interfaces (Checker.h and
CheckerVisitor.h).  The Checker interface is designed to be minimal
and simple for checker writers, and attempts to isolate them from much
of the gore of the internal analysis engine.

= How It Works =

The analyzer is inspired by several foundational research papers ([1],
[2]).  (FIXME: kremenek to add more links)

In a nutshell, the analyzer is basically a source code simulator that
traces out possible paths of execution.  The state of the program
(values of variables and expressions) is encapsulated by the state
(ProgramState).  A location in the program is called a program point
(ProgramPoint), and the combination of state and program point is a
node in an exploded graph (ExplodedGraph).  The term "exploded" comes
from exploding the control-flow edges in the control-flow graph (CFG).

Conceptually the analyzer does a reachability analysis through the
ExplodedGraph.  We start at a root node, which has the entry program
point and initial state, and then simulate transitions by analyzing
individual expressions.  The analysis of an expression can cause the
state to change, resulting in a new node in the ExplodedGraph with an
updated program point and an updated state.  A bug is found by hitting
a node that satisfies some "bug condition" (basically a violation of a
checking invariant).

The analyzer traces out multiple paths by reasoning about branches and
then bifurcating the state: on the true branch the conditions of the
branch are assumed to be true and on the false branch the conditions
of the branch are assumed to be false.  Such "assumptions" create
constraints on the values of the program, and those constraints are
recorded in the ProgramState object (and are manipulated by the
ConstraintManager).  If assuming the conditions of a branch would
cause the constraints to be unsatisfiable, the branch is considered
infeasible and that path is not taken.  This is how we get
path-sensitivity.  We reduce exponential blow-up by caching nodes.  If
a new node with the same state and program point as an existing node
would get generated, the path "caches out" and we simply reuse the
existing node.  Thus the ExplodedGraph is not a DAG; it can contain
cycles as paths loop back onto each other and cache out.

ProgramState and ExplodedNodes are basically immutable once created.  Once
one creates a ProgramState, you need to create a new one to get a new
ProgramState.  This immutability is key since the ExplodedGraph represents
the behavior of the analyzed program from the entry point.  To
represent these efficiently, we use functional data structures (e.g.,
ImmutableMaps) which share data between instances.

Finally, individual Checkers work by also manipulating the analysis
state.  The analyzer engine talks to them via a visitor interface.
For example, the PreVisitCallExpr() method is called by ExprEngine
to tell the Checker that we are about to analyze a CallExpr, and the
checker is asked to check for any preconditions that might not be
satisfied.  The checker can do nothing, or it can generate a new
ProgramState and ExplodedNode which contains updated checker state.  If it
finds a bug, it can tell the BugReporter object about the bug,
providing it an ExplodedNode which is the last node in the path that
triggered the problem.

= Notes about C++ =

Since now constructors are seen before the variable that is constructed
in the CFG, we create a temporary object as the destination region that
is constructed into. See ExprEngine::VisitCXXConstructExpr().

In ExprEngine::processCallExit(), we always bind the object region to the
evaluated CXXConstructExpr. Then in VisitDeclStmt(), we compute the
corresponding lazy compound value if the variable is not a reference, and
bind the variable region to the lazy compound value. If the variable
is a reference, just use the object region as the initializer value.

Before entering a C++ method (or ctor/dtor), the 'this' region is bound
to the object region. In ctors, we synthesize 'this' region with
CXXRecordDecl*, which means we do not use type qualifiers. In methods, we
synthesize 'this' region with CXXMethodDecl*, which has getThisType()
taking type qualifiers into account. It does not matter we use qualified
'this' region in one method and unqualified 'this' region in another
method, because we only need to ensure the 'this' region is consistent
when we synthesize it and create it directly from CXXThisExpr in a single
method call.

= Working on the Analyzer =

If you are interested in bringing up support for C++ expressions, the
best place to look is the visitation logic in ExprEngine, which
handles the simulation of individual expressions.  There are plenty of
examples there of how other expressions are handled.

If you are interested in writing checkers, look at the Checker and
CheckerVisitor interfaces (Checker.h and CheckerVisitor.h).  Also look
at the files named *Checker.cpp for examples on how you can implement
these interfaces.

= Debugging the Analyzer =

There are some useful command-line options for debugging.  For example:

$ clang -cc1 -help | grep analyze
 -analyze-function <value>
 -analyzer-display-progress
 -analyzer-viz-egraph-graphviz
 ...

The first allows you to specify only analyzing a specific function.
The second prints to the console what function is being analyzed.  The
third generates a graphviz dot file of the ExplodedGraph.  This is
extremely useful when debugging the analyzer and viewing the
simulation results.

Of course, viewing the CFG (Control-Flow Graph) is also useful:

$ clang -cc1 -analyzer-checker-help-developer

 -analyzer-checker=debug.DumpCFG   Display Control-Flow Graphs
 -analyzer-checker=debug.ViewCFG   View Control-Flow Graphs using GraphViz
(outdated below?)
 -cfg-add-implicit-dtors           Add C++ implicit destructors to CFGs for all analyses
 -cfg-add-initializers             Add C++ initializers to CFGs for all analyses
 -unoptimized-cfg                  Generate unoptimized CFGs for all analyses

debug.DumpCFG dumps a textual representation of the CFG to the console, and
debug.ViewCFG creates a GraphViz representation.

= References =

[1] Precise interprocedural dataflow analysis via graph reachability,
    T Reps, S Horwitz, and M Sagiv, POPL '95,
    http://portal.acm.org/citation.cfm?id=199462

[2] A memory model for static analysis of C programs, Z Xu, T
    Kremenek, and J Zhang, http://lcs.ios.ac.cn/~xzx/memmodel.pdf
