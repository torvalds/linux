Overview
========

LLDB is a large and complex codebase. This section will help you become more
familiar with the pieces that make up LLDB and give a general overview of the
general architecture.

LLDB has many code groupings that makeup the source base:

API
---

The API folder contains the public interface to LLDB.

We are currently vending a C++ API. In order to be able to add methods to this
API and allow people to link to our classes, we have certain rules that we must
follow:

- Classes can't inherit from any other classes.
- Classes can't contain virtual methods.
- Classes should be compatible with script bridging utilities like swig.
- Classes should be lightweight and be backed by a single member. Pointers (or
  shared pointers) are the preferred choice since they allow changing the
  contents of the backend without affecting the public object layout.
- The interface should be as minimal as possible in order to give a complete
  API.

By adhering to these rules we should be able to continue to vend a C++ API, and
make changes to the API as any additional methods added to these classes will
just be a dynamic loader lookup and they won't affect the class layout (since
they aren't virtual methods, and no members can be added to the class).

Breakpoint
----------

A collection of classes that implement our breakpoint classes. Breakpoints are
resolved symbolically and always continue to resolve themselves as your program
runs. Whether settings breakpoints by file and line, by symbol name, by symbol
regular expression, or by address, breakpoints will keep trying to resolve new
locations each time shared libraries are loaded. Breakpoints will of course
unresolve themselves when shared libraries are unloaded. Breakpoints can also
be scoped to be set only in a specific shared library. By default, breakpoints
can be set in any shared library and will continue to attempt to be resolved
with each shared library load.

Breakpoint options can be set on the breakpoint, or on the individual
locations. This allows flexibility when dealing with breakpoints and allows us
to do what the user wants.

Commands
--------

The command source files represent objects that implement the functionality for
all textual commands available in our command line interface.

Every command is backed by a ``lldb_private::CommandObject`` or
``lldb_private::CommandObjectMultiword`` object.

``lldb_private::CommandObjectMultiword`` are commands that have subcommands and
allow command line commands to be logically grouped into a hierarchy.

``lldb_private::CommandObject`` command line commands are the objects that
implement the functionality of the command. They can optionally define options
for themselves, as well as group those options into logical groups that can go
together. The help system is tied into these objects and can extract the syntax
and option groupings to display appropriate help for each command.

Core
----

The Core source files contain basic functionality that is required in the
debugger as well as the class representing the debugger itself (Debugger). A
wide variety of classes are implemented:

- Address (section offset addressing)
- AddressRange
- Broadcaster / Event / Listener
- Communication classes that use Connection objects
- Mangled names
- Source manager
- Value objects

Data Formatters
---------------

A collection of classes that implement the data formatters subsystem.

Data formatters provide a set of user-tweakable hooks in the ValueObjects world
that allow to customize presentation aspects of variables. While users interact
with formatters mostly through the type command, inside LLDB there are a few
layers to the implementation: DataVisualization at the highest end of the
spectrum, backed by classes implementing individual formatters, matching rules,
etc.

For a general user-level introduction to data formatters, see :doc:`/use/variable`.

More details on their architecture can be found in :doc:`/resources/dataformatters`.

Expression
----------

Expression parsing files cover everything from evaluating DWARF expressions, to
evaluating expressions using Clang.

The DWARF expression parser has been heavily modified to support type
promotion, new opcodes needed for evaluating expressions with symbolic variable
references (expression local variables, program variables), and other operators
required by typical expressions such as assign, address of, float/double/long
double floating point values, casting, and more. The DWARF expression parser
uses a stack of lldb_private::Value objects. These objects know how to do the
standard C type promotion, and allow for symbolic references to variables in
the program and in the LLDB process (expression local and expression global
variables).

The expression parser uses a full instance of the Clang compiler in order to
accurately evaluate expressions. Hooks have been put into Clang so that the
compiler knows to ask about identifiers it doesn't know about. Once expressions
have be compiled into an AST, we can then traverse this AST and either generate
a DWARF expression that contains simple opcodes that can be quickly
re-evaluated each time an expression needs to be evaluated, or JIT'ed up into
code that can be run on the process being debugged.

Host
----

LLDB tries to abstract itself from the host upon which it is currently running
by providing a host abstraction layer. This layer includes functionality, whose
implementation varies wildly from host to host.

Host functionality includes abstraction layers for:

- Information about the host system (triple, list of running processes, etc.)
- Launching processes
- Various OS primitives like pipes and sockets

It also includes the base classes of the NativeProcess/Thread hierarchy, which
is used by lldb-server.

Interpreter
-----------

The interpreter classes are the classes responsible for being the base classes
needed for each command object, and is responsible for tracking and running
command line commands.

Symbol
------

Symbol classes involve everything needed in order to parse object files and
debug symbols. All the needed classes for compilation units (code and debug
info for a source file), functions, lexical blocks within functions, inlined
functions, types, declaration locations, and variables are in this section.

Target
------

Classes that are related to a debug target include:

- Target
- Process
- Thread
- Stack frames
- Stack frame registers
- ABI for function calling in process being debugged
- Execution context batons

Utility
-------

This module contains the lowest layers of LLDB. A lot of these classes don't
really have anything to do with debugging -- they are just there because the
higher layers of the debugger use these classes to implement their
functionality. Others are data structures used in many other parts of the
debugger. Most of the functionality in this module could be useful in an
application that is not a debugger; however, providing a general purpose C++
library is an explicit non-goal of this module..

This module provides following functionality:

- Abstract path manipulation (FileSpec)
- Architecture specification
- Data buffers (DataBuffer, DataEncoder, DataExtractor)
- Logging
- Structured data manipulation (JSON)
- Streams
- Timers

For historic reasons, some of this functionality overlaps that which is
provided by the LLVM support library.
