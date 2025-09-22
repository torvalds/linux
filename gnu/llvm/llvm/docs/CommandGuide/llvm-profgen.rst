llvm-profgen - LLVM SPGO profile generation tool
================================================

.. program:: llvm-profgen

SYNOPSIS
--------

:program:`llvm-profgen` [*commands*] [*options*]

DESCRIPTION
-----------

The :program:`llvm-profgen` utility generates a profile data file
from given perf script data files for sample-based profile guided
optimization(SPGO).

COMMANDS
--------
At least one of the following commands are required:

.. option:: --perfscript=<string[,string,...]>

  Path of perf-script trace created by Linux perf tool with `script`
  command(the raw perf.data should be profiled with -b).

.. option:: --binary=<string[,string,...]>

  Path of the input profiled binary files.

.. option:: --output=<string>

  Path of the output profile file.

OPTIONS
-------
:program:`llvm-profgen` supports the following options:

.. option:: --format=[text|binary|extbinary|compbinary|gcc]

  Specify the format of the generated profile. Supported <format>  are `text`,
  `binary`, `extbinary`, `compbinary`, `gcc`, see `llvm-profdata` for more
  descriptions of the format.

.. option:: --show-mmap-events

  Print mmap events.

.. option:: --show-disassembly

  Print disassembled code.

.. option:: --x86-asm-syntax=[att|intel]

  Specify whether to print assembly code in AT&T syntax (the default) or Intel
  syntax.
