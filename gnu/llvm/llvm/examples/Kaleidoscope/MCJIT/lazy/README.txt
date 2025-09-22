//===----------------------------------------------------------------------===/
//                          Kaleidoscope with MCJIT
//===----------------------------------------------------------------------===//

The files in this directory are meant to accompany the second blog in a series of
three blog posts that describe the process of porting the Kaleidoscope tutorial
to use the MCJIT execution engine instead of the older JIT engine.

The link of blog post-
https://blog.llvm.org/posts/2013-07-29-kaleidoscope-performance-with-mcjit/

The source code in this directory demonstrates the second version of the
program, now modified to implement a sort of 'lazy' compilation.

The toy-jit.cpp file contains a version of the original JIT-based source code
that has been modified to disable most stderr output for timing purposes.

To build the program you will need to have 'clang++' and 'llvm-config' in your 
path. If you attempt to build using the LLVM 3.3 release, some minor 
modifications will be required.

This directory also contains a Python script that may be used to generate random
input for the program and test scripts to capture data for rough performance
comparisons.
