This directory contains three utilities for fuzzing Clang: clang-fuzzer,
clang-objc-fuzzer, and clang-proto-fuzzer. All use libFuzzer to generate inputs
to clang via coverage-guided mutation.

The three utilities differ, however, in how they structure inputs to Clang.
clang-fuzzer makes no attempt to generate valid C++ programs and is therefore
primarily useful for stressing the surface layers of Clang (i.e. lexer, parser).

clang-objc-fuzzer is similar but for Objective-C: it makes no attempt to
generate a valid Objective-C program.

clang-proto-fuzzer uses a protobuf class to describe a subset of the C++
language and then uses libprotobuf-mutator to mutate instantiations of that
class, producing valid C++ programs in the process.  As a result,
clang-proto-fuzzer is better at stressing deeper layers of Clang and LLVM.

Some of the fuzzers have example corpuses inside the corpus_examples directory.

===================================
 Building clang-fuzzer
===================================
Within your LLVM build directory, run CMake with the following variable
definitions:
- CMAKE_C_COMPILER=clang
- CMAKE_CXX_COMPILER=clang++
- LLVM_USE_SANITIZE_COVERAGE=YES
- LLVM_USE_SANITIZER=Address

Then build the clang-fuzzer target.

Example:
  cd $LLVM_SOURCE_DIR
  mkdir build && cd build
  cmake .. -GNinja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_USE_SANITIZE_COVERAGE=YES -DLLVM_USE_SANITIZER=Address
  ninja clang-fuzzer

======================
 Running clang-fuzzer
======================
  bin/clang-fuzzer CORPUS_DIR


===================================
 Building clang-objc-fuzzer
===================================
Within your LLVM build directory, run CMake with the following variable
definitions:
- CMAKE_C_COMPILER=clang
- CMAKE_CXX_COMPILER=clang++
- LLVM_USE_SANITIZE_COVERAGE=YES
- LLVM_USE_SANITIZER=Address

Then build the clang-objc-fuzzer target.

Example:
  cd $LLVM_SOURCE_DIR
  mkdir build && cd build
  cmake .. -GNinja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_USE_SANITIZE_COVERAGE=YES -DLLVM_USE_SANITIZER=Address
  ninja clang-objc-fuzzer

======================
 Running clang-objc-fuzzer
======================
  bin/clang-objc-fuzzer CORPUS_DIR

e.g. using the example objc corpus,

  bin/clang-objc-fuzzer <path to corpus_examples/objc> <path to new directory to store  corpus findings>


=======================================================
 Building clang-proto-fuzzer (Linux-only instructions)
=======================================================
Install the necessary dependencies:
- binutils  // needed for libprotobuf-mutator
- liblzma-dev  // needed for libprotobuf-mutator
- libz-dev  // needed for libprotobuf-mutator
- docbook2x  // needed for libprotobuf-mutator
- Recent version of protobuf [3.3.0 is known to work]

Within your LLVM build directory, run CMake with the following variable
definitions:
- CMAKE_C_COMPILER=clang
- CMAKE_CXX_COMPILER=clang++
- LLVM_USE_SANITIZE_COVERAGE=YES
- LLVM_USE_SANITIZER=Address
- CLANG_ENABLE_PROTO_FUZZER=ON

Then build the clang-proto-fuzzer and clang-proto-to-cxx targets.  Optionally,
you may also build clang-fuzzer with this setup.

Example:
  cd $LLVM_SOURCE_DIR
  mkdir build && cd build
  cmake .. -GNinja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DLLVM_USE_SANITIZE_COVERAGE=YES -DLLVM_USE_SANITIZER=Address \
    -DCLANG_ENABLE_PROTO_FUZZER=ON
  ninja clang-proto-fuzzer clang-proto-to-cxx

This directory also contains a Dockerfile which sets up all required
dependencies and builds the fuzzers.

============================
 Running clang-proto-fuzzer
============================
  bin/clang-proto-fuzzer CORPUS_DIR

Arguments can be specified after -ignore_remaining_args=1 to modify the compiler
invocation.  For example, the following command line will fuzz LLVM with a
custom optimization level and target triple:
  bin/clang-proto-fuzzer CORPUS_DIR -ignore_remaining_args=1 -O3 -triple \
      arm64apple-ios9

To translate a clang-proto-fuzzer corpus output to C++:
  bin/clang-proto-to-cxx CORPUS_OUTPUT_FILE

===================
 llvm-proto-fuzzer
===================
Like, clang-proto-fuzzer, llvm-proto-fuzzer is also a protobuf-mutator based
fuzzer. It receives as input a cxx_loop_proto which it then converts into a
string of valid LLVM IR: a function with either a single loop or two nested
loops. It then creates a new string of IR by running optimization passes over
the original IR. Currently, it only runs a loop-vectorize pass but more passes
can easily be added to the fuzzer. Once there are two versions of the input
function (optimized and not), llvm-proto-fuzzer uses LLVM's JIT Engine to
compile both functions. Lastly, it runs both functions on a suite of inputs and
checks that both functions behave the same on all inputs. In this way,
llvm-proto-fuzzer can find not only compiler crashes, but also miscompiles
originating from LLVM's optimization passes.

llvm-proto-fuzzer is built very similarly to clang-proto-fuzzer. You can run the
fuzzer with the following command:
  bin/clang-llvm-proto-fuzzer CORPUS_DIR

To translate a cxx_loop_proto file into LLVM IR do:
  bin/clang-loop-proto-to-llvm CORPUS_OUTPUT_FILE
To translate a cxx_loop_proto file into C++ do:
  bin/clang-loop-proto-to-cxx CORPUS_OUTPUT_FILE

Note: To get a higher number of executions per second with llvm-proto-fuzzer it
helps to build it without ASan instrumentation and with the -O2 flag. Because
the fuzzer is not only compiling code, but also running it, as the inputs get
large, the time necessary to fuzz one input can get very high.
Example:
  cmake .. -GNinja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCLANG_ENABLE_PROTO_FUZZER=ON -DLLVM_USE_SANITIZE_COVERAGE=YES \
    -DCMAKE_CXX_FLAGS="-O2"
  ninja clang-llvm-proto-fuzzer clang-loop-proto-to-llvm
