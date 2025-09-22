dsymutil - manipulate archived DWARF debug symbol files
=======================================================

.. program:: dsymutil

SYNOPSIS
--------

| :program:`dsymutil` [*options*] *executable*

DESCRIPTION
-----------

:program:`dsymutil` links the DWARF debug information found in the object files
for an executable *executable* by using debug symbols information contained in
its symbol table. By default, the linked debug information is placed in a
``.dSYM`` bundle with the same name as the executable.

OPTIONS
-------
.. option:: --accelerator=<accelerator type>

 Specify the desired type of accelerator table. Valid options are 'Apple',
 'Dwarf', 'Default' and 'None'.

.. option:: --arch <arch>

 Link DWARF debug information only for specified CPU architecture types.
 Architectures may be specified by name. When using this option, an error will
 be returned if any architectures can not be properly linked.  This option can
 be specified multiple times, once for each desired architecture. All CPU
 architectures will be linked by default and any architectures that can't be
 properly linked will cause :program:`dsymutil` to return an error.

.. option:: --build-variant-suffix <suffix=buildvariant>

 Specify the build variant suffix used to build the executable file.
 There can be multiple variants for the binary of a product, each built
 slightly differently. The most common build variants are 'debug' and
 'profile'. Setting the DYLD_IMAGE_SUFFIX environment variable will
 cause dyld to load the specified variant at runtime.

.. option:: --dump-debug-map

 Dump the *executable*'s debug-map (the list of the object files containing the
 debug information) in YAML format and exit. No DWARF link will take place.

 .. option:: -D <path>

 Specify a directory that contain dSYM files to search for.
 This is used for mergeable libraries, so dsymutil knows where to look
 for dSYM files with  debug information about symbols present in those
 libraries.

.. option:: --fat64

 Use a 64-bit header when emitting universal binaries.

.. option:: --flat, -f

 Produce a flat dSYM file. A ``.dwarf`` extension will be appended to the
 executable name unless the output file is specified using the ``-o`` option.

.. option:: --gen-reproducer

 Generate a reproducer consisting of the input object files. Alias for
 --reproducer=GenerateOnExit.

.. option:: --help, -h

 Print this help output.

.. option:: --keep-function-for-static

 Make a static variable keep the enclosing function even if it would have been
 omitted otherwise.

.. option:: --minimize, -z

 When used when creating a dSYM file, this option will suppress the emission of
 the .debug_inlines, .debug_pubnames, and .debug_pubtypes sections since
 dsymutil currently has better equivalents: .apple_names and .apple_types. When
 used in conjunction with ``--update`` option, this option will cause redundant
 accelerator tables to be removed.

.. option:: --no-odr

 Do not use ODR (One Definition Rule) for uniquing C++ types.

.. option:: --no-output

 Do the link in memory, but do not emit the result file.

.. option:: --no-swiftmodule-timestamp

 Don't check the timestamp for swiftmodule files.

.. option:: --num-threads <threads>, -j <threads>

 Specifies the maximum number (``n``) of simultaneous threads to use when
 linking multiple architectures.

.. option:: --object-prefix-map <prefix=remapped>

 Remap object file paths (but no source paths) before processing.  Use
 this for Clang objects where the module cache location was remapped using
 ``-fdebug-prefix-map``; to help dsymutil find the Clang module cache.

.. option:: --oso-prepend-path <path>

 Specifies a ``path`` to prepend to all debug symbol object file paths.

.. option:: --out <filename>, -o <filename>

 Specifies an alternate ``path`` to place the dSYM bundle. The default dSYM
 bundle path is created by appending ``.dSYM`` to the executable name.

.. option:: -q, --quiet

 Enable quiet mode and limit output.

.. option:: --remarks-drop-without-debug

 Drop remarks without valid debug locations. Without this flags, all remarks are kept.

.. option:: --remarks-output-format <format>

 Specify the format to be used when serializing the linked remarks.

.. option:: --remarks-prepend-path <path>

 Specify a directory to prepend the paths of the external remark files.

.. option:: --reproducer <mode>

 Specify the reproducer generation mode. Valid options are 'GenerateOnExit',
 'GenerateOnCrash', 'Use', 'Off'.

.. option:: --statistics

 Print statistics about the contribution of each object file to the linked
 debug info. This prints a table after linking with the object file name, the
 size of the debug info in the object file (in bytes) and the size contributed
 (in bytes) to the linked dSYM. The table is sorted by the output size listing
 the object files with the largest contribution first.

.. option:: -s, --symtab

 Dumps the symbol table found in *executable* or object file(s) and exits.

.. option:: -S

 Output textual assembly instead of a binary dSYM companion file.

.. option:: --toolchain <toolchain>

 Embed the toolchain in the dSYM bundle's property list.

.. option:: -u, --update

 Update an existing dSYM file to contain the latest accelerator tables and
 other DWARF optimizations. This option will rebuild the '.apple_names' and
 '.apple_types' hashed accelerator tables.

.. option:: --use-reproducer <path>

 Use the object files from the given reproducer path. Alias for
 --reproducer=Use.

.. option:: --verbose

 Display verbose information when linking.

.. option:: --verify

 Run the DWARF verifier on the linked DWARF debug info.

.. option:: -v, --version

 Display the version of the tool.

.. option:: -y

 Treat *executable* as a YAML debug-map rather than an executable.

EXIT STATUS
-----------

:program:`dsymutil` returns 0 if the DWARF debug information was linked
successfully. Otherwise, it returns 1.

SEE ALSO
--------

:manpage:`llvm-dwarfdump(1)`
