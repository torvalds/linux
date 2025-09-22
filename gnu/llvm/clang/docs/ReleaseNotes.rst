===========================================
Clang |release| |ReleaseNotesTitle|
===========================================

.. contents::
   :local:
   :depth: 2

Written by the `LLVM Team <https://llvm.org/>`_

.. only:: PreRelease

  .. warning::
     These are in-progress notes for the upcoming Clang |version| release.
     Release notes for previous releases can be found on
     `the Releases Page <https://llvm.org/releases/>`_.

Introduction
============

This document contains the release notes for the Clang C/C++/Objective-C
frontend, part of the LLVM Compiler Infrastructure, release |release|. Here we
describe the status of Clang in some detail, including major
improvements from the previous release and new feature work. For the
general LLVM release notes, see `the LLVM
documentation <https://llvm.org/docs/ReleaseNotes.html>`_. For the libc++ release notes,
see `this page <https://libcxx.llvm.org/ReleaseNotes.html>`_. All LLVM releases
may be downloaded from the `LLVM releases web site <https://llvm.org/releases/>`_.

For more information about Clang or LLVM, including information about the
latest release, please see the `Clang Web Site <https://clang.llvm.org>`_ or the
`LLVM Web Site <https://llvm.org>`_.

Potentially Breaking Changes
============================
These changes are ones which we think may surprise users when upgrading to
Clang |release| because of the opportunity they pose for disruption to existing
code bases.

- Setting the deprecated CMake variable ``GCC_INSTALL_PREFIX`` (which sets the
  default ``--gcc-toolchain=``) now leads to a fatal error.

C/C++ Language Potentially Breaking Changes
-------------------------------------------

- Clang now supports raw string literals in ``-std=gnuXY`` mode as an extension in
  C99 and later. This behaviour can also be overridden using ``-f[no-]raw-string-literals``.
  Support of raw string literals in C++ is not affected. Fixes (#GH85703).

C++ Specific Potentially Breaking Changes
-----------------------------------------
- Clang now diagnoses function/variable templates that shadow their own template parameters, e.g. ``template<class T> void T();``.
  This error can be disabled via `-Wno-strict-primary-template-shadow` for compatibility with previous versions of clang.

- The behavior controlled by the `-frelaxed-template-template-args` flag is now
  on by default, and the flag is deprecated. Until the flag is finally removed,
  it's negative spelling can be used to obtain compatibility with previous
  versions of clang. The deprecation warning for the negative spelling can be
  disabled with `-Wno-deprecated-no-relaxed-template-template-args`.

- Clang no longer tries to form pointer-to-members from qualified and parenthesized unevaluated expressions
  such as ``decltype(&(foo::bar))``. (#GH40906).

- Clang now performs semantic analysis for unary operators with dependent operands
  that are known to be of non-class non-enumeration type prior to instantiation.

  This change uncovered a bug in libstdc++ 14.1.0 which may cause compile failures
  on systems using that version of libstdc++ and Clang 19, with an error that looks
  something like this:

  .. code-block:: text

    <source>:4:5: error: expression is not assignable
    4 |     ++this;
      |     ^ ~~~~

  To fix this, update libstdc++ to version 14.1.1 or greater.

ABI Changes in This Version
---------------------------
- Fixed Microsoft name mangling of implicitly defined variables used for thread
  safe static initialization of static local variables. This change resolves
  incompatibilities with code compiled by MSVC but might introduce
  incompatibilities with code compiled by earlier versions of Clang when an
  inline member function that contains a static local variable with a dynamic
  initializer is declared with ``__declspec(dllimport)``. (#GH83616).

- Fixed Microsoft name mangling of lifetime extended temporary objects. This
  change corrects missing back reference registrations that could result in
  incorrect back reference indexes and suprising demangled name results. Since
  MSVC uses a different mangling for these objects, compatibility is not affected.
  (#GH85423).

- Fixed Microsoft calling convention for returning certain classes with a
  templated constructor. If a class has a templated constructor, it should
  be returned indirectly even if it meets all the other requirements for
  returning a class in a register. This affects some uses of std::pair.
  (#GH86384).

- Fixed Microsoft calling convention when returning classes that have a deleted
  copy assignment operator. Such a class should be returned indirectly.

- Removed the global alias that was pointing to AArch64 Function Multiversioning
  ifuncs. Its purpose was to preserve backwards compatibility when the ".ifunc"
  suffix got removed from the name mangling. The alias interacts badly with
  GlobalOpt (see the issue #96197).

- Fixed Microsoft name mangling for auto non-type template arguments of pointer
  type for MSVC 1920+. This change resolves incompatibilities with code compiled
  by MSVC 1920+ but will introduce incompatibilities with code compiled by
  earlier versions of Clang unless such code is built with the compiler option
  `-fms-compatibility-version=19.14` to imitate the MSVC 1914 mangling behavior.

- Fixed Microsoft name mangling for auto non-type template arguments of pointer
  to member type for MSVC 1920+. This change resolves incompatibilities with code
  compiled by MSVC 1920+ but will introduce incompatibilities with code compiled by
  earlier versions of Clang unless such code is built with the compiler option
  `-fms-compatibility-version=19.14` to imitate the MSVC 1914 mangling behavior.
  (GH#70899).

AST Dumping Potentially Breaking Changes
----------------------------------------

- The text ast-dumper has improved printing of TemplateArguments.
- The text decl-dumper prints template parameters' trailing requires expressions now.

Clang Frontend Potentially Breaking Changes
-------------------------------------------
- Removed support for constructing on-stack ``TemplateArgumentList``\ s; interfaces should instead
  use ``ArrayRef<TemplateArgument>`` to pass template arguments. Transitioning internal uses to
  ``ArrayRef<TemplateArgument>`` reduces AST memory usage by 0.4% when compiling clang, and is
  expected to show similar improvements on other workloads.

- The ``-Wgnu-binary-literal`` diagnostic group no longer controls any
  diagnostics. Binary literals are no longer a GNU extension, they're now a C23
  extension which is controlled via ``-pedantic`` or ``-Wc23-extensions``. Use
  of ``-Wno-gnu-binary-literal`` will no longer silence this pedantic warning,
  which may break existing uses with ``-Werror``.

- The normalization of 3 element target triples where ``-none-`` is the middle
  element has changed. For example, ``armv7m-none-eabi`` previously normalized
  to ``armv7m-none-unknown-eabi``, with ``none`` for the vendor and ``unknown``
  for the operating system. It now normalizes to ``armv7m-unknown-none-eabi``,
  which has ``unknown`` vendor and ``none`` operating system.

  The affected triples are primarily for bare metal Arm where it is intended
  that ``none`` means that there is no operating system. As opposed to an unknown
  type of operating system.

  This change can cause clang to not find libraries, or libraries to be built at
  different file system locations. This can be fixed by changing your builds to
  use the new normalized triple. However, we recommend instead getting the
  normalized triple from clang itself, as this will make your builds more
  robust in case of future changes::

    $ clang --target=<your target triple> -print-target-triple
    <the normalized target triple>

- The ``hasTypeLoc`` AST matcher will no longer match a ``classTemplateSpecializationDecl``;
  existing uses should switch to ``templateArgumentLoc`` or ``hasAnyTemplateArgumentLoc`` instead.

Clang Python Bindings Potentially Breaking Changes
--------------------------------------------------
- Renamed ``CursorKind`` variant 272 from ``OMP_TEAMS_DISTRIBUTE_DIRECTIVE``
  to ``OMP_TEAMS_DISTRIBUTE_SIMD_DIRECTIVE``. The previous name was incorrect, it was a duplicate
  of variant 271.
- Renamed ``TypeKind`` variant 162 from ``OBJCCLASS`` to ``OBJCTYPEPARAM``.
  The previous name was incorrect, it was a duplicate of variant 28.
- Refactored enum implementation, switching to the standard library `Enum` type.

What's New in Clang |release|?
==============================
Some of the major new features and improvements to Clang are listed
here. Generic improvements to Clang as a whole or to its underlying
infrastructure are described first, followed by language-specific
sections with improvements to Clang's support for those languages.

- Implemented improvements to BMIs for C++20 Modules that can reduce
  the number of rebuilds during incremental recompilation. We are seeking
  feedback from Build System authors and other interested users, especially
  when you feel Clang changes the BMI and misses an opportunity to avoid
  recompilations or causes correctness issues. See StandardCPlusPlusModules
  `StandardCPlusPlusModules <StandardCPlusPlusModules.html>`_ for more details.

- The ``\par`` documentation comment command now supports an optional
  argument, which denotes the header of the paragraph started by
  an instance of the ``\par`` command comment. The implementation
  of the argument handling matches its semantics
  `in Doxygen <https://www.doxygen.nl/manual/commands.html#cmdpar>`.
  Namely, any text on the same line as the ``\par`` command will become
  a header for the paragaph, and if there is no text then the command
  will start a new paragraph.

C++ Language Changes
--------------------
- C++17 support is now completed, with the enablement of the
  relaxed temlate template argument matching rules introduced in P0522,
  which was retroactively applied as a defect report.
  While the implementation already existed since Clang 4, it was turned off by
  default, and was controlled with the `-frelaxed-template-template-args` flag.
  In this release, we implement provisional wording for a core defect on
  P0522 (CWG2398), which avoids the most serious compatibility issues caused
  by it, allowing us to enable it by default in this release.
  The flag is now deprecated, and will be removed in the next release, but can
  still be used to turn it off and regain compatibility with previous versions
  (#GH36505).
- Implemented ``_BitInt`` literal suffixes ``__wb`` or ``__WB`` as a Clang extension with ``unsigned`` modifiers also allowed. (#GH85223).

C++17 Feature Support
^^^^^^^^^^^^^^^^^^^^^
- Clang now exposes ``__GCC_DESTRUCTIVE_SIZE`` and ``__GCC_CONSTRUCTIVE_SIZE``
  predefined macros to support standard library implementations of
  ``std::hardware_destructive_interference_size`` and
  ``std::hardware_constructive_interference_size``, respectively. These macros
  are predefined in all C and C++ language modes. The values the macros
  expand to are not stable between releases of Clang and do not need to match
  the values produced by GCC, so these macros should not be used from header
  files because they may not be stable across multiple TUs (the values may vary
  based on compiler version as well as CPU tuning). #GH60174

C++14 Feature Support
^^^^^^^^^^^^^^^^^^^^^
- Sized deallocation is enabled by default in C++14 onwards. The user may specify
  ``-fno-sized-deallocation`` to disable it if there are some regressions.

C++20 Feature Support
^^^^^^^^^^^^^^^^^^^^^

- Clang won't perform ODR checks for decls in the global module fragment any
  more to ease the implementation and improve the user's using experience.
  This follows the MSVC's behavior. Users interested in testing the more strict
  behavior can use the flag '-Xclang -fno-skip-odr-check-in-gmf'.
  (#GH79240).

- Implemented the `__is_layout_compatible` and `__is_pointer_interconvertible_base_of`
  intrinsics to support
  `P0466R5: Layout-compatibility and Pointer-interconvertibility Traits <https://wg21.link/P0466R5>`_.

- Clang now implements [module.import]p7 fully. Clang now will import module
  units transitively for the module units coming from the same module of the
  current module units. Fixes #GH84002

- Initial support for class template argument deduction (CTAD) for type alias
  templates (`P1814R0 <https://wg21.link/p1814r0>`_).
  (#GH54051).

- We have sufficient confidence and experience with the concepts implementation
  to update the ``__cpp_concepts`` macro to `202002L`. This enables
  ``<expected>`` from libstdc++ to work correctly with Clang.

- User defined constructors are allowed for copy-list-initialization with CTAD.
  (#GH62925).

C++23 Feature Support
^^^^^^^^^^^^^^^^^^^^^

- Implemented `P2718R0: Lifetime extension in range-based for loops <https://wg21.link/P2718R0>`_. Also
  materialize temporary object which is a prvalue in discarded-value expression.
- Implemented `P1774R8: Portable assumptions <https://wg21.link/P1774R8>`_.

- Implemented `P2448R2: Relaxing some constexpr restrictions <https://wg21.link/P2448R2>`_.
  Note, the ``-Winvalid-constexpr`` diagnostic is now disabled in C++23 mode,
  but can be explicitly specified to retain the old diagnostic checking
  behavior.

- Added a ``__reference_converts_from_temporary`` builtin, completing the necessary compiler support for
  `P2255R2: Type trait to determine if a reference binds to a temporary <https://wg21.link/P2255R2>`_.

- Implemented `P2797R0: Static and explicit object member functions with the same parameter-type-lists <https://wg21.link/P2797R0>`_.
  This completes the support for "deducing this".

C++2c Feature Support
^^^^^^^^^^^^^^^^^^^^^

- Implemented `P2662R3 Pack Indexing <https://wg21.link/P2662R3>`_.

- Implemented `P2573R2: = delete("should have a reason"); <https://wg21.link/P2573R2>`_

- Implemented `P0609R3: Attributes for Structured Bindings <https://wg21.link/P0609R3>`_

- Implemented `P2748R5 Disallow Binding a Returned Glvalue to a Temporary <https://wg21.link/P2748R5>`_.

- Implemented `P2809R3: Trivial infinite loops are not Undefined Behavior <https://wg21.link/P2809R3>`_.

- Implemented `P3144R2 Deleting a Pointer to an Incomplete Type Should be Ill-formed <https://wg21.link/P3144R2>`_.

- Implemented `P2963R3 Ordering of constraints involving fold expressions <https://wg21.link/P2963R3>`_.


Resolutions to C++ Defect Reports
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
- Substitute template parameter pack, when it is not explicitly specified
  in the template parameters, but is deduced from a previous argument. (#GH78449)

- Type qualifications are now ignored when evaluating layout compatibility
  of two types.
  (`CWG1719: Layout compatibility and cv-qualification revisited <https://cplusplus.github.io/CWG/issues/1719.html>`_).

- Alignment of members is now respected when evaluating layout compatibility
  of structs.
  (`CWG2583: Common initial sequence should consider over-alignment <https://cplusplus.github.io/CWG/issues/2583.html>`_).

- ``[[no_unique_address]]`` is now respected when evaluating layout
  compatibility of two types.
  (`CWG2759: [[no_unique_address] and common initial sequence  <https://cplusplus.github.io/CWG/issues/2759.html>`_).

- Clang now diagnoses declarative nested-name-specifiers with pack-index-specifiers.
  (`CWG2858: Declarative nested-name-specifiers and pack-index-specifiers <https://cplusplus.github.io/CWG/issues/2858.html>`_).

- Clang now allows attributes on concepts.
  (`CWG2428: Deprecating a concept <https://cplusplus.github.io/CWG/issues/2428.html>`_).

- P0522 implementation is enabled by default in all language versions, and
  provisional wording for CWG2398 is implemented.

- Clang now performs type-only lookup for the name in ``using enum`` declaration.
  (`CWG2877: Type-only lookup for using-enum-declarator <https://cplusplus.github.io/CWG/issues/2877.html>`_).

- Clang now requires a template argument list after a template keyword.
  (`CWG96: Syntactic disambiguation using the template keyword <https://cplusplus.github.io/CWG/issues/96.html>`_).

- Clang now considers ``noexcept(typeid(expr))`` more carefully, instead of always assuming that ``std::bad_typeid`` can be thrown.
  (`CWG2191: Incorrect result for noexcept(typeid(v)) <https://cplusplus.github.io/CWG/issues/2191.html>`_).

C Language Changes
------------------

C2y Feature Support
^^^^^^^^^^^^^^^^^^^
- Clang now enables C2y mode with ``-std=c2y``. This sets ``__STDC_VERSION__``
  to ``202400L`` so that it's greater than the value for C23. The value of this
  macro is subject to change in the future.

C23 Feature Support
^^^^^^^^^^^^^^^^^^^
- No longer diagnose use of binary literals as an extension in C23 mode. Fixes
  #GH72017.

- Corrected parsing behavior for the ``alignas`` specifier/qualifier in C23. We
  previously handled it as an attribute as in C++, but there are parsing
  differences. The behavioral differences are:

  .. code-block:: c

     struct alignas(8) /* was accepted, now rejected */ S {
       char alignas(8) /* was rejected, now accepted */ C;
     };
     int i alignas(8) /* was accepted, now rejected */ ;

  Fixes (#GH81472).

- Clang now generates predefined macros of the form ``__TYPE_FMTB__`` and
  ``__TYPE_FMTb__`` (e.g., ``__UINT_FAST64_FMTB__``) in C23 mode for use with
  macros typically exposed from ``<inttypes.h>``, such as ``PRIb8``. (#GH81896)

- Clang now supports `N3018 The constexpr specifier for object definitions`
  <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3018.htm>`_.

- Properly promote bit-fields of bit-precise integer types to the field's type
  rather than to ``int``. #GH87641

- Added the ``INFINITY`` and ``NAN`` macros to Clang's ``<float.h>``
  freestanding implementation; these macros were defined in ``<math.h>`` in C99
  but C23 added them to ``<float.h>`` in
  `WG14 N2848 <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2848.pdf>`_.

- Clang now supports `N3017 <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3017.htm>`_
  ``#embed`` - a scannable, tooling-friendly binary resource inclusion mechanism.

- Added the ``FLT_NORM_MAX``, ``DBL_NORM_MAX``, and ``LDBL_NORM_MAX`` to the
  freestanding implementation of ``<float.h>`` that ships with Clang.

- Compiler support for `N2653 char8_t: A type for UTF-8 characters and strings`
  <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2653.htm>`_: ``u8`` string
  literals are now of type ``char8_t[N]`` in C23 and expose
  ``__CLANG_ATOMIC_CHAR8_T_LOCK_FREE``/``__GCC_ATOMIC_CHAR8_T_LOCK_FREE`` to
  implement the corresponding macro in ``<stdatomic.h>``.

Non-comprehensive list of changes in this release
-------------------------------------------------

- Added ``__builtin_readsteadycounter`` for reading fixed frequency hardware
  counters.

- ``__builtin_addc``, ``__builtin_subc``, and the other sizes of those
  builtins are now constexpr and may be used in constant expressions.

- Added ``__builtin_popcountg`` as a type-generic alternative to
  ``__builtin_popcount{,l,ll}`` with support for any unsigned integer type. Like
  the previous builtins, this new builtin is constexpr and may be used in
  constant expressions.

- Lambda expressions are now accepted in C++03 mode as an extension.

- Added ``__builtin_clzg`` and ``__builtin_ctzg`` as type-generic alternatives
  to ``__builtin_clz{,s,l,ll}`` and ``__builtin_ctz{,s,l,ll}`` respectively,
  with support for any unsigned integer type. Like the previous builtins, these
  new builtins are constexpr and may be used in constant expressions.

- ``__typeof_unqual__`` is available in all C modes as an extension, which behaves
  like ``typeof_unqual`` from C23, similar to ``__typeof__`` and ``typeof``.

- ``__builtin_reduce_{add|mul|xor|or|and|min|max}`` builtins now support scalable vectors.

* Shared libraries linked with either the ``-ffast-math``, ``-Ofast``, or
  ``-funsafe-math-optimizations`` flags will no longer enable flush-to-zero
  floating-point mode by default. This decision can be overridden with use of
  ``-mdaz-ftz``. This behavior now matches GCC's behavior.
  (`#57589 <https://github.com/llvm/llvm-project/issues/57589>`_)

* ``-fdenormal-fp-math=preserve-sign`` is no longer implied by ``-ffast-math``
  on x86 systems.

- Builtins ``__builtin_shufflevector()`` and ``__builtin_convertvector()`` may
  now be used within constant expressions.

- When compiling a constexpr function, Clang will check to see whether the
  function can *never* be used in a constant expression context and issues a
  diagnostic under the ``-Winvalid-constexpr`` diagostic flag (which defaults
  to an error). This check can be expensive because the mere presence of a
  function marked ``constexpr`` will cause us to undergo constant expression
  evaluation, even if the function is not called within the translation unit
  being compiled. Due to the expense, Clang no longer checks constexpr function
  bodies when the function is defined in a system header file or when
  ``-Winvalid-constexpr`` is not enabled for the function definition, which
  should result in mild compile-time performance improvements.

- Added ``__is_bitwise_cloneable`` which is used to check whether a type
  can be safely copied by memcpy/memmove.

- ``#pragma GCC diagnostic warning "-Wfoo"`` can now downgrade ``-Werror=foo``
  errors and certain default-to-error ``-W`` diagnostics to warnings.

- Support importing C++20 modules in clang-repl.

- Added support for ``TypeLoc::dump()`` for easier debugging, and improved
  textual and JSON dumping for various ``TypeLoc``-related nodes.

- Clang can now emit distinct type-based alias analysis tags for incompatible
  pointers, enabling more powerful alias analysis when accessing pointer types.
  The new behavior can be enabled using ``-fpointer-tbaa``.

- The ``__atomic_always_lock_free`` and ``__atomic_is_lock_free``
  builtins may now return true if the pointer argument is a
  compile-time constant (e.g. ``(void*)4``), and constant pointer is
  sufficiently-aligned for the access requested. Previously, only the
  type of the pointer was taken into account. This improves
  compatibility with GCC's libstdc++.

- The type traits builtin ``__is_nullptr`` is deprecated in CLang 19 and will be
  removed in Clang 20. ``__is_same(__remove_cv(T), decltype(nullptr))`` can be
  used instead to check whether a type ``T`` is a ``nullptr``.

New Compiler Flags
------------------
- ``-fsanitize=implicit-bitfield-conversion`` checks implicit truncation and
  sign change.
- ``-fsanitize=implicit-integer-conversion`` a group that replaces the previous
  group ``-fsanitize=implicit-conversion``.

- ``-Wmissing-designated-field-initializers``, grouped under ``-Wmissing-field-initializers``.
  This diagnostic can be disabled to make ``-Wmissing-field-initializers`` behave
  like it did before Clang 18.x. Fixes #GH56628

- ``-fexperimental-modules-reduced-bmi`` enables the Reduced BMI for C++20 named modules.
  See the document of standard C++ modules for details.

- ``-fexperimental-late-parse-attributes`` enables an experimental feature to
  allow late parsing certain attributes in specific contexts where they would
  not normally be late parsed. Currently this allows late parsing the
  `counted_by` attribute in C. See `Attribute Changes in Clang`_.

- ``-fseparate-named-sections`` uses separate unique sections for global
  symbols in named special sections (i.e. symbols annotated with
  ``__attribute__((section(...)))``. This enables linker GC to collect unused
  symbols without having to use a per-symbol section.

- ``-fms-define-stdc`` and its clang-cl counterpart ``/Zc:__STDC__``.
  Matches MSVC behaviour by defining ``__STDC__`` to ``1`` when
  MSVC compatibility mode is used. It has no effect for C++ code.

- ``-Wc++23-compat`` group was added to help migrating existing codebases
  to C++23.

- ``-Wc++2c-compat`` group was added to help migrating existing codebases
  to upcoming C++26.

- ``-fdisable-block-signature-string`` instructs clang not to emit the signature
  string for blocks. Disabling the string can potentially break existing code
  that relies on it. Users should carefully consider this possibiilty when using
  the flag.

- For the ARM target, added ``-Warm-interrupt-vfp-clobber`` that will emit a
  diagnostic when an interrupt handler is declared and VFP is enabled.

- ``-fpointer-tbaa`` enables emission of distinct type-based alias
  analysis tags for incompatible pointers.

Deprecated Compiler Flags
-------------------------

- The ``-Ofast`` command-line option has been deprecated. This option both
  enables the ``-O3`` optimization-level, as well as enabling non-standard
  ``-ffast-math`` behaviors. As such, it is somewhat misleading as an
  "optimization level". Users are advised to switch to ``-O3 -ffast-math`` if
  the use of non-standard math behavior is intended, and ``-O3`` otherwise.
  See `RFC <https://discourse.llvm.org/t/rfc-deprecate-ofast/78687>`_ for details.

Modified Compiler Flags
-----------------------
- Added a new diagnostic flag ``-Wreturn-mismatch`` which is grouped under
  ``-Wreturn-type``, and moved some of the diagnostics previously controlled by
  ``-Wreturn-type`` under this new flag. Fixes #GH72116.
- ``-fsanitize=implicit-conversion`` is now a group for both
  ``-fsanitize=implicit-integer-conversion`` and
  ``-fsanitize=implicit-bitfield-conversion``.

- Added ``-Wcast-function-type-mismatch`` under the ``-Wcast-function-type``
  warning group. Moved the diagnostic previously controlled by
  ``-Wcast-function-type`` to the new warning group and added
  ``-Wcast-function-type-mismatch`` to ``-Wextra``. #GH76872

  .. code-block:: c

     int x(long);
     typedef int (f2)(void*);
     typedef int (f3)();

     void func(void) {
       // Diagnoses under -Wcast-function-type, -Wcast-function-type-mismatch,
       // -Wcast-function-type-strict, -Wextra
       f2 *b = (f2 *)x;
       // Diagnoses under -Wcast-function-type, -Wcast-function-type-strict
       f3 *c = (f3 *)x;
     }

- Carved out ``-Wformat`` warning about scoped enums into a subwarning and
  make it controlled by ``-Wformat-pedantic``. Fixes #GH88595.

- Trivial infinite loops (i.e loops with a constant controlling expresion
  evaluating to ``true`` and an empty body such as ``while(1);``)
  are considered infinite, even when the ``-ffinite-loop`` flag is set.

- Diagnostics groups about compatibility with a particular C++ Standard version
  now include dianostics about C++26 features that are not present in older
  versions.

- Removed the "arm interrupt calling convention" warning that was included in
  ``-Wextra`` without its own flag. This warning suggested adding
  ``__attribute__((interrupt))`` to functions that are called from interrupt
  handlers to prevent clobbering VFP registers. Following this suggestion leads
  to unpredictable behavior by causing multiple exception returns from one
  exception. Fixes #GH34876.

Removed Compiler Flags
-------------------------

- The ``-freroll-loops`` flag has been removed. It had no effect since Clang 13.
- ``-m[no-]unaligned-access`` is removed for RISC-V and LoongArch.
  ``-m[no-]strict-align``, also supported by GCC, should be used instead. (#GH85350)

Attribute Changes in Clang
--------------------------
- Introduced a new function attribute ``__attribute__((amdgpu_max_num_work_groups(x, y, z)))`` or
  ``[[clang::amdgpu_max_num_work_groups(x, y, z)]]`` for the AMDGPU target. This attribute can be
  attached to HIP or OpenCL kernel function definitions to provide an optimization hint. The parameters
  ``x``, ``y``, and ``z`` specify the maximum number of workgroups for the respective dimensions,
  and each must be a positive integer when provided. The parameter ``x`` is required, while ``y`` and
  ``z`` are optional with default value of 1.

- The ``swiftasynccc`` attribute is now considered to be a Clang extension
  rather than a language standard feature. Please use
  ``__has_extension(swiftasynccc)`` to check the availability of this attribute
  for the target platform instead of ``__has_feature(swiftasynccc)``. Also,
  added a new extension query ``__has_extension(swiftcc)`` corresponding to the
  ``__attribute__((swiftcc))`` attribute.

- The ``_Nullable`` and ``_Nonnull`` family of type attributes can now apply
  to certain C++ class types, such as smart pointers:
  ``void useObject(std::unique_ptr<Object> _Nonnull obj);``.

  This works for standard library types including ``unique_ptr``, ``shared_ptr``,
  and ``function``. See
  `the attribute reference documentation <https://llvm.org/docs/AttributeReference.html#nullability-attributes>`_
  for the full list.

- The ``_Nullable`` attribute can be applied to C++ class declarations:
  ``template <class T> class _Nullable MySmartPointer {};``.

  This allows the ``_Nullable`` and ``_Nonnull`` family of type attributes to
  apply to this class.

- Clang now warns that the ``exclude_from_explicit_instantiation`` attribute
  is ignored when applied to a local class or a member thereof.

- The ``clspv_libclc_builtin`` attribute has been added to allow clspv
  (`OpenCL-C to Vulkan SPIR-V compiler <https://github.com/google/clspv>`_) to identify functions coming from libclc
  (`OpenCL-C builtin library <https://libclc.llvm.org>`_).
- The ``counted_by`` attribute is now allowed on pointers that are members of a
  struct in C.

- The ``counted_by`` attribute can now be late parsed in C when
  ``-fexperimental-late-parse-attributes`` is passed but only when attribute is
  used in the declaration attribute position. This allows using the
  attribute on existing code where it previously impossible to do so without
  re-ordering struct field declarations would break ABI as shown below.

  .. code-block:: c

     struct BufferTy {
       /* Refering to `count` requires late parsing */
       char* buffer __counted_by(count);
       /* Swapping `buffer` and `count` to avoid late parsing would break ABI */
       size_t count;
     };

- The attributes ``sized_by``, ``counted_by_or_null`` and ``sized_by_or_null```
  have been added as variants on ``counted_by``, each with slightly different semantics.
  ``sized_by`` takes a byte size parameter instead of an element count, allowing pointees
  with unknown size. The ``counted_by_or_null`` and ``sized_by_or_null`` variants are equivalent
  to their base variants, except the pointer can be null regardless of count/size value.
  If the pointer is null the size is effectively 0. ``sized_by_or_null`` is needed to properly
  annotate allocator functions like ``malloc`` that return a buffer of a given byte size, but can
  also return null.

- The ``guarded_by``, ``pt_guarded_by``, ``acquired_after``, ``acquired_before``
  attributes now support referencing struct members in C. The arguments are also
  now late parsed when ``-fexperimental-late-parse-attributes`` is passed like
  for ``counted_by``.

- Introduced new function type attributes ``[[clang::nonblocking]]``, ``[[clang::nonallocating]]``,
  ``[[clang::blocking]]``, and ``[[clang::allocating]]``, with GNU-style variants as well.
  The attributes declare constraints about a function's behavior pertaining to blocking and
  heap memory allocation.

- The ``hybrid_patchable`` attribute is now supported on ARM64EC targets. It can be used to specify
  that a function requires an additional x86-64 thunk, which may be patched at runtime.

Improvements to Clang's diagnostics
-----------------------------------
- Clang now emits an error instead of a warning for ``-Wundefined-internal``
  when compiling with `-pedantic-errors` to conform to the C standard

- Clang now applies syntax highlighting to the code snippets it
  prints.

- Clang now diagnoses member template declarations with multiple declarators.

- Clang now diagnoses use of the ``template`` keyword after declarative nested
  name specifiers.

- The ``-Wshorten-64-to-32`` diagnostic is now grouped under ``-Wimplicit-int-conversion`` instead
   of ``-Wconversion``. Fixes #GH69444.

- Clang now uses thousand separators when printing large numbers in integer overflow diagnostics.
  Fixes #GH80939.

- Clang now diagnoses friend declarations with an ``enum`` elaborated-type-specifier in language modes after C++98.

- Added diagnostics for C11 keywords being incompatible with language standards
  before C11, under a new warning group: ``-Wpre-c11-compat``.

- Now diagnoses an enumeration constant whose value is larger than can be
  represented by ``unsigned long long``, which can happen with a large constant
  using the ``wb`` or ``uwb`` suffix. The maximal underlying type is currently
  ``unsigned long long``, but this behavior may change in the future when Clang
  implements
  `WG14 N3029 <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3029.htm>`_.
  (#GH69352).

- Clang now diagnoses extraneous template parameter lists as a language extension.

- Clang now diagnoses declarative nested name specifiers that name alias templates.

- Clang now diagnoses lambda function expressions being implicitly cast to boolean values, under ``-Wpointer-bool-conversion``.
  Fixes #GH82512.

- Clang now provides improved warnings for the ``cleanup`` attribute to detect misuse scenarios,
  such as attempting to call ``free`` on an unallocated object. Fixes #GH79443.

- Clang no longer warns when the ``bitand`` operator is used with boolean
  operands, distinguishing it from potential typographical errors or unintended
  bitwise operations. Fixes #GH77601.

- Clang now correctly diagnoses no arguments to a variadic macro parameter as a C23/C++20 extension.
  Fixes #GH84495.

- Clang no longer emits a ``-Wexit-time destructors`` warning on static variables explicitly
  annotated with the ``clang::always_destroy`` attribute.
  Fixes #GH68686, #GH86486

- ``-Wmicrosoft``, ``-Wgnu``, or ``-pedantic`` is now required to diagnose C99
  flexible array members in a union or alone in a struct. Fixes GH#84565.

- Clang now no longer diagnoses type definitions in ``offsetof`` in C23 mode.
  Fixes #GH83658.

- New ``-Wformat-signedness`` diagnostic that warn if the format string requires an
  unsigned argument and the argument is signed and vice versa.

- Clang now emits ``unused argument`` warning when the -fmodule-output flag is used
  with an input that is not of type c++-module.

- Clang emits a ``-Wreturn-stack-address`` warning if a function returns a pointer or
  reference to a struct literal. Fixes #GH8678

- Clang emits a ``-Wunused-but-set-variable`` warning on C++ variables whose declaration
  (with initializer) entirely consist the condition expression of a if/while/for construct
  but are not actually used in the body of the if/while/for construct. Fixes #GH41447

- Clang emits a diagnostic when a tentative array definition is assumed to have
  a single element, but that diagnostic was never given a diagnostic group.
  Added the ``-Wtentative-definition-array`` warning group to cover this.
  Fixes #GH87766

- Clang now uses the correct type-parameter-key (``class`` or ``typename``) when printing
  template template parameter declarations.

- Clang now diagnoses requires expressions with explicit object parameters.

- Clang now looks up members of the current instantiation in the template definition context
  if the current instantiation has no dependent base classes.

  .. code-block:: c++

     template<typename T>
     struct A {
       int f() {
         return this->x; // error: no member named 'x' in 'A<T>'
       }
     };

- Clang emits a ``-Wparentheses`` warning for expressions with consecutive comparisons like ``x < y < z``.
  Fixes #GH20456.

- Clang no longer emits a "declared here" note for a builtin function that has no declaration in source.
  Fixes #GH93369.

- Clang now has an improved error message for captures that refer to a class member.
  Fixes #GH94764.

- Clang now diagnoses unsupported class declarations for ``std::initializer_list<E>`` when they are
  used rather than when they are needed for constant evaluation or when code is generated for them.
  The check is now stricter to prevent crashes for some unsupported declarations (Fixes #GH95495).

- Clang now diagnoses dangling cases where a pointer is assigned to a temporary
  that will be destroyed at the end of the full expression.
  Fixes #GH54492.

- Clang now shows implicit deduction guides when diagnosing overload resolution failure. #GH92393.

- Clang no longer emits a "no previous prototype" warning for Win32 entry points under ``-Wmissing-prototypes``.
  Fixes #GH94366.

- For the ARM target, calling an interrupt handler from another function is now an error. #GH95359.

- Clang now diagnoses integer constant expressions that are folded to a constant value as an extension in more circumstances. Fixes #GH59863

- Clang now diagnoses dangling assignments for pointer-like objects (annotated with `[[gsl::Pointer]]`) under `-Wdangling-assignment-gsl` (off by default)
  Fixes #GH63310.

- Clang now diagnoses uses of alias templates with a deprecated attribute. (Fixes #GH18236).

  .. code-block:: c++

     template <typename T>
     struct NoAttr {
     };

     template <typename T>
     using UsingWithAttr __attribute__((deprecated)) = NoAttr<T>;

     UsingWithAttr<int> objUsingWA; // warning: 'UsingWithAttr' is deprecated

- Clang now diagnoses undefined behavior in constant expressions more consistently. This includes invalid shifts, and signed overflow in arithmetic.

- Clang now diagnoses dangling references to fields of temporary objects. Fixes #GH81589.


Improvements to Clang's time-trace
----------------------------------

- Clang now specifies that using ``auto`` in a lambda parameter is a C++14 extension when
  appropriate. (`#46059: <https://github.com/llvm/llvm-project/issues/46059>`_).

- Clang now adds source file infomation for template instantiations as ``event["args"]["filename"]``. This
  added behind an option ``-ftime-trace-verbose``. This is expected to increase the size of trace by 2-3 times.

Improvements to Coverage Mapping
--------------------------------

- Macros defined in system headers are not expanded in coverage
  mapping. Conditional expressions in system header macros are no
  longer taken into account for branch coverage. They can be included
  with ``-mllvm -system-headers-coverage``.
  (`#78920: <https://github.com/llvm/llvm-project/issues/78920>`_)
- MC/DC Coverage has been improved.
  (`#82448: <https://github.com/llvm/llvm-project/pull/82448>`_)

  - The maximum number of conditions is no longer limited to 6. See
    `this <SourceBasedCodeCoverage.html#mc-dc-instrumentation>` for
    more details.

Bug Fixes in This Version
-------------------------
- Clang's ``-Wundefined-func-template`` no longer warns on pure virtual
  functions. (#GH74016)

- Fixed missing warnings when comparing mismatched enumeration constants
  in C (#GH29217)

- Clang now accepts elaborated-type-specifiers that explicitly specialize
  a member class template for an implicit instantiation of a class template.

- Fixed missing warnings when doing bool-like conversions in C23 (#GH79435).
- Clang's ``-Wshadow`` no longer warns when an init-capture is named the same as
  a class field unless the lambda can capture this.
  Fixes (#GH71976)

- Clang now accepts qualified partial/explicit specializations of variable templates that
  are not nominable in the lookup context of the specialization.

- Clang now doesn't produce false-positive warning `-Wconstant-logical-operand`
  for logical operators in C23.
  Fixes (#GH64356).

- ``__is_trivially_relocatable`` no longer returns ``false`` for volatile-qualified types.
  Fixes (#GH77091).

- Clang no longer produces a false-positive `-Wunused-variable` warning
  for variables created through copy initialization having side-effects in C++17 and later.
  Fixes (#GH64356) (#GH79518).

- Fix value of predefined macro ``__FUNCTION__`` in MSVC compatibility mode.
  Fixes (#GH66114).

- Clang now emits errors for explicit specializations/instatiations of lambda call
  operator.
  Fixes (#GH83267).

- Fix crash on ill-formed partial specialization with CRTP.
  Fixes (#GH89374).

- Clang now correctly generates overloads for bit-precise integer types for
  builtin operators in C++. Fixes #GH82998.

- Fix crash when destructor definition is preceded with an equals sign.
  Fixes (#GH89544).

- When performing mixed arithmetic between ``_Complex`` floating-point types and integers,
  Clang now correctly promotes the integer to its corresponding real floating-point
  type only rather than to the complex type (e.g. ``_Complex float / int`` is now evaluated
  as ``_Complex float / float`` rather than ``_Complex float / _Complex float``), as mandated
  by the C standard. This significantly improves codegen of `*` and `/` especially.
  Fixes #GH31205.

- Fixes an assertion failure on invalid code when trying to define member
  functions in lambdas.

- Fixed a regression in CTAD that a friend declaration that befriends itself may cause
  incorrect constraint substitution. (#GH86769).

- Fixed an assertion failure on invalid InitListExpr in C89 mode (#GH88008).

- Fixed missing destructor calls when we branch from middle of an expression.
  This could happen through a branch in stmt-expr or in an expression containing a coroutine
  suspension. Fixes (#GH63818) (#GH88478).

- Clang will no longer diagnose an erroneous non-dependent ``switch`` condition
  during instantiation, and instead will only diagnose it once, during checking
  of the function template.

- Clang now allows the value of unroll count to be zero in ``#pragma GCC unroll`` and ``#pragma unroll``.
  The values of 0 and 1 block any unrolling of the loop. This keeps the same behavior with GCC.
  Fixes (`#88624 <https://github.com/llvm/llvm-project/issues/88624>`_).

- Clang will no longer emit a duplicate -Wunused-value warning for an expression
  `(A, B)` which evaluates to glvalue `B` that can be converted to non ODR-use. (#GH45783)

- Clang now correctly disallows VLA type compound literals, e.g. ``(int[size]){}``,
  as the C standard mandates. (#GH89835)

- ``__is_array`` and ``__is_bounded_array`` no longer return ``true`` for
  zero-sized arrays. Fixes (#GH54705).

- Correctly reject declarations where a statement is required in C.
  Fixes #GH92775

- Fixed `static_cast` to array of unknown bound. Fixes (#GH62863).

- Fixed Clang crashing when failing to perform some C++ Initialization Sequences. (#GH98102)

- ``__is_trivially_equality_comparable`` no longer returns true for types which
  have a constrained defaulted comparison operator (#GH89293).

- Fixed Clang from generating dangling StringRefs when deserializing Exprs & Stmts (#GH98667)

- ``__has_unique_object_representations`` correctly handles arrays of unknown bounds of
  types by ensuring they are complete and instantiating them if needed. Fixes (#GH95311).

- ``typeof_unqual`` now properly removes type qualifiers from arrays and their element types. (#GH92667)

- Fixed an assertion failure when a template non-type parameter contains
  an invalid expression.

- Fixed the definition of ``ATOMIC_FLAG_INIT`` in ``<stdatomic.h>`` so it can
  be used in C++.

Bug Fixes to Compiler Builtins
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- Fix crash when atomic builtins are called with pointer to zero-size struct (#GH90330)

- Clang now allows pointee types of atomic builtin arguments to be complete template types
  that was not instantiated elsewhere.

Bug Fixes to Attribute Support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Bug Fixes to C++ Support
^^^^^^^^^^^^^^^^^^^^^^^^

- Fix crash when calling the constructor of an invalid class.
  (#GH10518) (#GH67914) (#GH78388)
- Fix crash when using lifetimebound attribute in function with trailing return.
  (#GH73619)
- Addressed an issue where constraints involving injected class types are perceived
  distinct from its specialization types. (#GH56482)
- Fixed a bug where variables referenced by requires-clauses inside
  nested generic lambdas were not properly injected into the constraint scope. (#GH73418)
- Fixed a crash where substituting into a requires-expression that refers to function
  parameters during the equivalence determination of two constraint expressions.
  (#GH74447)
- Fixed deducing auto& from const int in template parameters of partial
  specializations. (#GH77189)
- Fix for crash when using a erroneous type in a return statement.
  (#GH63244) (#GH79745)
- Fixed an out-of-bounds error caused by building a recovery expression for ill-formed
  function calls while substituting into constraints. (#GH58548)
- Fix incorrect code generation caused by the object argument
  of ``static operator()`` and ``static operator[]`` calls not being evaluated. (#GH67976)
- Fix crash and diagnostic with const qualified member operator new.
  Fixes (#GH79748)
- Fixed a crash where substituting into a requires-expression that involves parameter packs
  during the equivalence determination of two constraint expressions. (#GH72557)
- Fix a crash when specializing an out-of-line member function with a default
  parameter where we did an incorrect specialization of the initialization of
  the default parameter. (#GH68490)
- Fix a crash when trying to call a varargs function that also has an explicit object parameter.
  Fixes (#GH80971)
- Reject explicit object parameters on `new` and `delete` operators. (#GH82249)
- Fix a crash when trying to call a varargs function that also has an explicit object parameter. (#GH80971)
- Fixed a bug where abbreviated function templates would append their invented template parameters to
  an empty template parameter lists.
- Fix parsing of abominable function types inside type traits. Fixes #GH77585
- Clang now classifies aggregate initialization in C++17 and newer as constant
  or non-constant more accurately. Previously, only a subset of the initializer
  elements were considered, misclassifying some initializers as constant. Partially fixes
  #GH80510.
- Clang now ignores top-level cv-qualifiers on function parameters in template partial orderings. (#GH75404)
- No longer reject valid use of the ``_Alignas`` specifier when declaring a
  local variable, which is supported as a C11 extension in C++. Previously, it
  was only accepted at namespace scope but not at local function scope.
- Clang no longer tries to call consteval constructors at runtime when they appear in a member initializer. (#GH82154)
- Fix crash when using an immediate-escalated function at global scope. (#GH82258)
- Correctly immediate-escalate lambda conversion functions. (#GH82258)
- Fixed an issue where template parameters of a nested abbreviated generic lambda within
  a requires-clause lie at the same depth as those of the surrounding lambda. This,
  in turn, results in the wrong template argument substitution during constraint checking.
  (#GH78524)
- Clang no longer instantiates the exception specification of discarded candidate function
  templates when determining the primary template of an explicit specialization.
- Fixed a crash in Microsoft compatibility mode where unqualified dependent base class
  lookup searches the bases of an incomplete class.
- Fix a crash when an unresolved overload set is encountered on the RHS of a ``.*`` operator.
  (#GH53815)
- In ``__restrict``-qualified member functions, attach ``__restrict`` to the pointer type of
  ``this`` rather than the pointee type.
  Fixes (#GH82941), (#GH42411) and (#GH18121).
- Clang now properly reports supported C++11 attributes when using
  ``__has_cpp_attribute`` and parses attributes with arguments in C++03 (#GH82995)
- Clang now properly diagnoses missing 'default' template arguments on a variety
  of templates. Previously we were diagnosing on any non-function template
  instead of only on class, alias, and variable templates, as last updated by
  CWG2032. Fixes (#GH83461)
- Fixed an issue where an attribute on a declarator would cause the attribute to
  be destructed prematurely. This fixes a pair of Chromium that were brought to
  our attention by an attempt to fix in (#GH77703). Fixes (#GH83385).
- Fix evaluation of some immediate calls in default arguments.
  Fixes (#GH80630)
- Fixed an issue where the ``RequiresExprBody`` was involved in the lambda dependency
  calculation. (#GH56556), (#GH82849).
- Fix a bug where overload resolution falsely reported an ambiguity when it was comparing
  a member-function against a non member function or a member-function with an
  explicit object parameter against a member function with no explicit object parameter
  when one of the function had more specialized templates. Fixes #GH82509 and #GH74494
- Clang now supports direct lambda calls inside of a type alias template declarations.
  This addresses (#GH70601), (#GH76674), (#GH79555), (#GH81145) and (#GH82104).
- Allow access to a public template alias declaration that refers to friend's
  private nested type. (#GH25708).
- Fixed a crash in constant evaluation when trying to access a
  captured ``this`` pointer in a lambda with an explicit object parameter.
  Fixes (#GH80997)
- Fix an issue where missing set friend declaration in template class instantiation.
  Fixes (#GH84368).
- Fixed a crash while checking constraints of a trailing requires-expression of a lambda, that the
  expression references to an entity declared outside of the lambda. (#GH64808)
- Clang's __builtin_bit_cast will now produce a constant value for records with empty bases. See:
  (#GH82383)
- Fix a crash when instantiating a lambda that captures ``this`` outside of its context. Fixes (#GH85343).
- Fix an issue where a namespace alias could be defined using a qualified name (all name components
  following the first `::` were ignored).
- Fix an out-of-bounds crash when checking the validity of template partial specializations. (part of #GH86757).
- Fix an issue caused by not handling invalid cases when substituting into the parameter mapping of a constraint. Fixes (#GH86757).
- Fixed a bug that prevented member function templates of class templates declared with a deduced return type
  from being explicitly specialized for a given implicit instantiation of the class template.
- Fixed a crash when ``this`` is used in a dependent class scope function template specialization
  that instantiates to a static member function.
- Fix crash when inheriting from a cv-qualified type. Fixes #GH35603
- Fix a crash when the using enum declaration uses an anonymous enumeration. Fixes (#GH86790).
- Handled an edge case in ``getFullyPackExpandedSize`` so that we now avoid a false-positive diagnostic. (#GH84220)
- Clang now correctly tracks type dependence of by-value captures in lambdas with an explicit
  object parameter.
  Fixes (#GH70604), (#GH79754), (#GH84163), (#GH84425), (#GH86054), (#GH86398), and (#GH86399).
- Fix a crash when deducing ``auto`` from an invalid dereference (#GH88329).
- Fix a crash in requires expression with templated base class member function. Fixes (#GH84020).
- Fix a crash caused by defined struct in a type alias template when the structure
  has fields with dependent type. Fixes (#GH75221).
- Fix the Itanium mangling of lambdas defined in a member of a local class (#GH88906)
- Fixed a crash when trying to evaluate a user-defined ``static_assert`` message whose ``size()``
  function returns a large or negative value. Fixes (#GH89407).
- Fixed a use-after-free bug in parsing of type constraints with default arguments that involve lambdas. (#GH67235)
- Fixed bug in which the body of a consteval lambda within a template was not parsed as within an
  immediate function context.
- Fix CTAD for ``std::initializer_list``. This allows ``std::initializer_list{1, 2, 3}`` to be deduced as
  ``std::initializer_list<int>`` as intended.
- Fix a bug on template partial specialization whose template parameter is `decltype(auto)`.
- Fix a bug on template partial specialization with issue on deduction of nontype template parameter
  whose type is `decltype(auto)`. Fixes (#GH68885).
- Clang now correctly treats the noexcept-specifier of a friend function to be a complete-class context.
- Fix an assertion failure when parsing an invalid members of an anonymous class. (#GH85447)
- Fixed a misuse of ``UnresolvedLookupExpr`` for ill-formed templated expressions. Fixes (#GH48673), (#GH63243)
  and (#GH88832).
- Clang now defers all substitution into the exception specification of a function template specialization
  until the noexcept-specifier is instantiated.
- Fix a crash when an implicitly declared ``operator==`` function with a trailing requires-clause has its
  constraints compared to that of another declaration.
- Fix a bug where explicit specializations of member functions/function templates would have substitution
  performed incorrectly when checking constraints. Fixes (#GH90349).
- Clang now allows constrained member functions to be explicitly specialized for an implicit instantiation
  of a class template.
- Fix a C++23 bug in implementation of P2564R3 which evaluates immediate invocations in place
  within initializers for variables that are usable in constant expressions or are constant
  initialized, rather than evaluating them as a part of the larger manifestly constant evaluated
  expression.
- Fix a bug in access control checking due to dealyed checking of friend declaration. Fixes (#GH12361).
- Correctly treat the compound statement of an ``if consteval`` as an immediate context. Fixes (#GH91509).
- When partial ordering alias templates against template template parameters,
  allow pack expansions when the alias has a fixed-size parameter list. Fixes (#GH62529).
- Clang now ignores template parameters only used within the exception specification of candidate function
  templates during partial ordering when deducing template arguments from a function declaration or when
  taking the address of a function template.
- Fix a bug with checking constrained non-type template parameters for equivalence. Fixes (#GH77377).
- Fix a bug where the last argument was not considered when considering the most viable function for
  explicit object argument member functions. Fixes (#GH92188).
- Fix a C++11 crash when a non-const non-static member function is defined out-of-line with
  the ``constexpr`` specifier. Fixes (#GH61004).
- Clang no longer transforms dependent qualified names into implicit class member access expressions
  until it can be determined whether the name is that of a non-static member.
- Clang now correctly diagnoses when the current instantiation is used as an incomplete base class.
- Clang no longer treats ``constexpr`` class scope function template specializations of non-static members
  as implicitly ``const`` in language modes after C++11.
- Fixed a crash when trying to emit captures in a lambda call operator with an explicit object
  parameter that is called on a derived type of the lambda.
  Fixes (#GH87210), (GH89541).
- Clang no longer tries to check if an expression is immediate-escalating in an unevaluated context.
  Fixes (#GH91308).
- Fix a crash caused by a regression in the handling of ``source_location``
  in dependent contexts. Fixes (#GH92680).
- Fixed a crash when diagnosing failed conversions involving template parameter
  packs. (#GH93076)
- Fixed a regression introduced in Clang 18 causing a static function overloading a non-static function
  with the same parameters not to be diagnosed. (Fixes #GH93456).
- Clang now diagnoses unexpanded parameter packs in attributes. (Fixes #GH93269).
- Clang now allows ``@$``` in raw string literals. Fixes (#GH93130).
- Fix an assertion failure when checking invalid ``this`` usage in the wrong context. (Fixes #GH91536).
- Clang no longer models dependent NTTP arguments as ``TemplateParamObjectDecl`` s. Fixes (#GH84052).
- Fix incorrect merging of modules which contain using declarations which shadow
  other declarations. This could manifest as ODR checker false positives.
  Fixes (`#80252 <https://github.com/llvm/llvm-project/issues/80252>`_)
- Fix a regression introduced in Clang 18 causing incorrect overload resolution in the presence of functions only
  differering by their constraints when only one of these function was variadic.
- Fix a crash when a variable is captured by a block nested inside a lambda. (Fixes #GH93625).
- Fixed a type constraint substitution issue involving a generic lambda expression. (#GH93821)
- Fix a crash caused by improper use of ``__array_extent``. (#GH80474)
- Fixed several bugs in capturing variables within unevaluated contexts. (#GH63845), (#GH67260), (#GH69307),
  (#GH88081), (#GH89496), (#GH90669), (#GH91633) and (#GH97453).
- Fixed a crash in constraint instantiation under nested lambdas with dependent parameters.
- Fixed handling of brace ellison when building deduction guides. (#GH64625), (#GH83368).
- Fixed a failed assertion when attempting to convert an integer representing the difference
  between the addresses of two labels (a GNU extension) to a pointer within a constant expression. (#GH95366).
- Fix immediate escalation bugs in the presence of dependent call arguments. (#GH94935)
- Clang now diagnoses explicit specializations with storage class specifiers in all contexts.
- Fix an assertion failure caused by parsing a lambda used as a default argument for the value of a
  forward-declared class. (#GH93512).
- Fixed a bug in access checking inside return-type-requirement of compound requirements. (#GH93788).
- Fixed an assertion failure about invalid conversion when calling lambda. (#GH96205).
- Fixed a bug where the first operand of binary ``operator&`` would be transformed as if it was the operand
  of the address of operator. (#GH97483).
- Fixed an assertion failure about a constant expression which is a known integer but is not
  evaluated to an integer. (#GH96670).
- Fixed a bug where references to lambda capture inside a ``noexcept`` specifier were not correctly
  instantiated. (#GH95735).
- Fixed a CTAD substitution bug involving type aliases that reference outer template parameters. (#GH94614).
- Clang now correctly handles unexpanded packs in the template parameter list of a generic lambda expression
  (#GH48937)
- Fix a crash when parsing an invalid type-requirement in a requires expression. (#GH51868)
- Fix parsing of built-in type-traits such as ``__is_pointer`` in libstdc++ headers. (#GH95598)
- Fixed failed assertion when resolving context of defaulted comparison method outside of struct. (#GH96043).
- Clang now diagnoses explicit object parameters in member pointers and other contexts where they should not appear.
  Fixes (#GH85992).
- Fixed a crash-on-invalid bug involving extraneous template parameter with concept substitution. (#GH73885)
- Fixed assertion failure by skipping the analysis of an invalid field declaration. (#GH99868)
- Fix an issue with dependent source location expressions (#GH106428), (#GH81155), (#GH80210), (#GH85373)
- Fix handling of ``_`` as the name of a lambda's init capture variable. (#GH107024)
- Fixed recognition of ``std::initializer_list`` when it's surrounded with ``extern "C++"`` and exported
  out of a module (which is the case e.g. in MSVC's implementation of ``std`` module). (#GH118218)

Bug Fixes to AST Handling
^^^^^^^^^^^^^^^^^^^^^^^^^
- Clang now properly preserves ``FoundDecls`` within a ``ConceptReference``. (#GH82628)
- The presence of the ``typename`` keyword is now stored in ``TemplateTemplateParmDecl``.
- Fixed malformed AST generated for anonymous union access in templates. (#GH90842)
- Improved preservation of qualifiers and sugar in `TemplateNames`, including
  template keyword.

Miscellaneous Bug Fixes
^^^^^^^^^^^^^^^^^^^^^^^

- Fixed an infinite recursion in ASTImporter, on return type declared inside
  body of C++11 lambda without trailing return (#GH68775).
- Fixed declaration name source location of instantiated function definitions (GH71161).
- Improve diagnostic output to print an expression instead of 'no argument` when comparing Values as template arguments.

Miscellaneous Clang Crashes Fixed
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

- Do not attempt to dump the layout of dependent types or invalid declarations
  when ``-fdump-record-layouts-complete`` is passed. Fixes #GH83684.
- Unhandled StructuralValues in the template differ (#GH93068).

OpenACC Specific Changes
------------------------

Target Specific Changes
-----------------------

AMDGPU Support
^^^^^^^^^^^^^^

X86 Support
^^^^^^^^^^^

- Remove knl/knm specific ISA supports: AVX512PF, AVX512ER, PREFETCHWT1
- Support has been removed for the AMD "3DNow!" instruction-set.
  Neither modern AMD CPUs, nor any Intel CPUs implement these
  instructions, and they were never widely used.

  * The options ``-m3dnow`` and ``-m3dnowa`` are no longer honored, and will emit a warning if used.
  * The macros ``__3dNOW__`` and ``__3dNOW_A__`` are no longer ever set by the compiler.
  * The header ``<mm3dnow.h>`` is deprecated, and emits a warning if included.
  * The 3dNow intrinsic functions have been removed: ``_m_femms``,
    ``_m_pavgusb``, ``_m_pf2id``, ``_m_pfacc``, ``_m_pfadd``,
    ``_m_pfcmpeq``, ``_m_pfcmpge``, ``_m_pfcmpgt``, ``_m_pfmax``,
    ``_m_pfmin``, ``_m_pfmul``, ``_m_pfrcp``, ``_m_pfrcpit1``,
    ``_m_pfrcpit2``, ``_m_pfrsqrt``, ``_m_pfrsqrtit1``, ``_m_pfsub``,
    ``_m_pfsubr``, ``_m_pi2fd``, ``_m_pmulhrw``, ``_m_pf2iw``,
    ``_m_pfnacc``, ``_m_pfpnacc``, ``_m_pi2fw``, ``_m_pswapdsf``,
    ``_m_pswapdsi``.
  * The compiler builtins corresponding to each of the above
    intrinsics have also been removed  (``__builtin_ia32_femms``, and so on).
  * "3DNow!" instructions remain supported in assembly code, including
    inside inline-assembly.

Arm and AArch64 Support
^^^^^^^^^^^^^^^^^^^^^^^

- ARMv7+ targets now default to allowing unaligned access, except Armv6-M, and
  Armv8-M without the Main Extension. Baremetal targets should check that the
  new default will work with their system configurations, since it requires
  that SCTLR.A is 0, SCTLR.U is 1, and that the memory in question is
  configured as "normal" memory. This brings Clang in-line with the default
  settings for GCC and Arm Compiler. Aside from making Clang align with other
  compilers, changing the default brings major performance and code size
  improvements for most targets. We have not changed the default behavior for
  ARMv6, but may revisit that decision in the future. Users can restore the old
  behavior with -m[no-]unaligned-access.

- An alias identifier (rdma) has been added for targeting the AArch64
  Architecture Extension which uses Rounding Doubling Multiply Accumulate
  instructions (rdm). The identifier is available on the command line as
  a feature modifier for -march and -mcpu as well as via target attributes
  like ``target_version`` or ``target_clones``.

- Support has been added for the following processors (-mcpu identifiers in parenthesis):
    * Arm Cortex-R52+ (cortex-r52plus).
    * Arm Cortex-R82AE (cortex-r82ae).
    * Arm Cortex-A78AE (cortex-a78ae).
    * Arm Cortex-A520AE (cortex-a520ae).
    * Arm Cortex-A720AE (cortex-a720ae).
    * Arm Cortex-A725 (cortex-a725).
    * Arm Cortex-X925 (cortex-x925).
    * Arm Neoverse-N3 (neoverse-n3).
    * Arm Neoverse-V3 (neoverse-v3).
    * Arm Neoverse-V3AE (neoverse-v3ae).
- ``-mbranch-protection=gcs`` has been added which enables support for the
  Guarded Control Stack extension, and ``-mbranch-protection=standard`` also
  enables this. Enabling GCS causes the GCS GNU property bit to be set on output
  objects. It doesn't cause any code generation changes, as the code generated
  by clang is already compatible with GCS.

 - Experimental support has been added for pointer authentication ABI for С/C++.

 - Pointer authentication ABI could be enabled for AArch64 Linux via
   ``-mabi=pauthtest`` option or via specifying ``pauthtest`` environment part of
   target triple.

 - The C23 ``_BitInt`` implementation has been brought into compliance
   with AAPCS32 and AAPCS64.

Android Support
^^^^^^^^^^^^^^^

Windows Support
^^^^^^^^^^^^^^^

- The clang-cl ``/Ot`` compiler option ("optimize for speed", also implied by
  ``/O2``) now maps to clang's ``-O3`` optimizataztion level instead of ``-O2``.
  Users who prefer the old behavior can use ``clang-cl /Ot /clang:-O2 ...``.

- Clang-cl now supports function targets with intrinsic headers. This allows
  for runtime feature detection of intrinsics. Previously under clang-cl
  ``immintrin.h`` and similar intrinsic headers would only include the intrinsics
  if building with that feature enabled at compile time, e.g. ``avxintrin.h``
  would only be included if AVX was enabled at compile time. This was done to work
  around include times from MSVC STL including ``intrin.h`` under clang-cl.
  Clang-cl now provides ``intrin0.h`` for MSVC STL and therefore all intrinsic
  features without requiring enablement at compile time. Fixes #GH53520

- Improved compile times with MSVC STL. MSVC provides ``intrin0.h`` which is a
  header that only includes intrinsics that are used by MSVC STL to avoid the
  use of ``intrin.h``. MSVC STL when compiled under clang uses ``intrin.h``
  instead. Clang-cl now provides ``intrin0.h`` for the same compiler throughput
  purposes as MSVC. Clang-cl also provides ``yvals_core.h`` to redefine
  ``_STL_INTRIN_HEADER`` to expand to ``intrin0.h`` instead of ``intrin.h``.
  This also means that if all intrinsic features are enabled at compile time
  including STL headers will no longer slow down compile times since ``intrin.h``
  is not included from MSVC STL.

- When the target triple is `*-windows-msvc` strict aliasing is now disabled by default
  to ensure compatibility with msvc. Previously strict aliasing was only disabled if the
  driver mode was cl.

LoongArch Support
^^^^^^^^^^^^^^^^^

- ``-march=la64v1.0`` and ``-march=la64v1.1`` have been added to select the
  ``la64v1.0`` and ``la64v1.1`` architecture respectively. And ``-march=la664``
  is added to support the ``la664`` micro-architecture.
- The 128-bits SIMD extension (``LSX``) is enabled by default.
- ``-msimd=`` has beend added to select the SIMD extension(s) to be enabled.
- Predefined macros ``__loongarch_simd_width`` and ``__loongarch_frecipe`` are
  added.

RISC-V Support
^^^^^^^^^^^^^^

- ``__attribute__((rvv_vector_bits(N)))`` is now supported for RVV vbool*_t types.
- Profile names in ``-march`` option are now supported.
- Passing empty structs/unions as arguments in C++ is now handled correctly. The behavior is similar to GCC's.
- ``-m[no-]scalar-strict-align`` and ``-m[no-]vector-strict-align`` options have
  been added to give separate control of whether scalar or vector misaligned
  accesses may be created. ``-m[no-]strict-align`` applies to both scalar and
  vector.

PowerPC Support
^^^^^^^^^^^^^^^

- Clang now emits errors for impossible ``__attribute__((musttail))``.
- Added support for ``-mcpu=[pwr11 | power11]`` and ``-mtune=[pwr11 | power11]``.
- Added support for ``builtin_cpu_supports`` on AIX, along with a subset of
  features that can be queried.

CUDA/HIP Language Changes
^^^^^^^^^^^^^^^^^^^^^^^^^

- PTX is no longer included by default when compiling for CUDA. Using
  ``--cuda-include-ptx=all`` will return the old behavior.

CUDA Support
^^^^^^^^^^^^
- Clang now supports CUDA SDK up to 12.5

AIX Support
^^^^^^^^^^^

- Introduced the ``-maix-small-local-dynamic-tls`` option to produce a faster
  access sequence for local-dynamic TLS variables where the offset from the TLS
  base is encoded as an immediate operand.
  This access sequence is not used for TLS variables larger than 32KB, and is
  currently only supported on 64-bit mode.
- Introduced the options ``-mtocdata/-mno-tocdata`` to enable/disable TOC data
  transformations for the listed suitable variables.
- Introduced the ``-maix-shared-lib-tls-model-opt`` option to enable the tuning
  of changing local-dynamic mode access(es) to initial-exec access(es) at the
  function level on 64-bit mode.
- Clang now emits errors for ``-gdwarf-5``.
- Added the support of the OpenMP runtime libomp on AIX. OpenMP applications can be
  compiled with ``-fopenmp`` and execute on AIX.

NetBSD Support
^^^^^^^^^^^^^^

- Removed support for building NetBSD/i386 6.x or older binaries.

WebAssembly Support
^^^^^^^^^^^^^^^^^^^

The -mcpu=generic configuration now enables multivalue and reference-types.
These proposals are standardized and available in all major engines. Enabling
multivalue here only enables the language feature but does not turn on the
multivalue ABI (this enables non-ABI uses of multivalue, like exnref).

AVR Support
^^^^^^^^^^^

DWARF Support in Clang
----------------------

Floating Point Support in Clang
-------------------------------

- Add ``__builtin__fmaf16`` builtin for floating point types.

Fixed Point Support in Clang
----------------------------

- Support fixed point precision macros according to ``7.18a.3`` of
  `ISO/IEC TR 18037:2008 <https://standards.iso.org/ittf/PubliclyAvailableStandards/c051126_ISO_IEC_TR_18037_2008.zip>`_.

AST Matchers
------------

- Fixes a long-standing performance issue in parent map generation for
  ancestry-based matchers such as ``hasParent`` and ``hasAncestor``, making
  them significantly faster.
- ``isInStdNamespace`` now supports Decl declared with ``extern "C++"``.
- Add ``isExplicitObjectMemberFunction``.
- Fixed ``forEachArgumentWithParam`` and ``forEachArgumentWithParamType`` to
  not skip the explicit object parameter for operator calls.
- Fixed captureVars assertion failure if not capturesVariables. (#GH76425)
- ``forCallable`` now properly preserves binding on successful match. (#GH89657)

clang-format
------------

- ``AlwaysBreakTemplateDeclarations`` is deprecated and renamed to
  ``BreakTemplateDeclarations``.
- ``AlwaysBreakAfterReturnType`` is deprecated and renamed to
  ``BreakAfterReturnType``.
- Handles Java switch expressions.
- Adds ``AllowShortCaseExpressionOnASingleLine`` option.
- Adds ``AlignCaseArrows`` suboption to ``AlignConsecutiveShortCaseStatements``.
- Adds ``LeftWithLastLine`` suboption to ``AlignEscapedNewlines``.
- Adds ``KeepEmptyLines`` option to deprecate ``KeepEmptyLinesAtEOF``
  and ``KeepEmptyLinesAtTheStartOfBlocks``.
- Add ``ExceptDoubleParentheses`` sub-option for ``SpacesInParensOptions``
  to override addition of spaces between multiple, non-redundant parentheses
  similar to the rules used for ``RemoveParentheses``.

libclang
--------

- ``clang_getSpellingLocation`` now correctly resolves macro expansions; that
  is, it returns the spelling location instead of the expansion location.

Static Analyzer
---------------

New features
^^^^^^^^^^^^

- The attribute ``[[clang::suppress]]`` can now be applied to declarations.
  (#GH80371)

- Support C++23 static operator calls. (#GH84972)

Crash and bug fixes
^^^^^^^^^^^^^^^^^^^

- Fixed crashing on loops if the loop variable was declared in switch blocks
  but not under any case blocks if ``unroll-loops=true`` analyzer config is
  set. (#GH68819)

- Fixed a crash in ``security.cert.env.InvalidPtr`` checker when accidentally
  matched user-defined ``strerror`` and similar library functions. (#GH88181)

- Fixed a crash when storing through an address that refers to the address of
  a label. (#GH89185)

- Fixed a crash when using ``__builtin_bitcast(type, array)`` as an array
  subscript. (#GH94496)

- Z3 crosschecking (aka. Z3 refutation) is now bounded, and can't consume
  more total time than the eymbolic execution itself. (#GH97298)

- In clang-18, we regressed in terms of analysis time for projects having many
  nested loops with buffer indexing or shifting or other binary operations.
  For example, functions computing different hash values. Some of this slowdown
  was attributed to taint analysis, which is fixed now. (#GH105493)

- ``std::addressof``, ``std::as_const``, ``std::forward``,
  ``std::forward_like``, ``std::move``, ``std::move_if_noexcept``, are now
  modeled just like their builtin counterpart. (#GH94193)

Improvements
^^^^^^^^^^^^

Moved checkers
^^^^^^^^^^^^^^

- Moved ``alpha.cplusplus.ArrayDelete`` out of the ``alpha`` package
  to ``cplusplus.ArrayDelete``. (#GH83985)
  `Documentation <https://clang.llvm.org/docs/analyzer/checkers.html#cplusplus-arraydelete-c>`__.

- Moved ``alpha.unix.Stream`` out of the ``alpha`` package to
  ``unix.Stream``. (#GH89247)
  `Documentation <https://clang.llvm.org/docs/analyzer/checkers.html#unix-stream-c>`__.

- Moved ``alpha.unix.BlockInCriticalSection`` out of the ``alpha`` package to
  ``unix.BlockInCriticalSection``. (#GH93815)
  `Documentation <https://clang.llvm.org/docs/analyzer/checkers.html#unix-blockincriticalsection-c-c>`__.

- Moved ``alpha.security.cert.pos.34c`` out of the ``alpha`` package to
  ``security.PutenvStackArray``. (#GH92424, #GH93815)
  `Documentation <https://clang.llvm.org/docs/analyzer/checkers.html#security-putenvstackarray-c>`__.

- Moved ``alpha.core.SizeofPtr`` into ``clang-tidy``
  ``bugprone-sizeof-expression``. (#GH95118, #GH94356)
  `Documentation <https://clang.llvm.org/extra/clang-tidy/checks/bugprone/sizeof-expression.html>`__.

.. _release-notes-sanitizers:

Sanitizers
----------

- ``-fsanitize=signed-integer-overflow`` now instruments signed arithmetic even
  when ``-fwrapv`` is enabled. Previously, only division checks were enabled.

  Users with ``-fwrapv`` as well as a sanitizer group like
  ``-fsanitize=undefined`` or ``-fsanitize=integer`` enabled may want to
  manually disable potentially noisy signed integer overflow checks with
  ``-fno-sanitize=signed-integer-overflow``

- ``-fsanitize=cfi -fsanitize-cfi-cross-dso`` (cross-DSO CFI instrumentation)
  now generates the ``__cfi_check`` function with proper target-specific
  attributes, for example allowing unwind table generation.

Python Binding Changes
----------------------

- Exposed `CXRewriter` API as `class Rewriter`.
- Add some missing kinds from Index.h (CursorKind: 149-156, 272-320, 420-437.
  TemplateArgumentKind: 5-9. TypeKind: 161-175 and 178).
- Add support for retrieving binary operator information through
  Cursor.binary_operator().

OpenMP Support
--------------

- Added support for the `[[omp::assume]]` attribute.
- AIX added an include directory for ``omp.h`` at ``/opt/IBM/openxlCSDK/include/openmp``.

Additional Information
======================

A wide variety of additional information is available on the `Clang web
page <https://clang.llvm.org/>`_. The web page contains versions of the
API documentation which are up-to-date with the Git version of
the source code. You can access versions of these documents specific to
this release by going into the "``clang/docs/``" directory in the Clang
tree.

If you have any questions or comments about Clang, please feel free to
contact us on the `Discourse forums (Clang Frontend category)
<https://discourse.llvm.org/c/clang/6>`_.
