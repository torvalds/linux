clang, clang++, clang-cpp - the Clang C, C++, and Objective-C compiler
======================================================================

SYNOPSIS
--------

:program:`clang` [*options*] *filename ...*

DESCRIPTION
-----------

:program:`clang` is a C, C++, and Objective-C compiler which encompasses
preprocessing, parsing, optimization, code generation, assembly, and linking.
Depending on which high-level mode setting is passed, Clang will stop before
doing a full link.  While Clang is highly integrated, it is important to
understand the stages of compilation, to understand how to invoke it.  These
stages are:

Driver
    The clang executable is actually a small driver which controls the overall
    execution of other tools such as the compiler, assembler and linker.
    Typically you do not need to interact with the driver, but you
    transparently use it to run the other tools.

Preprocessing
    This stage handles tokenization of the input source file, macro expansion,
    #include expansion and handling of other preprocessor directives.  The
    output of this stage is typically called a ".i" (for C), ".ii" (for C++),
    ".mi" (for Objective-C), or ".mii" (for Objective-C++) file.

Parsing and Semantic Analysis
    This stage parses the input file, translating preprocessor tokens into a
    parse tree.  Once in the form of a parse tree, it applies semantic
    analysis to compute types for expressions as well and determine whether
    the code is well formed. This stage is responsible for generating most of
    the compiler warnings as well as parse errors. The output of this stage is
    an "Abstract Syntax Tree" (AST).

Code Generation and Optimization
    This stage translates an AST into low-level intermediate code (known as
    "LLVM IR") and ultimately to machine code.  This phase is responsible for
    optimizing the generated code and handling target-specific code generation.
    The output of this stage is typically called a ".s" file or "assembly" file.

    Clang also supports the use of an integrated assembler, in which the code
    generator produces object files directly. This avoids the overhead of
    generating the ".s" file and of calling the target assembler.

Assembler
    This stage runs the target assembler to translate the output of the
    compiler into a target object file. The output of this stage is typically
    called a ".o" file or "object" file.

Linker
    This stage runs the target linker to merge multiple object files into an
    executable or dynamic library. The output of this stage is typically called
    an "a.out", ".dylib" or ".so" file.

:program:`Clang Static Analyzer`

The Clang Static Analyzer is a tool that scans source code to try to find bugs
through code analysis.  This tool uses many parts of Clang and is built into
the same driver.  Please see <https://clang-analyzer.llvm.org> for more details
on how to use the static analyzer.

OPTIONS
-------

Stage Selection Options
~~~~~~~~~~~~~~~~~~~~~~~

.. option:: -E

 Run the preprocessor stage.

.. option:: -fsyntax-only

 Run the preprocessor, parser and semantic analysis stages.

.. option:: -S

 Run the previous stages as well as LLVM generation and optimization stages
 and target-specific code generation, producing an assembly file.

.. option:: -c

 Run all of the above, plus the assembler, generating a target ".o" object file.

.. option:: no stage selection option

 If no stage selection option is specified, all stages above are run, and the
 linker is run to combine the results into an executable or shared library.

Language Selection and Mode Options
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. option:: -x <language>

 Treat subsequent input files as having type language.

.. option:: -std=<standard>

 Specify the language standard to compile for.

 Supported values for the C language are:

  | ``c89``
  | ``c90``
  | ``iso9899:1990``

   ISO C 1990

  | ``iso9899:199409``

   ISO C 1990 with amendment 1

  | ``gnu89``
  | ``gnu90``

   ISO C 1990 with GNU extensions

  | ``c99``
  | ``iso9899:1999``

   ISO C 1999

  | ``gnu99``

   ISO C 1999 with GNU extensions

  | ``c11``
  | ``iso9899:2011``

   ISO C 2011

  | ``gnu11``

   ISO C 2011 with GNU extensions

  | ``c17``
  | ``iso9899:2017``

   ISO C 2017

  | ``gnu17``

   ISO C 2017 with GNU extensions

 The default C language standard is ``gnu17``, except on PS4, where it is
 ``gnu99``.

 Supported values for the C++ language are:

  | ``c++98``
  | ``c++03``

   ISO C++ 1998 with amendments

  | ``gnu++98``
  | ``gnu++03``

   ISO C++ 1998 with amendments and GNU extensions

  | ``c++11``

   ISO C++ 2011 with amendments

  | ``gnu++11``

    ISO C++ 2011 with amendments and GNU extensions

  | ``c++14``

   ISO C++ 2014 with amendments

  | ``gnu++14``

   ISO C++ 2014 with amendments and GNU extensions

  | ``c++17``

   ISO C++ 2017 with amendments

  | ``gnu++17``

   ISO C++ 2017 with amendments and GNU extensions

  | ``c++20``

   ISO C++ 2020 with amendments

  | ``gnu++20``

   ISO C++ 2020 with amendments and GNU extensions

  | ``c++23``

   ISO C++ 2023 with amendments

  | ``gnu++23``

   ISO C++ 2023 with amendments and GNU extensions

  | ``c++2c``

   Working draft for C++2c

  | ``gnu++2c``

   Working draft for C++2c with GNU extensions

 The default C++ language standard is ``gnu++17``.

 Supported values for the OpenCL language are:

  | ``cl1.0``

   OpenCL 1.0

  | ``cl1.1``

   OpenCL 1.1

  | ``cl1.2``

   OpenCL 1.2

  | ``cl2.0``

   OpenCL 2.0

 The default OpenCL language standard is ``cl1.0``.

 Supported values for the CUDA language are:

  | ``cuda``

   NVIDIA CUDA(tm)

.. option:: -stdlib=<library>

 Specify the C++ standard library to use; supported options are libstdc++ and
 libc++. If not specified, platform default will be used.

.. option:: -rtlib=<library>

 Specify the compiler runtime library to use; supported options are libgcc and
 compiler-rt. If not specified, platform default will be used.

.. option:: -ansi

 Same as -std=c89.

.. option:: -ObjC, -ObjC++

 Treat source input files as Objective-C and Object-C++ inputs respectively.

.. option:: -trigraphs

 Enable trigraphs.

.. option:: -ffreestanding

 Indicate that the file should be compiled for a freestanding, not a hosted,
 environment. Note that it is assumed that a freestanding environment will
 additionally provide `memcpy`, `memmove`, `memset` and `memcmp`
 implementations, as these are needed for efficient codegen for many programs.

.. option:: -fno-builtin

 Disable special handling and optimizations of well-known library functions,
 like :c:func:`strlen` and :c:func:`malloc`.

.. option:: -fno-builtin-<function>

 Disable special handling and optimizations for the specific library function.
 For example, ``-fno-builtin-strlen`` removes any special handling for the
 :c:func:`strlen` library function.

.. option:: -fno-builtin-std-<function>

 Disable special handling and optimizations for the specific C++ standard
 library function in namespace ``std``. For example,
 ``-fno-builtin-std-move_if_noexcept`` removes any special handling for the
 :cpp:func:`std::move_if_noexcept` library function.

 For C standard library functions that the C++ standard library also provides
 in namespace ``std``, use :option:`-fno-builtin-\<function\>` instead.

.. option:: -fmath-errno

 Indicate that math functions should be treated as updating :c:data:`errno`.

.. option:: -fpascal-strings

 Enable support for Pascal-style strings with "\\pfoo".

.. option:: -fms-extensions

 Enable support for Microsoft extensions.

.. option:: -fmsc-version=

 Set ``_MSC_VER``. When on Windows, this defaults to either the same value as
 the currently installed version of cl.exe, or ``1933``. Not set otherwise.

.. option:: -fborland-extensions

 Enable support for Borland extensions.

.. option:: -fwritable-strings

 Make all string literals default to writable.  This disables uniquing of
 strings and other optimizations.

.. option:: -flax-vector-conversions, -flax-vector-conversions=<kind>, -fno-lax-vector-conversions

 Allow loose type checking rules for implicit vector conversions.
 Possible values of <kind>:

 - ``none``: allow no implicit conversions between vectors
 - ``integer``: allow implicit bitcasts between integer vectors of the same
   overall bit-width
 - ``all``: allow implicit bitcasts between any vectors of the same
   overall bit-width

 <kind> defaults to ``integer`` if unspecified.

.. option:: -fblocks

 Enable the "Blocks" language feature.

.. option:: -fobjc-abi-version=version

 Select the Objective-C ABI version to use. Available versions are 1 (legacy
 "fragile" ABI), 2 (non-fragile ABI 1), and 3 (non-fragile ABI 2).

.. option:: -fobjc-nonfragile-abi-version=<version>

 Select the Objective-C non-fragile ABI version to use by default. This will
 only be used as the Objective-C ABI when the non-fragile ABI is enabled
 (either via :option:`-fobjc-nonfragile-abi`, or because it is the platform
 default).

.. option:: -fobjc-nonfragile-abi, -fno-objc-nonfragile-abi

 Enable use of the Objective-C non-fragile ABI. On platforms for which this is
 the default ABI, it can be disabled with :option:`-fno-objc-nonfragile-abi`.

Target Selection Options
~~~~~~~~~~~~~~~~~~~~~~~~

Clang fully supports cross compilation as an inherent part of its design.
Depending on how your version of Clang is configured, it may have support for a
number of cross compilers, or may only support a native target.

.. option:: -arch <architecture>

  Specify the architecture to build for (Mac OS X specific).

.. option:: -target <architecture>

  Specify the architecture to build for (all platforms).

.. option:: -mmacos-version-min=<version>

  When building for macOS, specify the minimum version supported by your
  application.

.. option:: -miphoneos-version-min

  When building for iPhone OS, specify the minimum version supported by your
  application.

.. option:: --print-supported-cpus

  Print out a list of supported processors for the given target (specified
  through ``--target=<architecture>`` or :option:`-arch` ``<architecture>``). If no
  target is specified, the system default target will be used.

.. option:: -mcpu=?, -mtune=?

  Acts as an alias for :option:`--print-supported-cpus`.

.. option:: -mcpu=help, -mtune=help

  Acts as an alias for :option:`--print-supported-cpus`.

.. option:: -march=<cpu>

  Specify that Clang should generate code for a specific processor family
  member and later.  For example, if you specify -march=i486, the compiler is
  allowed to generate instructions that are valid on i486 and later processors,
  but which may not exist on earlier ones.

.. option:: --print-enabled-extensions

  Prints the list of extensions that are enabled for the target specified by the
  combination of `--target`, `-march`, and `-mcpu` values. Currently, this
  option is only supported on AArch64 and RISC-V. On RISC-V, this option also
  prints out the ISA string of enabled extensions.

.. option:: --print-supported-extensions

  Prints the list of all extensions that are supported for every CPU target
  for an architecture (specified through ``--target=<architecture>`` or
  :option:`-arch` ``<architecture>``). If no target is specified, the system
  default target will be used. Currently, this option is only supported on
  AArch64 and RISC-V.

Code Generation Options
~~~~~~~~~~~~~~~~~~~~~~~

.. option:: -O0, -O1, -O2, -O3, -Ofast, -Os, -Oz, -Og, -O, -O4

  Specify which optimization level to use:

    :option:`-O0` Means "no optimization": this level compiles the fastest and
    generates the most debuggable code.

    :option:`-O1` Somewhere between :option:`-O0` and :option:`-O2`.

    :option:`-O2` Moderate level of optimization which enables most
    optimizations.

    :option:`-O3` Like :option:`-O2`, except that it enables optimizations that
    take longer to perform or that may generate larger code (in an attempt to
    make the program run faster).

    :option:`-Ofast` Enables all the optimizations from :option:`-O3` along
    with other aggressive optimizations that may violate strict compliance with
    language standards. This is deprecated in Clang 19 and a warning is emitted
    that :option:`-O3` in combination with :option:`-ffast-math` should be used
    instead if the request for non-standard math behavior is intended. There
    is no timeline yet for removal; the aim is to discourage use of
    :option:`-Ofast` due to the surprising behavior of an optimization flag
    changing the observable behavior of correct code.

    :option:`-Os` Like :option:`-O2` with extra optimizations to reduce code
    size.

    :option:`-Oz` Like :option:`-Os` (and thus :option:`-O2`), but reduces code
    size further.

    :option:`-Og` Like :option:`-O1`. In future versions, this option might
    disable different optimizations in order to improve debuggability.

    :option:`-O` Equivalent to :option:`-O1`.

    :option:`-O4` and higher

      Currently equivalent to :option:`-O3`

.. option:: -g, -gline-tables-only, -gmodules

  Control debug information output.  Note that Clang debug information works
  best at :option:`-O0`.  When more than one option starting with `-g` is
  specified, the last one wins:

    :option:`-g` Generate debug information.

    :option:`-gline-tables-only` Generate only line table debug information. This
    allows for symbolicated backtraces with inlining information, but does not
    include any information about variables, their locations or types.

    :option:`-gmodules` Generate debug information that contains external
    references to types defined in Clang modules or precompiled headers instead
    of emitting redundant debug type information into every object file.  This
    option transparently switches the Clang module format to object file
    containers that hold the Clang module together with the debug information.
    When compiling a program that uses Clang modules or precompiled headers,
    this option produces complete debug information with faster compile
    times and much smaller object files.

    This option should not be used when building static libraries for
    distribution to other machines because the debug info will contain
    references to the module cache on the machine the object files in the
    library were built on.

.. option:: -fstandalone-debug -fno-standalone-debug

  Clang supports a number of optimizations to reduce the size of debug
  information in the binary. They work based on the assumption that the
  debug type information can be spread out over multiple compilation units.
  For instance, Clang will not emit type definitions for types that are not
  needed by a module and could be replaced with a forward declaration.
  Further, Clang will only emit type info for a dynamic C++ class in the
  module that contains the vtable for the class.

  The :option:`-fstandalone-debug` option turns off these optimizations.
  This is useful when working with 3rd-party libraries that don't come with
  debug information.  This is the default on Darwin.  Note that Clang will
  never emit type information for types that are not referenced at all by the
  program.

.. option:: -feliminate-unused-debug-types

  By default, Clang does not emit type information for types that are defined
  but not used in a program. To retain the debug info for these unused types,
  the negation **-fno-eliminate-unused-debug-types** can be used.

.. option:: -fexceptions

  Allow exceptions to be thrown through Clang compiled stack frames (on many
  targets, this will enable unwind information for functions that might have
  an exception thrown through them). For most targets, this is enabled by
  default for C++.

.. option:: -ftrapv

  Generate code to catch integer overflow errors.  Signed integer overflow is
  undefined in C. With this flag, extra code is generated to detect this and
  abort when it happens.

.. option:: -fvisibility

  This flag sets the default visibility level.

.. option:: -fcommon, -fno-common

  This flag specifies that variables without initializers get common linkage.
  It can be disabled with :option:`-fno-common`.

.. option:: -ftls-model=<model>

  Set the default thread-local storage (TLS) model to use for thread-local
  variables. Valid values are: "global-dynamic", "local-dynamic",
  "initial-exec" and "local-exec". The default is "global-dynamic". The default
  model can be overridden with the tls_model attribute. The compiler will try
  to choose a more efficient model if possible.

.. option:: -flto, -flto=full, -flto=thin, -emit-llvm

  Generate output files in LLVM formats, suitable for link time optimization.
  When used with :option:`-S` this generates LLVM intermediate language
  assembly files, otherwise this generates LLVM bitcode format object files
  (which may be passed to the linker depending on the stage selection options).

  The default for :option:`-flto` is "full", in which the
  LLVM bitcode is suitable for monolithic Link Time Optimization (LTO), where
  the linker merges all such modules into a single combined module for
  optimization. With "thin", :doc:`ThinLTO <../ThinLTO>`
  compilation is invoked instead.

  .. note::

     On Darwin, when using :option:`-flto` along with :option:`-g` and
     compiling and linking in separate steps, you also need to pass
     ``-Wl,-object_path_lto,<lto-filename>.o`` at the linking step to instruct the
     ld64 linker not to delete the temporary object file generated during Link
     Time Optimization (this flag is automatically passed to the linker by Clang
     if compilation and linking are done in a single step). This allows debugging
     the executable as well as generating the ``.dSYM`` bundle using :manpage:`dsymutil(1)`.

Driver Options
~~~~~~~~~~~~~~

.. option:: -###

  Print (but do not run) the commands to run for this compilation.

.. option:: --help

  Display available options.

.. option:: -Qunused-arguments

  Do not emit any warnings for unused driver arguments.

.. option:: -Wa,<args>

  Pass the comma separated arguments in args to the assembler.

.. option:: -Wl,<args>

  Pass the comma separated arguments in args to the linker.

.. option:: -Wp,<args>

  Pass the comma separated arguments in args to the preprocessor.

.. option:: -Xanalyzer <arg>

  Pass arg to the static analyzer.

.. option:: -Xassembler <arg>

  Pass arg to the assembler.

.. option:: -Xlinker <arg>

  Pass arg to the linker.

.. option:: -Xpreprocessor <arg>

  Pass arg to the preprocessor.

.. option:: -o <file>

  Write output to file.

.. option:: -print-file-name=<file>

  Print the full library path of file.

.. option:: -print-libgcc-file-name

  Print the library path for the currently used compiler runtime library
  ("libgcc.a" or "libclang_rt.builtins.*.a").

.. option:: -print-prog-name=<name>

  Print the full program path of name.

.. option:: -print-search-dirs

  Print the paths used for finding libraries and programs.

.. option:: -save-temps

  Save intermediate compilation results.

.. option:: -save-stats, -save-stats=cwd, -save-stats=obj

  Save internal code generation (LLVM) statistics to a file in the current
  directory (:option:`-save-stats`/"-save-stats=cwd") or the directory
  of the output file ("-save-state=obj").

  You can also use environment variables to control the statistics reporting.
  Setting ``CC_PRINT_INTERNAL_STAT`` to ``1`` enables the feature, the report
  goes to stdout in JSON format.

  Setting ``CC_PRINT_INTERNAL_STAT_FILE`` to a file path makes it report
  statistics to the given file in the JSON format.

  Note that ``-save-stats`` take precedence over ``CC_PRINT_INTERNAL_STAT``
  and ``CC_PRINT_INTERNAL_STAT_FILE``.

.. option:: -integrated-as, -no-integrated-as

  Used to enable and disable, respectively, the use of the integrated
  assembler. Whether the integrated assembler is on by default is target
  dependent.

.. option:: -time

  Time individual commands.

.. option:: -ftime-report

  Print timing summary of each stage of compilation.

.. option:: -v

  Show commands to run and use verbose output.


Diagnostics Options
~~~~~~~~~~~~~~~~~~~

.. option:: -fshow-column, -fshow-source-location, -fcaret-diagnostics, -fdiagnostics-fixit-info, -fdiagnostics-parseable-fixits, -fdiagnostics-print-source-range-info, -fprint-source-range-info, -fdiagnostics-show-option, -fmessage-length

  These options control how Clang prints out information about diagnostics
  (errors and warnings). Please see the Clang User's Manual for more information.

Preprocessor Options
~~~~~~~~~~~~~~~~~~~~

.. option:: -D<macroname>=<value>

  Adds an implicit #define into the predefines buffer which is read before the
  source file is preprocessed.

.. option:: -U<macroname>

  Adds an implicit #undef into the predefines buffer which is read before the
  source file is preprocessed.

.. option:: -include <filename>

  Adds an implicit #include into the predefines buffer which is read before the
  source file is preprocessed.

.. option:: -I<directory>

  Add the specified directory to the search path for include files.

.. option:: -F<directory>

  Add the specified directory to the search path for framework include files.

.. option:: -nostdinc

  Do not search the standard system directories or compiler builtin directories
  for include files.

.. option:: -nostdlibinc

  Do not search the standard system directories for include files, but do
  search compiler builtin include directories.

.. option:: -nobuiltininc

  Do not search clang's builtin directory for include files.

.. option:: -fkeep-system-includes

  Usable only with :option:`-E`. Do not copy the preprocessed content of
  "system" headers to the output; instead, preserve the #include directive.
  This can greatly reduce the volume of text produced by :option:`-E` which
  can be helpful when trying to produce a "small" reproduceable test case.

  This option does not guarantee reproduceability, however. If the including
  source defines preprocessor symbols that influence the behavior of system
  headers (for example, ``_XOPEN_SOURCE``) the operation of :option:`-E` will
  remove that definition and thus can change the semantics of the included
  header. Also, using a different version of the system headers (especially a
  different version of the STL) may result in different behavior. Always verify
  the preprocessed file by compiling it separately.


ENVIRONMENT
-----------

.. envvar:: TMPDIR, TEMP, TMP

  These environment variables are checked, in order, for the location to write
  temporary files used during the compilation process.

.. envvar:: CPATH

  If this environment variable is present, it is treated as a delimited list of
  paths to be added to the default system include path list. The delimiter is
  the platform dependent delimiter, as used in the PATH environment variable.

  Empty components in the environment variable are ignored.

.. envvar:: C_INCLUDE_PATH, OBJC_INCLUDE_PATH, CPLUS_INCLUDE_PATH, OBJCPLUS_INCLUDE_PATH

  These environment variables specify additional paths, as for :envvar:`CPATH`, which are
  only used when processing the appropriate language.

.. envvar:: MACOSX_DEPLOYMENT_TARGET

  If :option:`-mmacos-version-min` is unspecified, the default deployment
  target is read from this environment variable. This option only affects
  Darwin targets.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/issues/>.  Most bug reports should
include preprocessed source files (use the :option:`-E` option) and the full
output of the compiler, along with information to reproduce.

SEE ALSO
--------

:manpage:`as(1)`, :manpage:`clang-local(1)`, :manpage:`ld(1)`
