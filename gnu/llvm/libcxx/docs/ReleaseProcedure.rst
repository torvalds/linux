.. _ReleaseProcedure:

=================
Release procedure
=================

The LLVM project creates a new release twice a year following a fixed
`schedule <https://llvm.org/docs/HowToReleaseLLVM.html#annual-release-schedule>`__.
This page describes the libc++ procedure for that release.

Prepare the release
===================

This is done by the libc++ developers.

It should be finished before the Release managers start branching the new
release:

* Make sure ``libcxx/docs/ReleaseNotes/<VERSION>.rst`` is up to date. Typically
  this file is updated when contributing patches. Still there might be some
  information added regarding the general improvements of larger projects.

* Make sure the deprecated features on this page are up to date. Typically a
  new deprecated feature should be added to the release notes and this page.
  However this should be verified so removals won't get forgotten.

* Make sure the latest Unicode version is used. The C++ Standard
  `refers to the Unicode Standard <https://wg21.link/intro.refs#1.10>`__

  ``The Unicode Consortium. The Unicode Standard. Available from: https://www.unicode.org/versions/latest/``

  Typically the Unicode Consortium has one release per year. The libc++
  format library uses the Unicode Standard. Libc++ should be updated to the
  latest Unicode version. Updating means using the latest data files and, if
  needed, adapting the code to changes in the Unicode Standard.

* Make sure all libc++ supported compilers in the CI are updated to their
  latest release.

Branching
=========

This is done by the LLVM Release managers.

After branching for an LLVM release:

1. Update ``_LIBCPP_VERSION`` in ``libcxx/include/__config``
2. Update the version number in ``libcxx/docs/conf.py``
3. Update ``_LIBCPPABI_VERSION`` in ``libcxxabi/include/cxxabi.h``
4. Update ``_LIBUNWIND_VERSION`` in ``libunwind/include/__libunwind_config.h``
5. Create a release notes file for the next release from the template
6. Point to the new release notes file from ``libcxx/docs/ReleaseNotes.rst``

Post branching
==============

This is done by the libc++ developers.

After branching it takes a couple of days before the new LLVM ToT version is
available on `<https://apt.llvm.org>`_. Once it is available the pre-commit CI
can start using the new ToT version. In order to make sure patches can be
backported to the release branch the oldest compiler is not removed yet.

The section ``Upcoming Deprecations and Removals`` is cleared by the release
managers. Copy back the items that were in this section.

The items that need changing are marked with ``LLVM POST-BRANCH``.

Post release
============

This is done by the libc++ developers.

Support for the ToT - 3 version is removed:

- Search for ``LLVM RELEASE`` and address their comments
- Search for test that have ``UNSUPPORTED`` or ``XFAIL`` for the no longer supported version
- Search for ``TODO(LLVM-<ToT>)`` and address their comments
