==========
Debug Mode
==========

.. contents::
   :local:

.. _using-debug-mode:

Using the debug mode
====================

Libc++ provides a debug mode that enables special debugging checks meant to detect
incorrect usage of the standard library. These checks are disabled by default, but
they can be enabled by vendors when building the library by using ``LIBCXX_ENABLE_DEBUG_MODE``.

Since the debug mode has ABI implications, users should compile their whole program,
including any dependent libraries, against a Standard library configured identically
with respect to the debug mode. In other words, they should not mix code built against
a Standard library with the debug mode enabled with code built against a Standard library
where the debug mode is disabled.

Furthermore, users should not rely on a stable ABI being provided when the debug mode is
enabled -- we reserve the right to change the ABI at any time. If you need a stable ABI
and still want some level of hardening, you should look into enabling :ref:`assertions <assertions-mode>`
instead.

The debug mode provides various checks to aid application debugging.

Comparator consistency checks
-----------------------------
Libc++ provides some checks for the consistency of comparators passed to algorithms. Specifically,
many algorithms such as ``binary_search``, ``merge``, ``next_permutation``, and ``sort``, wrap the
user-provided comparator to assert that `!comp(y, x)` whenever `comp(x, y)`. This can cause the
user-provided comparator to be evaluated up to twice as many times as it would be without the
debug mode, and causes the library to violate some of the Standard's complexity clauses.

Iterator bounds checking
------------------------
The library provides iterators that ensure they are within the bounds of their container when dereferenced.
Arithmetic can be performed on these iterators to create out-of-bounds iterators, but they cannot be dereferenced
when out-of-bounds. The following classes currently provide iterators that have bounds checking:

- ``std::string``
- ``std::vector<T>`` (``T != bool``)
- ``std::span``

.. TODO: Add support for iterator bounds checking in ``std::string_view`` and ``std::array``

Iterator ownership checking
---------------------------
The library provides iterator ownership checking, which allows catching cases where e.g.
an iterator from container ``X`` is used as a position to insert into container ``Y``.
The following classes support iterator ownership checking:

- ``std::string``
- ``std::vector<T>`` (``T != bool``)
- ``std::list``
- ``std::unordered_map``
- ``std::unordered_multimap``
- ``std::unordered_set``
- ``std::unordered_multiset``

Randomizing unspecified behavior
--------------------------------
The library supports the randomization of unspecified behavior. For example, randomizing
the relative order of equal elements in ``std::sort`` or randomizing both parts of the
partition after calling ``std::nth_element``. This effort helps migrating to potential
future faster versions of these algorithms that might not have the exact same behavior.
In particular, it makes it easier to deflake tests that depend on unspecified behavior.
A seed can be used to make such failures reproducible: use ``_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY_SEED=seed``.
