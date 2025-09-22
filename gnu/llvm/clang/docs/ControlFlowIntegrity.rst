======================
Control Flow Integrity
======================

.. toctree::
   :hidden:

   ControlFlowIntegrityDesign

.. contents::
   :local:

Introduction
============

Clang includes an implementation of a number of control flow integrity (CFI)
schemes, which are designed to abort the program upon detecting certain forms
of undefined behavior that can potentially allow attackers to subvert the
program's control flow. These schemes have been optimized for performance,
allowing developers to enable them in release builds.

To enable Clang's available CFI schemes, use the flag ``-fsanitize=cfi``.
You can also enable a subset of available :ref:`schemes <cfi-schemes>`.
As currently implemented, all schemes rely on link-time optimization (LTO);
so it is required to specify ``-flto``, and the linker used must support LTO,
for example via the `gold plugin`_.

To allow the checks to be implemented efficiently, the program must
be structured such that certain object files are compiled with CFI
enabled, and are statically linked into the program. This may preclude
the use of shared libraries in some cases.

The compiler will only produce CFI checks for a class if it can infer hidden
LTO visibility for that class. LTO visibility is a property of a class that
is inferred from flags and attributes. For more details, see the documentation
for :doc:`LTO visibility <LTOVisibility>`.

The ``-fsanitize=cfi-{vcall,nvcall,derived-cast,unrelated-cast}`` flags
require that a ``-fvisibility=`` flag also be specified. This is because the
default visibility setting is ``-fvisibility=default``, which would disable
CFI checks for classes without visibility attributes. Most users will want
to specify ``-fvisibility=hidden``, which enables CFI checks for such classes.

Experimental support for :ref:`cross-DSO control flow integrity
<cfi-cross-dso>` exists that does not require classes to have hidden LTO
visibility. This cross-DSO support has unstable ABI at this time.

.. _gold plugin: https://llvm.org/docs/GoldPlugin.html

.. _cfi-schemes:

Available schemes
=================

Available schemes are:

  -  ``-fsanitize=cfi-cast-strict``: Enables :ref:`strict cast checks
     <cfi-strictness>`.
  -  ``-fsanitize=cfi-derived-cast``: Base-to-derived cast to the wrong
     dynamic type.
  -  ``-fsanitize=cfi-unrelated-cast``: Cast from ``void*`` or another
     unrelated type to the wrong dynamic type.
  -  ``-fsanitize=cfi-nvcall``: Non-virtual call via an object whose vptr is of
     the wrong dynamic type.
  -  ``-fsanitize=cfi-vcall``: Virtual call via an object whose vptr is of the
     wrong dynamic type.
  -  ``-fsanitize=cfi-icall``: Indirect call of a function with wrong dynamic
     type.
  -  ``-fsanitize=cfi-mfcall``: Indirect call via a member function pointer with
     wrong dynamic type.

You can use ``-fsanitize=cfi`` to enable all the schemes and use
``-fno-sanitize`` flag to narrow down the set of schemes as desired.
For example, you can build your program with
``-fsanitize=cfi -fno-sanitize=cfi-nvcall,cfi-icall``
to use all schemes except for non-virtual member function call and indirect call
checking.

Remember that you have to provide ``-flto`` or ``-flto=thin`` if at
least one CFI scheme is enabled.

Trapping and Diagnostics
========================

By default, CFI will abort the program immediately upon detecting a control
flow integrity violation. You can use the :ref:`-fno-sanitize-trap=
<controlling-code-generation>` flag to cause CFI to print a diagnostic
similar to the one below before the program aborts.

.. code-block:: console

    bad-cast.cpp:109:7: runtime error: control flow integrity check for type 'B' failed during base-to-derived cast (vtable address 0x000000425a50)
    0x000000425a50: note: vtable is of type 'A'
     00 00 00 00  f0 f1 41 00 00 00 00 00  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  20 5a 42 00
                  ^

If diagnostics are enabled, you can also configure CFI to continue program
execution instead of aborting by using the :ref:`-fsanitize-recover=
<controlling-code-generation>` flag.

Forward-Edge CFI for Virtual Calls
==================================

This scheme checks that virtual calls take place using a vptr of the correct
dynamic type; that is, the dynamic type of the called object must be a
derived class of the static type of the object used to make the call.
This CFI scheme can be enabled on its own using ``-fsanitize=cfi-vcall``.

For this scheme to work, all translation units containing the definition
of a virtual member function (whether inline or not), other than members
of :ref:`ignored <cfi-ignorelist>` types or types with public :doc:`LTO
visibility <LTOVisibility>`, must be compiled with ``-flto`` or ``-flto=thin``
enabled and be statically linked into the program.

Performance
-----------

A performance overhead of less than 1% has been measured by running the
Dromaeo benchmark suite against an instrumented version of the Chromium
web browser. Another good performance benchmark for this mechanism is the
virtual-call-heavy SPEC 2006 xalancbmk.

Note that this scheme has not yet been optimized for binary size; an increase
of up to 15% has been observed for Chromium.

Bad Cast Checking
=================

This scheme checks that pointer casts are made to an object of the correct
dynamic type; that is, the dynamic type of the object must be a derived class
of the pointee type of the cast. The checks are currently only introduced
where the class being casted to is a polymorphic class.

Bad casts are not in themselves control flow integrity violations, but they
can also create security vulnerabilities, and the implementation uses many
of the same mechanisms.

There are two types of bad cast that may be forbidden: bad casts
from a base class to a derived class (which can be checked with
``-fsanitize=cfi-derived-cast``), and bad casts from a pointer of
type ``void*`` or another unrelated type (which can be checked with
``-fsanitize=cfi-unrelated-cast``).

The difference between these two types of casts is that the first is defined
by the C++ standard to produce an undefined value, while the second is not
in itself undefined behavior (it is well defined to cast the pointer back
to its original type) unless the object is uninitialized and the cast is a
``static_cast`` (see C++14 [basic.life]p5).

If a program as a matter of policy forbids the second type of cast, that
restriction can normally be enforced. However it may in some cases be necessary
for a function to perform a forbidden cast to conform with an external API
(e.g. the ``allocate`` member function of a standard library allocator). Such
functions may be :ref:`ignored <cfi-ignorelist>`.

For this scheme to work, all translation units containing the definition
of a virtual member function (whether inline or not), other than members
of :ref:`ignored <cfi-ignorelist>` types or types with public :doc:`LTO
visibility <LTOVisibility>`, must be compiled with ``-flto`` or ``-flto=thin``
enabled and be statically linked into the program.

Non-Virtual Member Function Call Checking
=========================================

This scheme checks that non-virtual calls take place using an object of
the correct dynamic type; that is, the dynamic type of the called object
must be a derived class of the static type of the object used to make the
call. The checks are currently only introduced where the object is of a
polymorphic class type.  This CFI scheme can be enabled on its own using
``-fsanitize=cfi-nvcall``.

For this scheme to work, all translation units containing the definition
of a virtual member function (whether inline or not), other than members
of :ref:`ignored <cfi-ignorelist>` types or types with public :doc:`LTO
visibility <LTOVisibility>`, must be compiled with ``-flto`` or ``-flto=thin``
enabled and be statically linked into the program.

.. _cfi-strictness:

Strictness
----------

If a class has a single non-virtual base and does not introduce or override
virtual member functions or fields other than an implicitly defined virtual
destructor, it will have the same layout and virtual function semantics as
its base. By default, casts to such classes are checked as if they were made
to the least derived such class.

Casting an instance of a base class to such a derived class is technically
undefined behavior, but it is a relatively common hack for introducing
member functions on class instances with specific properties that works under
most compilers and should not have security implications, so we allow it by
default. It can be disabled with ``-fsanitize=cfi-cast-strict``.

Indirect Function Call Checking
===============================

This scheme checks that function calls take place using a function of the
correct dynamic type; that is, the dynamic type of the function must match
the static type used at the call. This CFI scheme can be enabled on its own
using ``-fsanitize=cfi-icall``.

For this scheme to work, each indirect function call in the program, other
than calls in :ref:`ignored <cfi-ignorelist>` functions, must call a
function which was either compiled with ``-fsanitize=cfi-icall`` enabled,
or whose address was taken by a function in a translation unit compiled with
``-fsanitize=cfi-icall``.

If a function in a translation unit compiled with ``-fsanitize=cfi-icall``
takes the address of a function not compiled with ``-fsanitize=cfi-icall``,
that address may differ from the address taken by a function in a translation
unit not compiled with ``-fsanitize=cfi-icall``. This is technically a
violation of the C and C++ standards, but it should not affect most programs.

Each translation unit compiled with ``-fsanitize=cfi-icall`` must be
statically linked into the program or shared library, and calls across
shared library boundaries are handled as if the callee was not compiled with
``-fsanitize=cfi-icall``.

This scheme is currently supported on a limited set of targets: x86,
x86_64, arm, arch64 and wasm.

``-fsanitize-cfi-icall-generalize-pointers``
--------------------------------------------

Mismatched pointer types are a common cause of cfi-icall check failures.
Translation units compiled with the ``-fsanitize-cfi-icall-generalize-pointers``
flag relax pointer type checking for call sites in that translation unit,
applied across all functions compiled with ``-fsanitize=cfi-icall``.

Specifically, pointers in return and argument types are treated as equivalent as
long as the qualifiers for the type they point to match. For example, ``char*``,
``char**``, and ``int*`` are considered equivalent types. However, ``char*`` and
``const char*`` are considered separate types.

``-fsanitize-cfi-icall-generalize-pointers`` is not compatible with
``-fsanitize-cfi-cross-dso``.

.. _cfi-icall-experimental-normalize-integers:

``-fsanitize-cfi-icall-experimental-normalize-integers``
--------------------------------------------------------

This option enables normalizing integer types as vendor extended types for
cross-language LLVM CFI/KCFI support with other languages that can't represent
and encode C/C++ integer types.

Specifically, integer types are encoded as their defined representations (e.g.,
8-bit signed integer, 16-bit signed integer, 32-bit signed integer, ...) for
compatibility with languages that define explicitly-sized integer types (e.g.,
i8, i16, i32, ..., in Rust).

``-fsanitize-cfi-icall-experimental-normalize-integers`` is compatible with
``-fsanitize-cfi-icall-generalize-pointers``.

This option is currently experimental.

.. _cfi-canonical-jump-tables:

``-fsanitize-cfi-canonical-jump-tables``
----------------------------------------

The default behavior of Clang's indirect function call checker will replace
the address of each CFI-checked function in the output file's symbol table
with the address of a jump table entry which will pass CFI checks. We refer
to this as making the jump table `canonical`. This property allows code that
was not compiled with ``-fsanitize=cfi-icall`` to take a CFI-valid address
of a function, but it comes with a couple of caveats that are especially
relevant for users of cross-DSO CFI:

- There is a performance and code size overhead associated with each
  exported function, because each such function must have an associated
  jump table entry, which must be emitted even in the common case where the
  function is never address-taken anywhere in the program, and must be used
  even for direct calls between DSOs, in addition to the PLT overhead.

- There is no good way to take a CFI-valid address of a function written in
  assembly or a language not supported by Clang. The reason is that the code
  generator would need to insert a jump table in order to form a CFI-valid
  address for assembly functions, but there is no way in general for the
  code generator to determine the language of the function. This may be
  possible with LTO in the intra-DSO case, but in the cross-DSO case the only
  information available is the function declaration. One possible solution
  is to add a C wrapper for each assembly function, but these wrappers can
  present a significant maintenance burden for heavy users of assembly in
  addition to adding runtime overhead.

For these reasons, we provide the option of making the jump table non-canonical
with the flag ``-fno-sanitize-cfi-canonical-jump-tables``. When the jump
table is made non-canonical, symbol table entries point directly to the
function body. Any instances of a function's address being taken in C will
be replaced with a jump table address.

This scheme does have its own caveats, however. It does end up breaking
function address equality more aggressively than the default behavior,
especially in cross-DSO mode which normally preserves function address
equality entirely.

Furthermore, it is occasionally necessary for code not compiled with
``-fsanitize=cfi-icall`` to take a function address that is valid
for CFI. For example, this is necessary when a function's address
is taken by assembly code and then called by CFI-checking C code. The
``__attribute__((cfi_canonical_jump_table))`` attribute may be used to make
the jump table entry of a specific function canonical so that the external
code will end up taking an address for the function that will pass CFI checks.

``-fsanitize=cfi-icall`` and ``-fsanitize=function``
----------------------------------------------------

This tool is similar to ``-fsanitize=function`` in that both tools check
the types of function calls. However, the two tools occupy different points
on the design space; ``-fsanitize=function`` is a developer tool designed
to find bugs in local development builds, whereas ``-fsanitize=cfi-icall``
is a security hardening mechanism designed to be deployed in release builds.

``-fsanitize=function`` has a higher space and time overhead due to a more
complex type check at indirect call sites, which may make it unsuitable for
deployment.

On the other hand, ``-fsanitize=function`` conforms more closely with the C++
standard and user expectations around interaction with shared libraries;
the identity of function pointers is maintained, and calls across shared
library boundaries are no different from calls within a single program or
shared library.

.. _kcfi:

``-fsanitize=kcfi``
-------------------

This is an alternative indirect call control-flow integrity scheme designed
for low-level system software, such as operating system kernels. Unlike
``-fsanitize=cfi-icall``, it doesn't require ``-flto``, won't result in
function pointers being replaced with jump table references, and never breaks
cross-DSO function address equality. These properties make KCFI easier to
adopt in low-level software. KCFI is limited to checking only function
pointers, and isn't compatible with executable-only memory.

Member Function Pointer Call Checking
=====================================

This scheme checks that indirect calls via a member function pointer
take place using an object of the correct dynamic type. Specifically, we
check that the dynamic type of the member function referenced by the member
function pointer matches the "function pointer" part of the member function
pointer, and that the member function's class type is related to the base
type of the member function. This CFI scheme can be enabled on its own using
``-fsanitize=cfi-mfcall``.

The compiler will only emit a full CFI check if the member function pointer's
base type is complete. This is because the complete definition of the base
type contains information that is necessary to correctly compile the CFI
check. To ensure that the compiler always emits a full CFI check, it is
recommended to also pass the flag ``-fcomplete-member-pointers``, which
enables a non-conforming language extension that requires member pointer
base types to be complete if they may be used for a call.

For this scheme to work, all translation units containing the definition
of a virtual member function (whether inline or not), other than members
of :ref:`ignored <cfi-ignorelist>` types or types with public :doc:`LTO
visibility <LTOVisibility>`, must be compiled with ``-flto`` or ``-flto=thin``
enabled and be statically linked into the program.

This scheme is currently not compatible with cross-DSO CFI or the
Microsoft ABI.

.. _cfi-ignorelist:

Ignorelist
==========

A :doc:`SanitizerSpecialCaseList` can be used to relax CFI checks for certain
source files, functions and types using the ``src``, ``fun`` and ``type``
entity types. Specific CFI modes can be be specified using ``[section]``
headers.

.. code-block:: bash

    # Suppress all CFI checking for code in a file.
    src:bad_file.cpp
    src:bad_header.h
    # Ignore all functions with names containing MyFooBar.
    fun:*MyFooBar*
    # Ignore all types in the standard library.
    type:std::*
    # Disable only unrelated cast checks for this function
    [cfi-unrelated-cast]
    fun:*UnrelatedCast*
    # Disable CFI call checks for this function without affecting cast checks
    [cfi-vcall|cfi-nvcall|cfi-icall]
    fun:*BadCall*


.. _cfi-cross-dso:

Shared library support
======================

Use **-f[no-]sanitize-cfi-cross-dso** to enable the cross-DSO control
flow integrity mode, which allows all CFI schemes listed above to
apply across DSO boundaries. As in the regular CFI, each DSO must be
built with ``-flto`` or ``-flto=thin``.

Normally, CFI checks will only be performed for classes that have hidden LTO
visibility. With this flag enabled, the compiler will emit cross-DSO CFI
checks for all classes, except for those which appear in the CFI ignorelist
or which use a ``no_sanitize`` attribute.

Design
======

Please refer to the :doc:`design document<ControlFlowIntegrityDesign>`.

Publications
============

`Control-Flow Integrity: Principles, Implementations, and Applications <https://research.microsoft.com/pubs/64250/ccs05.pdf>`_.
Martin Abadi, Mihai Budiu, Úlfar Erlingsson, Jay Ligatti.

`Enforcing Forward-Edge Control-Flow Integrity in GCC & LLVM <http://www.pcc.me.uk/~peter/acad/usenix14.pdf>`_.
Caroline Tice, Tom Roeder, Peter Collingbourne, Stephen Checkoway,
Úlfar Erlingsson, Luis Lozano, Geoff Pike.
