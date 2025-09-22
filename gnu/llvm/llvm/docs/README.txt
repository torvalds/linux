LLVM Documentation
==================

LLVM's documentation is written in reStructuredText, a lightweight
plaintext markup language (file extension `.rst`). While the
reStructuredText documentation should be quite readable in source form, it
is mostly meant to be processed by the Sphinx documentation generation
system to create HTML pages which are hosted on <https://llvm.org/docs/> and
updated after every commit. Manpage output is also supported, see below.

If you instead would like to generate and view the HTML locally, install
Sphinx <http://sphinx-doc.org/> and then do:

    cd <build-dir>
    cmake -DLLVM_ENABLE_SPHINX=true -DSPHINX_OUTPUT_HTML=true <src-dir>
    make -j3 docs-llvm-html
    $BROWSER <build-dir>/docs/html/index.html

The mapping between reStructuredText files and generated documentation is
`docs/Foo.rst` <-> `<build-dir>/docs//html/Foo.html` <-> `https://llvm.org/docs/Foo.html`.

If you are interested in writing new documentation, you will want to read
`SphinxQuickstartTemplate.rst` which will get you writing documentation
very fast and includes examples of the most important reStructuredText
markup syntax.

Manpage Output
===============

Building the manpages is similar to building the HTML documentation. The
primary difference is to use the `man` makefile target, instead of the
default (which is `html`). Sphinx then produces the man pages in the
directory `<build-dir>/docs/man/`.

    cd <build-dir>
    cmake -DLLVM_ENABLE_SPHINX=true -DSPHINX_OUTPUT_MAN=true <src-dir>
    make -j3 docs-llvm-man
    man -l <build-dir>/docs/man/FileCheck.1

The correspondence between .rst files and man pages is
`docs/CommandGuide/Foo.rst` <-> `<build-dir>/docs//man/Foo.1`.
These .rst files are also included during HTML generation so they are also
viewable online (as noted above) at e.g.
`https://llvm.org/docs/CommandGuide/Foo.html`.

Checking links
==============

The reachability of external links in the documentation can be checked by
running:

    cd llvm/docs/
    sphinx-build -b linkcheck . _build/lintcheck/
    # report will be generated in _build/lintcheck/output.txt

Doxygen page Output
==============

Install doxygen <https://www.doxygen.nl/download.html> and dot2tex <https://dot2tex.readthedocs.io/en/latest>.

    cd <build-dir>
    cmake -DLLVM_ENABLE_DOXYGEN=On <llvm-top-src-dir>
    make doxygen-llvm # for LLVM docs
    make doxygen-clang # for clang docs

It will generate html in

    <build-dir>/docs/doxygen/html # for LLVM docs
    <build-dir>/tools/clang/docs/doxygen/html # for clang docs
