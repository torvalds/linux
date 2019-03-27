# Fuzzing

Each fuzzing target can be built with multiple engines.
Zstd provides a fuzz corpus for each target that can be downloaded with
the command:

```
make corpora
```

It will download each corpus into `./corpora/TARGET`.

## fuzz.py

`fuzz.py` is a helper script for building and running fuzzers.
Run `./fuzz.py -h` for the commands and run `./fuzz.py COMMAND -h` for
command specific help.

### Generating Data

`fuzz.py` provides a utility to generate seed data for each fuzzer.

```
make -C ../tests decodecorpus
./fuzz.py gen TARGET
```

By default it outputs 100 samples, each at most 8KB into `corpora/TARGET-seed`,
but that can be configured with the `--number`, `--max-size-log` and `--seed`
flags.

### Build
It respects the usual build environment variables `CC`, `CFLAGS`, etc.
The environment variables can be overridden with the corresponding flags
`--cc`, `--cflags`, etc.
The specific fuzzing engine is selected with `LIB_FUZZING_ENGINE` or
`--lib-fuzzing-engine`, the default is `libregression.a`.
It has flags that can easily set up sanitizers `--enable-{a,ub,m}san`, and
coverage instrumentation `--enable-coverage`.
It sets sane defaults which can be overriden with flags `--debug`,
`--enable-ubsan-pointer-overlow`, etc.
Run `./fuzz.py build -h` for help.

### Running Fuzzers

`./fuzz.py` can run `libfuzzer`, `afl`, and `regression` tests.
See the help of the relevant command for options.
Flags not parsed by `fuzz.py` are passed to the fuzzing engine.
The command used to run the fuzzer is printed for debugging.

## LibFuzzer

```
# Build libfuzzer if necessary
make libFuzzer
# Build the fuzz targets
./fuzz.py build all --enable-coverage --enable-asan --enable-ubsan --lib-fuzzing-engine Fuzzer/libFuzzer.a --cc clang --cxx clang++
# OR equivalently
CC=clang CXX=clang++ LIB_FUZZING_ENGINE=Fuzzer/libFuzzer.a ./fuzz.py build all --enable-coverage --enable-asan --enable-ubsan
# Run the fuzzer
./fuzz.py libfuzzer TARGET -max_len=8192 -jobs=4
```

where `TARGET` could be `simple_decompress`, `stream_round_trip`, etc.

### MSAN

Fuzzing with `libFuzzer` and `MSAN` will require building a C++ standard library
and libFuzzer with MSAN.
`fuzz.py` respects the environment variables / flags `MSAN_EXTRA_CPPFLAGS`,
`MSAN_EXTRA_CFLAGS`, `MSAN_EXTRA_CXXFLAGS`, `MSAN_EXTRA_LDFLAGS` to easily pass
the extra parameters only for MSAN.

## AFL

The default `LIB_FUZZING_ENGINE` is `libregression.a`, which produces a binary
that AFL can use.

```
# Build the fuzz targets
CC=afl-clang CXX=afl-clang++ ./fuzz.py build all --enable-asan --enable-ubsan
# Run the fuzzer without a memory limit because of ASAN
./fuzz.py afl TARGET -m none
```

## Regression Testing

The regression rest supports the `all` target to run all the fuzzers in one
command.

```
CC=clang CXX=clang++ ./fuzz.py build all --enable-asan --enable-ubsan
./fuzz.py regression all
CC=clang CXX=clang++ ./fuzz.py build all --enable-msan
./fuzz.py regression all
```
