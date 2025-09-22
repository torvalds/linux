llvm-objdump - LLVM's object file dumper
========================================

.. program:: llvm-objdump

SYNOPSIS
--------

:program:`llvm-objdump` [*commands*] [*options*] [*filenames...*]

DESCRIPTION
-----------
The :program:`llvm-objdump` utility prints the contents of object files and
final linked images named on the command line. If no file name is specified,
:program:`llvm-objdump` will attempt to read from *a.out*. If *-* is used as a
file name, :program:`llvm-objdump` will process a file on its standard input
stream.

COMMANDS
--------
At least one of the following commands are required, and some commands can be
combined with other commands:

.. option:: -a, --archive-headers

  Display the information contained within an archive's headers.

.. option:: -d, --disassemble

  Disassemble all executable sections found in the input files. On some
  architectures (AArch64, PowerPC, x86), all known instructions are disassembled by
  default. On the others, :option:`--mcpu` or :option:`--mattr` is needed to
  enable some instruction sets. Disabled instructions are displayed as
  ``<unknown>``.

.. option:: -D, --disassemble-all

  Disassemble all sections found in the input files.

.. option:: --disassemble-symbols=<symbol1[,symbol2,...]>

  Disassemble only the specified symbols. Takes demangled symbol names when
  :option:`--demangle` is specified, otherwise takes mangled symbol names.
  Implies :option:`--disassemble`.

.. option:: --dwarf=<value>

  Dump the specified DWARF debug sections. The supported values are:

  `frames` - .debug_frame

.. option:: -f, --file-headers

  Display the contents of the overall file header.

.. option:: --fault-map-section

  Display the content of the fault map section.

.. option:: -h, --headers, --section-headers

  Display summaries of the headers for each section.

.. option:: --help

  Display usage information and exit. Does not stack with other commands.

.. option:: -p, --private-headers

  Display format-specific file headers.

.. option:: -r, --reloc

  Display the relocation entries in the file.

.. option:: -R, --dynamic-reloc

  Display the dynamic relocation entries in the file.

.. option:: --raw-clang-ast

  Dump the raw binary contents of the clang AST section.

.. option:: -s, --full-contents

  Display the contents of each section.

.. option:: -t, --syms

  Display the symbol table.

.. option:: -T, --dynamic-syms

  Display the contents of the dynamic symbol table.

.. option:: -u, --unwind-info

  Display the unwind info of the input(s).

  This operation is only currently supported for COFF and Mach-O object files.

.. option:: -v, --version

  Display the version of the :program:`llvm-objdump` executable. Does not stack
  with other commands.

.. option:: -x, --all-headers

  Display all available header information. Equivalent to specifying
  :option:`--archive-headers`, :option:`--file-headers`,
  :option:`--private-headers`, :option:`--reloc`, :option:`--section-headers`,
  and :option:`--syms`.

OPTIONS
-------
:program:`llvm-objdump` supports the following options:

.. option:: --adjust-vma=<offset>

  Increase the displayed address in disassembly or section header printing by
  the specified offset.

.. option:: --arch-name=<string>

  Specify the target architecture when disassembling. Use :option:`--version`
  for a list of available targets.

.. option:: --build-id=<string>

  Look up the object using the given build ID, specified as a hexadecimal
  string. The found object is handled as if it were an input filename.

.. option:: -C, --demangle

  Demangle symbol names in the output.

.. option:: --debug-file-directory <path>

  Provide a path to a directory with a `.build-id` subdirectory to search for
  debug information for stripped binaries. Multiple instances of this argument
  are searched in the order given.

.. option:: --debuginfod, --no-debuginfod

  Whether or not to try debuginfod lookups for debug binaries. Unless specified,
  debuginfod is only enabled if libcurl was compiled in (``LLVM_ENABLE_CURL``)
  and at least one server URL was provided by the environment variable
  ``DEBUGINFOD_URLS``.

.. option:: --debug-vars=<format>

  Print the locations (in registers or memory) of source-level variables
  alongside disassembly. ``format`` may be ``unicode`` or ``ascii``, defaulting
  to ``unicode`` if omitted.

.. option:: --debug-vars-indent=<width>

  Distance to indent the source-level variable display, relative to the start
  of the disassembly. Defaults to 52 characters.

.. option:: -j, --section=<section1[,section2,...]>

  Perform commands on the specified sections only. For Mach-O use
  `segment,section` to specify the section name.

.. option:: -l, --line-numbers

  When disassembling, display source line numbers. Implies
  :option:`--disassemble`.

.. option:: -M, --disassembler-options=<opt1[,opt2,...]>

  Pass target-specific disassembler options. Available options:

  * ``reg-names-std``: ARM only (default). Print in ARM 's instruction set documentation, with r13/r14/r15 replaced by sp/lr/pc.
  * ``reg-names-raw``: ARM only. Use r followed by the register number.
  * ``no-aliases``: AArch64 and RISC-V only. Print raw instruction mnemonic instead of pseudo instruction mnemonic.
  * ``numeric``: RISC-V only. Print raw register names instead of ABI mnemonic. (e.g. print x1 instead of ra)
  * ``att``: x86 only (default). Print in the AT&T syntax.
  * ``intel``: x86 only. Print in the intel syntax.


.. option::  --disassembler-color=<mode>

  Enable or disable disassembler color output.

  * ``off``: Disable disassembler color output.
  * ``on``: Enable disassembler color output.
  * ``terminal``: Enable disassembler color output if the terminal supports it (default).

.. option:: --mcpu=<cpu-name>

  Target a specific CPU type for disassembly. Specify ``--mcpu=help`` to display
  available CPUs.

.. option:: --mattr=<a1,+a2,-a3,...>

  Enable/disable target-specific attributes. Specify ``--mattr=help`` to display
  the available attributes.

.. option:: -mllvm <arg>

   Specify an argument to forward to LLVM's CommandLine library.

.. option:: --no-leading-addr, --no-addresses

  When disassembling, do not print leading addresses for instructions or inline
  relocations.

.. option:: --no-print-imm-hex

  Do not use hex format for immediate values in disassembly output.

.. option:: --no-show-raw-insn

  When disassembling, do not print the raw bytes of each instruction.

.. option:: --offloading

  Display the content of the LLVM offloading section.

.. option:: --prefix=<prefix>

  When disassembling with the :option:`--source` option, prepend ``prefix`` to
  absolute paths.

.. option:: --prefix-strip=<level>

  When disassembling with the :option:`--source` option, strip out ``level``
  initial directories from absolute paths. This option has no effect without
  :option:`--prefix`.

.. option:: --print-imm-hex

  Use hex format when printing immediate values in disassembly output (default).

.. option:: -S, --source

  When disassembling, display source interleaved with the disassembly. Implies
  :option:`--disassemble`.

.. option:: --show-all-symbols

  Show all symbols during disassembly, even if multiple symbols are defined at
  the same location.

.. option:: --show-lma

  Display the LMA column when dumping ELF section headers. Defaults to off
  unless any section has different VMA and LMAs.

.. option:: --start-address=<address>

  When disassembling, only disassemble from the specified address.

  When printing relocations, only print the relocations patching offsets from at least ``address``.

  When printing symbols, only print symbols with a value of at least ``address``.

.. option:: --stop-address=<address>

  When disassembling, only disassemble up to, but not including the specified address.

  When printing relocations, only print the relocations patching offsets up to ``address``.

  When printing symbols, only print symbols with a value up to ``address``.

.. option:: --symbolize-operands

  When disassembling, symbolize a branch target operand to print a label instead of a real address.

  When printing a PC-relative global symbol reference, print it as an offset from the leading symbol.

  When a bb-address-map section is present (i.e., the object file is built with
  ``-fbasic-block-sections=labels``), labels are retrieved from that section
  instead. If a pgo-analysis-map is present alongside the bb-address-map, any
  available analyses are printed after the relevant block label. By default,
  any analysis with a special representation (i.e. BlockFrequency,
  BranchProbability, etc) are printed as raw hex values.

  Only works with PowerPC objects or X86 linked images.

  Example:
    A non-symbolized branch instruction with a local target and pc-relative memory access like

  .. code-block:: none

      cmp eax, dword ptr [rip + 4112]
      jge 0x20117e <_start+0x25>

  might become

  .. code-block:: none

     <L0>:
       cmp eax, dword ptr <g>
       jge	<L0>

.. option:: --pretty-pgo-analysis-map

  When using :option:`--symbolize-operands` with bb-address-map and
  pgo-analysis-map, print analyses using the same format as their analysis
  passes would. An example of pretty format would be printing block frequencies
  relative to the entry block, the same as BFI.

  Only works when :option:`--symbolize-operands` is enabled.

.. option:: --triple=<string>

  Target triple to disassemble for, see ``--version`` for available targets.

.. option:: -w, --wide

  Ignored for compatibility with GNU objdump.

.. option:: --x86-asm-syntax=<style>

  Deprecated.
  When used with :option:`--disassemble`, choose style of code to emit from
  X86 backend. Supported values are:

   .. option:: att

    AT&T-style assembly

   .. option:: intel

    Intel-style assembly


  The default disassembly style is **att**.

.. option:: -z, --disassemble-zeroes

  Do not skip blocks of zeroes when disassembling.

.. option:: @<FILE>

  Read command-line options and commands from response file `<FILE>`.

MACH-O ONLY OPTIONS AND COMMANDS
--------------------------------

.. option:: --arch=<architecture>

  Specify the architecture to disassemble. see ``--version`` for available
  architectures.

.. option:: --archive-member-offsets

  Print the offset to each archive member for Mach-O archives (requires
  :option:`--archive-headers`).

.. option:: --bind

  Display binding info

.. option:: --data-in-code

  Display the data in code table.

.. option:: --dis-symname=<name>

  Disassemble just the specified symbol's instructions.

.. option:: --chained-fixups

  Print chained fixup information.

.. option:: --dyld-info

  Print bind and rebase information used by dyld to resolve external
  references in a final linked binary.

.. option:: --dylibs-used

  Display the shared libraries used for linked files.

.. option:: --dsym=<string>

  Use .dSYM file for debug info.

.. option:: --dylib-id

  Display the shared library's ID for dylib files.

.. option:: --exports-trie

  Display exported symbols.

.. option:: --function-starts [=<addrs|names|both>]

  Print the function starts table for Mach-O objects. Either ``addrs``
  (default) to print only the addresses of functions, ``names`` to print only
  the names of the functions (when available), or ``both`` to print the
  names beside the addresses.

.. option:: -g

  Print line information from debug info if available.

.. option:: --full-leading-addr

  Print the full leading address when disassembling.

.. option:: --indirect-symbols

  Display the indirect symbol table.

.. option:: --info-plist

  Display the info plist section as strings.

.. option:: --lazy-bind

  Display lazy binding info.

.. option:: --link-opt-hints

  Display the linker optimization hints.

.. option:: -m, --macho

  Use Mach-O specific object file parser. Commands and other options may behave
  differently when used with ``--macho``.

.. option:: --no-leading-headers

  Do not print any leading headers.

.. option:: --no-symbolic-operands

  Do not print symbolic operands when disassembling.

.. option:: --non-verbose

  Display the information for Mach-O objects in non-verbose or numeric form.

.. option:: --objc-meta-data

  Display the Objective-C runtime meta data.

.. option:: --private-header

  Display only the first format specific file header.

.. option:: --rebase

  Display rebasing information.

.. option:: --rpaths

  Display runtime search paths for the binary.

.. option:: --universal-headers

  Display universal headers.

.. option:: --weak-bind

  Display weak binding information.

XCOFF ONLY OPTIONS AND COMMANDS
---------------------------------

.. option:: --symbol-description

  Add symbol description to disassembly output.

.. option:: --traceback-table

  Decode traceback table in disassembly output. Implies :option:`--disassemble`.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/labels/tools:llvm-objdump/>.

SEE ALSO
--------

:manpage:`llvm-nm(1)`, :manpage:`llvm-otool(1)`, :manpage:`llvm-readelf(1)`,
:manpage:`llvm-readobj(1)`
