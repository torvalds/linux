===================
Feature Test Macros
===================

.. contents::
   :local:

Overview
========

Libc++ implements the C++ feature test macros as specified in the C++20 standard,
and before that in non-normative guiding documents
(`See cppreference <https://en.cppreference.com/w/User:D41D8CD98F/feature_testing_macros>`_)


Design
======

Feature test macros are tricky to track, implement, test, and document correctly.
They must be available from a list of headers, they may have different values in
different dialects, and they may or may not be implemented by libc++. In order to
track all of these conditions correctly and easily, we want a Single Source of
Truth (SSoT) that defines each feature test macro, its values, the headers it
lives in, and whether or not is is implemented by libc++. From this SSoA we
have enough information to automatically generate the `<version>` header,
the tests, and the documentation.

Therefore we maintain a SSoA in `libcxx/utils/generate_feature_test_macro_components.py`
which doubles as a script to generate the following components:

* The `<version>` header.
* The version tests under `support.limits.general`.
* Documentation of libc++'s implementation of each macro.

Usage
=====

The `generate_feature_test_macro_components.py` script is used to track and
update feature test macros in libc++.

Whenever a feature test macro is added or changed, the table should be updated
and the script should be re-ran. The script will clobber the existing test files,
the documentation and the `<version>` header.
