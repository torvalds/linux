.. _cxx2b-status:

================================
libc++ C++2b Status
================================

.. include:: ../Helpers/Styles.rst

.. contents::
   :local:


Overview
================================

In November 2020, the C++ standard committee adopted the first changes to the next version of the C++ standard, known here as "C++2b" (probably to be C++23).

This page shows the status of libc++; the status of clang's support of the language features is `here <https://clang.llvm.org/cxx_status.html#cxx23>`__.

.. attention:: Features in unreleased drafts of the standard are subject to change.

The groups that have contributed papers:

-  CWG - Core Language Working group
-  LWG - Library working group
-  SG1 - Study group #1 (Concurrency working group)

.. note:: "Nothing to do" means that no library changes were needed to implement this change.

.. _paper-status-cxx2b:

Paper Status
====================================

.. csv-table::
   :file: Cxx2bPapers.csv
   :header-rows: 1
   :widths: auto

.. note::

   .. [#note-P0533R9] P0533R9: ``isfinite``, ``isinf``, ``isnan`` and ``isnormal`` are implemented.
   .. [#note-P1413R3] P1413R3: ``std::aligned_storage_t`` and ``std::aligned_union_t`` are marked deprecated, but
      clang doesn't issue a diagnostic for deprecated using template declarations.

.. _issues-status-cxx2b:

Library Working Group Issues Status
====================================

.. csv-table::
   :file: Cxx2bIssues.csv
   :header-rows: 1
   :widths: auto
