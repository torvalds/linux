========
Multilib
========

Introduction
============

This document describes how multilib is implemented in Clang.

What is multilib and why might you care?
If you're :doc:`cross compiling<CrossCompilation>` then you can't use native
system headers and libraries. To address this, you can use a combination of
``--sysroot``, ``-isystem`` and ``-L`` options to point Clang at suitable
directories for your target.
However, when there are many possible directories to choose from, it's not
necessarily obvious which one to pick.
Multilib allows a toolchain designer to imbue the toolchain with the ability to
pick a suitable directory automatically, based on the options the user provides
to Clang. For example, if the user specifies
``--target=arm-none-eabi -mcpu=cortex-m4`` the toolchain can choose a directory
containing headers and libraries suitable for Armv7E-M, because it knows that's
a suitable architecture for Arm Cortex-M4.
Multilib can also choose between libraries for the same architecture based on
other options. For example if the user specifies ``-fno-exceptions`` then a
toolchain could select libraries built without exception support, thereby
reducing the size of the resulting binary.

Design
======

Clang supports GCC's ``-print-multi-lib`` and ``-print-multi-directory``
options. These are described in
`GCC Developer Options <https://gcc.gnu.org/onlinedocs/gcc-12.2.0/gcc/Developer-Options.html>`_.

There are two ways to configure multilib in Clang: hard-coded or via a
configuration file.

Hard-coded Multilib
===================

The available libraries can be hard-coded in Clang. Typically this is done
using the ``MultilibBuilder`` interface in
``clang/include/clang/Driver/MultilibBuilder.h``.
There are many examples of this in ``lib/Driver/ToolChains/Gnu.cpp``.
The remainder of this document will not focus on this type of multilib.

EXPERIMENTAL Multilib via configuration file
============================================

Some Clang toolchains support loading multilib configuration from a
``multilib.yaml`` configuration file.

A ``multilib.yaml`` configuration file specifies which multilib variants are
available, their relative location, what compilation options were used to build
them, and the criteria by which they are selected.

Multilib processing
===================

Clang goes through the following steps to use multilib from a configuration
file:

#. Normalize command line options. Clang can accept the same
   information via different options - for example,
   ``--target=arm-none-eabi -march=armv7-m`` and
   ``--target=armv7m-none-eabi`` are equivalent.
   Clang normalizes the command line before passing them to the multilib system.
   To see what flags are emitted for a given set of command line options, use
   the ``-print-multi-flags-experimental`` command line option
   along with the rest of the options you want to use.
#. Load ``multilib.yaml`` from sysroot.
#. Generate additional flags. ``multilib.yaml`` contains a ``Mappings`` section,
   which specifies how to generate additional flags based on the flags derived
   from command line options. Flags are matched using regular expressions.
   These regular expressions shall use the POSIX extended regular expression
   syntax.
#. Match flags against multilib variants. If the generated flags are a superset
   of the flags specified for a multilib variant then the variant is considered
   a match.
   If more than one variant matches then a toolchain may opt to either use only
   the *last* matching multilib variant, or may use all matching variants,
   thereby :ref:`layering<multilib-layering>` them.
#. Generate ``-isystem`` and ``-L`` options. Iterate in reverse order over
   the matching multilib variants, and generate ``-isystem`` and ``-L``
   options based on the multilib variant's directory.

.. _multilib-layering:

Multilib layering
=================

When Clang selects multilib variants, it may find that more than one variant
matches.

It is up to the ToolChain subclass to decide what to do in this case.
There are two options permitted:

#. Use only the *last* matching multilib variant. This option exists primarily
   for compatibility with the previous multilib design.
#. Use all matching variants, thereby layering them.

This decision is hard-coded per ToolChain subclass. The latter option is
preferred for ToolChain subclasses without backwards compatibility
requirements.

If the latter option is chosen then ``-isystem`` and ``-L`` options will be
generated for each matching multilib variant, in reverse order.

This means that the compiler or linker will find files in the last matching
multilib variant that has the given file.
This behaviour permits multilib variants with only a partial set of files.
This means a toolchain can be distributed with one base multilib variant
containing all system headers and includes, and more specialised multilib
variants containing only files that are different to those in the base variant.

For example, a multilib variant could be compiled with ``-fno-exceptions``.
This option doesn't affect the content of header files, nor does it affect the
C libraries. Therefore if multilib layering is supported by the ToolChain
subclass and a suitable base multilib variant is present then the
``-fno-exceptions`` multilib variant need only contain C++ libraries.

It is the responsibility of layered multilib authors to ensure that headers and
libraries in each layer are complete enough to mask any incompatibilities.

Stability
=========

Multilib via configuration file shall be considered an experimental feature
until LLVM 18, at which point ``-print-multi-flags-experimental``
should be renamed to ``-print-multi-flags``.
A toolchain can opt in to using this feature by including a ``multilib.yaml``
file in its distribution, once support for it is added in relevant ToolChain
subclasses.
Once stability is reached, flags emitted by ``-print-multi-flags``
should not be removed or changed, although new flags may be added.

Restrictions
============

Despite the name, multilib is used to locate both ``include`` and ``lib``
directories. Therefore it is important that consistent options are passed to
the Clang driver when both compiling and linking. Otherwise inconsistent
``include`` and ``lib`` directories may be used, and the results will be
undefined.

EXPERIMENTAL multilib.yaml
==========================

The below example serves as a small of a possible multilib, and documents
the available options.

For a more comprehensive example see
``clang/test/Driver/baremetal-multilib.yaml`` in the ``llvm-project`` sources.

.. code-block:: yaml

  # multilib.yaml

  # This format is experimental and is likely to change!

  # Syntax is YAML 1.2

  # This required field defines the version of the multilib.yaml format.
  # Clang will emit an error if this number is greater than its current multilib
  # version or if its major version differs, but will accept lesser minor
  # versions.
  MultilibVersion: 1.0

  # The rest of this file is in two parts:
  # 1. A list of multilib variants.
  # 2. A list of regular expressions that may match flags generated from
  #    command line options, and further flags that shall be added if the
  #    regular expression matches.
  # It is acceptable for the file to contain properties not documented here,
  # and these will be ignored by Clang.

  # List of multilib variants. Required.
  # The ordering of items in the variants list is important if more than one
  # variant can match the same set of flags. See the docs on multilib layering
  # for more info.
  Variants:

  # Example of a multilib variant targeting Arm v6-M.
  # Dir is the relative location of the directory containing the headers
  # and/or libraries.
  # Exactly how Dir is used is left up to the ToolChain subclass to define, but
  # typically it will be joined to the sysroot.
  - Dir: thumb/v6-m
    # List of one or more normalized command line options, as generated by Clang
    # from the command line options or from Mappings below.
    # Here, if the flags are a superset of {target=thumbv6m-unknown-none-eabi}
    # then this multilib variant will be considered a match.
    Flags: [--target=thumbv6m-unknown-none-eabi]

  # Similarly, a multilib variant targeting Arm v7-M with an FPU (floating
  # point unit).
  - Dir: thumb/v7-m
    # Here, the flags generated by Clang must be a superset of
    # {--target=thumbv7m-none-eabi, -mfpu=fpv4-sp-d16} for this multilib variant
    # to be a match.
    Flags: [--target=thumbv7m-none-eabi, -mfpu=fpv4-sp-d16]


  # The second section of the file is a list of regular expressions that are
  # used to map from flags generated from command line options to custom flags.
  # This is optional.
  # Each regular expression must match a whole flag string.
  # Flags in the "Flags" list will be added if any flag generated from command
  # line options matches the regular expression.
  Mappings:

  # Set a "--target=thumbv7m-none-eabi" flag if the regular expression matches
  # any of the flags generated from the command line options.
  # Match is a POSIX extended regular expression string.
  - Match: --target=thumbv([7-9]|[1-9][0-9]+).*
    # Flags is a list of one or more strings.
    Flags: [--target=thumbv7m-none-eabi]

Design principles
=================

Stable interface
----------------

``multilib.yaml`` and ``-print-multi-flags-experimental`` are new
interfaces to Clang. In order for them to be usable over time and across LLVM
versions their interfaces should be stable.
The new multilib system will be considered experimental in LLVM 17, but in
LLVM 18 it will be stable. In particular this is important to which multilib
selection flags Clang generates from command line options. Once a flag is
generated by a released version of Clang it may be used in ``multilib.yaml``
files that exist independently of the LLVM release cycle, and therefore
ceasing to generate the flag would be a breaking change and should be
avoided.

However, an exception is the normalization of ``-march``.
``-march`` for Arm architectures contains a list of enabled and disabled
extensions and this list is likely to grow. Therefore ``-march`` flags are
unstable.

Incomplete interface
--------------------

The new multilib system does multilib selection based on only a limited set of
command line options, and limits which flags can be used for multilib
selection. This is in order to avoid committing to too large an interface.
Later LLVM versions can add support for multilib selection from more command
line options as needed.

Extensible
----------

It is likely that the configuration format will need to evolve in future to
adapt to new requirements.
Using a format like YAML that supports key-value pairs helps here as it's
trivial to add new keys alongside existing ones.

Backwards compatibility
-----------------------

New versions of Clang should be able to use configuration written for earlier
Clang versions.
To avoid behaving in a way that may be subtly incorrect, Clang should be able
to detect if the configuration is too new and emit an error.

Forwards compatibility
----------------------

As an author of a multilib configuration, it should be possible to design the
configuration in such a way that it is likely to work well with future Clang
versions. For example, if a future version of Clang is likely to add support
for newer versions of an architecture and the architecture is known to be
designed for backwards compatibility then it should be possible to express
compatibility for such architecture versions in the multilib configuration.

Not GNU spec files
------------------

The GNU spec files standard is large and complex and there's little desire to
import that complexity to LLVM. It's also heavily oriented towards processing
command line argument strings which is hard to do correctly, hence the large
amount of logic dedicated to that task in the Clang driver. While compatibility
with GNU would bring benefits, the cost in this case is deemed too high.

Avoid re-inventing feature detection in the configuration
---------------------------------------------------------

A large amount of logic in the Clang driver is dedicated to inferring which
architectural features are available based on the given command line options.
It is neither desirable nor practical to repeat such logic in each multilib
configuration. Instead the configuration should be able to benefit from the
heavy lifting Clang already does to detect features.

Low maintenance
---------------

Multilib is a relatively small feature in the scheme of things so supporting it
should accordingly take little time. Where possible this should be achieved by
implementing it in terms of existing features in the LLVM codebase.

Minimal additional API surface
------------------------------

The greater the API surface, the greater the difficulty of keeping it stable.
Where possible the additional API surface should be kept small by defining it
in relation to existing APIs. An example of this is keeping a simple
relationship between flag names and command line options where possible.
Since the command line options are part of a stable API they are unlikely
to change, and therefore the flag names get the same stability.

Low compile-time overhead
-------------------------

If the process of selecting multilib directories must be done on every
invocation of the Clang driver then it must have a negligible impact on
overall compile time.
