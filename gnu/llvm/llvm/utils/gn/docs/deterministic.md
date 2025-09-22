Deterministic builds with LLVM's GN build
=========================================

Summary: Use the following args.gn.

    use_relative_paths_in_debug_info = true

It is possible to produce [locally deterministic][1] builds of LLVM
with the GN build. It requires some configuration though.

1. Make debug info use relative paths by setting
   `use_relative_paths_in_debug_info = true` in your `args.gn` file. With this
   set, current debuggers need minor configuration to keep working.  See
   "Getting to local determinism" and "Getting debuggers to work well with
   locally deterministic builds" in the [deterministic builds][1] documentation
   for details.

1: http://blog.llvm.org/2019/11/deterministic-builds-with-clang-and-lld.html
