.. _cxx23-status:

================================
libc++ C++23 Status
================================

.. include:: ../Helpers/Styles.rst

.. contents::
   :local:


Overview
================================

In November 2020, the C++ standard committee adopted the first changes to the next version of the C++ standard, known here as "C++2b" (probably to be C++23).
In February 2023, the C++ standard committee approved this draft, and sent it to ISO for approval as C++23.

This page shows the status of libc++; the status of clang's support of the language features is `here <https://clang.llvm.org/cxx_status.html#cxx23>`__.

.. attention:: Features in unreleased drafts of the standard are subject to change.

The groups that have contributed papers:

-  CWG - Core Language Working group
-  LWG - Library working group
-  SG1 - Study group #1 (Concurrency working group)

.. note:: "Nothing to do" means that no library changes were needed to implement this change.

.. _paper-status-cxx23:

Paper Status
====================================

.. csv-table::
   :file: Cxx23Papers.csv
   :header-rows: 1
   :widths: auto

.. note::

   .. [#note-P0533R9] P0533R9: ``isfinite``, ``isinf``, ``isnan`` and ``isnormal`` are implemented.
   .. [#note-P1413R3] P1413R3: ``std::aligned_storage_t`` and ``std::aligned_union_t`` are marked deprecated, but
      clang doesn't issue a diagnostic for deprecated using template declarations.
   .. [#note-P2520R0] P2520R0: Libc++ implemented this paper as a DR in C++20 as well.
   .. [#note-P2602R2] P2602R2: Libc++ implemented this paper as a DR in C++20 as well.
   .. [#note-P2711R1] P2711R1: ``join_with_view`` hasn't been done yet since this type isn't implemented yet.
   .. [#note-P2770R0] P2770R0: ``join_with_view`` hasn't been done yet since this type isn't implemented yet.
   .. [#note-P2693R1] P2693R1: The formatter for ``std::thread::id`` is implemented.
      The formatter for ``stacktrace`` is not implemented, since ``stacktrace`` is
      not implemented yet.

.. _issues-status-cxx23:

Library Working Group Issues Status
====================================

.. csv-table::
   :file: Cxx23Issues.csv
   :header-rows: 1
   :widths: auto

.. note::

   .. [#note-LWG3750] LWG3750 Only ``__cpp_lib_format_ranges`` is fully implemented.
   .. [#note-LWG3798] LWG3798: ``join_with_view``, ``zip_transform_view``, and ``adjacent_transform_view`` haven't been done yet since these types aren't implemented yet.
   .. [#note-LWG3036] LWG3036: This issue was reverted by P2875R4
