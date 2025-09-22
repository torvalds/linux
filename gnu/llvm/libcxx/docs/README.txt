libc++ Documentation
====================

The libc++ documentation is written using the Sphinx documentation generator. It is
currently tested with Sphinx 1.1.3.

To build the documents into html configure libc++ with the following cmake options:

  * -DLLVM_ENABLE_SPHINX=ON
  * -DLIBCXX_INCLUDE_DOCS=ON

After configuring libc++ with these options the make rule `docs-libcxx-html`
should be available.

The documentation in this directory is published at https://libcxx.llvm.org. It is kept up-to-date
by a build bot: https://lab.llvm.org/buildbot/#/builders/publish-sphinx-docs. If you notice that the
documentation is not updating anymore, please contact one of the maintainers.
