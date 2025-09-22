.. _using-libcxx:

============
Using libc++
============

.. contents::
  :local:

Usually, libc++ is packaged and shipped by a vendor through some delivery vehicle
(operating system distribution, SDK, toolchain, etc) and users don't need to do
anything special in order to use the library.

This page contains information about configuration knobs that can be used by
users when they know libc++ is used by their toolchain, and how to use libc++
when it is not the default library used by their toolchain.


Using a different version of the C++ Standard
=============================================

Libc++ implements the various versions of the C++ Standard. Changing the version of
the standard can be done by passing ``-std=c++XY`` to the compiler. Libc++ will
automatically detect what Standard is being used and will provide functionality that
matches that Standard in the library.

.. code-block:: bash

  $ clang++ -std=c++17 test.cpp

.. warning::
  Using ``-std=c++XY`` with a version of the Standard that has not been ratified yet
  is considered unstable. Libc++ reserves the right to make breaking changes to the
  library until the standard has been ratified.


Enabling experimental C++ Library features
==========================================

Libc++ provides implementations of some experimental features. Experimental features
are either Technical Specifications (TSes) or official features that were voted to
the Standard but whose implementation is not complete or stable yet in libc++. Those
are disabled by default because they are neither API nor ABI stable. However, the
``-fexperimental-library`` compiler flag can be defined to turn those features on.

The following features are currently considered experimental and are only provided
when ``-fexperimental-library`` is passed:

* The parallel algorithms library (``<execution>`` and the associated algorithms)
* ``std::stop_token``, ``std::stop_source`` and ``std::stop_callback``
* ``std::jthread``
* ``std::chrono::tzdb`` and related time zone functionality

.. warning::
  Experimental libraries are experimental.
    * The contents of the ``<experimental/...>`` headers and the associated static
      library will not remain compatible between versions.
    * No guarantees of API or ABI stability are provided.
    * When the standardized version of an experimental feature is implemented,
      the experimental feature is removed two releases after the non-experimental
      version has shipped. The full policy is explained :ref:`here <experimental features>`.

.. note::
  On compilers that do not support the ``-fexperimental-library`` flag, users can
  define the ``_LIBCPP_ENABLE_EXPERIMENTAL`` macro and manually link against the
  appropriate static library (usually shipped as ``libc++experimental.a``) to get
  access to experimental library features.


Using libc++ when it is not the system default
==============================================

On systems where libc++ is provided but is not the default, Clang provides a flag
called ``-stdlib=`` that can be used to decide which standard library is used.
Using ``-stdlib=libc++`` will select libc++:

.. code-block:: bash

  $ clang++ -stdlib=libc++ test.cpp

On systems where libc++ is the library in use by default such as macOS and FreeBSD,
this flag is not required.


.. _alternate libcxx:

Using a custom built libc++
===========================

Most compilers provide a way to disable the default behavior for finding the
standard library and to override it with custom paths. With Clang, this can
be done with:

.. code-block:: bash

  $ clang++ -nostdinc++ -nostdlib++           \
            -isystem <install>/include/c++/v1 \
            -L <install>/lib                  \
            -Wl,-rpath,<install>/lib          \
            -lc++                             \
            test.cpp

The option ``-Wl,-rpath,<install>/lib`` adds a runtime library search path,
which causes the system's dynamic linker to look for libc++ in ``<install>/lib``
whenever the program is loaded.

GCC does not support the ``-nostdlib++`` flag, so one must use ``-nodefaultlibs``
instead. Since that removes all the standard system libraries and not just libc++,
the system libraries must be re-added manually. For example:

.. code-block:: bash

  $ g++ -nostdinc++ -nodefaultlibs           \
        -isystem <install>/include/c++/v1    \
        -L <install>/lib                     \
        -Wl,-rpath,<install>/lib             \
        -lc++ -lc++abi -lm -lc -lgcc_s -lgcc \
        test.cpp


GDB Pretty printers for libc++
==============================

GDB does not support pretty-printing of libc++ symbols by default. However, libc++ does
provide pretty-printers itself. Those can be used as:

.. code-block:: bash

  $ gdb -ex "source <libcxx>/utils/gdb/libcxx/printers.py" \
        -ex "python register_libcxx_printer_loader()" \
        <args>

.. _include-what-you-use:

include-what-you-use (IWYU)
===========================

libc++ provides an IWYU `mapping file <https://github.com/include-what-you-use/include-what-you-use/blob/master/docs/IWYUMappings.md>`_,
which drastically improves the accuracy of the tool when using libc++. To use the mapping file with
IWYU, you should run the tool like so:

.. code-block:: bash

  $ include-what-you-use -Xiwyu --mapping_file=/path/to/libcxx/include/libcxx.imp file.cpp

If you would prefer to not use that flag, then you can replace ``/path/to/include-what-you-use/share/libcxx.imp``
file with the libc++-provided ``libcxx.imp`` file.

Libc++ Configuration Macros
===========================

Libc++ provides a number of configuration macros which can be used to enable
or disable extended libc++ behavior, including enabling hardening or thread
safety annotations.

**_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS**:
  This macro is used to enable -Wthread-safety annotations on libc++'s
  ``std::mutex`` and ``std::lock_guard``. By default, these annotations are
  disabled and must be manually enabled by the user.

**_LIBCPP_HARDENING_MODE**:
  This macro is used to choose the :ref:`hardening mode <using-hardening-modes>`.

**_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS**:
  This macro is used to disable all visibility annotations inside libc++.
  Defining this macro and then building libc++ with hidden visibility gives a
  build of libc++ which does not export any symbols, which can be useful when
  building statically for inclusion into another library.

**_LIBCPP_NO_VCRUNTIME**:
  Microsoft's C and C++ headers are fairly entangled, and some of their C++
  headers are fairly hard to avoid. In particular, `vcruntime_new.h` gets pulled
  in from a lot of other headers and provides definitions which clash with
  libc++ headers, such as `nothrow_t` (note that `nothrow_t` is a struct, so
  there's no way for libc++ to provide a compatible definition, since you can't
  have multiple definitions).

  By default, libc++ solves this problem by deferring to Microsoft's vcruntime
  headers where needed. However, it may be undesirable to depend on vcruntime
  headers, since they may not always be available in cross-compilation setups,
  or they may clash with other headers. The `_LIBCPP_NO_VCRUNTIME` macro
  prevents libc++ from depending on vcruntime headers. Consequently, it also
  prevents libc++ headers from being interoperable with vcruntime headers (from
  the aforementioned clashes), so users of this macro are promising to not
  attempt to combine libc++ headers with the problematic vcruntime headers. This
  macro also currently prevents certain `operator new`/`operator delete`
  replacement scenarios from working, e.g. replacing `operator new` and
  expecting a non-replaced `operator new[]` to call the replaced `operator new`.

**_LIBCPP_DISABLE_DEPRECATION_WARNINGS**:
  This macro disables warnings when using deprecated components. For example,
  using `std::auto_ptr` when compiling in C++11 mode will normally trigger a
  warning saying that `std::auto_ptr` is deprecated. If the macro is defined,
  no warning will be emitted. By default, this macro is not defined.

C++17 Specific Configuration Macros
-----------------------------------
**_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR**:
  This macro is used to re-enable `auto_ptr`.

**_LIBCPP_ENABLE_CXX17_REMOVED_BINDERS**:
  This macro is used to re-enable the `binder1st`, `binder2nd`,
  `pointer_to_unary_function`, `pointer_to_binary_function`, `mem_fun_t`,
  `mem_fun1_t`, `mem_fun_ref_t`, `mem_fun1_ref_t`, `const_mem_fun_t`,
  `const_mem_fun1_t`, `const_mem_fun_ref_t`, and `const_mem_fun1_ref_t`
  class templates, and the `bind1st`, `bind2nd`, `mem_fun`, `mem_fun_ref`,
  and `ptr_fun` functions.

**_LIBCPP_ENABLE_CXX17_REMOVED_RANDOM_SHUFFLE**:
  This macro is used to re-enable the `random_shuffle` algorithm.

**_LIBCPP_ENABLE_CXX17_REMOVED_UNEXPECTED_FUNCTIONS**:
  This macro is used to re-enable `set_unexpected`, `get_unexpected`, and
  `unexpected`.

C++20 Specific Configuration Macros
-----------------------------------
**_LIBCPP_ENABLE_CXX20_REMOVED_SHARED_PTR_UNIQUE**:
  This macro is used to re-enable the function
  ``std::shared_ptr<...>::unique()``.

**_LIBCPP_ENABLE_CXX20_REMOVED_BINDER_TYPEDEFS**:
  This macro is used to re-enable the `argument_type`, `result_type`,
  `first_argument_type`, and `second_argument_type` members of class
  templates such as `plus`, `logical_not`, `hash`, and `owner_less`.

**_LIBCPP_ENABLE_CXX20_REMOVED_NEGATORS**:
  This macro is used to re-enable `not1`, `not2`, `unary_negate`,
  and `binary_negate`.

**_LIBCPP_ENABLE_CXX20_REMOVED_RAW_STORAGE_ITERATOR**:
  This macro is used to re-enable `raw_storage_iterator`.

**_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS**:
  This macro is used to re-enable `is_literal_type`, `is_literal_type_v`,
  `result_of` and `result_of_t`.


C++26 Specific Configuration Macros
-----------------------------------

**_LIBCPP_ENABLE_CXX26_REMOVED_CODECVT**:
  This macro is used to re-enable all named declarations in ``<codecvt>``.

**_LIBCPP_ENABLE_CXX26_REMOVED_STRING_RESERVE**:
  This macro is used to re-enable the function
  ``std::basic_string<...>::reserve()``.

**_LIBCPP_ENABLE_CXX26_REMOVED_ALLOCATOR_MEMBERS**:
  This macro is used to re-enable redundant member of ``allocator<T>::is_always_equal``.

**_LIBCPP_ENABLE_CXX26_REMOVED_STRSTREAM**:
  This macro is used to re-enable all named declarations in ``<strstream>``.

**_LIBCPP_ENABLE_CXX26_REMOVED_WSTRING_CONVERT**:
  This macro is used to re-enable the ``wstring_convert`` and ``wbuffer_convert``
  in ``<locale>``.

Libc++ Extensions
=================

This section documents various extensions provided by libc++, how they're
provided, and any information regarding how to use them.

Extended integral type support
------------------------------

Several platforms support types that are not specified in the Standard, such as
the 128-bit integral types ``__int128_t`` and ``__uint128_t``. As an extension,
libc++ does a best-effort attempt to support these types like other integral
types, by supporting them notably in:

* ``<bits>``
* ``<charconv>``
* ``<functional>``
* ``<type_traits>``
* ``<format>``
* ``<random>``

Additional types supported in random distributions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The `C++ Standard <http://eel.is/c++draft/rand#req.genl-1.5>`_ mentions that instantiating several random number
distributions with types other than ``short``, ``int``, ``long``, ``long long``, and their unsigned versions is
undefined. As an extension, libc++ supports instantiating ``binomial_distribution``, ``discrete_distribution``,
``geometric_distribution``, ``negative_binomial_distribution``, ``poisson_distribution``, and ``uniform_int_distribution``
with ``int8_t``, ``__int128_t`` and their unsigned versions.

Extensions to ``<format>``
--------------------------

The exposition only type ``basic-format-string`` and its typedefs
``format-string`` and ``wformat-string`` became ``basic_format_string``,
``format_string``, and ``wformat_string`` in C++23. Libc++ makes these types
available in C++20 as an extension.

For padding Unicode strings the ``format`` library relies on the Unicode
Standard. Libc++ retroactively updates the Unicode Standard in older C++
versions. This allows the library to have better estimates for newly introduced
Unicode code points, without requiring the user to use the latest C++ version
in their code base.

In C++26 formatting pointers gained a type ``P`` and allows to use
zero-padding. These options have been retroactively applied to C++20.

Extensions to the C++23 modules ``std`` and ``std.compat``
----------------------------------------------------------

Like other major implementations, libc++ provides C++23 modules ``std`` and
``std.compat`` in C++20 as an extension"

Constant-initialized std::string
--------------------------------

As an implementation-specific optimization, ``std::basic_string`` (``std::string``,
``std::wstring``, etc.) may either store the string data directly in the object, or else store a
pointer to heap-allocated memory, depending on the length of the string.

As of C++20, the constructors are now declared ``constexpr``, which permits strings to be used
during constant-evaluation time. In libc++, as in other common implementations, it is also possible
to constant-initialize a string object (e.g. via declaring a variable with ``constinit`` or
``constexpr``), but, only if the string is short enough to not require a heap allocation. Reliance
upon this should be discouraged in portable code, as the allowed length differs based on the
standard-library implementation and also based on whether the platform uses 32-bit or 64-bit
pointers.

.. code-block:: cpp

  // Non-portable: 11-char string works on 64-bit libc++, but not on 32-bit.
  constinit std::string x = "hello world";

  // Prefer to use string_view, or remove constinit/constexpr from the variable definition:
  constinit std::string_view x = "hello world";
  std::string_view y = "hello world";

.. _turning-off-asan:

Turning off ASan annotation in containers
-----------------------------------------

``__asan_annotate_container_with_allocator`` is a customization point to allow users to disable
`Address Sanitizer annotations for containers <https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow>`_ for specific allocators. This may be necessary for allocators that access allocated memory.
This customization point exists only when ``_LIBCPP_HAS_ASAN_CONTAINER_ANNOTATIONS_FOR_ALL_ALLOCATORS`` Feature Test Macro is defined.

For allocators not running destructors, it is also possible to `bulk-unpoison memory <https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning>`_ instead of disabling annotations altogether.

The struct may be specialized for user-defined allocators. It is a `Cpp17UnaryTypeTrait <http://eel.is/c++draft/type.traits#meta.rqmts>`_ with a base characteristic of ``true_type`` if the container is allowed to use annotations and ``false_type`` otherwise.

The annotations for a ``user_allocator`` can be disabled like this:

.. code-block:: cpp

  #ifdef _LIBCPP_HAS_ASAN_CONTAINER_ANNOTATIONS_FOR_ALL_ALLOCATORS
  template <class T>
  struct std::__asan_annotate_container_with_allocator<user_allocator<T>> : std::false_type {};
  #endif

Why may I want to turn it off?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are a few reasons why you may want to turn off annotations for an allocator.
Unpoisoning may not be an option, if (for example) you are not maintaining the allocator.

* You are using allocator, which does not call destructor during deallocation.
* You are aware that memory allocated with an allocator may be accessed, even when unused by container.

Platform specific behavior
==========================

Windows
-------

The ``stdout``, ``stderr``, and ``stdin`` file streams can be placed in
Unicode mode by a suitable call to ``_setmode()``. When in this mode,
the sequence of bytes read from, or written to, these streams is interpreted
as a sequence of little-endian ``wchar_t`` elements. Thus, use of
``std::cout``, ``std::cerr``, or ``std::cin`` with streams in Unicode mode
will not behave as they usually do since bytes read or written won't be
interpreted as individual ``char`` elements. However, ``std::wcout``,
``std::wcerr``, and ``std::wcin`` will behave as expected.

Wide character stream such as ``std::wcin`` or ``std::wcout`` imbued with a
locale behave differently than they otherwise do. By default, wide character
streams don't convert wide characters but input/output them as is. If a
specific locale is imbued, the IO with the underlying stream happens with
regular ``char`` elements, which are converted to/from wide characters
according to the locale. Note that this doesn't behave as expected if the
stream has been set in Unicode mode.
