//===----------------------------------------------------------------------===/
//                          Kaleidoscope with MCJIT
//===----------------------------------------------------------------------===//

The files in this directory are meant to accompany the first blog in a series of
three blog posts that describe the process of porting the Kaleidoscope tutorial
to use the MCJIT execution engine instead of the older JIT engine.

The link of blog post-
https://blog.llvm.org/posts/2013-07-22-using-mcjit-with-kaleidoscope-tutorial/

The source code in this directory demonstrates the initial working version of
the program before subsequent performance improvements are applied.

To build the program you will need to have 'clang++' and 'llvm-config' in your 
path. If you attempt to build using the LLVM 3.3 release, some minor 
modifications will be required, as mentioned in the blog posts.
