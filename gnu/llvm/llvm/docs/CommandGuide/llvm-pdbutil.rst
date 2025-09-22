llvm-pdbutil - PDB File forensics and diagnostics
=================================================

.. program:: llvm-pdbutil

.. contents::
   :local:

Synopsis
--------

:program:`llvm-pdbutil` [*subcommand*] [*options*]

Description
-----------

Display types, symbols, CodeView records, and other information from a
PDB file, as well as manipulate and create PDB files.  :program:`llvm-pdbutil`
is normally used by FileCheck-based tests to test LLVM's PDB reading and
writing functionality, but can also be used for general PDB file investigation
and forensics, or as a replacement for cvdump.

Subcommands
-----------

:program:`llvm-pdbutil` is separated into several subcommands each tailored to
a different purpose.  A brief summary of each command follows, with more detail
in the sections that follow.

  * :ref:`pretty_subcommand` - Dump symbol and type information in a format that
    tries to look as much like the original source code as possible.
  * :ref:`dump_subcommand` - Dump low level types and structures from the PDB
    file, including CodeView records, hash tables, PDB streams, etc.
  * :ref:`bytes_subcommand` - Dump data from the PDB file's streams, records,
    types, symbols, etc as raw bytes.
  * :ref:`yaml2pdb_subcommand` - Given a yaml description of a PDB file, produce
    a valid PDB file that matches that description.
  * :ref:`pdb2yaml_subcommand` - For a given PDB file, produce a YAML
    description of some or all of the file in a way that the PDB can be
    reconstructed.
  * :ref:`merge_subcommand` - Given two PDBs, produce a third PDB that is the
    result of merging the two input PDBs.

.. _pretty_subcommand:

pretty
~~~~~~

.. program:: llvm-pdbutil pretty

.. important::
   The **pretty** subcommand is built on the Windows DIA SDK, and as such is not
   supported on non-Windows platforms.

USAGE: :program:`llvm-pdbutil` pretty [*options*] <input PDB file>

Summary
^^^^^^^^^^^

The *pretty* subcommand displays a very high level representation of your
program's debug info.  Since it is built on the Windows DIA SDK which is the
standard API that Windows tools and debuggers query debug information, it
presents a more authoritative view of how a debugger is going to interpret your
debug information than a mode which displays low-level CodeView records.

Options
^^^^^^^

Filtering and Sorting Options
+++++++++++++++++++++++++++++

.. note::
   *exclude* filters take priority over *include* filters.  So if a filter
   matches both an include and an exclude rule, then it is excluded.

.. option:: -exclude-compilands=<string>

 When dumping compilands, compiland source-file contributions, or per-compiland
 symbols, this option instructs **llvm-pdbutil** to omit any compilands that
 match the specified regular expression.

.. option:: -exclude-symbols=<string>

 When dumping global, public, or per-compiland symbols, this option instructs
 **llvm-pdbutil** to omit any symbols that match the specified regular
 expression.

.. option:: -exclude-types=<string>

 When dumping types, this option instructs **llvm-pdbutil** to omit any types
 that match the specified regular expression.

.. option:: -include-compilands=<string>

 When dumping compilands, compiland source-file contributions, or per-compiland
 symbols, limit the initial search to only those compilands that match the
 specified regular expression.

.. option:: -include-symbols=<string>

 When dumping global, public, or per-compiland symbols, limit the initial
 search to only those symbols that match the specified regular expression.

.. option:: -include-types=<string>

 When dumping types, limit the initial search to only those types that match
 the specified regular expression.

.. option:: -min-class-padding=<uint>

 Only display types that have at least the specified amount of alignment
 padding, accounting for padding in base classes and aggregate field members.

.. option:: -min-class-padding-imm=<uint>

 Only display types that have at least the specified amount of alignment
 padding, ignoring padding in base classes and aggregate field members.

.. option:: -min-type-size=<uint>

 Only display types T where sizeof(T) is greater than or equal to the specified
 amount.

.. option:: -no-compiler-generated

 Don't show compiler generated types and symbols

.. option:: -no-enum-definitions

 When dumping an enum, don't show the full enum (e.g. the individual enumerator
 values).

.. option:: -no-system-libs

 Don't show symbols from system libraries

Symbol Type Options
+++++++++++++++++++
.. option:: -all

 Implies all other options in this category.

.. option:: -class-definitions=<format>

 Displays class definitions in the specified format.

 .. code-block:: text

    =all      - Display all class members including data, constants, typedefs, functions, etc (default)
    =layout   - Only display members that contribute to class size.
    =none     - Don't display class definitions (e.g. only display the name and base list)

.. option:: -class-order

 Displays classes in the specified order.

 .. code-block:: text

    =none            - Undefined / no particular sort order (default)
    =name            - Sort classes by name
    =size            - Sort classes by size
    =padding         - Sort classes by amount of padding
    =padding-pct     - Sort classes by percentage of space consumed by padding
    =padding-imm     - Sort classes by amount of immediate padding
    =padding-pct-imm - Sort classes by percentage of space consumed by immediate padding

.. option::  -class-recurse-depth=<uint>

 When dumping class definitions, stop after recursing the specified number of times.  The
 default is 0, which is no limit.

.. option::  -classes

 Display classes

.. option::  -compilands

 Display compilands (e.g. object files)

.. option::  -enums

 Display enums

.. option::  -externals

 Dump external (e.g. exported) symbols

.. option::  -globals

 Dump global symbols

.. option::  -lines

 Dump the mappings between source lines and code addresses.

.. option::  -module-syms

 Display symbols (variables, functions, etc) for each compiland

.. option::  -sym-types=<types>

 Type of symbols to dump when -globals, -externals, or -module-syms is
 specified. (default all)

 .. code-block:: text

    =thunks - Display thunk symbols
    =data   - Display data symbols
    =funcs  - Display function symbols
    =all    - Display all symbols (default)

.. option::  -symbol-order=<order>

 For symbols dumped via the -module-syms, -globals, or -externals options, sort
 the results in specified order.

 .. code-block:: text

    =none - Undefined / no particular sort order
    =name - Sort symbols by name
    =size - Sort symbols by size

.. option::  -typedefs

 Display typedef types

.. option::  -types

 Display all types (implies -classes, -enums, -typedefs)

Other Options
+++++++++++++

.. option:: -color-output

 Force color output on or off.  By default, color if used if outputting to a
 terminal.

.. option:: -load-address=<uint>

 When displaying relative virtual addresses, assume the process is loaded at the
 given address and display what would be the absolute address.

.. _dump_subcommand:

dump
~~~~

USAGE: :program:`llvm-pdbutil` dump [*options*] <input PDB file>

.. program:: llvm-pdbutil dump

Summary
^^^^^^^^^^^

The **dump** subcommand displays low level information about the structure of a
PDB file.  It is used heavily by LLVM's testing infrastructure, but can also be
used for PDB forensics.  It serves a role similar to that of Microsoft's
`cvdump` tool.

.. note::
   The **dump** subcommand exposes internal details of the file format.  As
   such, the reader should be familiar with :doc:`/PDB/index` before using this
   command.

Options
^^^^^^^

MSF Container Options
+++++++++++++++++++++

.. option:: -streams

 dump a summary of all of the streams in the PDB file.

.. option:: -stream-blocks

 In conjunction with :option:`-streams`, add information to the output about
 what blocks the specified stream occupies.

.. option:: -summary

 Dump MSF and PDB header information.

Module & File Options
+++++++++++++++++++++

.. option:: -modi=<uint>

 For all options that dump information from each module/compiland, limit to
 the specified module.

.. option:: -files

 Dump the source files that contribute to each displayed module.

.. option:: -il

 Dump inlinee line information (DEBUG_S_INLINEELINES CodeView subsection)

.. option:: -l

 Dump line information (DEBUG_S_LINES CodeView subsection)

.. option:: -modules

 Dump compiland information

.. option:: -xme

 Dump cross module exports (DEBUG_S_CROSSSCOPEEXPORTS CodeView subsection)

.. option:: -xmi

 Dump cross module imports (DEBUG_S_CROSSSCOPEIMPORTS CodeView subsection)

Symbol Options
++++++++++++++

.. option:: -globals

 dump global symbol records

.. option:: -global-extras

 dump additional information about the globals, such as hash buckets and hash
 values.

.. option:: -publics

 dump public symbol records

.. option:: -public-extras

 dump additional information about the publics, such as hash buckets and hash
 values.

.. option:: -symbols

 dump symbols (functions, variables, etc) for each module dumped.

.. option:: -sym-data

 For each symbol record dumped as a result of the :option:`-symbols` option,
 display the full bytes of the record in binary as well.

Type Record Options
+++++++++++++++++++

.. option:: -types

 Dump CodeView type records from TPI stream

.. option:: -type-extras

 Dump additional information from the TPI stream, such as hashes and the type
 index offsets array.

.. option:: -type-data

 For each type record dumped, display the full bytes of the record in binary as
 well.

.. option:: -type-index=<uint>

 Only dump types with the specified type index.

.. option:: -ids

 Dump CodeView type records from IPI stream.

.. option:: -id-extras

 Dump additional information from the IPI stream, such as hashes and the type
 index offsets array.

.. option:: -id-data

 For each ID record dumped, display the full bytes of the record in binary as
 well.

.. option:: -id-index=<uint>

 only dump ID records with the specified hexadecimal type index.

.. option:: -dependents

 When used in conjunction with :option:`-type-index` or :option:`-id-index`,
 dumps the entire dependency graph for the specified index instead of just the
 single record with the specified index.  For example, if type index 0x4000 is
 a function whose return type has index 0x3000, and you specify
 `-dependents=0x4000`, then this would dump both records (as well as any other
 dependents in the tree).

Miscellaneous Options
+++++++++++++++++++++

.. option:: -all

 Implies most other options.

.. option:: -section-contribs

 Dump section contributions.

.. option:: -section-headers

 Dump image section headers.

.. option:: -section-map

 Dump section map.

.. option:: -string-table

 Dump PDB string table.

.. _bytes_subcommand:

bytes
~~~~~

USAGE: :program:`llvm-pdbutil` bytes [*options*] <input PDB file>

.. program:: llvm-pdbutil bytes

Summary
^^^^^^^

Like the **dump** subcommand, the **bytes** subcommand displays low level
information about the structure of a PDB file, but it is used for even deeper
forensics.  The **bytes** subcommand finds various structures in a PDB file
based on the command line options specified, and dumps them in hex.  Someone
working on support for emitting PDBs would use this heavily, for example, to
compare one PDB against another PDB to ensure byte-for-byte compatibility.  It
is not enough to simply compare the bytes of an entire file, or an entire stream
because it's perfectly fine for the same structure to exist at different
locations in two different PDBs, and "finding" the structure is half the battle.

Options
^^^^^^^

MSF File Options
++++++++++++++++

.. option:: -block-range=<start[-end]>

 Dump binary data from specified range of MSF file blocks.

.. option:: -byte-range=<start[-end]>

 Dump binary data from specified range of bytes in the file.

.. option:: -fpm

 Dump the MSF free page map.

.. option:: -stream-data=<string>

 Dump binary data from the specified streams.  Format is SN[:Start][@Size].
 For example, `-stream-data=7:3@12` dumps 12 bytes from stream 7, starting
 at offset 3 in the stream.

PDB Stream Options
++++++++++++++++++

.. option:: -name-map

 Dump bytes of PDB Name Map

DBI Stream Options
++++++++++++++++++

.. option:: -ec

 Dump the edit and continue map substream of the DBI stream.

.. option:: -files

 Dump the file info substream of the DBI stream.

.. option:: -modi

 Dump the modi substream of the DBI stream.

.. option:: -sc

 Dump section contributions substream of the DBI stream.

.. option:: -sm

 Dump the section map from the DBI stream.

.. option:: -type-server

 Dump the type server map from the DBI stream.

Module Options
++++++++++++++

.. option:: -mod=<uint>

 Limit all options in this category to the specified module index.  By default,
 options in this category will dump bytes from all modules.

.. option:: -chunks

 Dump the bytes of each module's C13 debug subsection.

.. option:: -split-chunks

 When specified with :option:`-chunks`, split the C13 debug subsection into a
 separate chunk for each subsection type, and dump them separately.

.. option:: -syms

 Dump the symbol record substream from each module.

Type Record Options
+++++++++++++++++++

.. option:: -id=<uint>

 Dump the record from the IPI stream with the given type index.

.. option:: -type=<uint>

 Dump the record from the TPI stream with the given type index.

.. _pdb2yaml_subcommand:

pdb2yaml
~~~~~~~~

USAGE: :program:`llvm-pdbutil` pdb2yaml [*options*] <input PDB file>

.. program:: llvm-pdbutil pdb2yaml

Summary
^^^^^^^

Options
^^^^^^^

.. _yaml2pdb_subcommand:

yaml2pdb
~~~~~~~~

USAGE: :program:`llvm-pdbutil` yaml2pdb [*options*] <input YAML file>

.. program:: llvm-pdbutil yaml2pdb

Summary
^^^^^^^

Generate a PDB file from a YAML description.  The YAML syntax is not described
here.  Instead, use :ref:`llvm-pdbutil pdb2yaml <pdb2yaml_subcommand>` and
examine the output for an example starting point.

Options
^^^^^^^

.. option:: -pdb=<file-name>

Write the resulting PDB to the specified file.

.. _merge_subcommand:

merge
~~~~~

USAGE: :program:`llvm-pdbutil` merge [*options*] <input PDB file 1> <input PDB file 2>

.. program:: llvm-pdbutil merge

Summary
^^^^^^^

Merge two PDB files into a single file.

Options
^^^^^^^

.. option:: -pdb=<file-name>

Write the resulting PDB to the specified file.
