test-suite Guide
================

Quickstart
----------

1. The lit test runner is required to run the tests. You can either use one
   from an LLVM build:

   ```bash
   % <path to llvm build>/bin/llvm-lit --version
   lit 0.8.0dev
   ```

   An alternative is installing it as a python package in a python virtual
   environment:

   ```bash
   % mkdir venv
   % virtualenv venv
   % . venv/bin/activate
   % pip install svn+https://llvm.org/svn/llvm-project/llvm/trunk/utils/lit
   % lit --version
   lit 0.8.0dev
   ```

2. Check out the `test-suite` module with:

   ```bash
   % git clone https://github.com/llvm/llvm-test-suite.git test-suite
   ```

3. Create a build directory and use CMake to configure the suite. Use the
   `CMAKE_C_COMPILER` option to specify the compiler to test. Use a cache file
   to choose a typical build configuration:

   ```bash
   % mkdir test-suite-build
   % cd test-suite-build
   % cmake -DCMAKE_C_COMPILER=<path to llvm build>/bin/clang \
           -C../test-suite/cmake/caches/O3.cmake \
           ../test-suite
   ```

**NOTE!** if you are using your built clang, and you want to build and run the
MicroBenchmarks/XRay microbenchmarks, you need to add `compiler-rt` to your
`LLVM_ENABLE_RUNTIMES` cmake flag.

4. Build the benchmarks:

   ```text
   % make
   Scanning dependencies of target timeit-target
   [  0%] Building C object tools/CMakeFiles/timeit-target.dir/timeit.c.o
   [  0%] Linking C executable timeit-target
   ...
   ```

5. Run the tests with lit:

   ```text
   % llvm-lit -v -j 1 -o results.json .
   -- Testing: 474 tests, 1 threads --
   PASS: test-suite :: MultiSource/Applications/ALAC/decode/alacconvert-decode.test (1 of 474)
   ********** TEST 'test-suite :: MultiSource/Applications/ALAC/decode/alacconvert-decode.test' RESULTS **********
   compile_time: 0.2192
   exec_time: 0.0462
   hash: "59620e187c6ac38b36382685ccd2b63b"
   size: 83348
   **********
   PASS: test-suite :: MultiSource/Applications/ALAC/encode/alacconvert-encode.test (2 of 474)
   ...
   ```
**NOTE!** even in the case you only want to get the compile-time results(code size, llvm stats etc),
you need to run the test with the above `llvm-lit` command. In that case, the *results.json* file will
contain compile-time metrics.

6. Show and compare result files (optional):

   ```bash
   # Make sure pandas and scipy are installed. Prepend `sudo` if necessary.
   % pip install pandas scipy
   # Show a single result file:
   % test-suite/utils/compare.py results.json
   # Compare two result files:
   % test-suite/utils/compare.py results_a.json results_b.json
   ```


Structure
---------

The test-suite contains benchmark and test programs.  The programs come with
reference outputs so that their correctness can be checked.  The suite comes
with tools to collect metrics such as benchmark runtime, compilation time and
code size.

The test-suite is divided into several directories:

-  `SingleSource/`

   Contains test programs that are only a single source file in size.  A
   subdirectory may contain several programs.

-  `MultiSource/`

   Contains subdirectories which entire programs with multiple source files.
   Large benchmarks and whole applications go here.

-  `MicroBenchmarks/`

   Programs using the [google-benchmark](https://github.com/google/benchmark)
   library. The programs define functions that are run multiple times until the
   measurement results are statistically significant.

-  `External/`

   Contains descriptions and test data for code that cannot be directly
   distributed with the test-suite. The most prominent members of this
   directory are the SPEC CPU benchmark suites.
   See [External Suites](#external-suites).

-  `Bitcode/`

   These tests are mostly written in LLVM bitcode.

-  `CTMark/`

   Contains symbolic links to other benchmarks forming a representative sample
   for compilation performance measurements.

### Benchmarks

Every program can work as a correctness test. Some programs are unsuitable for
performance measurements. Setting the `TEST_SUITE_BENCHMARKING_ONLY` CMake
option to `ON` will disable them.


Configuration
-------------

The test-suite has configuration options to customize building and running the
benchmarks. CMake can print a list of them:

```bash
% cd test-suite-build
# Print basic options:
% cmake -LH
# Print all options:
% cmake -LAH
```

### Common Configuration Options

- `CMAKE_C_FLAGS`

  Specify extra flags to be passed to C compiler invocations.  The flags are
  also passed to the C++ compiler and linker invocations.  See
  [https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_FLAGS.html](https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_FLAGS.html)

- `CMAKE_C_COMPILER`

  Select the C compiler executable to be used. Note that the C++ compiler is
  inferred automatically i.e. when specifying `path/to/clang` CMake will
  automatically use `path/to/clang++` as the C++ compiler.  See
  [https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER.html](https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER.html)

- `CMAKE_Fortran_COMPILER`

  Select the Fortran compiler executable to be used. Not set by default and not
  required unless running the Fortran Test Suite.

- `CMAKE_BUILD_TYPE`

  Select a build type like `OPTIMIZE` or `DEBUG` selecting a set of predefined
  compiler flags. These flags are applied regardless of the `CMAKE_C_FLAGS`
  option and may be changed by modifying `CMAKE_C_FLAGS_OPTIMIZE` etc.  See
  [https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html](https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html)

- `TEST_SUITE_FORTRAN`

  Activate that Fortran tests. This is a work in progress. More information can be
  found in the [Flang documentation](https://flang.llvm.org/docs/FortranLLVMTestSuite.html)

- `TEST_SUITE_RUN_UNDER`

  Prefix test invocations with the given tool. This is typically used to run
  cross-compiled tests within a simulator tool.

- `TEST_SUITE_BENCHMARKING_ONLY`

  Disable tests that are unsuitable for performance measurements. The disabled
  tests either run for a very short time or are dominated by I/O performance
  making them unsuitable as compiler performance tests.

- `TEST_SUITE_SUBDIRS`

  Semicolon-separated list of directories to include. This can be used to only
  build parts of the test-suite or to include external suites.  This option
  does not work reliably with deeper subdirectories as it skips intermediate
  `CMakeLists.txt` files which may be required.

- `TEST_SUITE_COLLECT_STATS`

  Collect internal LLVM statistics. Appends `-save-stats=obj` when invoking the
  compiler and makes the lit runner collect and merge the statistic files.

- `TEST_SUITE_RUN_BENCHMARKS`

  If this is set to `OFF` then lit will not actually run the tests but just
  collect build statistics like compile time and code size.

- `TEST_SUITE_USE_PERF`

  Use the `perf` tool for time measurement instead of the `timeit` tool that
  comes with the test-suite.  The `perf` is usually available on linux systems.

- `TEST_SUITE_SPEC2000_ROOT`, `TEST_SUITE_SPEC2006_ROOT`, `TEST_SUITE_SPEC2017_ROOT`, ...

  Specify installation directories of external benchmark suites. You can find
  more information about expected versions or usage in the README files in the
  `External` directory (such as `External/SPEC/README`)

### Common CMake Flags

- `-GNinja`

  Generate build files for the ninja build tool.

- `-Ctest-suite/cmake/caches/<cachefile.cmake>`

  Use a CMake cache.  The test-suite comes with several CMake caches which
  predefine common or tricky build configurations.


Displaying and Analyzing Results
--------------------------------

The `compare.py` script displays and compares result files.  A result file is
produced when invoking lit with the `-o filename.json` flag.

Example usage:

- Basic Usage:

  ```text
  % test-suite/utils/compare.py baseline.json
  Warning: 'test-suite :: External/SPEC/CINT2006/403.gcc/403.gcc.test' has No metrics!
  Tests: 508
  Metric: exec_time

  Program                                         baseline

  INT2006/456.hmmer/456.hmmer                   1222.90
  INT2006/464.h264ref/464.h264ref               928.70
  ...
               baseline
  count  506.000000
  mean   20.563098
  std    111.423325
  min    0.003400
  25%    0.011200
  50%    0.339450
  75%    4.067200
  max    1222.896800
  ```

- Show compile_time or text segment size metrics:

  ```bash
  % test-suite/utils/compare.py -m compile_time baseline.json
  % test-suite/utils/compare.py -m size.__text baseline.json
  ```

- Compare two result files and filter short running tests:

  ```bash
  % test-suite/utils/compare.py --filter-short baseline.json experiment.json
  ...
  Program                                         baseline  experiment  diff

  SingleSour.../Benchmarks/Linpack/linpack-pc     5.16      4.30        -16.5%
  MultiSourc...erolling-dbl/LoopRerolling-dbl     7.01      7.86         12.2%
  SingleSour...UnitTests/Vectorizer/gcc-loops     3.89      3.54        -9.0%
  ...
  ```

- Merge multiple baseline and experiment result files by taking the minimum
  runtime each:

  ```bash
  % test-suite/utils/compare.py base0.json base1.json base2.json vs exp0.json exp1.json exp2.json
  ```

### Continuous Tracking with LNT

LNT is a set of client and server tools for continuously monitoring
performance. You can find more information at
[https://llvm.org/docs/lnt](https://llvm.org/docs/lnt). The official LNT instance
of the LLVM project is hosted at [http://lnt.llvm.org](http://lnt.llvm.org).


External Suites
---------------

External suites such as SPEC can be enabled by either

- placing (or linking) them into the `test-suite/test-suite-externals/xxx` directory (example: `test-suite/test-suite-externals/speccpu2000`)
- using a configuration option such as `-D TEST_SUITE_SPEC2000_ROOT=path/to/speccpu2000`

You can find further information in the respective README files such as
`test-suite/External/SPEC/README`.

For the SPEC benchmarks you can switch between the `test`, `train` and
`ref` input datasets via the `TEST_SUITE_RUN_TYPE` configuration option.
The `train` dataset is used by default.


Custom Suites
-------------

You can build custom suites using the test-suite infrastructure. A custom suite
has a `CMakeLists.txt` file at the top directory. The `CMakeLists.txt` will be
picked up automatically if placed into a subdirectory of the test-suite or when
setting the `TEST_SUITE_SUBDIRS` variable:

```bash
% cmake -DTEST_SUITE_SUBDIRS=path/to/my/benchmark-suite ../test-suite
```


Profile Guided Optimization
---------------------------

Profile guided optimization requires to compile and run twice. First the
benchmark should be compiled with profile generation instrumentation enabled
and setup for training data. The lit runner will merge the profile files
using `llvm-profdata` so they can be used by the second compilation run.

Example:
```bash
# Profile generation run using LLVM IR PGO:
% cmake -DTEST_SUITE_PROFILE_GENERATE=ON \
        -DTEST_SUITE_USE_IR_PGO=ON \
        -DTEST_SUITE_RUN_TYPE=train \
        ../test-suite
% make
% llvm-lit .
# Use the profile data for compilation and actual benchmark run:
% cmake -DTEST_SUITE_PROFILE_GENERATE=OFF \
        -DTEST_SUITE_PROFILE_USE=ON \
        -DTEST_SUITE_RUN_TYPE=ref \
        .
% make
% llvm-lit -o result.json .
```

To use Clang frontend's PGO instead of LLVM IR PGO, set `-DTEST_SUITE_USE_IR_PGO=OFF`.

The `TEST_SUITE_RUN_TYPE` setting only affects the SPEC benchmark suites.


Cross Compilation and External Devices
--------------------------------------

### Compilation

CMake allows to cross compile to a different target via toolchain files. More
information can be found here:

- [https://llvm.org/docs/lnt/tests.html#cross-compiling](https://llvm.org/docs/lnt/tests.html#cross-compiling)

- [https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html)

Cross compilation from macOS to iOS is possible with the
`test-suite/cmake/caches/target-target-*-iphoneos-internal.cmake` CMake cache
files; this requires an internal iOS SDK.

### Running

There are two ways to run the tests in a cross compilation setting:

- Via SSH connection to an external device: The `TEST_SUITE_REMOTE_HOST` option
  should be set to the SSH hostname.  The executables and data files need to be
  transferred to the device after compilation.  This is typically done via the
  `rsync` make target.  After this, the lit runner can be used on the host
  machine. It will prefix the benchmark and verification command lines with an
  `ssh` command.

  Example:

  ```bash
  % cmake -G Ninja -D CMAKE_C_COMPILER=path/to/clang \
          -C ../test-suite/cmake/caches/target-arm64-iphoneos-internal.cmake \
          -D CMAKE_BUILD_TYPE=Release \
          -D TEST_SUITE_REMOTE_HOST=mydevice \
          ../test-suite
  % ninja
  % ninja rsync
  % llvm-lit -j1 -o result.json .
  ```

- You can specify a simulator for the target machine with the
  `TEST_SUITE_RUN_UNDER` setting. The lit runner will prefix all benchmark
  invocations with it.


Running the test-suite via LNT
------------------------------

The LNT tool can run the test-suite. Use this when submitting test results to
an LNT instance.  See
[https://llvm.org/docs/lnt/tests.html#llvm-cmake-test-suite](https://llvm.org/docs/lnt/tests.html#llvm-cmake-test-suite)
for details.

Running the test-suite via Makefiles (deprecated)
-------------------------------------------------

**Note**: The test-suite comes with a set of Makefiles that are considered
deprecated.  They do not support newer testing modes like `Bitcode` or
`Microbenchmarks` and are harder to use.

Old documentation is available in the
[test-suite Makefile Guide](TestSuiteMakefileGuide).
