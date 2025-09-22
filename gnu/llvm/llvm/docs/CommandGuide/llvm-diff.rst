llvm-diff - LLVM structural 'diff'
==================================

.. program:: llvm-diff

SYNOPSIS
--------

**llvm-diff** [*options*] *module 1* *module 2* [*global name ...*]

DESCRIPTION
-----------

**llvm-diff** compares the structure of two LLVM modules, primarily
focusing on differences in function definitions.  Insignificant
differences, such as changes in the ordering of globals or in the
names of local values, are ignored.

An input module will be interpreted as an assembly file if its name
ends in '.ll';  otherwise it will be read in as a bitcode file.

If a list of global names is given, just the values with those names
are compared; otherwise, all global values are compared, and
diagnostics are produced for globals which only appear in one module
or the other.

**llvm-diff** compares two functions by comparing their basic blocks,
beginning with the entry blocks.  If the terminators seem to match,
then the corresponding successors are compared; otherwise they are
ignored.  This algorithm is very sensitive to changes in control flow,
which tend to stop any downstream changes from being detected.

**llvm-diff** is intended as a debugging tool for writers of LLVM
passes and frontends.  It does not have a stable output format.

EXIT STATUS
-----------

If **llvm-diff** finds no differences between the modules, it will exit
with 0 and produce no output.  Otherwise it will exit with a non-zero
value.

BUGS
----

Many important differences, like changes in linkage or function
attributes, are not diagnosed.

Changes in memory behavior (for example, coalescing loads) can cause
massive detected differences in blocks.
