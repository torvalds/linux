Open Projects
=============

The following is a mostly unordered set of the ideas for improvements to the
LLDB debugger. Some are fairly deep, some would require less effort.

Speed up type realization in lldb
---------------------------------

The type of problem I'm addressing here is the situation where you are
debugging a large program (lldb built with debug clang/swift will do) and you
go to print a simple expression, and lldb goes away for 30 seconds. When you
sample it, it is always busily churning through all the CU's in the world
looking for something.  The problem isn't that looking for something in
particular is slow, but rather that we somehow turned an bounded search (maybe
a subtype of "std::string" into an unbounded search (all things with the name
of that subtype.)  Or didn't stop when we got a reasonable answer proximate to
the context of the search, but let the search leak out globally. And quite
likely there are other issues that I haven't guessed yet. But if you end up
churning though 3 or 4 Gig of debug info, that's going to be slow no matter how
well written your debug reader is...

My guess is the work will be more in the general symbol lookup than in the
DWARF parser in particular, but it may be a combination of both.

As a user debugging a largish program, this is the most obvious lameness of
lldb.

Symbol name completion in the expression parser
-----------------------------------------------

This is the other obvious lameness of lldb.  You can do:

::

   (lldb) frame var foo.b

and we will tell you it is "foo.bar". But you can't do that in the expression
parser. This will require collaboration with the clang/swift folks to get the
right extension points in the compiler. And whatever they are, lldb will need
use them to tell the compiler about what names are available. It will be
important to avoid the pitfalls of #1 where we wander into the entire DWARF
world.

Make a high speed asynchronous communication channel
----------------------------------------------------

All lldb debugging nowadays is done by talking to a debug agent. We used the
gdb-remote protocol because that is universal, and good enough, and you have to
support it anyway since so many little devices & JTAG's and VM's etc support
it. But it is really old, not terribly high performance, and can't really
handle sending or receiving messages while the process is supposedly running.
It should have compression built in, remove the hand-built checksums and rely
on the robust communication protocols we always have nowadays, allow for
out-of-order requests/replies, allow for reconnecting to a temporarily
disconnected debug session, regularize all of the packet formatting into JSON
or BSON or whatever while including a way to do large binary transfers. It must
be possible to come up with something faster, and better tunable for the many
communications pathways we end up supporting.

Fix local variable lookup in the lldb expression parser
-------------------------------------------------------

The injection of local variables into the clang expression parser is
currently done incorrectly - it happens too late in the lookup. This results
in namespace variables & functions, same named types and ivars shadowing
locals when it should be the other way around. An attempt was made to fix
this by manually inserting all the visible local variables into wrapper
function in the expression text. This mostly gets the job done but that
method means you have to realize all the types and locations of all local
variables for even the simplest of expressions, and when run on large
programs (e.g. lldb) it would cause unacceptable delays. And it was very
fragile since an error in realizing any of the locals would cause all
expressions run in that context to fail. We need to fix this by adjusting
the points where name lookup calls out to lldb in clang.

Support calling SB & commands everywhere and support non-stop debugging
-----------------------------------------------------------------------

There is a fairly ad-hoc system to handle when it is safe to run SB API's and
command line commands. This is actually a bit of a tricky problem, since we
allow access to the command line and SB API from some funky places in lldb. The
Operating System plugins are the most obvious instance, since they get run
right after lldb is told by debugserver that the process has stopped, but
before it has finished collating the information from the stop for presentation
to the higher levels. But breakpoint callbacks have some of the same problems,
and other things like the scripted stepping operations and any fancier
extension points we want to add to the debugger are going to be hard to
implement robustly till we work on a finer-grained and more explicit control
over who gets to control the process state.

We also won't have any chance of supporting non-stop debugging - which is a
useful mode for programs that have a lot of high-priority or real-time worker
threads - until we get this sorted out.

Finish the language abstraction and remove all the unnecessary API's
--------------------------------------------------------------------

An important part of making lldb a more useful "debugger toolkit" as opposed to
a C/C++/ObjC/Swift debugger is to have a clean abstraction for language
support. We did most, but not all, of the physical separation.  We need to
finish that. And then by force of necessity the API's really look like the
interface to a C++ type system with a few swift bits added on.  How you would
go about adding a new language is unclear and much more trouble than it is
worth at present. But if we made this nice, we could add a lot of value to
other language projects.

Add some syntax to generate data formatters from type definitions
-----------------------------------------------------------------

Uses of the data formatters fall into two types. There are data formatters for
types where the structure elements pretty much tell you how to present the
data, you just need a little expression language to express how to turn them
into what the user expects to see. Then there are the ones (like pretty much
all our Foundation/AppKit/UIKit formatters) that use deep magic to figure out
how the type is actually laid out. The latter are pretty much always going to
have to be done by hand.

But for the ones where the information is expressed in the fields, it would be
great to have a way to express the instructions to produce summaries and
children in some form you could embed next to the types and have the compiler
produce a byte code form of the instructions and then make that available to
lldb along with the library. This isn't as simple as having clang run over the
headers and produce something from the types directly. After all, clang has no
way of knowing that the interesting thing about a std::vector is the elements
that you get by calling size (for the summary) and [] for the elements. But it
shouldn't be hard to come up with a generic markup to express this.

Allow the expression parser to access dynamic type/data formatter information
-----------------------------------------------------------------------------

This seems like a smaller one. The symptom is your object is Foo child of
Bar, and in the Locals view you see all the fields of Foo, but because the
static type of the object is Bar, you can't see any of the fields of Foo.
But if you could get this working, you could hijack the mechanism to make
the results of the value object summaries/synthetic children available to
expressions. And if you can do that, you could add other properties to an
object externally (through Python or some other extension point) and then
have these also available in the expression parser. You could use this to
express invariants for data structures, or other more advanced uses of types
in the debugger.

Another version of this is to allow access to synthetic children in the
expression parser. Otherwise you end up in situations like:

::

  (lldb) print return_a_foo()
  (SomeVectorLikeType) $1 = {
    [0] = 0
    [1] = 1
    [2] = 2
    [3] = 3
    [4] = 4
  }

That's good but:

::

  (lldb) print return_a_foo()[2]

fails because the expression parser doesn't know anything about the
array-like nature of SomeVectorLikeType that it gets from the synthetic
children.

Recover thread information lazily
---------------------------------

LLDB stores all the user intentions for a thread in the ThreadPlans stored in
the Thread class. That allows us to reliably implement a very natural model for
users moving through a debug session. For example, if step-over stops at a
breakpoint in an function in a younger region of the stack, continue will
complete the step-over rather than having to manually step out. But that means
that it is important that the Thread objects live as long as the Threads they
represent. For programs with many threads, but only one that you are debugging,
that makes stepping less efficient, since now you have to fetch the thread list
on every step or stepping doesn't work correctly. This is especially an issue
when the threads are provided by an Operating System plugin, where it may take
non-trivial work to reconstruct the thread list. It would be better to fetch
threads lazily but keep "unseen" threads in a holding area, and only retire
them when we know we've fetched the whole thread list and ensured they are no
longer alive.

Make Python-backed commands first class citizens
------------------------------------------------

As it stands, Python commands have no way to advertise their options. They are
required to parse their arguments by hand. That leads to inconsistency, and
more importantly means they can't take advantage of auto-generated help and
command completion. This leaves python-backed commands feeling worse than
built-in ones.

As part of this job, it would also be great to hook automatically hook the
"type" of an option value or argument (e.g. eArgTypeShlibName) to sensible
default completers. You need to be able to over-ride this in more complicated
scenarios (like in "break set" where the presence of a "-s" option limits the
search for completion of a "-n" option.) But in common cases it is unnecessary
busy-work to have to supply the completer AND the type. If this worked, then it
would be easier for Python commands to also get correct completers.

Reimplement the command interpreter commands using the SB API
-------------------------------------------------------------

Currently, all the CommandObject::DoExecute methods are implemented using the
lldb_private API's. That generally means that there's code that gets duplicated
between the CommandObject and the SB API that does roughly the same thing. We
would reduce this code duplication, present a single coherent face to the users
of lldb, and keep ourselves more honest about what we need in the SB API's if
we implemented the CommandObjects::DoExecute methods using the SB API's.

BTW, it is only the way it was much easier to develop lldb if it had a
functioning command-line early on. So we did that first, and developed the SB
API's when lldb was more mature. There's no good technical reason to have the
commands use the lldb_private API's.

Documentation and better examples
---------------------------------

We need to put the lldb syntax docs in the tutorial somewhere that is more
easily accessible. On suggestion is to add non-command based help to the help
system, and then have a "help lldb" or "help syntax" type command with this
info. Be nice if the non-command based help could be hierarchical so you could
make topics.

There's a fair bit of docs about the SB API's, but it is spotty. Some classes
are well documented in the Python "help (lldb.SBWhatever)" and some are not.

We need more conceptual docs. And we need more examples. And we could provide a
clean pluggable example for using LLDB standalone from Python. The
process_events.py is a start of this, but it just handles process events, and
it is really a quick sketch not a polished expandable proto-tool.

Make a more accessible plugin architecture for lldb
---------------------------------------------------

Right now, you can only use the Python or SB API's to extend an extant lldb.
You can't implement any of the actual lldb Plugins as plugins. That means
anybody that wants to add new Object file/Process/Language etc support has to
build and distribute their own lldb. This is tricky because the API's the
plugins use are currently not stable (and recently have been changing quite a
lot.) We would have to define a subset of lldb_private that you could use, and
some way of telling whether the plugins were compatible with the lldb. But
long-term, making this sort of extension possible will make lldb more appealing
for research and 3rd party uses.

Use instruction emulation to reduce the overhead for breakpoints
----------------------------------------------------------------

At present, breakpoints are implemented by inserting a trap instruction, then
when the trap is hit, replace the trap with the actual instruction and single
step. Then swap back and continue. This causes problems for read only text, and
also means that no-stop debugging must either stop all threads briefly to handle
this two-step or risk missing some breakpoint hits. If you emulated the
instruction and wrote back the results, you wouldn't have these problems, and
it would also save a stop per breakpoint hit. Since we use breakpoints to
implement stepping, this savings could be significant on slow connections.

Use the JIT to speed up conditional breakpoint evaluation
---------------------------------------------------------

We already JIT and cache the conditional expressions for breakpoints for the C
family of languages, so we aren't re-compiling every time you hit the
breakpoint. And if we couldn't IR interpret the expression, we leave the JIT'ed
code in place for reuse. But it would be even better if we could also insert
the "stop or not" decision into the code at the breakpoint, so you would only
actually stop the process when the condition was true. Greg's idea was that if
you had a conditional breakpoint set when you started the debug session, Xcode
could rebuild and insert enough no-ops that we could instrument the breakpoint
site and call the conditional expression, and only trap if the conditional was
true.

Broaden the idea in "target stop-hook" to cover more events in the debugger
---------------------------------------------------------------------------

Shared library loads, command execution, User directed memory/register reads
and writes are all places where you would reasonably want to hook into the
debugger.

Mock classes for testing
------------------------

We need "ProcessMock" and "ObjectFileMock" and the like. These would be real
plugin implementations for their underlying lldb classes, with the addition
that you can prime them from some sort of text based input files. For classes
that manage changes over time (like process) you would need to program the
state at StopPoint 0, StopPoint 1, etc. These could then be used for testing
reactions to complex threading problems & the like, and also for simulating
hard-to-test environments (like bare board debugging).

Expression parser needs syntax for "{symbol,type} A in CU B.cpp"
----------------------------------------------------------------

Sometimes you need to specify non-visible or ambiguous types to the expression
parser. We were planning to do $b_dot_cpp$A or something like. You might want
to specify a static in a function, in a source file, or in a shared library. So
the syntax should support all these.

Add a "testButDontAbort" style test to the UnitTest framework
-------------------------------------------------------------

The way we use unittest now (maybe this is the only way it can work, I don't
know) you can't report a real failure and continue with the test. That is
appropriate in some cases: if I'm supposed to hit breakpoint A before I
evaluate an expression, and don't hit breakpoint A, the test should fail. But
it means that if I want to test five different expressions, I can either do it
in one test, which is good because it means I only have to fire up one process,
attach to it, and get it to a certain point. But it also means if the first
test fails, the other four don't even get run. So though at first we wrote a
bunch of test like this, as time went on we switched more to writing "one at a
time" tests because they were more robust against a single failure. That makes
the test suite run much more slowly. It would be great to add a
"test_but_dont_abort" variant of the tests, then we could gang tests that all
drive to the same place and do similar things. As an added benefit, it would
allow us to be more thorough in writing tests, since each test would have lower
costs.

Convert the dotest style tests to use lldbutil.run_to_source_breakpoint
-----------------------------------------------------------------------

run_to_source_breakpoint & run_to_name_breakpoint provide a compact API that
does in one line what the first 10 or 20 lines of most of the old tests now do
by hand. Using these functions makes tests much more readable, and by
centralizing common functionality will make maintaining the testsuites easier
in the future. This is more of a finger exercise, and perhaps best implemented
by a rule like: "If you touch a test case, and it isn't using
run_to_source_breakpoint, please make it do so".

Unify Watchpoint's & Breakpoints
--------------------------------

Option handling isn't shared, and more importantly the PerformAction's have a
lot of duplicated common code, most of which works less well on the Watchpoint
side.

Reverse debugging
-----------------

This is kind of a holy grail, it's hard to support for complex apps (many
threads, shared memory, etc.) But it would be SO nice to have...

Non-stop debugging
------------------

By this I mean allowing some threads in the target program to run while
stopping other threads. This is supported in name in lldb at present, but lldb
makes the assumption "If I get a stop, I won't get another stop unless I
actually run the program." in a bunch of places so getting it to work reliably
will be some a good bit of work. And figuring out how to present this in the UI
will also be tricky.

Fix and continue
----------------

We did this in gdb without a real JIT. The implementation shouldn't be that
hard, especially if you can build the executable for fix and continue. The
tricky part is how to verify that the user can only do the kinds of fixes that
are safe to do. No changing object sizes is easy to detect, but there were many
more subtle changes (function you are fixing is on the stack...) that take more
work to prevent. And then you have to explain these conditions the user in some
helpful way.

Unified IR interpreter
----------------------

Currently IRInterpreter implements a portion of the LLVM IR, but it doesn't
handle vector data types and there are plenty of instructions it also doesn't
support. Conversely, lli supports most of LLVM's IR but it doesn't handle
remote memory and its function calling support is very rudimentary. It would be
useful to unify these and make the IR interpreter -- both for LLVM and LLDB --
better. An alternate strategy would be simply to JIT into the current process
but have callbacks for non-stack memory access.

Teach lldb to predict exception propagation at the throw site
-------------------------------------------------------------

There are a bunch of places in lldb where we need to know at the point where an
exception is thrown, what frame will catch the exception.

For instance, if an expression throws an exception, we need to know whether the
exception will be caught in the course of the expression evaluation.  If so it
would be safe to let the expression continue.  But since we would destroy the
state of the thread if we let the exception escape the expression, we currently
stop the expression evaluation if we see a throw.  If we knew where it would be
caught we could distinguish these two cases.

Similarly, when you step over a call that throws, you want to stop at the throw
point if you know the exception will unwind past the frame you were stepping in,
but it would annoying to have the step abort every time an exception was thrown.
If we could predict the catching frame, we could do this right.

And of course, this would be a useful piece of information to display when stopped
at a throw point.

Add predicates to the nodes of settings
---------------------------------------

It would be very useful to be able to give values to settings that are dependent
on the triple, or executable name, for targets, or on whether a process is local
or remote, or on the name of a thread, etc.  The original intent (and there is
a sketch of this in the settings parsing code) was to be able to say:

::

  (lldb) settings set target{arch=x86_64}.process.thread{name=foo}...

The exact details are still to be worked out, however.

Resurrect Type Validators
-------------------------

This half-implemented feature was removed in
https://reviews.llvm.org/D71310 but the general idea might still be
useful: Type Validators look at a ValueObject, and make sure that
there is nothing semantically wrong with the object's contents to
easily catch corrupted data.
