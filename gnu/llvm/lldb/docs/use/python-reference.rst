Python Reference
================

The entire LLDB API is available as Python functions through a script bridging
interface. This means the LLDB API's can be used directly from python either
interactively or to build python apps that provide debugger features.

Additionally, Python can be used as a programmatic interface within the lldb
command interpreter (we refer to this for brevity as the embedded interpreter).
Of course, in this context it has full access to the LLDB API - with some
additional conveniences we will call out in the FAQ.

Documentation
--------------

The LLDB API is contained in a python module named lldb. A useful resource when
writing Python extensions is the lldb Python classes reference guide.

The documentation is also accessible in an interactive debugger session with
the following command:

::

   (lldb) script help(lldb)
      Help on package lldb:

      NAME
         lldb - The lldb module contains the public APIs for Python binding.

      FILE
         /System/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/Python/lldb/__init__.py

      DESCRIPTION
   ...

You can also get help using a module class name. The full API that is exposed
for that class will be displayed in a man page style window. Below we want to
get help on the lldb.SBFrame class:

::

   (lldb) script help(lldb.SBFrame)
      Help on class SBFrame in module lldb:

      class SBFrame(__builtin__.object)
      |  Represents one of the stack frames associated with a thread.
      |  SBThread contains SBFrame(s). For example (from test/lldbutil.py),
      |
      |  def print_stacktrace(thread, string_buffer = False):
      |      '''Prints a simple stack trace of this thread.'''
      |
   ...

Or you can get help using any python object, here we use the lldb.process
object which is a global variable in the lldb module which represents the
currently selected process:

::

   (lldb) script help(lldb.process)
      Help on SBProcess in module lldb object:

      class SBProcess(__builtin__.object)
      |  Represents the process associated with the target program.
      |
      |  SBProcess supports thread iteration. For example (from test/lldbutil.py),
      |
      |  # ==================================================
      |  # Utility functions related to Threads and Processes
      |  # ==================================================
      |
   ...

Embedded Python Interpreter
---------------------------

The embedded python interpreter can be accessed in a variety of ways from
within LLDB. The easiest way is to use the lldb command script with no
arguments at the lldb command prompt:

::

   (lldb) script
   Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.
   >>> 2+3
   5
   >>> hex(12345)
   '0x3039'
   >>>

This drops you into the embedded python interpreter. When running under the
script command, lldb sets some convenience variables that give you quick access
to the currently selected entities that characterize the program and debugger
state. In each case, if there is no currently selected entity of the
appropriate type, the variable's IsValid method will return false. These
variables are:

+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+
| Variable          | Type                | Equivalent                          | Description                                                                         |
+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+
| ``lldb.debugger`` | `lldb.SBDebugger`   | `SBTarget.GetDebugger`              | Contains the debugger object whose ``script`` command was invoked.                  |
|                   |                     |                                     | The `lldb.SBDebugger` object owns the command interpreter                           |
|                   |                     |                                     | and all the targets in your debug session.  There will always be a                  |
|                   |                     |                                     | Debugger in the embedded interpreter.                                               |
+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+
| ``lldb.target``   | `lldb.SBTarget`     | `SBDebugger.GetSelectedTarget`      | Contains the currently selected target - for instance the one made with the         |
|                   |                     |                                     | ``file`` or selected by the ``target select <target-index>`` command.               |
|                   |                     | `SBProcess.GetTarget`               | The `lldb.SBTarget` manages one running process, and all the executable             |
|                   |                     |                                     | and debug files for the process.                                                    |
+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+
| ``lldb.process``  | `lldb.SBProcess`    | `SBTarget.GetProcess`               | Contains the process of the currently selected target.                              |
|                   |                     |                                     | The `lldb.SBProcess` object manages the threads and allows access to                |
|                   |                     | `SBThread.GetProcess`               | memory for the process.                                                             |
+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+
| ``lldb.thread``   | `lldb.SBThread`     | `SBProcess.GetSelectedThread`       | Contains the currently selected thread.                                             |
|                   |                     |                                     | The `lldb.SBThread` object manages the stack frames in that thread.                 |
|                   |                     | `SBFrame.GetThread`                 | A thread is always selected in the command interpreter when a target stops.         |
|                   |                     |                                     | The ``thread select <thread-index>`` command can be used to change the              |
|                   |                     |                                     | currently selected thread.  So as long as you have a stopped process, there will be |
|                   |                     |                                     | some selected thread.                                                               |
+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+
| ``lldb.frame``    | `lldb.SBFrame`      | `SBThread.GetSelectedFrame`         | Contains the currently selected stack frame.                                        |
|                   |                     |                                     | The `lldb.SBFrame` object manage the stack locals and the register set for          |
|                   |                     |                                     | that stack.                                                                         |
|                   |                     |                                     | A stack frame is always selected in the command interpreter when a target stops.    |
|                   |                     |                                     | The ``frame select <frame-index>`` command can be used to change the                |
|                   |                     |                                     | currently selected frame.  So as long as you have a stopped process, there will     |
|                   |                     |                                     | be some selected frame.                                                             |
+-------------------+---------------------+-------------------------------------+-------------------------------------------------------------------------------------+

While extremely convenient, these variables have a couple caveats that you
should be aware of. First of all, they hold the values of the selected objects
on entry to the embedded interpreter. They do not update as you use the LLDB
API's to change, for example, the currently selected stack frame or thread.

Moreover, they are only defined and meaningful while in the interactive Python
interpreter. There is no guarantee on their value in any other situation, hence
you should not use them when defining Python formatters, breakpoint scripts and
commands (or any other Python extension point that LLDB provides). For the
latter you'll be passed an `SBDebugger`, `SBTarget`, `SBProcess`, `SBThread` or
`SBFrame` instance and you can use the functions from the "Equivalent" column
to navigate between them.

As a rationale for such behavior, consider that lldb can run in a multithreaded
environment, and another thread might call the "script" command, changing the
value out from under you.

To get started with these objects and LLDB scripting, please note that almost
all of the lldb Python objects are able to briefly describe themselves when you
pass them to the Python print function:

::

   (lldb) script
   Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.
   >>> print lldb.debugger
   Debugger (instance: "debugger_1", id: 1)
   >>> print lldb.target
   a.out
   >>> print lldb.process
   SBProcess: pid = 59289, state = stopped, threads = 1, executable = a.out
   >>> print lldb.thread
   SBThread: tid = 0x1f03
   >>> print lldb.frame
   frame #0: 0x0000000100000bb6 a.out main + 54 at main.c:16


Running a python script when a breakpoint gets hit
--------------------------------------------------

One very powerful use of the lldb Python API is to have a python script run
when a breakpoint gets hit. Adding python scripts to breakpoints provides a way
to create complex breakpoint conditions and also allows for smart logging and
data gathering.

When your process hits a breakpoint to which you have attached some python
code, the code is executed as the body of a function which takes three
arguments:

::

  def breakpoint_function_wrapper(frame, bp_loc, internal_dict):
     # Your code goes here

or:

::

  def breakpoint_function_wrapper(frame, bp_loc, extra_args, internal_dict):
     # Your code goes here


+-------------------+-------------------------------+-------------------------------------------------------------------------------------------------------------------------------------------+
| Argument          | Type                          | Description                                                                                                                               |
+-------------------+-------------------------------+-------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame``         | `lldb.SBFrame`                | The current stack frame where the breakpoint got hit.                                                                                     |
|                   |                               | The object will always be valid.                                                                                                          |
|                   |                               | This ``frame`` argument might *not* match the currently selected stack frame found in the `lldb` module global variable ``lldb.frame``.   |
+-------------------+-------------------------------+-------------------------------------------------------------------------------------------------------------------------------------------+
| ``bp_loc``        | `lldb.SBBreakpointLocation`   | The breakpoint location that just got hit. Breakpoints are represented by `lldb.SBBreakpoint`                                             |
|                   |                               | objects. These breakpoint objects can have one or more locations. These locations                                                         |
|                   |                               | are represented by `lldb.SBBreakpointLocation` objects.                                                                                   |
+-------------------+-------------------------------+-------------------------------------------------------------------------------------------------------------------------------------------+
| ``extra_args``    | `lldb.SBStructuredData`       | ``Optional`` If your breakpoint callback function takes this extra parameter, then when the callback gets added to a breakpoint, its      |
|                   |                               | contents can parametrize this use of the callback.  For instance, instead of writing a callback that stops when the caller is "Foo",      |
|                   |                               | you could take the function name from a field in the ``extra_args``, making the callback more general.  The ``-k`` and ``-v`` options     |
|                   |                               | to ``breakpoint command add`` will be passed as a Dictionary in the ``extra_args`` parameter, or you can provide it with the SB API's.    |
+-------------------+-------------------------------+-------------------------------------------------------------------------------------------------------------------------------------------+
| ``internal_dict`` | ``dict``                      | The python session dictionary as a standard python dictionary object.                                                                     |
+-------------------+-------------------------------+-------------------------------------------------------------------------------------------------------------------------------------------+

Optionally, a Python breakpoint command can return a value. Returning False
tells LLDB that you do not want to stop at the breakpoint. Any other return
value (including None or leaving out the return statement altogether) is akin
to telling LLDB to actually stop at the breakpoint. This can be useful in
situations where a breakpoint only needs to stop the process when certain
conditions are met, and you do not want to inspect the program state manually
at every stop and then continue.

An example will show how simple it is to write some python code and attach it
to a breakpoint. The following example will allow you to track the order in
which the functions in a given shared library are first executed during one run
of your program. This is a simple method to gather an order file which can be
used to optimize function placement within a binary for execution locality.

We do this by setting a regular expression breakpoint that will match every
function in the shared library. The regular expression '.' will match any
string that has at least one character in it, so we will use that. This will
result in one lldb.SBBreakpoint object that contains an
lldb.SBBreakpointLocation object for each function. As the breakpoint gets hit,
we use a counter to track the order in which the function at this particular
breakpoint location got hit. Since our code is passed the location that was
hit, we can get the name of the function from the location, disable the
location so we won't count this function again; then log some info and continue
the process.

Note we also have to initialize our counter, which we do with the simple
one-line version of the script command.

Here is the code:

::

   (lldb) breakpoint set --func-regex=. --shlib=libfoo.dylib
   Breakpoint created: 1: regex = '.', module = libfoo.dylib, locations = 223
   (lldb) script counter = 0
   (lldb) breakpoint command add --script-type python 1
   Enter your Python command(s). Type 'DONE' to end.
   > # Increment our counter.  Since we are in a function, this must be a global python variable
   > global counter
   > counter += 1
   > # Get the name of the function
   > name = frame.GetFunctionName()
   > # Print the order and the function name
   > print '[%i] %s' % (counter, name)
   > # Disable the current breakpoint location so it doesn't get hit again
   > bp_loc.SetEnabled(False)
   > # No need to stop here
   > return False
   > DONE

The breakpoint command add command above attaches a python script to breakpoint 1. To remove the breakpoint command:

::

   (lldb) breakpoint command delete 1


Using the python api's to create custom breakpoints
---------------------------------------------------


Another use of the Python API's in lldb is to create a custom breakpoint
resolver. This facility was added in r342259.

It allows you to provide the algorithm which will be used in the breakpoint's
search of the space of the code in a given Target to determine where to set the
breakpoint locations - the actual places where the breakpoint will trigger. To
understand how this works you need to know a little about how lldb handles
breakpoints.

In lldb, a breakpoint is composed of three parts: the Searcher, the Resolver,
and the Stop Options. The Searcher and Resolver cooperate to determine how
breakpoint locations are set and differ between each breakpoint type. Stop
options determine what happens when a location triggers and includes the
commands, conditions, ignore counts, etc. Stop options are common between all
breakpoint types, so for our purposes only the Searcher and Resolver are
relevant.

The Searcher's job is to traverse in a structured way the code in the current
target. It proceeds from the Target, to search all the Modules in the Target,
in each Module it can recurse into the Compile Units in that module, and within
each Compile Unit it can recurse over the Functions it contains.

The Searcher can be provided with a SearchFilter that it will use to restrict
this search. For instance, if the SearchFilter specifies a list of Modules, the
Searcher will not recurse into Modules that aren't on the list. When you pass
the -s modulename flag to break set you are creating a Module-based search
filter. When you pass -f filename.c to break set -n you are creating a file
based search filter. If neither of these is specified, the breakpoint will have
a no-op search filter, so all parts of the program are searched and all
locations accepted.

The Resolver has two functions. The most important one is the callback it
provides. This will get called at the appropriate time in the course of the
search. The callback is where the job of adding locations to the breakpoint
gets done.

The other function is specifying to the Searcher at what depth in the above
described recursion it wants to be called. Setting a search depth also provides
a stop for the recursion. For instance, if you request a Module depth search,
then the callback will be called for each Module as it gets added to the
Target, but the searcher will not recurse into the Compile Units in the module.

One other slight subtlety is that the depth at which you get called back is not
necessarily the depth at which the SearchFilter is specified. For instance,
if you are doing symbol searches, it is convenient to use the Module depth for
the search, since symbols are stored in the module. But the SearchFilter might
specify some subset of CompileUnits, so not all the symbols you might find in
each module will pass the search. You don't need to handle this situation
yourself, since SBBreakpoint::AddLocation will only add locations that pass the
Search Filter. This API returns an SBError to inform you whether your location
was added.

When the breakpoint is originally created, its Searcher will process all the
currently loaded modules. The Searcher will also visit any new modules as they
are added to the target. This happens, for instance, when a new shared library
gets added to the target in the course of running, or on rerunning if any of
the currently loaded modules have been changed. Note, in the latter case, all
the locations set in the old module will get deleted and you will be asked to
recreate them in the new version of the module when your callback gets called
with that module. For this reason, you shouldn't try to manage the locations
you add to the breakpoint yourself. Note that the Breakpoint takes care of
deduplicating equal addresses in AddLocation, so you shouldn't need to worry
about that anyway.

At present, when adding a scripted Breakpoint type, you can only provide a
custom Resolver, not a custom SearchFilter.

The custom Resolver is provided as a Python class with the following methods:

+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| Name               | Arguments                             | Description                                                                                                      |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| ``__init__``       | ``bkpt``:`lldb.SBBreakpoint`          | This is the constructor for the new Resolver.                                                                    |
|                    | ``extra_args``:`lldb.SBStructuredData`|                                                                                                                  |
|                    |                                       |                                                                                                                  |
|                    |                                       | ``bkpt`` is the breakpoint owning this Resolver.                                                                 |
|                    |                                       |                                                                                                                  |
|                    |                                       |                                                                                                                  |
|                    |                                       | ``extra_args`` is an `SBStructuredData` object that the user can pass in when creating instances of this         |
|                    |                                       | breakpoint.  It is not required, but is quite handy.  For instance if you were implementing a breakpoint on some |
|                    |                                       | symbol name, you could write a generic symbol name based Resolver, and then allow the user to pass               |
|                    |                                       | in the particular symbol in the extra_args                                                                       |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| ``__callback__``   | ``sym_ctx``:`lldb.SBSymbolContext`    | This is the Resolver callback.                                                                                   |
|                    |                                       | The ``sym_ctx`` argument will be filled with the current stage                                                   |
|                    |                                       | of the search.                                                                                                   |
|                    |                                       |                                                                                                                  |
|                    |                                       |                                                                                                                  |
|                    |                                       | For instance, if you asked for a search depth of lldb.eSearchDepthCompUnit, then the                             |
|                    |                                       | target, module and compile_unit fields of the sym_ctx will be filled.  The callback should look just in the      |
|                    |                                       | context passed in ``sym_ctx`` for new locations.  If the callback finds an address of interest, it               |
|                    |                                       | can add it to the breakpoint with the `SBBreakpoint.AddLocation` method, using the breakpoint passed             |
|                    |                                       | in to the ``__init__`` method.                                                                                   |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| ``__get_depth__``  | ``None``                              | Specify the depth at which you wish your callback to get called.  The currently supported options are:           |
|                    |                                       |                                                                                                                  |
|                    |                                       | `lldb.eSearchDepthModule`                                                                                        |
|                    |                                       | `lldb.eSearchDepthCompUnit`                                                                                      |
|                    |                                       | `lldb.eSearchDepthFunction`                                                                                      |
|                    |                                       |                                                                                                                  |
|                    |                                       | For instance, if you are looking                                                                                 |
|                    |                                       | up symbols, which are stored at the Module level, you will want to get called back module by module.             |
|                    |                                       | So you would want to return `lldb.eSearchDepthModule`.  This method is optional.  If not provided the search     |
|                    |                                       | will be done at Module depth.                                                                                    |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| ``get_short_help`` | ``None``                              | This is an optional method.  If provided, the returned string will be printed at the beginning of                |
|                    |                                       | the description for this breakpoint.                                                                             |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+

To define a new breakpoint command defined by this class from the lldb command
line, use the command:

::

  (lldb) breakpoint set -P MyModule.MyResolverClass

You can also populate the extra_args SBStructuredData with a dictionary of
key/value pairs with:

::

  (lldb) breakpoint set -P MyModule.MyResolverClass -k key_1 -v value_1 -k key_2 -v value_2

Although you can't write a scripted SearchFilter, both the command line and the
SB API's for adding a scripted resolver allow you to specify a SearchFilter
restricted to certain modules or certain compile units. When using the command
line to create the resolver, you can specify a Module specific SearchFilter by
passing the -s ModuleName option - which can be specified multiple times. You
can also specify a SearchFilter restricted to certain compile units by passing
in the -f CompUnitName option. This can also be specified more than once. And
you can mix the two to specify "this comp unit in this module". So, for
instance,

::

  (lldb) breakpoint set -P MyModule.MyResolverClass -s a.out

will use your resolver, but will only recurse into or accept new locations in
the module a.out.

Another option for creating scripted breakpoints is to use the
SBTarget.CreateBreakpointFromScript API. This one has the advantage that you
can pass in an arbitrary SBStructuredData object, so you can create more
complex parametrizations. SBStructuredData has a handy SetFromJSON method which
you can use for this purpose. Your __init__ function gets passed this
SBStructuredData object. This API also allows you to directly provide the list
of Modules and the list of CompileUnits that will make up the SearchFilter. If
you pass in empty lists, the breakpoint will use the default "search
everywhere,accept everything" filter.

Using the python API' to create custom stepping logic
-----------------------------------------------------

A slightly esoteric use of the Python API's is to construct custom stepping
types. LLDB's stepping is driven by a stack of "thread plans" and a fairly
simple state machine that runs the plans. You can create a Python class that
works as a thread plan, and responds to the requests the state machine makes to
run its operations.

There is a longer discussion of scripted thread plans and the state machine,
and several interesting examples of their use in:

https://github.com/llvm/llvm-project/blob/main/lldb/examples/python/scripted_step.py

And for a MUCH fuller discussion of the whole state machine, see:

https://github.com/llvm/llvm-project/blob/main/lldb/include/lldb/Target/ThreadPlan.h

If you are reading those comments it is useful to know that scripted thread
plans are set to be "ControllingPlans", and not "OkayToDiscard".

To implement a scripted step, you define a python class that has the following
methods:

+-------------------+------------------------------------+---------------------------------------------------------------------------------------+
| Name              | Arguments                          | Description                                                                           |
+-------------------+------------------------------------+---------------------------------------------------------------------------------------+
| ``__init__``      | ``thread_plan``:`lldb.SBThreadPlan`| This is the underlying `SBThreadPlan` that is pushed onto the plan stack.             |
|                   |                                    | You will want to store this away in an ivar.  Also, if you are going to               |
|                   |                                    | use one of the canned thread plans, you can queue it at this point.                   |
+-------------------+------------------------------------+---------------------------------------------------------------------------------------+
| ``explains_stop`` | ``event``: `lldb.SBEvent`          | Return True if this stop is part of your thread plans logic, false otherwise.         |
+-------------------+------------------------------------+---------------------------------------------------------------------------------------+
| ``is_stale``      | ``None``                           | If your plan is no longer relevant (for instance, you were                            |
|                   |                                    | stepping in a particular stack frame, but some other operation                        |
|                   |                                    | pushed that frame off the stack) return True and your plan will                       |
|                   |                                    | get popped.                                                                           |
+-------------------+------------------------------------+---------------------------------------------------------------------------------------+
| ``should_step``   | ``None``                           | Return ``True`` if you want lldb to instruction step one instruction,                 |
|                   |                                    | or False to continue till the next breakpoint is hit.                                 |
+-------------------+------------------------------------+---------------------------------------------------------------------------------------+
| ``should_stop``   | ``event``: `lldb.SBEvent`          | If your plan wants to stop and return control to the user at this point, return True. |
|                   |                                    | If your plan is done at this point, call SetPlanComplete on your                      |
|                   |                                    | thread plan instance.                                                                 |
|                   |                                    | Also, do any work you need here to set up the next stage of stepping.                 |
+-------------------+------------------------------------+---------------------------------------------------------------------------------------+

To use this class to implement a step, use the command:

::

  (lldb) thread step-scripted -C MyModule.MyStepPlanClass

Or use the SBThread.StepUsingScriptedThreadPlan API. The SBThreadPlan passed
into your __init__ function can also push several common plans (step
in/out/over and run-to-address) in front of itself on the stack, which can be
used to compose more complex stepping operations. When you use subsidiary plans
your explains_stop and should_stop methods won't get called until the
subsidiary plan is done, or the process stops for an event the subsidiary plan
doesn't explain. For instance, step over plans don't explain a breakpoint hit
while performing the step-over.


Create a new lldb command using a Python function
-------------------------------------------------

Python functions can be used to create new LLDB command interpreter commands,
which will work like all the natively defined lldb commands. This provides a
very flexible and easy way to extend LLDB to meet your debugging requirements.

To write a python function that implements a new LLDB command define the
function to take five arguments as follows:

::

  def command_function(debugger, command, exe_ctx, result, internal_dict):
      # Your code goes here

The meaning of the arguments is given in the table below.

If you provide a Python docstring in your command function LLDB will use it
when providing "long help" for your command, as in:

::

  def command_function(debugger, command, result, internal_dict):
      """This command takes a lot of options and does many fancy things"""
      # Your code goes here

though providing help can also be done programmatically (see below).

Prior to lldb 3.5.2 (April 2015), LLDB Python command definitions didn't take the SBExecutionContext
argument. So you may still see commands where the command definition is:

::

  def command_function(debugger, command, result, internal_dict):
      # Your code goes here

Using this form is strongly discouraged because it can only operate on the "currently selected"
target, process, thread, frame.  The command will behave as expected when run
directly on the command line.  But if the command is used in a stop-hook, breakpoint
callback, etc. where the response to the callback determines whether we will select
this or that particular process/frame/thread, the global "currently selected"
entity is not necessarily the one the callback is meant to handle.  In that case, this
command definition form can't do the right thing.

+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| Argument          | Type                           | Description                                                                                                                      |
+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| ``debugger``      | `lldb.SBDebugger`              | The current debugger object.                                                                                                     |
+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| ``command``       | ``python string``              | A python string containing all arguments for your command. If you need to chop up the arguments                                  |
|                   |                                | try using the ``shlex`` module's ``shlex.split(command)`` to properly extract the                                                |
|                   |                                | arguments.                                                                                                                       |
+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| ``exe_ctx``       | `lldb.SBExecutionContext`      | An execution context object carrying around information on the inferior process' context in which the command is expected to act |
|                   |                                |                                                                                                                                  |
|                   |                                | *Optional since lldb 3.5.2, unavailable before*                                                                                  |
+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| ``result``        | `lldb.SBCommandReturnObject`   | A return object which encapsulates success/failure information for the command and output text                                   |
|                   |                                | that needs to be printed as a result of the command. The plain Python "print" command also works but                             |
|                   |                                | text won't go in the result by default (it is useful as a temporary logging facility).                                           |
+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| ``internal_dict`` | ``python dict object``         | The dictionary for the current embedded script session which contains all variables                                              |
|                   |                                | and functions.                                                                                                                   |
+-------------------+--------------------------------+----------------------------------------------------------------------------------------------------------------------------------+

Since lldb 3.7, Python commands can also be implemented by means of a class
which should implement the following interface:

::

  class CommandObjectType:
      def __init__(self, debugger, internal_dict):
          this call should initialize the command with respect to the command interpreter for the passed-in debugger
      def __call__(self, debugger, command, exe_ctx, result):
          this is the actual bulk of the command, akin to Python command functions
      def get_short_help(self):
          this call should return the short help text for this command[1]
      def get_long_help(self):
          this call should return the long help text for this command[1]
      def get_repeat_command(self, command):
          The auto-repeat command is what will get executed when the user types just
          a return at the next prompt after this command is run.  Even if your command
          was run because it was specified as a repeat command, that invocation will still
          get asked for IT'S repeat command, so you can chain a series of repeats, for instance
          to implement a pager.

          The command argument is the command that is about to be executed.

          If this call returns None, then the ordinary repeat mechanism will be used
          If this call returns an empty string, then auto-repeat is disabled
          If this call returns any other string, that will be the repeat command [1]

[1] This method is optional.

As a convenience, you can treat the result object as a Python file object, and
say

::

  print >>result, "my command does lots of cool stuff"

SBCommandReturnObject and SBStream both support this file-like behavior by
providing write() and flush() calls at the Python layer.

One other handy convenience when defining lldb command-line commands is the
command command script import which will import a module specified by file
path, so you don't have to change your PYTHONPATH for temporary scripts. It
also has another convenience that if your new script module has a function of
the form:

::

  def __lldb_init_module(debugger, internal_dict):
      # Command Initialization code goes here

where debugger and internal_dict are as above, that function will get run when
the module is loaded allowing you to add whatever commands you want into the
current debugger. Note that this function will only be run when using the LLDB
command ``command script import``, it will not get run if anyone imports your
module from another module.

The standard test for ``__main__``, like many python modules do, is useful for
creating scripts that can be run from the command line. However, for command
line scripts, the debugger instance must be created manually. Sample code would
look like:

::

  if __name__ == '__main__':
      # Initialize the debugger before making any API calls.
      lldb.SBDebugger.Initialize()
      # Create a new debugger instance in your module if your module
      # can be run from the command line. When we run a script from
      # the command line, we won't have any debugger object in
      # lldb.debugger, so we can just create it if it will be needed
      debugger = lldb.SBDebugger.Create()

      # Next, do whatever work this module should do when run as a command.
      # ...

      # Finally, dispose of the debugger you just made.
      lldb.SBDebugger.Destroy(debugger)
      # Terminate the debug session
      lldb.SBDebugger.Terminate()


Now we can create a module called ls.py in the file ~/ls.py that will implement
a function that can be used by LLDB's python command code:

::

  #!/usr/bin/env python

  import lldb
  import commands
  import optparse
  import shlex

  def ls(debugger, command, result, internal_dict):
      print >>result, (commands.getoutput('/bin/ls %s' % command))

  # And the initialization code to add your commands
  def __lldb_init_module(debugger, internal_dict):
      debugger.HandleCommand('command script add -f ls.ls ls')
      print 'The "ls" python command has been installed and is ready for use.'

Now we can load the module into LLDB and use it

::

  $ lldb
  (lldb) command script import ~/ls.py
  The "ls" python command has been installed and is ready for use.
  (lldb) ls -l /tmp/
  total 365848
  -rw-r--r--@  1 someuser  wheel         6148 Jan 19 17:27 .DS_Store
  -rw-------   1 someuser  wheel         7331 Jan 19 15:37 crash.log

You can also make "container" commands to organize the commands you are adding to
lldb.  Most of the lldb built-in commands structure themselves this way, and using
a tree structure has the benefit of leaving the one-word command space free for user
aliases.  It can also make it easier to find commands if you are adding more than
a few of them.  Here's a trivial example of adding two "utility" commands into a
"my-utilities" container:

::

  #!/usr/bin/env python

  import lldb

  def first_utility(debugger, command, result, internal_dict):
      print("I am the first utility")

  def second_utility(debugger, command, result, internal_dict):
      print("I am the second utility")

  # And the initialization code to add your commands
  def __lldb_init_module(debugger, internal_dict):
      debugger.HandleCommand('command container add -h "A container for my utilities" my-utilities')
      debugger.HandleCommand('command script add -f my_utilities.first_utility -h "My first utility" my-utilities first')
      debugger.HandleCommand('command script add -f my_utilities.second_utility -h "My second utility" my-utilities second')
      print('The "my-utilities" python command has been installed and its subcommands are ready for use.')

Then your new commands are available under the my-utilities node:

::

  (lldb) help my-utilities
  A container for my utilities

  Syntax: my-utilities

  The following subcommands are supported:

      first  -- My first utility  Expects 'raw' input (see 'help raw-input'.)
      second -- My second utility  Expects 'raw' input (see 'help raw-input'.)

  For more help on any particular subcommand, type 'help <command> <subcommand>'.
  (lldb) my-utilities first
  I am the first utility


A more interesting template has been created in the source repository that can
help you to create lldb command quickly:

https://github.com/llvm/llvm-project/blob/main/lldb/examples/python/cmdtemplate.py

A commonly required facility is being able to create a command that does some
token substitution, and then runs a different debugger command (usually, it
po'es the result of an expression evaluated on its argument). For instance,
given the following program:

::

  #import <Foundation/Foundation.h>
  NSString*
  ModifyString(NSString* src)
  {
  	return [src stringByAppendingString:@"foobar"];
  }

  int main()
  {
  	NSString* aString = @"Hello world";
  	NSString* anotherString = @"Let's be friends";
  	return 1;
  }

you may want a pofoo X command, that equates po [ModifyString(X)
capitalizedString]. The following debugger interaction shows how to achieve
that goal:

::

  (lldb) script
  Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.
  >>> def pofoo_funct(debugger, command, result, internal_dict):
  ...	cmd = "po [ModifyString(" + command + ") capitalizedString]"
  ...	debugger.HandleCommand(cmd)
  ...
  >>> ^D
  (lldb) command script add pofoo -f pofoo_funct
  (lldb) pofoo aString
  $1 = 0x000000010010aa00 Hello Worldfoobar
  (lldb) pofoo anotherString
  $2 = 0x000000010010aba0 Let's Be Friendsfoobar

Using the lldb.py module in Python
----------------------------------

LLDB has all of its core code build into a shared library which gets used by
the `lldb` command line application. On macOS this shared library is a
framework: LLDB.framework and on other unix variants the program is a shared
library: lldb.so. LLDB also provides an lldb.py module that contains the
bindings from LLDB into Python. To use the LLDB.framework to create your own
stand-alone python programs, you will need to tell python where to look in
order to find this module. This is done by setting the PYTHONPATH environment
variable, adding a path to the directory that contains the lldb.py python
module. The lldb driver program has an option to report the path to the lldb
module. You can use that to point to correct lldb.py:

For csh and tcsh:

::

  % setenv PYTHONPATH `lldb -P`

For sh and bash:

::

  $ export PYTHONPATH=`lldb -P`

Alternately, you can append the LLDB Python directory to the sys.path list
directly in your Python code before importing the lldb module.

Now your python scripts are ready to import the lldb module. Below is a python
script that will launch a program from the current working directory called
"a.out", set a breakpoint at "main", and then run and hit the breakpoint, and
print the process, thread and frame objects if the process stopped:

::

  #!/usr/bin/env python

  import lldb
  import os

  def disassemble_instructions(insts):
      for i in insts:
          print i

  # Set the path to the executable to debug
  exe = "./a.out"

  # Create a new debugger instance
  debugger = lldb.SBDebugger.Create()

  # When we step or continue, don't return from the function until the process
  # stops. Otherwise we would have to handle the process events ourselves which, while doable is
  #a little tricky.  We do this by setting the async mode to false.
  debugger.SetAsync (False)

  # Create a target from a file and arch
  print "Creating a target for '%s'" % exe

  target = debugger.CreateTargetWithFileAndArch (exe, lldb.LLDB_ARCH_DEFAULT)

  if target:
      # If the target is valid set a breakpoint at main
      main_bp = target.BreakpointCreateByName ("main", target.GetExecutable().GetFilename());

      print main_bp

      # Launch the process. Since we specified synchronous mode, we won't return
      # from this function until we hit the breakpoint at main
      process = target.LaunchSimple (None, None, os.getcwd())

      # Make sure the launch went ok
      if process:
          # Print some simple process info
          state = process.GetState ()
          print process
          if state == lldb.eStateStopped:
              # Get the first thread
              thread = process.GetThreadAtIndex (0)
              if thread:
                  # Print some simple thread info
                  print thread
                  # Get the first frame
                  frame = thread.GetFrameAtIndex (0)
                  if frame:
                      # Print some simple frame info
                      print frame
                      function = frame.GetFunction()
                      # See if we have debug info (a function)
                      if function:
                          # We do have a function, print some info for the function
                          print function
                          # Now get all instructions for this function and print them
                          insts = function.GetInstructions(target)
                          disassemble_instructions (insts)
                      else:
                          # See if we have a symbol in the symbol table for where we stopped
                          symbol = frame.GetSymbol();
                          if symbol:
                              # We do have a symbol, print some info for the symbol
                              print symbol

Writing lldb frame recognizers in Python
----------------------------------------

Frame recognizers allow for retrieving information about special frames based
on ABI, arguments or other special properties of that frame, even without
source code or debug info. Currently, one use case is to extract function
arguments that would otherwise be inaccessible, or augment existing arguments.

Adding a custom frame recognizer is done by implementing a Python class and
using the 'frame recognizer add' command. The Python class should have a
'get_recognized_arguments' method and it will receive an argument of type
lldb.SBFrame representing the current frame that we are trying to recognize.
The method should return a (possibly empty) list of lldb.SBValue objects that
represent the recognized arguments.

An example of a recognizer that retrieves the file descriptor values from libc
functions 'read', 'write' and 'close' follows:

::

  class LibcFdRecognizer(object):
    def get_recognized_arguments(self, frame):
      if frame.name in ["read", "write", "close"]:
        fd = frame.EvaluateExpression("$arg1").unsigned
        target = frame.thread.process.target
        value = target.CreateValueFromExpression("fd", "(int)%d" % fd)
        return [value]
      return []

The file containing this implementation can be imported via ``command script import``
and then we can register this recognizer with ``frame recognizer add``.
It's important to restrict the recognizer to the libc library (which is
libsystem_kernel.dylib on macOS) to avoid matching functions with the same name
in other modules:

::

  (lldb) command script import .../fd_recognizer.py
  (lldb) frame recognizer add -l fd_recognizer.LibcFdRecognizer -n read -s libsystem_kernel.dylib

When the program is stopped at the beginning of the 'read' function in libc, we can view the recognizer arguments in 'frame variable':

::

  (lldb) b read
  (lldb) r
  Process 1234 stopped
  * thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.3
      frame #0: 0x00007fff06013ca0 libsystem_kernel.dylib`read
  (lldb) frame variable
  (int) fd = 3

Writing Target Stop-Hooks in Python
-----------------------------------

Stop hooks fire whenever the process stops just before control is returned to the
user.  Stop hooks can either be a set of lldb command-line commands, or can
be implemented by a suitably defined Python class.  The Python based stop-hooks
can also be passed as set of -key -value pairs when they are added, and those
will get packaged up into a SBStructuredData Dictionary and passed to the
constructor of the Python object managing the stop hook.  This allows for
parametrization of the stop hooks.

To add a Python-based stop hook, first define a class with the following methods:

+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| Name               | Arguments                             | Description                                                                                                      |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| ``__init__``       | ``target: lldb.SBTarget``             | This is the constructor for the new stop-hook.                                                                   |
|                    | ``extra_args: lldb.SBStructuredData`` |                                                                                                                  |
|                    |                                       |                                                                                                                  |
|                    |                                       | ``target`` is the SBTarget to which the stop hook is added.                                                      |
|                    |                                       |                                                                                                                  |
|                    |                                       | ``extra_args`` is an SBStructuredData object that the user can pass in when creating instances of this           |
|                    |                                       | breakpoint.  It is not required, but allows for reuse of stop-hook classes.                                      |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+
| ``handle_stop``    | ``exe_ctx: lldb.SBExecutionContext``  | This is the called when the target stops.                                                                        |
|                    | ``stream: lldb.SBStream``             |                                                                                                                  |
|                    |                                       | ``exe_ctx`` argument will be filled with the current stop point for which the stop hook is                       |
|                    |                                       | being evaluated.                                                                                                 |
|                    |                                       |                                                                                                                  |
|                    |                                       | ``stream`` an lldb.SBStream, anything written to this stream will be written to the debugger console.            |
|                    |                                       |                                                                                                                  |
|                    |                                       | The return value is a "Should Stop" vote from this thread.  If the method returns either True or no return       |
|                    |                                       | this thread votes to stop.  If it returns False, then the thread votes to continue after all the stop-hooks      |
|                    |                                       | are evaluated.                                                                                                   |
|                    |                                       | Note, the --auto-continue flag to 'target stop-hook add' overrides a True return value from the method.          |
+--------------------+---------------------------------------+------------------------------------------------------------------------------------------------------------------+

To use this class in lldb, run the command:

::

   (lldb) command script import MyModule.py
   (lldb) target stop-hook add -P MyModule.MyStopHook -k first -v 1 -k second -v 2

where MyModule.py is the file containing the class definition MyStopHook.
