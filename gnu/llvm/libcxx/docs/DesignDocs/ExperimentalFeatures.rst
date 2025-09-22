=====================
Experimental Features
=====================

.. contents::
   :local:

.. _experimental features:

Overview
========

Libc++ implements technical specifications (TSes) and ships them as experimental
features that users are free to try out. The goal is to allow getting feedback
on those experimental features.

However, libc++ does not provide the same guarantees about those features as
it does for the rest of the library. In particular, no ABI or API stability
is guaranteed, and experimental features are deprecated once the non-experimental
equivalent has shipped in the library. This document outlines the details of
that process.

Background
==========

The "end game" of a Technical Specification (TS) is to have the features in
there added to a future version of the C++ Standard. When this happens, the TS
can be retired. Sometimes, only part of at TS is added to the standard, and
the rest of the features may be incorporated into the next version of the TS.

Adoption leaves library implementors with two implementations of a feature,
one in namespace ``std``, and the other in namespace ``std::experimental``.
The first one will continue to evolve (via issues and papers), while the other
will not. Gradually they will diverge. It's not good for users to have two
(subtly) different implementations of the same functionality in the same library.

Design
======

When a feature is adopted into the main standard, we implement it in namespace
``std``. Once that implementation is complete, we then create a deprecation
warning for the corresponding experimental feature warning users to move off
of it and to the now-standardized feature.

These deprecation warnings are guarded by a macro of the form
``_LIBCPP_NO_EXPERIMENTAL_DEPRECATION_WARNING_<FEATURE>``, which
can be defined by users to disable the deprecation warning. Whenever
possible, deprecation warnings are put on a per-declaration basis
using the ``[[deprecated]]`` attribute, which also allows disabling
the warnings using ``-Wno-deprecated-declarations``.

After **2 releases** of LLVM, the experimental feature is removed completely
(and the deprecation notice too). Using the experimental feature simply becomes
an error. Furthermore, when an experimental header becomes empty due to the
removal of the corresponding experimental feature, the header is removed.
Keeping the header around creates incorrect assumptions from users and breaks
``__has_include``.


Status of TSes
==============

Library Fundamentals TS `V1 <https://wg21.link/N4480>`__ and `V2 <https://wg21.link/N4617>`__
---------------------------------------------------------------------------------------------

Most (but not all) of the features of the LFTS were accepted into C++17.

+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| Section | Feature                                               | Shipped in ``std`` | To be removed from ``std::experimental`` | Notes                   |
+=========+=======================================================+====================+==========================================+=========================+
| 2.1     | ``uses_allocator construction``                       | 5.0                | 7.0                                      |                         |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.1.2   | ``erased_type``                                       |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.2.1   | ``tuple_size_v``                                      | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.2.2   | ``apply``                                             | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.3.1   | All of the ``_v`` traits in ``<type_traits>``         | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.3.2   | ``invocation_type`` and ``raw_invocation_type``       |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.3.3   | Logical operator traits                               | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.3.3   | Detection Idiom                                       | 5.0                |                                          | Only partially in C++17 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.4.1   | All of the ``_v`` traits in ``<ratio>``               | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.5.1   | All of the ``_v`` traits in ``<chrono>``              | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.6.1   | All of the ``_v`` traits in ``<system_error>``        | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 3.7     | ``propagate_const``                                   |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 4.2     | Enhancements to ``function``                          | Not yet            |                                          |                         |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 4.3     | searchers                                             | 7.0                | 9.0                                      |                         |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 5       | optional                                              | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 6       | ``any``                                               | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 7       | ``string_view``                                       | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.2.1   | ``shared_ptr`` enhancements                           | Not yet            | Never added                              |                         |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.2.2   | ``weak_ptr`` enhancements                             | Not yet            | Never added                              |                         |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.5     | ``memory_resource``                                   | 16.0               | 18.0                                     | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.6     | ``polymorphic_allocator``                             | 16.0               | 18.0                                     | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.7     | ``resource_adaptor``                                  |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.8     | Access to program-wide ``memory_resource`` objects    | 16.0               | 18.0                                     | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.9     | Pool resource classes                                 | 16.0               | 18.0                                     | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.10    | ``monotonic_buffer_resource``                         | 16.0               | 18.0                                     | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.11    | Alias templates using polymorphic memory resources    | 16.0               | 18.0                                     | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 8.12    | Non-owning pointers                                   |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 11.2    | ``promise``                                           |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 11.3    | ``packaged_task``                                     |                    | n/a                                      | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 12.2    | ``search``                                            | 7.0                | 9.0                                      |                         |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 12.3    | ``sample``                                            | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 12.4    | ``shuffle``                                           |                    |                                          | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 13.1    | ``gcd`` and ``lcm``                                   | 5.0                | 7.0                                      | Removed                 |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 13.2    | Random number generation                              |                    |                                          | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
| 14      | Reflection Library                                    |                    |                                          | Not part of C++17       |
+---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+


`FileSystem TS <https://wg21.link/N4100>`__
-------------------------------------------
The FileSystem TS was accepted (in totality) for C++17.
The FileSystem TS implementation was shipped in namespace ``std`` in LLVM 7.0, and was
removed in LLVM 11.0 (due to the lack of deprecation warnings before LLVM 9.0).

Parallelism TS `V1 <https://wg21.link/N4507>`__ and `V2 <https://wg21.link/N4706>`__
------------------------------------------------------------------------------------
Some (most) of the Parallelism TS was accepted for C++17.
We have not yet shipped an implementation of the Parallelism TS.

`Coroutines TS <https://wg21.link/N4680>`__
-------------------------------------------
The Coroutines TS was accepted for C++20.
An implementation of the Coroutines TS was shipped in LLVM 5.0 in namespace ``std::experimental``,
and C++20 Coroutines shipped in LLVM 14.0. The implementation of the Coroutines TS in ``std::experimental``
has been removed in LLVM 17.0.

`Networking TS <https://wg21.link/N4656>`__
-------------------------------------------
The Networking TS is not yet part of a shipping standard.
We have not yet shipped an implementation of the Networking TS.

`Ranges TS <https://wg21.link/N4685>`__
---------------------------------------
The Ranges TS was accepted for C++20.
We will not ship an implementation of the Ranges TS, however we are actively working on
the implementation of C++20 Ranges.

`Concepts TS <https://wg21.link/N4641>`__
-----------------------------------------
The Concepts TS was accepted for C++20.
We will not ship an implementation of the Concepts TS, however we are shipping an
implementation of C++20 Concepts.

`Concurrency TS <https://wg21.link/P0159>`__
--------------------------------------------
The Concurrency TS was adopted in Kona (2015).
None of the Concurrency TS was accepted for C++17.
We have not yet shipped an implementation of the Concurrency TS.

.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | Section | Feature                                               | Shipped in ``std`` | To be removed from ``std::experimental`` | Notes                   |
.. +=========+=======================================================+====================+==========================================+=========================+
.. | 2.3     | class template ``future``                             |                    |                                          |                         |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.4     | class template ``shared_future``                      |                    |                                          |                         |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.5     | class template ``promise``                            |                    |                                          | Only using ``future``   |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.6     | class template ``packaged_task``                      |                    |                                          | Only using ``future``   |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.7     | function template ``when_all``                        |                    |                                          | Not part of C++17       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.8     | class template ``when_any_result``                    |                    |                                          | Not part of C++17       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.9     | function template ``when_any``                        |                    |                                          | Not part of C++17       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.10    | function template ``make_ready_future``               |                    |                                          | Not part of C++17       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 2.11    | function template ``make_exeptional_future``          |                    |                                          | Not part of C++17       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 3       | ``latches`` and ``barriers``                          |                    |                                          | Not part of C++17       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
.. | 4       | Atomic Smart Pointers                                 |                    |                                          | Adopted for C++20       |
.. +---------+-------------------------------------------------------+--------------------+------------------------------------------+-------------------------+
