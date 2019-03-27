Testing
=======

Zstandard CI testing is split up into three sections:
short, medium, and long tests.

Short Tests
-----------
Short tests run on CircleCI for new commits on every branch and pull request.
They consist of the following tests:
- Compilation on all supported targets (x86, x86_64, ARM, AArch64, PowerPC, and PowerPC64)
- Compilation on various versions of gcc, clang, and g++
- `tests/playTests.sh` on x86_64, without the tests on long data (CLI tests)
- Small tests (`tests/legacy.c`, `tests/longmatch.c`, `tests/symbols.c`) on x64_64

Medium Tests
------------
Medium tests run on every commit and pull request to `dev` branch, on TravisCI.
They consist of the following tests:
- The following tests run with UBsan and Asan on x86_64 and x86, as well as with
  Msan on x86_64
  - `tests/playTests.sh --test-long-data`
  - Fuzzer tests: `tests/fuzzer.c`, `tests/zstreamtest.c`, and `tests/decodecorpus.c`
- `tests/zstreamtest.c` under Tsan (streaming mode, including multithreaded mode)
- Valgrind Test (`make -C tests valgrindTest`) (testing CLI and fuzzer under valgrind)
- Fuzzer tests (see above) on ARM, AArch64, PowerPC, and PowerPC64

Long Tests
----------
Long tests run on all commits to `master` branch,
and once a day on the current version of `dev` branch,
on TravisCI.
They consist of the following tests:
- Entire test suite (including fuzzers and some other specialized tests) on:
  - x86_64 and x86 with UBsan and Asan
  - x86_64 with Msan
  - ARM, AArch64, PowerPC, and PowerPC64
- Streaming mode fuzzer with Tsan (for the `zstdmt` testing)
- ZlibWrapper tests, including under valgrind
- Versions test (ensuring `zstd` can decode files from all previous versions)
- `pzstd` with asan and tsan, as well as in 32-bits mode
- Testing `zstd` with legacy mode off
- Testing `zbuff` (old streaming API)
- Entire test suite and make install on macOS
