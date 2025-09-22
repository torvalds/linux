MemProfiling RT
================================
This directory contains sources of the MemProfiling (MemProf) runtime library.

Directory structure:
README.txt       : This file.
CMakeLists.txt   : File for cmake-based build.
memprof_*.{cc,h}    : Sources of the memprof runtime library.

Also MemProf runtime needs the following libraries:
lib/interception/      : Machinery used to intercept function calls.
lib/sanitizer_common/  : Code shared between various sanitizers.

MemProf runtime can only be built by CMake. You can run MemProf tests
from the root of your CMake build tree:

make check-memprof
