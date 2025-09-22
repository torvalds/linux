llvm-lib - LLVM lib.exe compatible library tool
===============================================

.. program:: llvm-lib

SYNOPSIS
--------

**llvm-lib** [/libpath:<path>] [/out:<output>] [/llvmlibthin]
[/ignore] [/machine] [/nologo] [files...]

DESCRIPTION
-----------

The **llvm-lib** command is intended to be a ``lib.exe`` compatible
tool. See https://msdn.microsoft.com/en-us/library/7ykb2k5f for the
general description.

**llvm-lib** has the following extensions:

* Bitcode files in symbol tables.
  **llvm-lib** includes symbols from both bitcode files and regular
  object files in the symbol table.

* Creating thin archives.
  The /llvmlibthin option causes **llvm-lib** to create thin archive
  that contain only the symbol table and the header for the various
  members. These files are much smaller, but are not compatible with
  link.exe (lld can handle them).
