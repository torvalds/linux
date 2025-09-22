//===----------------------------------------------------------------------===/
//                          Kaleidoscope with MCJIT
//===----------------------------------------------------------------------===//

The files in this directory are meant to accompany the third blog in a series of
three blog posts that describe the process of porting the Kaleidoscope tutorial
to use the MCJIT execution engine instead of the older JIT engine.

The link of blog post-
https://blog.llvm.org/posts/2013-08-02-object-caching-with-kaleidoscope/

The source code in this directory demonstrates the third version of the
program, now modified to accept an input IR file on the command line and,
optionally, to use a basic caching mechanism to store generated object images.

The toy-jit.cpp file contains a version of the original JIT-based source code
that has been modified to support the input IR file command line option.

To build the program you will need to have 'clang++' and 'llvm-config' in your 
path. If you attempt to build using the LLVM 3.3 release, some minor 
modifications will be required.

This directory also contains a Python script that may be used to generate random
input for the program and test scripts to capture data for rough performance
comparisons.  Another Python script will split generated input files into
definitions and function calls for the purpose of testing the IR input and
caching facilities.
