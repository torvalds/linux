=====================
Clang Offload Bundler
=====================

.. contents::
   :local:

.. _clang-offload-bundler:

Introduction
============

For heterogeneous single source programming languages, use one or more
``--offload-arch=<target-id>`` Clang options to specify the target IDs of the
code to generate for the offload code regions.

The tool chain may perform multiple compilations of a translation unit to
produce separate code objects for the host and potentially multiple offloaded
devices. The ``clang-offload-bundler`` tool may be used as part of the tool
chain to combine these multiple code objects into a single bundled code object.

The tool chain may use a bundled code object as an intermediate step so that
each tool chain step consumes and produces a single file as in traditional
non-heterogeneous tool chains. The bundled code object contains the code objects
for the host and all the offload devices.

A bundled code object may also be used to bundle just the offloaded code
objects, and embedded as data into the host code object. The host compilation
includes an ``init`` function that will use the runtime corresponding to the
offload kind (see :ref:`clang-offload-kind-table`) to load the offload code
objects appropriate to the devices present when the host program is executed.

:program:`clang-offload-bundler` is located in
`clang/tools/clang-offload-bundler`.

.. code-block:: console

  $ clang-offload-bundler -help
  OVERVIEW: A tool to bundle several input files of the specified type <type>
  referring to the same source file but different targets into a single
  one. The resulting file can also be unbundled into different files by
  this tool if -unbundle is provided.

  USAGE: clang-offload-bundler [options]

  OPTIONS:

  Generic Options:

    --help                  - Display available options (--help-hidden for more)
    --help-list             - Display list of available options (--help-list-hidden for more)
    --version               - Display the version of this program

  clang-offload-bundler options:

    --###                   - Print any external commands that are to be executed instead of actually executing them - for testing purposes.
    --allow-missing-bundles - Create empty files if bundles are missing when unbundling.
    --bundle-align=<uint>   - Alignment of bundle for binary files
    --check-input-archive   - Check if input heterogeneous archive is valid in terms of TargetID rules.
    --inputs=<string>       - [<input file>,...]
    --list                  - List bundle IDs in the bundled file.
    --outputs=<string>      - [<output file>,...]
    --targets=<string>      - [<offload kind>-<target triple>,...]
    --type=<string>         - Type of the files to be bundled/unbundled.
                              Current supported types are:
                                i   - cpp-output
                                ii  - c++-cpp-output
                                cui - cuda/hip-output
                                d   - dependency
                                ll  - llvm
                                bc  - llvm-bc
                                s   - assembler
                                o   - object
                                a   - archive of bundled files
                                gch - precompiled-header
                                ast - clang AST file
    --unbundle              - Unbundle bundled file into several output files.

Usage
=====

This tool can be used as follows for bundling:

::

  clang-offload-bundler -targets=triple1,triple2 -type=ii -inputs=a.triple1.ii,a.triple2.ii -outputs=a.ii

or, it can be used as follows for unbundling:

::

  clang-offload-bundler -targets=triple1,triple2 -type=ii -outputs=a.triple1.ii,a.triple2.ii -inputs=a.ii -unbundle


Supported File Formats
======================

Multiple text and binary file formats are supported for bundling/unbundling. See
:ref:`supported-file-formats-table` for a list of currently supported input
formats. Use the ``File Type`` column to determine the value to pass to the
``--type`` option based on the type of input files while bundling/unbundling.

  .. table:: Supported File Formats
     :name: supported-file-formats-table

     +--------------------------+----------------+-------------+
     | File Format              | File Type      | Text/Binary |
     +==========================+================+=============+
     | CPP output               |        i       |     Text    |
     +--------------------------+----------------+-------------+
     | C++ CPP output           |       ii       |     Text    |
     +--------------------------+----------------+-------------+
     | CUDA/HIP output          |       cui      |     Text    |
     +--------------------------+----------------+-------------+
     | Dependency               |        d       |     Text    |
     +--------------------------+----------------+-------------+
     | LLVM                     |       ll       |     Text    |
     +--------------------------+----------------+-------------+
     | LLVM Bitcode             |       bc       |    Binary   |
     +--------------------------+----------------+-------------+
     | Assembler                |        s       |     Text    |
     +--------------------------+----------------+-------------+
     | Object                   |        o       |    Binary   |
     +--------------------------+----------------+-------------+
     | Archive of bundled files |        a       |    Binary   |
     +--------------------------+----------------+-------------+
     | Precompiled header       |       gch      |    Binary   |
     +--------------------------+----------------+-------------+
     | Clang AST file           |       ast      |    Binary   |
     +--------------------------+----------------+-------------+

.. _clang-bundled-code-object-layout-text:

Bundled Text File Layout
========================

The text file formats are concatenated with comments that have a magic string
and bundle entry ID in between. The BNF syntax to represent a code object
bundle file is:

::

  <file>    ::== <bundle> | <bundle> <file>
  <bundle>  ::== <comment> <start> <bundle_id> <eol> <bundle> <eol>
                 <comment> end <bundle_id> <eol>
  <start>   ::== OFFLOAD_BUNDLER_MAGIC_STR__START__
  <end>     ::== OFFLOAD_BUNDLER_MAGIC_STR__END__

**comment**
  The symbol used for starting single-line comment in the file type of
  constituting bundles. E.g. it is ";" for ll ``File Type`` and "#" for "s"
  ``File Type``.

**bundle_id**
  The :ref:`clang-bundle-entry-id` for the enclosing bundle.

**eol**
  The end of line character.

**bundle**
  The code object stored in one of the supported text file formats.

**OFFLOAD_BUNDLER_MAGIC_STR__**
  Magic string that marks the existence of offloading data i.e.
  "__CLANG_OFFLOAD_BUNDLE__".

.. _clang-bundled-code-object-layout:

Bundled Binary File Layout
==========================

The layout of a bundled code object is defined by the following table:

  .. table:: Bundled Code Object Layout
    :name: bundled-code-object-layout-table

    =================================== ======= ================ ===============================
    Field                               Type    Size in Bytes    Description
    =================================== ======= ================ ===============================
    Magic String                        string  24               ``__CLANG_OFFLOAD_BUNDLE__``
    Number Of Bundle Entries            integer 8                Number of bundle entries.
    1st Bundle Entry Code Object Offset integer 8                Byte offset from beginning of
                                                                 bundled code object to 1st code
                                                                 object.
    1st Bundle Entry Code Object Size   integer 8                Byte size of 1st code object.
    1st Bundle Entry ID Length          integer 8                Character length of bundle
                                                                 entry ID of 1st code object.
    1st Bundle Entry ID                 string  1st Bundle Entry Bundle entry ID of 1st code
                                                ID Length        object. This is not NUL
                                                                 terminated. See
                                                                 :ref:`clang-bundle-entry-id`.
    \...
    Nth Bundle Entry Code Object Offset integer 8
    Nth Bundle Entry Code Object Size   integer 8
    Nth Bundle Entry ID Length          integer 8
    Nth Bundle Entry ID                 string  1st Bundle Entry
                                                ID Length
    1st Bundle Entry Code Object        bytes   1st Bundle Entry
                                                Code Object Size
    \...
    Nth Bundle Entry Code Object        bytes   Nth Bundle Entry
                                                Code Object Size
    =================================== ======= ================ ===============================

.. _clang-bundle-entry-id:

Bundle Entry ID
===============

Each entry in a bundled code object (see :ref:`clang-bundled-code-object-layout-text`
and :ref:`clang-bundled-code-object-layout`) has a bundle entry ID that indicates
the kind of the entry's code object and the runtime that manages it.

Bundle entry ID syntax is defined by the following BNF syntax:

.. code::

  <bundle-entry-id> ::== <offload-kind> "-" <target-triple> [ "-" <target-id> ]

Where:

**offload-kind**
  The runtime responsible for managing the bundled entry code object. See
  :ref:`clang-offload-kind-table`.

  .. table:: Bundled Code Object Offload Kind
      :name: clang-offload-kind-table

      ============= ==============================================================
      Offload Kind  Description
      ============= ==============================================================
      host          Host code object. ``clang-offload-bundler`` always includes
                    this entry as the first bundled code object entry. For an
                    embedded bundled code object this entry is not used by the
                    runtime and so is generally an empty code object.

      hip           Offload code object for the HIP language. Used for all
                    HIP language offload code objects when the
                    ``clang-offload-bundler`` is used to bundle code objects as
                    intermediate steps of the tool chain. Also used for AMD GPU
                    code objects before ABI version V4 when the
                    ``clang-offload-bundler`` is used to create a *fat binary*
                    to be loaded by the HIP runtime. The fat binary can be
                    loaded directly from a file, or be embedded in the host code
                    object as a data section with the name ``.hip_fatbin``.

      hipv4         Offload code object for the HIP language. Used for AMD GPU
                    code objects with at least ABI version V4 and above when the
                    ``clang-offload-bundler`` is used to create a *fat binary*
                    to be loaded by the HIP runtime. The fat binary can be
                    loaded directly from a file, or be embedded in the host code
                    object as a data section with the name ``.hip_fatbin``.

      openmp        Offload code object for the OpenMP language extension.
      ============= ==============================================================

Note: The distinction between the `hip` and `hipv4` offload kinds is historically based.
Originally, these designations might have indicated different versions of the
code object ABI. However, as the system has evolved, the ABI version is now embedded
directly within the code object itself, making these historical distinctions irrelevant
during the unbundling process. Consequently, `hip` and `hipv4` are treated as compatible
in current implementations, facilitating interchangeable handling of code objects
without differentiation based on offload kind.

**target-triple**
    The target triple of the code object. See `Target Triple
    <https://clang.llvm.org/docs/CrossCompilation.html#target-triple>`_.

    The bundler accepts target triples with or without the optional environment
    field:

    ``<arch><sub>-<vendor>-<sys>``, or
    ``<arch><sub>-<vendor>-<sys>-<env>``

    However, in order to standardize outputs for tools that consume bitcode
    bundles, bundles written by the bundler internally use only the 4-field
    target triple:

    ``<arch><sub>-<vendor>-<sys>-<env>``

**target-id**
  The canonical target ID of the code object. Present only if the target
  supports a target ID. See :ref:`clang-target-id`.

.. _code-object-composition:

Bundled Code Object Composition
-------------------------------

  * Each entry of a bundled code object must have a different bundle entry ID.
  * There can be multiple entries for the same processor provided they differ
    in target feature settings.
  * If there is an entry with a target feature specified as *Any*, then all
    entries must specify that target feature as *Any* for the same processor.

There may be additional target specific restrictions.

.. _compatibility-bundle-entry-id:

Compatibility Rules for Bundle Entry ID
---------------------------------------

  A code object, specified using its Bundle Entry ID, can be loaded and
  executed on a target processor, if:

  * Their offload kinds are the same or comptible.
  * Their target triples are compatible.
  * Their Target IDs are compatible as defined in :ref:`compatibility-target-id`.

.. _clang-target-id:

Target ID
=========

A target ID is used to indicate the processor and optionally its configuration,
expressed by a set of target features, that affect ISA generation. It is target
specific if a target ID is supported, or if the target triple alone is
sufficient to specify the ISA generation.

It is used with the ``-mcpu=<target-id>`` and ``--offload-arch=<target-id>``
Clang compilation options to specify the kind of code to generate.

It is also used as part of the bundle entry ID to identify the code object. See
:ref:`clang-bundle-entry-id`.

Target ID syntax is defined by the following BNF syntax:

.. code::

  <target-id> ::== <processor> ( ":" <target-feature> ( "+" | "-" ) )*

Where:

**processor**
  Is a the target specific processor or any alternative processor name.

**target-feature**
  Is a target feature name that is supported by the processor. Each target
  feature must appear at most once in a target ID and can have one of three
  values:

  *Any*
    Specified by omitting the target feature from the target ID.
    A code object compiled with a target ID specifying the default
    value of a target feature can be loaded and executed on a processor
    configured with the target feature on or off.

  *On*
    Specified by ``+``, indicating the target feature is enabled. A code
    object compiled with a target ID specifying a target feature on
    can only be loaded on a processor configured with the target feature on.

  *Off*
    specified by ``-``, indicating the target feature is disabled. A code
    object compiled with a target ID specifying a target feature off
    can only be loaded on a processor configured with the target feature off.

.. _compatibility-target-id:

Compatibility Rules for Target ID
---------------------------------

  A code object compiled for a Target ID is considered compatible for a
  target, if:

  * Their processor is same.
  * Their feature set is compatible as defined above.

There are two forms of target ID:

*Non-Canonical Form*
  The non-canonical form is used as the input to user commands to allow the user
  greater convenience. It allows both the primary and alternative processor name
  to be used and the target features may be specified in any order.

*Canonical Form*
  The canonical form is used for all generated output to allow greater
  convenience for tools that consume the information. It is also used for
  internal passing of information between tools. Only the primary and not
  alternative processor name is used and the target features are specified in
  alphabetic order. Command line tools convert non-canonical form to canonical
  form.

Target Specific information
===========================

Target specific information is available for the following:

*AMD GPU*
  AMD GPU supports target ID and target features. See `User Guide for AMDGPU Backend
  <https://llvm.org/docs/AMDGPUUsage.html>`_ which defines the `processors
  <https://llvm.org/docs/AMDGPUUsage.html#amdgpu-processors>`_ and `target
  features <https://llvm.org/docs/AMDGPUUsage.html#amdgpu-target-features>`_
  supported.

Most other targets do not support target IDs.

Archive Unbundling
==================

Unbundling of a heterogeneous device archive (HDA) is done to create device specific
archives. HDA is in a format compatible with GNU ``ar`` utility and contains a
collection of bundled device binaries where each bundle file will contain
device binaries for a host and one or more targets. The output device-specific
archive is in a format compatible with GNU ``ar`` utility and contains a
collection of device binaries for a specific target.

::

  Heterogeneous Device Archive, HDA = {F1.X, F2.X, ..., FN.Y}
  where, Fi = Bundle{Host-DeviceBinary, T1-DeviceBinary, T2-DeviceBinary, ...,
                     Tm-DeviceBinary},
         Ti = {Target i, qualified using Bundle Entry ID},
         X/Y = \*.bc for AMDGPU and \*.cubin for NVPTX

  Device Specific Archive, DSA(Tk) = {F1-Tk-DeviceBinary.X, F2-Tk-DeviceBinary.X, ...
                                      FN-Tk-DeviceBinary.Y}
  where, Fi-Tj-DeviceBinary.X represents device binary of i-th bundled device
  binary file for target Tj.

The clang-offload-bundler extracts compatible device binaries for a given target
from the bundled device binaries in a heterogeneous device archive and creates
a target-specific device archive without bundling.

The clang-offload-bundler determines whether a device binary is compatible
with a target by comparing bundle IDs. Two bundle IDs are considered
compatible if:

  * Their offload kinds are the same
  * Their target triples are the same
  * Their Target IDs are the same

Creating a Heterogeneous Device Archive
---------------------------------------

1. Compile source file(s) to generate object file(s)

  ::

    clang -O2 -fopenmp -fopenmp-targets=amdgcn-amd-amdhsa,amdgcn-amd-amdhsa,\
       nvptx64-nvidia-cuda, nvptx64-nvidia-cuda \
      -Xopenmp-target=amdgcn-amd-amdhsa -march=gfx906:sramecc-:xnack+ \
      -Xopenmp-target=amdgcn-amd-amdhsa -march=gfx906:sramecc+:xnack+ \
      -Xopenmp-target=nvptx64-nvidia-cuda -march=sm_70 \
      -Xopenmp-target=nvptx64-nvidia-cuda -march=sm_80 \
      -c func_1.c -o func_1.o

    clang -O2 -fopenmp -fopenmp-targets=amdgcn-amd-amdhsa,amdgcn-amd-amdhsa,
      nvptx64-nvidia-cuda, nvptx64-nvidia-cuda \
      -Xopenmp-target=amdgcn-amd-amdhsa -march=gfx906:sramecc-:xnack+ \
      -Xopenmp-target=amdgcn-amd-amdhsa -march=gfx906:sramecc+:xnack+ \
      -Xopenmp-target=nvptx64-nvidia-cuda -march=sm_70 \
      -Xopenmp-target=nvptx64-nvidia-cuda -march=sm_80 \
      -c func_2.c -o func_2.o

2. Create a heterogeneous device archive by combining all the object file(s)

  ::

    llvm-ar cr libFatArchive.a func_1.o func_2.o

Extracting a Device Specific Archive
------------------------------------

UnbundleArchive takes a heterogeneous device archive file (".a") as input
containing bundled device binary files, and a list of offload targets (not
host), and extracts the device binaries into a new archive file for each
offload target. Each resulting archive file contains all device binaries
compatible with that particular offload target. Compatibility between a
device binary in HDA and a target is based on the compatibility between their
bundle entry IDs as defined in :ref:`compatibility-bundle-entry-id`.

Following cases may arise during compatibility testing:

* A binary is compatible with one or more targets: Insert the binary into the
  device-specific archive of each compatible target.
* A binary is not compatible with any target: Skip the binary.
* One or more binaries are compatible with a target: Insert all binaries into
  the device-specific archive of the target. The insertion need not be ordered.
* No binary is compatible with a target: If ``allow-missing-bundles`` option is
  present then create an empty archive for the target. Otherwise, produce an
  error without creating an archive.

The created archive file does not contain an index of the symbols and device
binary files are named as <<Parent Bundle Name>-<DeviceBinary's TargetID>>,
with ':' replaced with '_'.

Usage
-----

::

  clang-offload-bundler --unbundle --inputs=libFatArchive.a -type=a \
   -targets=openmp-amdgcn-amdhsa-gfx906:sramecc+:xnack+, \
            openmp-amdgcn-amdhsa-gfx908:sramecc-:xnack+  \
   -outputs=devicelib-gfx906.a,deviceLib-gfx908.a

.. _additional-options-archive-unbundling:

Additional Options while Archive Unbundling
-------------------------------------------

**-allow-missing-bundles**
  Create an empty archive file if no compatible device binary is found.

**-check-input-archive**
  Check if input heterogeneous device archive follows rules for composition
  as defined in :ref:`code-object-composition` before creating device-specific
  archive(s).

**-debug-only=CodeObjectCompatibility**
  Verbose printing of matched/unmatched comparisons between bundle entry id of
  a device binary from HDA and bundle entry ID of a given target processor
  (see :ref:`compatibility-bundle-entry-id`).

Compression and Decompression
=============================

``clang-offload-bundler`` provides features to compress and decompress the full
bundle, leveraging inherent redundancies within the bundle entries. Use the
`-compress` command-line option to enable this compression capability.

The compressed offload bundle begins with a header followed by the compressed binary data:

- **Magic Number (4 bytes)**:
    This is a unique identifier to distinguish compressed offload bundles. The value is the string 'CCOB' (Compressed Clang Offload Bundle).

- **Version Number (16-bit unsigned int)**:
    This denotes the version of the compressed offload bundle format. The current version is `2`.

- **Compression Method (16-bit unsigned int)**:
    This field indicates the compression method used. The value corresponds to either `zlib` or `zstd`, represented as a 16-bit unsigned integer cast from the LLVM compression enumeration.

- **Total File Size (32-bit unsigned int)**:
    This is the total size (in bytes) of the file, including the header. Available in version 2 and above.

- **Uncompressed Binary Size (32-bit unsigned int)**:
    This is the size (in bytes) of the binary data before it was compressed.

- **Hash (64-bit unsigned int)**:
    This is a 64-bit truncated MD5 hash of the uncompressed binary data. It serves for verification and caching purposes.

- **Compressed Data**:
    The actual compressed binary data follows the header. Its size can be inferred from the total size of the file minus the header size.
