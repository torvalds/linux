.. _hardening:

===============
Hardening Modes
===============

.. contents::
   :local:

.. _using-hardening-modes:

Using hardening modes
=====================

libc++ provides several hardening modes, where each mode enables a set of
assertions that prevent undefined behavior caused by violating preconditions of
the standard library. Different hardening modes make different trade-offs
between the amount of checking and runtime performance. The available hardening
modes are:

- **Unchecked mode/none**, which disables all hardening checks.
- **Fast mode**, which contains a set of security-critical checks that can be
  done with relatively little overhead in constant time and are intended to be
  used in production. We recommend most projects adopt this.
- **Extensive mode**, which contains all the checks from fast mode and some
  additional checks for undefined behavior that incur relatively little overhead
  but aren't security-critical. Production builds requiring a broader set of
  checks than fast mode should consider enabling extensive mode. The additional
  rigour impacts performance more than fast mode: we recommend benchmarking to
  determine if that is acceptable for your program.
- **Debug mode**, which enables all the available checks in the library,
  including heuristic checks that might have significant performance overhead as
  well as internal library assertions. This mode should be used in
  non-production environments (such as test suites, CI, or local development).
  We don’t commit to a particular level of performance in this mode and it’s
  *not* intended to be used in production.

.. note::

   Enabling hardening has no impact on the ABI.

Notes for users
---------------

As a libc++ user, consult with your vendor to determine the level of hardening
enabled by default.

Users wishing for a different hardening level to their vendor default are able
to control the level by passing **one** of the following options to the compiler:

- ``-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE``
- ``-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST``
- ``-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE``
- ``-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG``

.. warning::

   The exact numeric values of these macros are unspecified and users should not
   rely on them (e.g. expect the values to be sorted in any way).

.. warning::

   If you would prefer to override the hardening level on a per-translation-unit
   basis, you must do so **before** including any headers to avoid `ODR issues`_.

.. _`ODR issues`: https://en.cppreference.com/w/cpp/language/definition#:~:text=is%20ill%2Dformed.-,One%20Definition%20Rule,-Only%20one%20definition

.. note::

   Since the static and shared library components of libc++ are built by the
   vendor, setting this macro will have no impact on the hardening mode for the
   pre-built components. Most libc++ code is header-based, so a user-provided
   value for ``_LIBCPP_HARDENING_MODE`` will be mostly respected.

Notes for vendors
-----------------

Vendors can set the default hardening mode by providing
``LIBCXX_HARDENING_MODE`` as a configuration option, with the possible values of
``none``, ``fast``, ``extensive`` and ``debug``. The default value is ``none``
which doesn't enable any hardening checks (this mode is sometimes called the
``unchecked`` mode).

This option controls both the hardening mode that the precompiled library is
built with and the default hardening mode that users will build with. If set to
``none``, the precompiled library will not contain any assertions, and user code
will default to building without assertions.

Vendors can also override the way the program is terminated when an assertion
fails by :ref:`providing a custom header <override-assertion-handler>`.

Assertion categories
====================

Inside the library, individual assertions are grouped into different
*categories*. Each hardening mode enables a different set of assertion
categories; categories provide an additional layer of abstraction that makes it
easier to reason about the high-level semantics of a hardening mode.

.. note::

  Users are not intended to interact with these categories directly -- the
  categories are considered internal to the library and subject to change.

- ``valid-element-access`` -- checks that any attempts to access a container
  element, whether through the container object or through an iterator, are
  valid and do not attempt to go out of bounds or otherwise access
  a non-existent element. This also includes operations that set up an imminent
  invalid access (e.g. incrementing an end iterator). For iterator checks to
  work, bounded iterators must be enabled in the ABI. Types like
  ``std::optional`` and ``std::function`` are considered containers (with at
  most one element) for the purposes of this check.

- ``valid-input-range`` -- checks that ranges (whether expressed as an iterator
  pair, an iterator and a sentinel, an iterator and a count, or
  a ``std::range``) given as input to library functions are valid:
  - the sentinel is reachable from the begin iterator;
  - TODO(hardening): both iterators refer to the same container.

  ("input" here refers to "an input given to an algorithm", not to an iterator
  category)

  Violating assertions in this category leads to an out-of-bounds access.

- ``non-null`` -- checks that the pointer being dereferenced is not null. On
  most modern platforms, the zero address does not refer to an actual location
  in memory, so a null pointer dereference would not compromise the memory
  security of a program (however, it is still undefined behavior that can result
  in strange errors due to compiler optimizations).

- ``non-overlapping-ranges`` -- for functions that take several ranges as
  arguments, checks that those ranges do not overlap.

- ``valid-deallocation`` -- checks that an attempt to deallocate memory is valid
  (e.g. the given object was allocated by the given allocator). Violating this
  category typically results in a memory leak.

- ``valid-external-api-call`` -- checks that a call to an external API doesn't
  fail in an unexpected manner. This includes triggering documented cases of
  undefined behavior in an external library (like attempting to unlock an
  unlocked mutex in pthreads). Any API external to the library falls under this
  category (from system calls to compiler intrinsics). We generally don't expect
  these failures to compromise memory safety or otherwise create an immediate
  security issue.

- ``compatible-allocator`` -- checks any operations that exchange nodes between
  containers to make sure the containers have compatible allocators.

- ``argument-within-domain`` -- checks that the given argument is within the
  domain of valid arguments for the function. Violating this typically produces
  an incorrect result (e.g. ``std::clamp`` returns the original value without
  clamping it due to incorrect functors) or puts an object into an invalid state
  (e.g. a string view where only a subset of elements is accessible). This
  category is for assertions violating which doesn't cause any immediate issues
  in the library -- whatever the consequences are, they will happen in the user
  code.

- ``pedantic`` -- checks preconditions that are imposed by the Standard, but
  violating which happens to be benign in libc++.

- ``semantic-requirement`` -- checks that the given argument satisfies the
  semantic requirements imposed by the Standard. Typically, there is no simple
  way to completely prove that a semantic requirement is satisfied; thus, this
  would often be a heuristic check and it might be quite expensive.

- ``internal`` -- checks that internal invariants of the library hold. These
  assertions don't depend on user input.

- ``uncategorized`` -- for assertions that haven't been properly classified yet.
  This category is an escape hatch used for some existing assertions in the
  library; all new code should have its assertions properly classified.

Mapping between the hardening modes and the assertion categories
================================================================

.. list-table::
    :header-rows: 1
    :widths: auto

    * - Category name
      - ``fast``
      - ``extensive``
      - ``debug``
    * - ``valid-element-access``
      - ✅
      - ✅
      - ✅
    * - ``valid-input-range``
      - ✅
      - ✅
      - ✅
    * - ``non-null``
      - ❌
      - ✅
      - ✅
    * - ``non-overlapping-ranges``
      - ❌
      - ✅
      - ✅
    * - ``valid-deallocation``
      - ❌
      - ✅
      - ✅
    * - ``valid-external-api-call``
      - ❌
      - ✅
      - ✅
    * - ``compatible-allocator``
      - ❌
      - ✅
      - ✅
    * - ``argument-within-domain``
      - ❌
      - ✅
      - ✅
    * - ``pedantic``
      - ❌
      - ✅
      - ✅
    * - ``semantic-requirement``
      - ❌
      - ❌
      - ✅
    * - ``internal``
      - ❌
      - ❌
      - ✅
    * - ``uncategorized``
      - ❌
      - ✅
      - ✅

.. note::

  At the moment, each subsequent hardening mode is a strict superset of the
  previous one (in other words, each subsequent mode only enables additional
  assertion categories without disabling any), but this won't necessarily be
  true for any hardening modes that might be added in the future.

.. note::

  The categories enabled by each mode are subject to change and users should not
  rely on the precise assertions enabled by a mode at a given point in time.
  However, the library does guarantee to keep the hardening modes stable and
  to fulfill the semantics documented here.

Hardening assertion failure
===========================

In production modes (``fast`` and ``extensive``), a hardening assertion failure
immediately ``_traps <https://llvm.org/docs/LangRef.html#llvm-trap-intrinsic>``
the program. This is the safest approach that also minimizes the code size
penalty as the failure handler maps to a single instruction. The downside is
that the failure provides no additional details other than the stack trace
(which might also be affected by optimizations).

TODO(hardening): describe ``__builtin_verbose_trap`` once we can use it.

In the ``debug`` mode, an assertion failure terminates the program in an
unspecified manner and also outputs the associated error message to the error
output. This is less secure and increases the size of the binary (among other
things, it has to store the error message strings) but makes the failure easier
to debug. It also allows testing the error messages in our test suite.

.. _override-assertion-handler:

Overriding the assertion failure handler
----------------------------------------

Vendors can override the default assertion handler mechanism by following these
steps:

- create a header file that provides a definition of a macro called
  ``_LIBCPP_ASSERTION_HANDLER``. The macro will be invoked when a hardening
  assertion fails, with a single parameter containing a null-terminated string
  with the error message.
- when configuring the library, provide the path to custom header (relative to
  the root of the repository) via the CMake variable
  ``LIBCXX_ASSERTION_HANDLER_FILE``.

Note that almost all libc++ headers include the assertion handler header which
means it should not include anything non-trivial from the standard library to
avoid creating circular dependencies.

There is no existing mechanism for users to override the assertion handler
because the ability to do the override other than at configure-time carries an
unavoidable code size penalty that would otherwise be imposed on all users,
whether they require such customization or not. Instead, we let vendors decide
what's right on their platform for their users -- a vendor who wishes to provide
this capability is free to do so, e.g. by declaring the assertion handler as an
overridable function.

ABI
===

Setting a hardening mode does **not** affect the ABI. Each mode uses the subset
of checks available in the current ABI configuration which is determined by the
platform.

It is important to stress that whether a particular check is enabled depends on
the combination of the selected hardening mode and the hardening-related ABI
options. Some checks require changing the ABI from the "default" to store
additional information in the library classes -- e.g. checking whether an
iterator is valid upon dereference generally requires storing data about bounds
inside the iterator object. Using ``std::span`` as an example, setting the
hardening mode to ``fast`` will always enable the ``valid-element-access``
checks when accessing elements via a ``std::span`` object, but whether
dereferencing a ``std::span`` iterator does the equivalent check depends on the
ABI configuration.

ABI options
-----------

Vendors can use the following ABI options to enable additional hardening checks:

- ``_LIBCPP_ABI_BOUNDED_ITERATORS`` -- changes the iterator type of select
  containers (see below) to a bounded iterator that keeps track of whether it's
  within the bounds of the original container and asserts valid bounds on every
  dereference.

  ABI impact: changes the iterator type of the relevant containers.

  Supported containers:

  - ``span``;
  - ``string_view``.

- ``_LIBCPP_ABI_BOUNDED_ITERATORS_IN_STRING`` -- changes the iterator type of
  ``basic_string`` to a bounded iterator that keeps track of whether it's within
  the bounds of the original container and asserts it on every dereference and
  when performing iterator arithmetics.

  ABI impact: changes the iterator type of ``basic_string`` and its
  specializations, such as ``string`` and ``wstring``.

- ``_LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR`` -- changes the iterator type of
  ``vector`` to a bounded iterator that keeps track of whether it's within the
  bounds of the original container and asserts it on every dereference and when
  performing iterator arithmetics. Note: this doesn't yet affect
  ``vector<bool>``.

  ABI impact: changes the iterator type of ``vector`` (except ``vector<bool>``).

ABI tags
--------

We use ABI tags to allow translation units built with different hardening modes
to interact with each other without causing ODR violations. Knowing how
hardening modes are encoded into the ABI tags might be useful to examine
a binary and determine whether it was built with hardening enabled.

.. warning::
  We don't commit to the encoding scheme used by the ABI tags being stable
  between different releases of libc++. The tags themselves are never stable, by
  design -- new releases increase the version number. The following describes
  the state of the latest release and is for informational purposes only.

The first character of an ABI tag encodes the hardening mode:

- ``f`` -- [f]ast mode;
- ``s`` -- extensive ("[s]afe") mode;
- ``d`` -- [d]ebug mode;
- ``n`` -- [n]one mode.

Hardened containers status
==========================

.. list-table::
    :header-rows: 1
    :widths: auto

    * - Name
      - Member functions
      - Iterators (ABI-dependent)
    * - ``span``
      - ✅
      - ✅
    * - ``string_view``
      - ✅
      - ✅
    * - ``array``
      - ✅
      - ❌
    * - ``vector``
      - ✅
      - ✅ (see note)
    * - ``string``
      - ✅
      - ✅ (see note)
    * - ``list``
      - ✅
      - ❌
    * - ``forward_list``
      - ❌
      - ❌
    * - ``deque``
      - ✅
      - ❌
    * - ``map``
      - ❌
      - ❌
    * - ``set``
      - ❌
      - ❌
    * - ``multimap``
      - ❌
      - ❌
    * - ``multiset``
      - ❌
      - ❌
    * - ``unordered_map``
      - Partial
      - Partial
    * - ``unordered_set``
      - Partial
      - Partial
    * - ``unordered_multimap``
      - Partial
      - Partial
    * - ``unordered_multiset``
      - Partial
      - Partial
    * - ``mdspan``
      - ✅
      - ❌
    * - ``optional``
      - ✅
      - N/A
    * - ``function``
      - ❌
      - N/A
    * - ``variant``
      - N/A
      - N/A
    * - ``any``
      - N/A
      - N/A
    * - ``expected``
      - ✅
      - N/A
    * - ``valarray``
      - Partial
      - N/A
    * - ``bitset``
      - ❌
      - N/A

Note: for ``vector`` and ``string``, the iterator does not check for
invalidation (accesses made via an invalidated iterator still lead to undefined
behavior)

Note: ``vector<bool>`` iterator is not currently hardened.

Testing
=======

Please see :ref:`Testing documentation <testing-hardening-assertions>`.

Further reading
===============

- `Hardening RFC <https://discourse.llvm.org/t/rfc-hardening-in-libc/73925>`_:
  contains some of the design rationale.
