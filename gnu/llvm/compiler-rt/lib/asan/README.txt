AddressSanitizer RT
================================
This directory contains sources of the AddressSanitizer (ASan) runtime library.

Directory structure:
README.txt       : This file.
Makefile.mk      : File for make-based build.
CMakeLists.txt   : File for cmake-based build.
asan_*.{cc,h}    : Sources of the asan runtime library.
scripts/*        : Helper scripts.
tests/*          : ASan unit tests.

Also ASan runtime needs the following libraries:
lib/interception/      : Machinery used to intercept function calls.
lib/sanitizer_common/  : Code shared between various sanitizers.

ASan runtime currently also embeds part of LeakSanitizer runtime for
leak detection (lib/lsan/lsan_common.{cc,h}).

ASan runtime can only be built by CMake. You can run ASan tests
from the root of your CMake build tree:

make check-asan

For more instructions see:
https://github.com/google/sanitizers/wiki/AddressSanitizerHowToBuild
