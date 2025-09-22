.. _ContributingToLibcxx:

======================
Contributing to libc++
======================

This file contains notes about various tasks and processes specific to contributing
to libc++. If this is your first time contributing, please also read `this document
<https://www.llvm.org/docs/Contributing.html>`__ on general rules for contributing to LLVM.

If you plan on contributing to libc++, it can be useful to join the ``#libcxx`` channel
on `LLVM's Discord server <https://discord.gg/jzUbyP26tQ>`__.

Looking for pre-existing pull requests
======================================

Before you start working on any feature, please take a look at the open libc++ pull
requests to avoid duplicating someone else's work. You can do that on GitHub by
filtering pull requests `tagged with libc++ <https://github.com/llvm/llvm-project/pulls?q=is%3Apr+is%3Aopen+label%3Alibc%2B%2B>`__.
If you see that your feature is already being worked on, please consider chiming in
and helping review the code instead of duplicating work!

RFCs for significant user-affecting changes
===========================================

Before you start working on a change that can have significant impact on users of the library,
please consider creating a RFC on `libc++'s Discourse forum <https://discourse.llvm.org/c/runtimes/libcxx>`__.
This will ensure that you work in a direction that the project endorses and will ease reviewing your
contribution as directional questions can be raised early. Including a WIP patch is not mandatory, but
it can be useful to ground the discussion in something concrete.

Coding standards
================

In general, libc++ follows the
`LLVM Coding Standards <https://llvm.org/docs/CodingStandards.html>`_.
There are some deviations from these standards.

Libc++ uses ``__ugly_names``. These names are reserved for implementations, so
users may not use them in their own applications. When using a name like ``T``,
a user may have defined a macro that changes the meaning of ``T``. By using
``__ugly_names`` we avoid that problem. Other standard libraries and compilers
use these names too. To avoid common clashes with other uglified names used in
other implementations (e.g. system headers), the test in
``libcxx/test/libcxx/system_reserved_names.gen.py`` contains the list of
reserved names that can't be used.

Unqualified function calls are susceptible to
`argument-dependent lookup (ADL) <https://en.cppreference.com/w/cpp/language/adl>`_.
This means calling ``move(UserType)`` might not call ``std::move``. Therefore,
function calls must use qualified names to avoid ADL. Some functions in the
standard library `require ADL usage <http://eel.is/c++draft/contents#3>`_.
Names of classes, variables, concepts, and type aliases are not subject to ADL.
They don't need to be qualified.

Function overloading also applies to operators. Using ``&user_object`` may call
a user-defined ``operator&``. Use ``std::addressof`` instead. Similarly, to
avoid invoking a user-defined ``operator,``, make sure to cast the result to
``void`` when using the ``,``. For example:

.. code-block:: cpp

    for (; __first1 != __last1; ++__first1, (void)++__first2) {
      ...
    }

In general, try to follow the style of existing code. There are a few
exceptions:

- Prefer ``using foo = int`` over ``typedef int foo``. The compilers supported
  by libc++ accept alias declarations in all standard modes.

Other tips are:

- Keep the number of formatting changes in patches minimal.
- Provide separate patches for style fixes and for bug fixes or features. Keep in
  mind that large formatting patches may cause merge conflicts with other patches
  under review. In general, we prefer to avoid large reformatting patches.
- Keep patches self-contained. Large and/or complicated patches are harder to
  review and take a significant amount of time. It's fine to have multiple
  patches to implement one feature if the feature can be split into
  self-contained sub-tasks.


Resources
=========

Libc++ specific
---------------

- ``libcxx/include/__config`` -- this file contains the commonly used
  macros in libc++. Libc++ supports all C++ language versions. Newer versions
  of the Standard add new features. For example, making functions ``constexpr``
  in C++20 is done by using ``_LIBCPP_CONSTEXPR_SINCE_CXX20``. This means the
  function is ``constexpr`` in C++20 and later. The Standard does not allow
  making this available in C++17 or earlier, so we use a macro to implement
  this requirement.
- ``libcxx/test/support/test_macros.h`` -- similar to the above, but for the
  test suite.


ISO C++ Standard
----------------

Libc++ implements the library part of the ISO C++ standard. The official
publication must be bought from ISO or your national body. This is not
needed to work on libc++, there are other free resources available.

- The `LaTeX sources <https://github.com/cplusplus/draft>`_  used to
  create the official C++ standard. This can be used to create your own
  unofficial build of the standard.

- An `HTML rendered version of the draft <https://eel.is/c++draft/>`_  is
  available. This is the most commonly used place to look for the
  wording of the standard.

- An `alternative <https://github.com/timsong-cpp/cppwp>`_ is available.
  This link has both recent and historic versions of the standard.

- When implementing features, there are
  `general requirements <https://eel.is/c++draft/#library>`_.
  Most papers use this
  `jargon <http://eel.is/c++draft/structure#specifications>`_
  to describe how library functions work.

- The `WG21 redirect service <https://wg21.link/>`_ is a tool to quickly locate
  papers, issues, and wording in the standard.

- The `paper trail <https://github.com/cplusplus/papers/issues>`_ of
  papers is publicly available, including the polls taken. It
  contains links to the minutes of paper's discussion. Per ISO rules,
  these minutes are only accessible by members of the C++ committee.

- `Feature-Test Macros and Policies
  <https://isocpp.org/std/standing-documents/sd-6-sg10-feature-test-recommendations>`_
  contains information about feature-test macros in C++.
  It contains a list with all feature-test macros, their versions, and the paper
  that introduced them.

- `cppreference <https://en.cppreference.com/w/>`_ is a good resource
  for the usage of C++ library and language features. It's easier to
  read than the C++ Standard, but it lacks details needed to properly implement
  library features.


Pre-commit check list
=====================

Before committing or creating a review, please go through this check-list to make
sure you don't forget anything:

- Do you have :ref:`tests <testing>` for every public class and/or function you're adding or modifying?
- Did you update the synopsis of the relevant headers?
- Did you update the relevant files to track implementation status (in ``docs/Status/``)?
- Did you mark all functions and type declarations with the :ref:`proper visibility macro <visibility-macros>`?
- Did you add all new named declarations to the ``std`` module?
- If you added a header:

  - Did you add it to ``include/module.modulemap``?
  - Did you add it to ``include/CMakeLists.txt``?
  - If it's a public header, did you update ``utils/libcxx/header_information.py``?

- Did you add the relevant feature test macro(s) for your feature? Did you update the ``generate_feature_test_macro_components.py`` script with it?
- Did you run the ``libcxx-generate-files`` target and verify its output?
- If needed, did you add `_LIBCPP_PUSH_MACROS` and `_LIBCPP_POP_MACROS` to the relevant headers?

The review process
==================

After uploading your patch, you should see that the "libc++" review group is automatically
added as a reviewer for your patch. Once the group is marked as having approved your patch,
you can commit it. However, if you get an approval very quickly for a significant patch,
please try to wait a couple of business days before committing to give the opportunity for
other reviewers to chime in. If you need someone else to commit the patch for you, please
mention it and provide your ``Name <email@domain>`` for us to attribute the commit properly.

Note that the rule for accepting as the "libc++" review group is to wait for two members
of the group to have approved the patch, excluding the patch author. This is not a hard
rule -- for very simple patches, use your judgement. The `"libc++" review group <https://reviews.llvm.org/project/members/64/>`__
consists of frequent libc++ contributors with a good understanding of the project's
guidelines -- if you would like to be added to it, please reach out on Discord.

Exporting new symbols from the library
======================================

When exporting new symbols from libc++, you must update the ABI lists located in ``lib/abi``.
To test whether the lists are up-to-date, please run the target ``check-cxx-abilist``.
To regenerate the lists, use the target ``generate-cxx-abilist``.
The ABI lists must be updated for all supported platforms; currently Linux and
Apple.  If you don't have access to one of these platforms, you can download an
updated list from the failed build at
`Buildkite <https://buildkite.com/llvm-project/libcxx-ci>`__.
Look for the failed build and select the ``artifacts`` tab. There, download the
abilist for the platform, e.g.:

* C++<version>.
* MacOS X86_64 and MacOS arm64 for the Apple platform.


Pre-commit CI
=============

Introduction
------------

Unlike most parts of the LLVM project, libc++ uses a pre-commit CI [#]_. This
CI is hosted on `Buildkite <https://buildkite.com/llvm-project/libcxx-ci>`__ and
the build results are visible in the review on GitHub. Please make sure
the CI is green before committing a patch.

The CI tests libc++ for all :ref:`supported platforms <SupportedPlatforms>`.
The build is started for every commit added to a Pull Request. A complete CI
run takes approximately one hour. To reduce the load:

* The build is cancelled when a new commit is pushed to a PR that is already running CI.
* The build is done in several stages and cancelled when a stage fails.

Typically, the libc++ jobs use a Ubuntu Docker image. This image contains
recent `nightly builds <https://apt.llvm.org>`__ of all supported versions of
Clang and the current version of the ``main`` branch. These versions of Clang
are used to build libc++ and execute its tests.

Unless specified otherwise, the configurations:

* use a nightly build of the ``main`` branch of Clang,
* execute the tests using the language C++<latest>. This is the version
  "developed" by the C++ committee.

.. note:: Updating the Clang nightly builds in the Docker image is a manual
   process and is done at an irregular interval on purpose. When you need to
   have the latest nightly build to test recent Clang changes, ask in the
   ``#libcxx`` channel on `LLVM's Discord server
   <https://discord.gg/jzUbyP26tQ>`__.

.. [#] There's `LLVM Dev Meeting talk <https://www.youtube.com/watch?v=B7gB6van7Bw>`__
   explaining the benefits of libc++'s pre-commit CI.

Builds
------

Below is a short description of the most interesting CI builds [#]_:

* ``Format`` runs ``clang-format`` and uploads its output as an artifact. At the
  moment this build is a soft error and doesn't fail the build.
* ``Generated output`` runs the ``libcxx-generate-files`` build target and
  tests for non-ASCII characters in libcxx. Some files are excluded since they
  use Unicode, mainly tests. The output of these commands are uploaded as
  artifact.
* ``Documentation`` builds the documentation. (This is done early in the build
  process since it is cheap to run.)
* ``C++<version>`` these build steps test the various C++ versions, making sure all
  C++ language versions work with the changes made.
* ``Clang <version>`` these build steps test whether the changes work with all
  supported Clang versions.
* ``Booststrapping build`` builds Clang using the revision of the patch and
  uses that Clang version to build and test libc++. This validates the current
  Clang and lib++ are compatible.

  When a crash occurs in this build, the crash reproducer is available as an
  artifact.

* ``Modular build`` tests libc++ using Clang modules [#]_.
* ``GCC <version>`` tests libc++ with the latest stable GCC version. Only C++11
  and the latest C++ version are tested.
* ``Santitizers`` tests libc++ using the Clang sanitizers.
* ``Parts disabled`` tests libc++ with certain libc++ features disabled.
* ``Windows`` tests libc++ using MinGW and clang-cl.
* ``Apple`` tests libc++ on MacOS.
* ``ARM`` tests libc++ on various Linux ARM platforms.
* ``AIX`` tests libc++ on AIX.

.. [#] Not all steps are listed: steps are added and removed when the need arises.
.. [#] Clang modules are not the same as C++20's modules.

Infrastructure
--------------

All files of the CI infrastructure are in the directory ``libcxx/utils/ci``.
Note that quite a bit of this infrastructure is heavily Linux focused. This is
the platform used by most of libc++'s Buildkite runners and developers.

Dockerfile
~~~~~~~~~~

Contains the Docker image for the Ubuntu CI. Because the same Docker image is
used for the ``main`` and ``release`` branch, it should contain no hard-coded
versions.  It contains the used versions of Clang, various clang-tools,
GCC, and CMake.

.. note:: This image is pulled from Docker hub and not rebuild when changing
   the Dockerfile.

run-buildbot-container
~~~~~~~~~~~~~~~~~~~~~~

Helper script that pulls and runs the Docker image. This image mounts the LLVM
monorepo at ``/llvm``. This can be used to test with compilers not available on
your system.

run-buildbot
~~~~~~~~~~~~

Contains the build script executed on Buildkite. This script can be executed
locally or inside ``run-buildbot-container``. The script must be called with
the target to test. For example, ``run-buildbot generic-cxx20`` will build
libc++ and test it using C++20.

.. warning:: This script will overwrite the directory ``<llvm-root>/build/XX``
  where ``XX`` is the target of ``run-buildbot``.

This script contains as little version information as possible. This makes it
easy to use the script with a different compiler. This allows testing a
combination not in the libc++ CI. It can be used to add a new (temporary)
job to the CI. For example, testing the C++17 build with Clang-14 can be done
like:

.. code-block:: bash

  CC=clang-14 CXX=clang++-14 run-buildbot generic-cxx17

buildkite-pipeline.yml
~~~~~~~~~~~~~~~~~~~~~~

Contains the jobs executed in the CI. This file contains the version
information of the jobs being executed. Since this script differs between the
``main`` and ``release`` branch, both branches can use different compiler
versions.
