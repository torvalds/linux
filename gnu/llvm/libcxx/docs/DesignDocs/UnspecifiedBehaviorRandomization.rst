==================================
Unspecified Behavior Randomization
==================================

Background
==========

Consider the follow snippet which steadily happens in tests:


.. code-block:: cpp

    std::vector<std::pair<int, int>> v(SomeData());
    std::sort(v.begin(), v.end(), [](const auto& lhs, const auto& rhs) {
       return lhs.first < rhs.first;
    });

Under this assumption all elements in the vector whose first elements are equal
do not guarantee any order. Unfortunately, this prevents libcxx introducing
other implementations because tests might silently fail and the users might
heavily depend on the stability of implementations.

Goal
===================

Provide functionality for randomizing the unspecified behavior so that the users
can test and migrate their components and libcxx can introduce new sorting
algorithms and optimizations to the containers.

For example, as of LLVM version 13, libcxx sorting algorithm takes
`O(n^2) worst case <https://llvm.org/PR20837>`_ but according
to the standard its worst case should be `O(n log n)`. This effort helps users
to gradually fix their tests while updating to new faster algorithms.

Design
======

* Introduce new macro ``_LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY`` which should
  be a part of the libcxx config.
* This macro randomizes the unspecified behavior of algorithms and containers.
  For example, for sorting algorithm the input range is shuffled and then
  sorted.
* This macro is off by default because users should enable it only for testing
  purposes and/or migrations if they happen to libcxx.
* This feature is only available for C++11 and further because of
  ``std::shuffle`` availability.
* We may use `ASLR <https://en.wikipedia.org/wiki/Address_space_layout_randomization>`_ or
  static ``std::random_device`` for seeding the random number generator. This
  guarantees the same stability guarantee within a run but not through different
  runs, for example, for tests become flaky and eventually be seen as broken.
  For platforms which do not support ASLR, the seed is fixed during build.
* The users can fix the seed of the random number generator by providing
  ``_LIBCPP_RANDOMIZE_UNSPECIFIED_STABILITY_SEED=seed`` definition.

This comes with some side effects if any of the flags is on:

* Computation penalty, we think users are OK with that if they use this feature.
* Non reproducible results if they don't use the fixed seed.


Impact
------------------

Google has measured couple of thousands of tests to be dependent on the
stability of sorting and selection algorithms. As we also plan on updating
(or least, providing under flag more) sorting algorithms, this effort helps
doing it gradually and sustainably. This is also bad for users to depend on the
unspecified behavior in their tests, this effort helps to turn this flag in
debug mode.

Potential breakages
-------------------

None if the flag is off. If the flag is on, it may lead to some non-reproducible
results, for example, for caching.

Currently supported randomization
---------------------------------

* ``std::sort``, there is no guarantee on the order of equal elements
* ``std::partial_sort``, there is no guarantee on the order of equal elements and
   on the order of the remaining part
* ``std::nth_element``, there is no guarantee on the order from both sides of the
   partition

Patches welcome.
