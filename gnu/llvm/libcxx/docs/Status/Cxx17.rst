.. _cxx17-status:

================================
libc++ C++17 Status
================================

.. include:: ../Helpers/Styles.rst

.. contents::
   :local:


Overview
================================

In November 2014, the C++ standard committee created a draft for the next version of the C++ standard, initially known as "C++1z".
In February 2017, the C++ standard committee approved this draft, and sent it to ISO for approval as C++17.

This page shows the status of libc++; the status of clang's support of the language features is `here <https://clang.llvm.org/cxx_status.html#cxx17>`__.

.. attention:: Features in unreleased drafts of the standard are subject to change.

The groups that have contributed papers:

-  CWG - Core Language Working group
-  LWG - Library working group
-  SG1 - Study group #1 (Concurrency working group)

.. note:: "Nothing to do" means that no library changes were needed to implement this change.

.. _paper-status-cxx17:

Paper Status
====================================

.. csv-table::
   :file: Cxx17Papers.csv
   :header-rows: 1
   :widths: auto

.. note::

   .. [#note-P0067] P0067: ``std::(to|from)_chars`` for integrals has been available since version 7.0. ``std::to_chars`` for ``float`` and ``double`` since version 14.0 ``std::to_chars`` for ``long double`` uses the implementation for ``double``.
   .. [#note-P0226] P0226: Progress is tracked `here <https://https://libcxx.llvm.org/Status/SpecialMath.html>`_.
   .. [#note-P0607] P0607: The parts of P0607 that are not done are the ``<regex>`` bits.
   .. [#note-P0154] P0154: The required macros are only implemented as of clang 19.
   .. [#note-P0452] P0452: The changes to ``std::transform_inclusive_scan`` and ``std::transform_exclusive_scan`` have not yet been implemented.

.. _issues-status-cxx17:

Library Working Group Issues Status
====================================

.. csv-table::
   :file: Cxx17Issues.csv
   :header-rows: 1
   :widths: auto
