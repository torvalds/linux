llvm-dwarfdump - dump and verify DWARF debug information
========================================================

.. program:: llvm-dwarfdump

SYNOPSIS
--------

:program:`llvm-dwarfdump` [*options*] [*filename ...*]

DESCRIPTION
-----------

:program:`llvm-dwarfdump` parses DWARF sections in object files,
archives, and `.dSYM` bundles and prints their contents in
human-readable form. Only the .debug_info section is printed unless one of
the section-specific options or :option:`--all` is specified.

If no input file is specified, `a.out` is used instead. If `-` is used as the
input file, :program:`llvm-dwarfdump` reads the input from its standard input
stream.

OPTIONS
-------

.. option:: -a, --all

            Dump all supported DWARF sections.

.. option:: --arch=<arch>

            Dump DWARF debug information for the specified CPU architecture.
            Architectures may be specified by name or by number.  This
            option can be specified multiple times, once for each desired
            architecture.  All CPU architectures will be printed by
            default.

.. option:: -c, --show-children

            Show a debug info entry's children when selectively printing with
            the `=<offset>` argument of :option:`--debug-info`, or options such
            as :option:`--find` or :option:`--name`.

.. option:: --color

            Use colors in output.

.. option:: -f <name>, --find=<name>

            Search for the exact text <name> in the accelerator tables
            and print the matching debug information entries.
            When there is no accelerator tables or the name of the DIE
            you are looking for is not found in the accelerator tables,
            try using the slower but more complete :option:`--name` option.

.. option:: -F, --show-form

            Show DWARF form types after the DWARF attribute types.

.. option:: -h, --help

            Show help and usage for this command.

.. option:: --help-list

            Show help and usage for this command without grouping the options
            into categories.

.. option:: -i, --ignore-case

            Ignore case distinctions when using :option:`--name`.

.. option:: -n <name>, --name=<name>

            Find and print all debug info entries whose name
            (`DW_AT_name` attribute) is <name>.

.. option:: --lookup=<address>

            Look up <address> in the debug information and print out the file,
            function, block, and line table details.

.. option:: -o <path>

            Redirect output to a file specified by <path>, where `-` is the
            standard output stream.

.. option:: -p, --show-parents

            Show a debug info entry's parents when selectively printing with
            the `=<offset>` argument of :option:`--debug-info`, or options such
            as :option:`--find` or :option:`--name`.

.. option:: --parent-recurse-depth=<N>

            When displaying debug info entry parents, only show them to a
            maximum depth of <N>.

.. option:: --quiet

            Use with :option:`--verify` to not emit to `STDOUT`.

.. option:: -r <N>, --recurse-depth=<N>

            When displaying debug info entries, only show children to a maximum
            depth of <N>.

.. option:: --show-section-sizes

            Show the sizes of all debug sections, expressed in bytes.

.. option:: --show-sources

            Print all source files mentioned in the debug information. Absolute
            paths are given whenever possible.

.. option:: --statistics

            Collect debug info quality metrics and print the results
            as machine-readable single-line JSON output. The output
            format is described in the section below (:ref:`stats-format`).

.. option:: --summarize-types

            Abbreviate the description of type unit entries.

.. option:: -x, --regex

            Treat any <name> strings as regular expressions when searching
            with :option:`--name`. If :option:`--ignore-case` is also specified,
            the regular expression becomes case-insensitive.

.. option:: -u, --uuid

            Show the UUID for each architecture.

.. option:: --diff

            Dump the output in a format that is more friendly for comparing
            DWARF output from two different files.

.. option:: -v, --verbose

            Display verbose information when dumping. This can help to debug
            DWARF issues.

.. option:: --verify

            Verify the structure of the DWARF information by verifying the
            compile unit chains, DIE relationships graph, address
            ranges, and more.

.. option:: --version

            Display the version of the tool.

.. option:: --debug-abbrev, --debug-addr, --debug-aranges, --debug-cu-index, --debug-frame [=<offset>], --debug-gnu-pubnames, --debug-gnu-pubtypes, --debug-info [=<offset>], --debug-line [=<offset>], --debug-line-str, --debug-loc [=<offset>], --debug-loclists [=<offset>], --debug-macro, --debug-names, --debug-pubnames, --debug-pubtypes, --debug-ranges, --debug-rnglists, --debug-str, --debug-str-offsets, --debug-tu-index, --debug-types [=<offset>], --eh-frame [=<offset>], --gdb-index, --apple-names, --apple-types, --apple-namespaces, --apple-objc

            Dump the specified DWARF section by name. Only the
            `.debug_info` section is shown by default. Some entries
            support adding an `=<offset>` as a way to provide an
            optional offset of the exact entry to dump within the
            respective section. When an offset is provided, only the
            entry at that offset will be dumped, else the entire
            section will be dumped.

            The :option:`--debug-macro` option prints both the .debug_macro and the .debug_macinfo sections.

            The :option:`--debug-frame` and :option:`--eh-frame` options are aliases, in cases where both sections are present one command outputs both.

.. option:: @<FILE>

            Read command-line options from `<FILE>`.

.. _stats-format:

FORMAT OF STATISTICS OUTPUT
---------------------------

The :option:`--statistics` option generates single-line JSON output
representing quality metrics of the processed debug info. These metrics are
useful to compare changes between two compilers, particularly for judging
the effect that a change to the compiler has on the debug info quality.

The output is formatted as key-value pairs. The first pair contains a version
number. The following naming scheme is used for the keys:

      - `variables` ==> local variables and parameters
      - `local vars` ==> local variables
      - `params` ==> formal parameters

For aggregated values, the following keys are used:

      - `sum_of_all_variables(...)` ==> the sum applied to all variables
      - `#bytes` ==> the number of bytes
      - `#variables - entry values ...` ==> the number of variables excluding
        the entry values etc.

EXIT STATUS
-----------

:program:`llvm-dwarfdump` returns 0 if the input files were parsed and dumped
successfully. Otherwise, it returns 1.

SEE ALSO
--------

:manpage:`dsymutil(1)`
