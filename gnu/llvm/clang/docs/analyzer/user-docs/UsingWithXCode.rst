Running the analyzer within Xcode
=================================

.. contents::
   :local:

Since Xcode 3.2, users have been able to run the static analyzer `directly within Xcode <https://developer.apple.com/library/ios/recipes/xcode_help-source_editor/chapters/Analyze.html#//apple_ref/doc/uid/TP40009975-CH4-SW1>`_.

It integrates directly with the Xcode build system and presents analysis results directly within Xcode's editor.

Can I use the open source analyzer builds with Xcode?
-----------------------------------------------------

**Yes**. Instructions are included below.

.. image:: ../images/analyzer_xcode.png

**Viewing static analyzer results in Xcode**

Key features:
-------------

- **Integrated workflow:** Results are integrated within Xcode. There is no experience of using a separate tool, and activating the analyzer requires a single keystroke or mouse click.
- **Transparency:** Works effortlessly with Xcode projects (including iPhone projects).
- **Cons:** Doesn't work well with non-Xcode projects. For those, consider :doc:`CommandLineUsage`.

Getting Started
---------------

Xcode is available as a free download from Apple on the `Mac App Store <https://itunes.apple.com/us/app/xcode/id497799835?mt=12>`_, with `instructions available <https://developer.apple.com/library/ios/recipes/xcode_help-source_editor/chapters/Analyze.html#//apple_ref/doc/uid/TP40009975-CH4-SW1>`_ for using the analyzer.

Using open source analyzer builds with Xcode
--------------------------------------------

By default, Xcode uses the version of ``clang`` that came bundled with it to analyze your code. It is possible to change Xcode's behavior to use an alternate version of ``clang`` for this purpose while continuing to use the ``clang`` that came with Xcode for compiling projects.

Why try open source builds?
----------------------------

The advantage of using open source analyzer builds (provided on this website) is that they are often newer than the analyzer provided with Xcode, and thus can contain bug fixes, new checks, or simply better analysis.

On the other hand, new checks can be experimental, with results of variable quality. Users are encouraged to file bug reports (for any version of the analyzer) where they encounter false positives or other issues here: :doc:`FilingBugs`.

set-xcode-analyzer
------------------

Starting with analyzer build checker-234, analyzer builds contain a command line utility called ``set-xcode-analyzer`` that allows users to change what copy of ``clang`` that Xcode uses for analysis::

  $ set-xcode-analyzer -h
  Usage: set-xcode-analyzer [options]

  Options:
    -h, --help            show this help message and exit
    --use-checker-build=PATH
                          Use the Clang located at the provided absolute path,
                          e.g. /Users/foo/checker-1
    --use-xcode-clang     Use the Clang bundled with Xcode

Operationally, **set-xcode-analyzer** edits Xcode's configuration files to point it to use the version of ``clang`` you specify for static analysis. Within this model it provides you two basic modes:

- **--use-xcode-clang:** Switch Xcode (back) to using the ``clang`` that came bundled with it for static analysis.
- **--use-checker-build:** Switch Xcode to using the ``clang`` provided by the specified analyzer build.

Things to keep in mind
----------------------

- You should quit Xcode prior to running ``set-xcode-analyzer``.
- You will need to run ``set-xcode-analyzer`` under **``sudo``** in order to have write privileges to modify the Xcode configuration files.

Examples
--------

**Example 1**: Telling Xcode to use checker-235::

  $ pwd
  /tmp
  $ tar xjf checker-235.tar.bz2
  $ sudo checker-235/set-xcode-analyzer --use-checker-build=/tmp/checker-235

Note that you typically won't install an analyzer build in ``/tmp``, but the point of this example is that ``set-xcode-analyzer`` just wants a full path to an untarred analyzer build.

**Example 2**: Telling Xcode to use a very specific version of ``clang``::

  $ sudo set-xcode-analyzer --use-checker-build=~/mycrazyclangbuild/bin/clang

**Example 3**: Resetting Xcode to its default behavior::

  $ sudo set-xcode-analyzer --use-xcode-clang
