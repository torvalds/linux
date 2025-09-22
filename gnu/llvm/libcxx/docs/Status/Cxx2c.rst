.. _cxx2c-status:

================================
libc++ C++2c Status
================================

.. include:: ../Helpers/Styles.rst

.. contents::
   :local:


Overview
================================

In June 2023, the C++ standard committee adopted the first changes to the next version of the C++ standard, known here as "C++2c" (probably to be C++26).

This page shows the status of libc++; the status of clang's support of the language features is `here <https://clang.llvm.org/cxx_status.html#cxx2c>`__.

.. attention:: Features in unreleased drafts of the standard are subject to change.

The groups that have contributed papers:

-  CWG - Core Language Working group
-  LWG - Library working group
-  SG1 - Study group #1 (Concurrency working group)

.. note:: "Nothing to do" means that no library changes were needed to implement this change.

.. _paper-status-cxx2c:

Paper Status
====================================

.. csv-table::
   :file: Cxx2cPapers.csv
   :header-rows: 1
   :widths: auto

.. note::

   .. [#note-P2510R3] This paper is applied as DR against C++20. (MSVC STL and libstdc++ will do the same.)
   .. [#note-P3142R0] This paper is applied as DR against C++23. (MSVC STL and libstdc++ will do the same.)
   .. [#note-P2944R3] Implemented comparisons for ``reference_wrapper`` only.
   .. [#note-P2422R1] Libc++ keeps the ``nodiscard`` attributes as a conforming extension.
   .. [#note-P2997R1] This paper is applied as DR against C++20. (MSVC STL and libstdc++ will do the same.)

.. _issues-status-cxx2c:

Library Working Group Issues Status
====================================

.. csv-table::
   :file: Cxx2cIssues.csv
   :header-rows: 1
   :widths: auto
