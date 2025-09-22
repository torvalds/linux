On Demand Symbols
=================

On demand symbols can be enabled in LLDB for projects that generate debug info
for more than what is required by a normal debug session. Some build systems
enable debug information for all binaries and can end up producing many
gigabytes of debug information. This amount of debug information can greatly
increase debug session load times and can slow developer productivity when the
debug information isn't indexed. It can also cause expression evaluation to
be slow when types from all of the binaries have full debug info as each module
is queried for very common types, or global name lookups fail due to a mistyped
expression.

When should I consider enabling this feature?
---------------------------------------------

Anyone that has a build system that produces debug information for many
binaries that are not all required when you want to focus on debugging a few of
the produced binaries. Some build systems enable debug info as a project wide
switch and the build system files that control how things are built are not
easy to modify to produce debug info for only a small subset of the files being
linked. If your debug session startup times are slow because of too much debug
info, this feature might help you be more productive during daily use.

How do I enable on demand symbols?
----------------------------------

This feature is enabled using a LLDB setting:


::

   (lldb) settings set symbols.load-on-demand true

Users can also put this command into their ~/.lldbinit file so it is always
enabled.

How does this feature work?
---------------------------

This feature works by selectively enabling debug information for modules that
the user focuses on. It is designed to be enabled and work without the user
having to set any other settings and will try and determine when to enable
debug info access from the modules automatically. All modules with debug info
start off with their debug information turned off for expensive name and type
lookups. The debug information for line tables are always left enabled to allow
users to reliably set breakpoints by file and line. As the user debugs their
target, some simple things can cause module to get its debug information
enabled (called hydration):
- Setting a file and line breakpoint
- Any PC from any stack frame that maps to a module
- Setting a breakpoint by function name
- Finding a global variable by name

Since most users set breakpoints by file and line, this is an easy way for
people to inform the debugger that they want focus on this module. Breakpoints
by file and line are always enabled when on demand symbols is being used. Line
tables in debug information are cheap to parse and breakpoints will be able to
be set in any module that has debug info. Setting breakpoints by file and line
acts as one of the primary ways to enable debug info for a module as it is
the most common way to stop your program at interesting areas of your code.

Once the user hits a breakpoint, or stops the program for any other reason,
like a crash, assertion or signal, the debugger will calculate the stack frames
for one or more threads. Any stack frames whose PC value is contained within
one of a module's sections will have its debug information enabled. This allows
us to enable debug information for the areas of code that the user is stopped
in and will allow only the important subset of modules to have their debug
information enabled.

On demand symbol loading tries to avoid indexing the names within the debug
information and makes a few tradeoffs to allow name matching of functions and
globals without having to always index all of the debug information.
Setting breakpoints by function name can work, but we try to avoid using
debug information by relying on the symbol tables from a module. Debug
information name indexing is one of the most expensive operations that we are
trying to avoid with the on demand symbol loading so this is one of the main
tradeoffs of this feature. When setting a breakpoint by function name, if the
symbol table contains a match, the debug information will be enabled for that
module and the query will be completed using the debug information. This does
mean that setting breakpoints on inline function names can fail for modules
that have debug info, but no matches in the symbol table since inlined
functions don't exist in symbol tables. When using on demand symbol loading it
is encouraged to not strip the symbol tables of local symbols as this will
allow users to set breakpoints on all concrete functions reliably. Stripped
symbol tables have their local symbols removed from the symbol table which
means that static functions and non exported function will not appear in the
symbol tables. This can cause breakpoint setting by function name to fail when
previously it wouldn't fail.

Global variable lookups rely on the same methodology as breakpoint setting by
function name: we use the symbol tables to find a match first if debug
information isn't enabled for a module. If we find a match in the symbol table
for a global variable lookup, we will enable debug information and complete
the query using the debug information. It is encouraged to not strip your
symbol tables with this features as static variables and other non exported
globals will not appear in the symbol table and can lead to matches not being
found.

What other things might fail?
-----------------------------

The on demand symbol loading feature tries to limit expensive name lookups
within debug information. As such, some lookups by name might fail when they
wouldn't when this feature is not enabled:
- Setting breakpoints by function name for inlined functions
- Type lookups when the expression parser requests types by name
- Global variable lookups by name when the name of the variable is stripped

Setting breakpoints by function name can fail for inline function because this
information is only contained in the debug information. No symbols are created
for inlined functions unless there is a concrete copy of the inlined function
in that same module. As a result, we might not end up stopping at all inlined
functions when requested with this feature enabled. Setting file and line
breakpoints are a good way still use on demand symbol loading effectively
and still being able to stop at inline function invocations.

The expression parser often tries to lookup types by name when the user types
an expression. These are some of the most costly parts of expression evaluation
as the user can type something like "iterator" as part of their expression and
this can result in matches from all STL types in all modules. These kinds of
global type lookup queries can cause thousands of results to be found if debug
information is enabled. The way that most debug information is created these
days has the type information inlined into each module. Typically each module
will contain full type definitions in the debug info for any types that are
used in code. This means that when you type an expression when stopped, you
have debug information for all of the variables, arguments and global variables
in your current stack frame and we should be able to find type that are
important by using only the modules that have their debug information enabled.

The expression parser can also be asked to display global variables and they
can be looked up by name. For this feature to work reliably with on demand
symbol loading enabled, just don't strip your symbol tables and the expression
parser should have no problem finding your variables. Most global variables
that are exported will still be in your symbol table if it is stripped, but
static variables and non exported globals will not be.

Can I troubleshoot issues when I believe this feature is impeding my debugging?
-------------------------------------------------------------------------------

Logging has been added that can be enabled to help notify our engineers when
something is not producing results when this feature is enabled. This logging
can be enabled during a debug session and can be sent to the LLDB engineers
to help troubleshoot these situation. To enable logging, type the following
command:

::

   (lldb) log enable -f /tmp/ondemand.txt lldb on-demand

When the logging is enabled, we get full visibility into each query that would
have produced results if this feature were not enabled and will allow us to
troublshoot issues. Enabling this logging before an expression, setting a
breakpoint by name, or doing a type lookup can help us see the patterns that
cause failures and will help us improve this feature.
