llvm-readobj - LLVM Object Reader
=================================

.. program:: llvm-readobj

SYNOPSIS
--------

:program:`llvm-readobj` [*options*] [*input...*]

DESCRIPTION
-----------

The :program:`llvm-readobj` tool displays low-level format-specific information
about one or more object files.

If ``input`` is "``-``", :program:`llvm-readobj` reads from standard
input. Otherwise, it will read from the specified ``filenames``.

DIFFERENCES TO LLVM-READELF
---------------------------

:program:`llvm-readelf` is an alias for the :manpage:`llvm-readobj` tool with a
slightly different command-line interface and output that is GNU compatible.
Following is a list of differences between :program:`llvm-readelf` and
:program:`llvm-readobj`:

- :program:`llvm-readelf` uses `GNU` for the :option:`--elf-output-style` option
  by default. :program:`llvm-readobj` uses `LLVM`.
- :program:`llvm-readelf` allows single-letter grouped flags (e.g.
  ``llvm-readelf -SW`` is the same as  ``llvm-readelf -S -W``).
  :program:`llvm-readobj` does not allow grouping.
- :program:`llvm-readelf` provides :option:`-s` as an alias for
  :option:`--symbols`, for GNU :program:`readelf` compatibility, whereas it is
  an alias for :option:`--section-headers` in :program:`llvm-readobj`.
- :program:`llvm-readobj` provides ``-t`` as an alias for :option:`--symbols`.
  :program:`llvm-readelf` does not.
- :program:`llvm-readobj` provides ``--sr``, ``--sd``, ``--st`` and ``--dt`` as
  aliases for :option:`--section-relocations`, :option:`--section-data`,
  :option:`--section-symbols` and :option:`--dyn-symbols` respectively.
  :program:`llvm-readelf` does not provide these aliases, to avoid conflicting
  with grouped flags.

GENERAL AND MULTI-FORMAT OPTIONS
--------------------------------

These options are applicable to more than one file format, or are unrelated to
file formats.

.. option:: --all

 Equivalent to specifying all the main display options relevant to the file
 format.

.. option:: --addrsig

 Display the address-significance table.

.. option:: --decompress, -z

  Dump decompressed section content when used with ``-x`` or ``-p``.
  If the section(s) are not compressed, they are displayed as is.

.. option:: --demangle, -C

 Display demangled symbol names in the output. This option is only for ELF and
 XCOFF file formats.

.. option:: --expand-relocs

 When used with :option:`--relocs`, display each relocation in an expanded
 multi-line format.

.. option:: --file-header, -h

 Display file headers.

.. option:: --headers, -e

 Equivalent to setting: :option:`--file-header`, :option:`--program-headers`,
 and :option:`--sections`.

.. option:: --help

 Display a summary of command line options.

.. option:: --hex-dump=<section[,section,...]>, -x

 Display the specified section(s) as hexadecimal bytes. ``section`` may be a
 section index or section name.

 .. option:: --memtag

 Display information about memory tagging present in the binary. This includes
 various memtag-specific dynamic entries, decoded global descriptor sections,
 and decoded Android-specific ELF notes.

.. option:: --needed-libs

 Display the needed libraries.

.. option:: --no-demangle

 Do not demangle symbol names in the output. This option is only for ELF and
 XCOFF file formats. The option is enabled by default.

.. option:: --relocations, --relocs, -r

 Display the relocation entries in the file.

.. option:: --sections, --section-headers, -S

 Display all sections.

.. option:: --section-data, --sd

 When used with :option:`--sections`, display section data for each section
 shown. This option has no effect for GNU style output.

.. option:: --section-relocations, --sr

 When used with :option:`--sections`, display relocations for each section
 shown. This option has no effect for GNU style output.

.. option:: --section-symbols, --st

 When used with :option:`--sections`, display symbols for each section shown.
 This option has no effect for GNU style output.

.. option:: --sort-symbols=<sort_key[,sort_key]>

 Specify the keys to sort symbols before displaying symtab.
 Valid values for sort_key are ``name`` and ``type``.
.. option:: --stackmap

 Display contents of the stackmap section.

.. option:: --string-dump=<section[,section,...]>, -p

 Display the specified section(s) as a list of strings. ``section`` may be a
 section index or section name.

.. option:: --string-table

 Display contents of the string table.

.. option:: --symbols, --syms, -s

 Display the symbol table.

.. option:: --unwind, -u

 Display unwind information.

.. option:: --version

 Display the version of the :program:`llvm-readobj` executable.

.. option:: @<FILE>

 Read command-line options from response file `<FILE>`.

ELF SPECIFIC OPTIONS
--------------------

The following options are implemented only for the ELF file format.

.. option:: --arch-specific, -A

 Display architecture-specific information, e.g. the ARM attributes section on ARM.

.. option:: --bb-addr-map

 Display the contents of the basic block address map section(s), which contain the
 address of each function, along with the relative offset of each basic block.

 When pgo analysis maps are present, all analyses are printed as their raw
 value.

.. option:: --pretty-pgo-analysis-map

 When pgo analysis maps are present in the basic block address map section(s),
 analyses with special formats (i.e. BlockFrequency, BranchProbability, etc)
 are printed using the same format as their respective analysis pass.

 Requires :option:`--bb-addr-map` to have an effect.

.. option:: --dependent-libraries

 Display the dependent libraries section.

.. option:: --dyn-relocations

 Display the dynamic relocation entries.

.. option:: --dyn-symbols, --dyn-syms, --dt

 Display the dynamic symbol table.

.. option:: --dynamic-table, --dynamic, -d

 Display the dynamic table.

.. option:: --cg-profile

 Display the callgraph profile section.

.. option:: --histogram, -I

 Display a bucket list histogram for dynamic symbol hash tables.

.. option:: --elf-linker-options

 Display the linker options section.

.. option:: --elf-output-style=<value>

 Format ELF information in the specified style. Valid options are ``LLVM``,
 ``GNU``, and ``JSON``. ``LLVM`` output (the default) is an expanded and
 structured format. ``GNU`` output mimics the equivalent GNU :program:`readelf`
 output. ``JSON`` is JSON formatted output intended for machine consumption.

.. option:: --section-groups, -g

 Display section groups.

.. option:: --gnu-hash-table

 Display the GNU hash table for dynamic symbols.

.. option:: --hash-symbols

 Display the expanded hash table with dynamic symbol data.

.. option:: --hash-table

 Display the hash table for dynamic symbols.

.. option:: --memtag

 Display information about memory tagging present in the binary. This includes
 various dynamic entries, decoded global descriptor sections, and decoded
 Android-specific ELF notes.

.. option:: --notes, -n

 Display all notes.

.. option:: --pretty-print

 When used with :option:`--elf-output-style`, JSON output will be formatted in
 a more readable format.

.. option:: --program-headers, --segments, -l

 Display the program headers.

.. option:: --section-mapping

 Display the section to segment mapping.

.. option:: --stack-sizes

 Display the contents of the stack sizes section(s), i.e. pairs of function
 names and the size of their stack frames. Currently only implemented for GNU
 style output.

.. option:: --version-info, -V

 Display version sections.

MACH-O SPECIFIC OPTIONS
-----------------------

The following options are implemented only for the Mach-O file format.

.. option:: --macho-data-in-code

 Display the Data in Code command.

.. option:: --macho-dsymtab

 Display the Dsymtab command.

.. option:: --macho-indirect-symbols

 Display indirect symbols.

.. option:: --macho-linker-options

 Display the Mach-O-specific linker options.

.. option:: --macho-segment

 Display the Segment command.

.. option:: --macho-version-min

 Display the version min command.

PE/COFF SPECIFIC OPTIONS
------------------------

The following options are implemented only for the PE/COFF file format.

.. option:: --codeview

 Display CodeView debug information.

.. option:: --codeview-ghash

 Enable global hashing for CodeView type stream de-duplication.

.. option:: --codeview-merged-types

 Display the merged CodeView type stream.

.. option:: --codeview-subsection-bytes

 Dump raw contents of CodeView debug sections and records.

.. option:: --coff-basereloc

 Display the .reloc section.

.. option:: --coff-debug-directory

 Display the debug directory.

.. option:: --coff-tls-directory

 Display the TLS directory.

.. option:: --coff-directives

 Display the .drectve section.

.. option:: --coff-exports

 Display the export table.

.. option:: --coff-imports

 Display the import table.

.. option:: --coff-load-config

 Display the load config.

.. option:: --coff-resources

 Display the .rsrc section.

XCOFF SPECIFIC OPTIONS
----------------------

The following options are implemented only for the XCOFF file format.

.. option:: --auxiliary-header

  Display XCOFF Auxiliary header.

.. option:: --exception-section

  Display XCOFF exception section entries.

.. option:: --loader-section-header

  Display XCOFF loader section header.

.. option:: --loader-section-symbols

  Display symbol table of loader section.

.. option:: --loader-section-relocations

  Display relocation entries of loader section.

EXIT STATUS
-----------

:program:`llvm-readobj` returns 0 under normal operation. It returns a non-zero
exit code if there were any errors.

SEE ALSO
--------

:manpage:`llvm-nm(1)`, :manpage:`llvm-objdump(1)`, :manpage:`llvm-readelf(1)`
