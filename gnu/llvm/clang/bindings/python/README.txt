//===----------------------------------------------------------------------===//
// Clang Python Bindings
//===----------------------------------------------------------------------===//

This directory implements Python bindings for Clang.

You may need to set CLANG_LIBRARY_PATH so that the Clang library can be
found. The unit tests are designed to be run with any standard test
runner. For example:
--
$ env PYTHONPATH=$(echo ~/llvm/clang/bindings/python/) \
      CLANG_LIBRARY_PATH=$(llvm-config --libdir) \
  python3 -m unittest discover -v
tests.cindex.test_index.test_create ... ok
...

OK
--
