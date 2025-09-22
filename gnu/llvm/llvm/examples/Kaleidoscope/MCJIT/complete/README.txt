//===----------------------------------------------------------------------===/
//                          Kaleidoscope with MCJIT
//===----------------------------------------------------------------------===//

The files in this directory are meant to accompany the first in a series of
three blog posts that describe the process of porting the Kaleidoscope tutorial
to use the MCJIT execution engine instead of the older JIT engine.

The source code in this directory combines all previous versions, including the
old JIT-based implementation, into a single file for easy comparison with
command line options to select between the various possibilities.

To build the program you will need to have 'clang++' and 'llvm-config' in your 
path. If you attempt to build using the LLVM 3.3 release, some minor 
modifications will be required.

This directory also contains a Python script that may be used to generate random
input for the program and test scripts to capture data for rough performance
comparisons.  Another Python script will split generated input files into
definitions and function calls for the purpose of testing the IR input and
caching facilities.
