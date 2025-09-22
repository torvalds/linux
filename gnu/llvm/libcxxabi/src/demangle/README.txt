Itanium Name Demangler Library
==============================

Introduction
------------

This directory contains the generic itanium name demangler
library. The main purpose of the library is to demangle C++ symbols,
i.e. convert the string "_Z1fv" into "f()". You can also use the CRTP
base ManglingParser to perform some simple analysis on the mangled
name, or (in LLVM) use the opaque ItaniumPartialDemangler to query the
demangled AST.

Why are there multiple copies of the this library in the source tree?
---------------------------------------------------------------------

The canonical sources are in libcxxabi/src/demangle and some of the
files are copied to llvm/include/llvm/Demangle.  The simple reason for
this comes from before the monorepo, and both [sub]projects need to
demangle symbols, but neither can depend on each other.

* libcxxabi needs the demangler to implement __cxa_demangle, which is
  part of the itanium ABI spec.

* LLVM needs a copy for a bunch of places, and cannot rely on the
  system's __cxa_demangle because it a) might not be available (i.e.,
  on Windows), and b) may not be up-to-date on the latest language
  features.

The copy of the demangler in LLVM has some extra stuff that aren't
needed in libcxxabi (ie, the MSVC demangler, ItaniumPartialDemangler),
which depend on the shared generic components. Despite these
differences, we want to keep the "core" generic demangling library
identical between both copies to simplify development and testing.

If you're working on the generic library, then do the work first in
libcxxabi, then run libcxxabi/src/demangle/cp-to-llvm.sh. This
script takes as an optional argument the path to llvm, and copies the
changes you made to libcxxabi over.  Note that this script just
blindly overwrites all changes to the generic library in llvm, so be
careful.

Because the core demangler needs to work in libcxxabi, everything
needs to be declared in an anonymous namespace (see
DEMANGLE_NAMESPACE_BEGIN), and you can't introduce any code that
depends on the libcxx dylib.

FIXME: Now that LLVM is a monorepo, it should be possible to
de-duplicate this code, and have both LLVM and libcxxabi depend on a
shared demangler library.

Testing
-------

The tests are split up between libcxxabi/test/{unit,}test_demangle.cpp, and
llvm/unittest/Demangle. The llvm directory should only get tests for stuff not
included in the core library. In the future though, we should probably move all
the tests to LLVM.

It is also a really good idea to run libFuzzer after non-trivial changes, see
libcxxabi/fuzz/cxa_demangle_fuzzer.cpp and https://llvm.org/docs/LibFuzzer.html.
