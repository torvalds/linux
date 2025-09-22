llvm-dwarfutil - A tool to copy and manipulate debug info
=========================================================

.. program:: llvm-dwarfutil

SYNOPSIS
--------

:program:`llvm-dwarfutil` [*options*] *input* *output*

DESCRIPTION
-----------

:program:`llvm-dwarfutil` is a tool to copy and manipulate debug info.

In basic usage, it makes a semantic copy of the input to the output. If any
options are specified, the output may be modified along the way, e.g.
by removing unused debug info.

If "-" is specified for the input file, the input is read from the program's
standard input stream. If "-" is specified for the output file, the output
is written to the standard output stream of the program.

The tool is still in active development.

COMMAND-LINE OPTIONS
--------------------

.. option:: --garbage-collection

 Removes pieces of debug information related to discarded sections.
 When the linker does section garbage collection the abandoned debug info
 is left behind. Such abandoned debug info references address ranges using
 tombstone values. Thus, when this option is specified, the tool removes
 debug info which is marked with the tombstone value.

 That option is enabled by default.

.. option:: --odr-deduplication

 Remove duplicated types (if "One Definition Rule" is supported by source
 language). Keeps first type definition and removes other definitions,
 potentially significantly reducing the size of output debug info.

 That option is enabled by default.

.. option:: --help, -h

 Print a summary of command line options.

.. option:: --no-garbage-collection

 Disable :option:`--garbage-collection`.

.. option:: --no-odr-deduplication

 Disable :option:`--odr-deduplication`.

.. option:: --no-separate-debug-file

 Disable :option:`--separate-debug-file`.

.. option:: --num-threads=<n>, -j

 Specifies the maximum number (`n`) of simultaneous threads to use
 for processing.

.. option:: --separate-debug-file

 Generate separate file containing output debug info. Using
 :program:`llvm-dwarfutil` with that option equals to the
 following set of commands:

.. code-block:: console

 :program:`llvm-objcopy` --only-keep-debug in-file out-file.debug
 :program:`llvm-objcopy` --strip-debug in-file out-file
 :program:`llvm-objcopy` --add-gnu-debuglink=out-file.debug out-file

.. option:: --tombstone=<value>

 <value> can be one of the following values:

   - `bfd`: zero for all addresses and [1,1] for DWARF v4 (or less) address ranges and exec.

   - `maxpc`: -1 for all addresses and -2 for DWARF v4 (or less) address ranges.

   - `universal`: both `bfd` and `maxpc`.

   - `exec`: match with address ranges of executable sections.

   The value `universal` is used by default.

.. option:: --verbose

 Enable verbose logging. This option disables multi-thread mode.

.. option:: --verify

 Run the DWARF verifier on the output DWARF debug info.

.. option:: --version

 Print the version of this program.

SUPPORTED FORMATS
-----------------

The following formats are currently supported by :program:`llvm-dwarfutil`:

ELF

EXIT STATUS
-----------

:program:`llvm-dwarfutil` exits with a non-zero exit code if there is an error.
Otherwise, it exits with code 0.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/labels/tools:llvm-dwarfutil/>.
