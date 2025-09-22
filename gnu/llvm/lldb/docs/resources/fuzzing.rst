Fuzzing
=======

Overview
--------

LLDB has fuzzers that provide automated `fuzz testing <https://en.wikipedia.org/wiki/Fuzzing>`_ for different components of LLDB. The fuzzers are built with `libFuzzer <https://llvm.org/docs/LibFuzzer.html>`_ . Currently, there are fuzzers for target creation, LLDB's command interpreter and LLDB's expression evaluator.

Building the fuzzers
--------------------

Building the LLDB fuzzers requires a build configuration that has the address sanitizer and sanitizer coverage enabled. In addition to your regular CMake arguments, you will need these arguments to build the fuzzers:

::

   -DLLVM_USE_SANITIZER='Address' \
   -DLLVM_USE_SANITIZE_COVERAGE=On \
   -DCLANG_ENABLE_PROTO_FUZZER=ON

More information on libFuzzer's sanitizer coverage is available here: `<https://llvm.org/docs/LibFuzzer.html#fuzzer-usage>`_

If you want to debug LLDB itself when you find a bug using the fuzzers, use the CMake option ``-DCMAKE_BUILD_TYPE='RelWithDebInfo'``

To build a fuzzer, run the desired ninja command for the fuzzer(s) you want to build:

::

   $ ninja lldb-target-fuzzer
   $ ninja lldb-commandinterpreter-fuzzer
   $ ninja lldb-expression-fuzzer

Once built, the binaries for the fuzzers will exist in the ``bin`` directory of your build folder.

Continuous integration
----------------------

Currently, there are plans to integrate the LLDB fuzzers into the `OSS Fuzz <https://github.com/google/oss-fuzz>`_ project for continuous integration.

Running the fuzzers
-------------------

If you want to run the fuzzers locally, you can run the binaries that were generated with ninja from the build directory:

::

   $ ./bin/lldb-target-fuzzer
   $ ./bin/lldb-commandinterpreter-fuzzer
   $ ./bin/lldb-expression-fuzzer

This will run the fuzzer binaries directly, and you can use the `libFuzzer options <https://llvm.org/docs/LibFuzzer.html#options>`_ to customize how the fuzzers are run.

Another way to run the fuzzers is to use a ninja target that will both build the fuzzers and then run them immediately after. These custom targets run each fuzzer with command-line arguments that provide better fuzzing for the components being tested. Running the fuzzers this way will also create directories that will store any inputs that caused LLDB to crash, timeout or run out of memory. The directories are created for each fuzzer.

To run the custom ninja targets, run the command for your desired fuzzer:

::

   $ ninja fuzz-lldb-target
   $ ninja fuzz-lldb-commandinterpreter
   $ ninja fuzz-lldb-expression

Investigating and reproducing bugs
----------------------------------

When the fuzzers find an input that causes LLDB to crash, timeout or run out of memory, the input is saved to a file in the build directory. When running the fuzzer binaries directly this input is stored in a file named ``<crash/timeout/oom>-<hash>``.

When running the fuzzers using the custom ninja targets shown above, the inputs will be stored in ``fuzzer-artifacts/<fuzzer name>-artifacts``, which is created in your build directory. The input files will have the name ``<fuzzer name>-<crash/timeout/oom>-<hash>``.

If you want to reproduce the issue found by a fuzzer once you have gotten the input, you can pass the individual input to the fuzzer binary as a command-line argument:

::

   $ ./<fuzzer binary> <input you are investigating>
