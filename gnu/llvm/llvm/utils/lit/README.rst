===============================
 lit - A Software Testing Tool
===============================

About
=====

*lit* is a portable tool for executing LLVM and Clang style test suites,
summarizing their results, and providing indication of failures. *lit* is
designed to be a lightweight testing tool with as simple a user interface as
possible.


Features
========

 * Portable!
 * Flexible test discovery.
 * Parallel test execution.
 * Support for multiple test formats and test suite designs.


Documentation
=============

The official *lit* documentation is in the man page, available online at the LLVM
Command Guide: http://llvm.org/cmds/lit.html.


Source
======

The *lit* source is available as part of LLVM, in the LLVM source repository:
https://github.com/llvm/llvm-project/tree/main/llvm/utils/lit


Contributing to lit
===================

Please browse the issues labeled *tools:llvm-lit* in LLVM's issue tracker for
ideas on what to work on:
https://github.com/llvm/llvm-project/labels/tools%3Allvm-lit

Before submitting patches, run the test suite to ensure nothing has regressed::

    # From within your LLVM source directory.
    utils/lit/lit.py \
        --path /path/to/your/llvm/build/bin \
        utils/lit/tests

Note that lit's tests depend on ``not`` and ``FileCheck``, LLVM utilities.
You will need to have built LLVM tools in order to run lit's test suite
successfully.

You'll also want to confirm that lit continues to work when testing LLVM.
Follow the instructions in http://llvm.org/docs/TestingGuide.html to run the
regression test suite:

    make check-llvm

And be sure to run the llvm-lit wrapper script as well:

    /path/to/your/llvm/build/bin/llvm-lit utils/lit/tests

Finally, make sure lit works when installed via setuptools:

    python utils/lit/setup.py install
    lit --path /path/to/your/llvm/build/bin utils/lit/tests

