==================================
Stack Safety Analysis
==================================


Introduction
============

The Stack Safety Analysis determines if stack allocated variables can be
considered 'safe' from memory access bugs.

The primary purpose of the analysis is to be used by sanitizers to avoid
unnecessary instrumentation of 'safe' variables. SafeStack is going to be the
first user.

'safe' variables can be defined as variables that can not be used out-of-scope
(e.g. use-after-return) or accessed out of bounds. In the future it can be
extended to track other variable properties. E.g. we plan to extend
implementation with a check to make sure that variable is always initialized
before every read to optimize use-of-uninitialized-memory checks.

How it works
============

The analysis is implemented in two stages:

The intra-procedural, or 'local', stage performs a depth-first search inside
functions to collect all uses of each alloca, including loads/stores and uses as
arguments functions. After this stage we know which parts of the alloca are used
by functions itself but we don't know what happens after it is passed as
an argument to another function.

The inter-procedural, or 'global', stage, resolves what happens to allocas after
they are passed as function arguments. This stage performs a depth-first search
on function calls inside a single module and propagates allocas usage through
functions calls.

When used with ThinLTO, the global stage performs a whole program analysis over
the Module Summary Index.

Testing
=======

The analysis is covered with lit tests.

We expect that users can tolerate false classification of variables as
'unsafe' when in-fact it's 'safe'. This may lead to inefficient code. However, we
can't accept false 'safe' classification which may cause sanitizers to miss actual
bugs in instrumented code. To avoid that we want additional validation tool.

AddressSanitizer may help with this validation. We can instrument all variables
as usual but additionally store stack-safe information in the
``ASanStackVariableDescription``. Then if AddressSanitizer detects a bug on
a 'safe' variable we can produce an additional report to let the user know that
probably Stack Safety Analysis failed and we should check for a bug in the
compiler.
