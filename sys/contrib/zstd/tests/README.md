Programs and scripts for automated testing of Zstandard
=======================================================

This directory contains the following programs and scripts:
- `datagen` : Synthetic and parametrable data generator, for tests
- `fullbench`  : Precisely measure speed for each zstd inner functions
- `fuzzer`  : Test tool, to check zstd integrity on target platform
- `paramgrill` : parameter tester for zstd
- `test-zstd-speed.py` : script for testing zstd speed difference between commits
- `test-zstd-versions.py` : compatibility test between zstd versions stored on Github (v0.1+)
- `zbufftest`  : Test tool to check ZBUFF (a buffered streaming API) integrity
- `zstreamtest` : Fuzzer test tool for zstd streaming API
- `legacy` : Test tool to test decoding of legacy zstd frames
- `decodecorpus` : Tool to generate valid Zstandard frames, for verifying decoder implementations


#### `test-zstd-versions.py` - script for testing zstd interoperability between versions

This script creates `versionsTest` directory to which zstd repository is cloned.
Then all tagged (released) versions of zstd are compiled.
In the following step interoperability between zstd versions is checked.


#### `test-zstd-speed.py` - script for testing zstd speed difference between commits

This script creates `speedTest` directory to which zstd repository is cloned.
Then it compiles all branches of zstd and performs a speed benchmark for a given list of files (the `testFileNames` parameter).
After `sleepTime` (an optional parameter, default 300 seconds) seconds the script checks repository for new commits.
If a new commit is found it is compiled and a speed benchmark for this commit is performed.
The results of the speed benchmark are compared to the previous results.
If compression or decompression speed for one of zstd levels is lower than `lowerLimit` (an optional parameter, default 0.98) the speed benchmark is restarted.
If second results are also lower than `lowerLimit` the warning e-mail is send to recipients from the list (the `emails` parameter).

Additional remarks:
- To be sure that speed results are accurate the script should be run on a "stable" target system with no other jobs running in parallel
- Using the script with virtual machines can lead to large variations of speed results
- The speed benchmark is not performed until computers' load average is lower than `maxLoadAvg` (an optional parameter, default 0.75)
- The script sends e-mails using `mutt`; if `mutt` is not available it sends e-mails without attachments using `mail`; if both are not available it only prints a warning


The example usage with two test files, one e-mail address, and with an additional message:
```
./test-zstd-speed.py "silesia.tar calgary.tar" "email@gmail.com" --message "tested on my laptop" --sleepTime 60
```

To run the script in background please use:
```
nohup ./test-zstd-speed.py testFileNames emails &
```

The full list of parameters:
```
positional arguments:
  testFileNames         file names list for speed benchmark
  emails                list of e-mail addresses to send warnings

optional arguments:
  -h, --help            show this help message and exit
  --message MESSAGE     attach an additional message to e-mail
  --lowerLimit LOWERLIMIT
                        send email if speed is lower than given limit
  --maxLoadAvg MAXLOADAVG
                        maximum load average to start testing
  --lastCLevel LASTCLEVEL
                        last compression level for testing
  --sleepTime SLEEPTIME
                        frequency of repository checking in seconds
```

#### `decodecorpus` - tool to generate Zstandard frames for decoder testing
Command line tool to generate test .zst files.

This tool will generate .zst files with checksums,
as well as optionally output the corresponding correct uncompressed data for
extra verfication.

Example:
```
./decodecorpus -ptestfiles -otestfiles -n10000 -s5
```
will generate 10,000 sample .zst files using a seed of 5 in the `testfiles` directory,
with the zstd checksum field set,
as well as the 10,000 original files for more detailed comparison of decompression results.

```
./decodecorpus -t -T1mn
```
will choose a random seed, and for 1 minute,
generate random test frames and ensure that the
zstd library correctly decompresses them in both simple and streaming modes.

#### `paramgrill` - tool for generating compression table parameters and optimizing parameters on file given constraints

Full list of arguments
```
 -T#          : set level 1 speed objective
 -B#          : cut input into blocks of size # (default : single block)
 -S           : benchmarks a single run (example command: -Sl3w10h12)
    w# - windowLog
    h# - hashLog
    c# - chainLog
    s# - searchLog
    l# - minMatch
    t# - targetLength
    S# - strategy
    L# - level
 --zstd=      : Single run, parameter selection syntax same as zstdcli with more parameters
                    (Added forceAttachDictionary / fadt)
                    When invoked with --optimize, this represents the sample to exceed.
 --optimize=  : find parameters to maximize compression ratio given parameters
                    Can use all --zstd= commands to constrain the type of solution found in addition to the following constraints
    cSpeed=   : Minimum compression speed
    dSpeed=   : Minimum decompression speed
    cMem=     : Maximum compression memory
    lvl=      : Searches for solutions which are strictly better than that compression lvl in ratio and cSpeed,
    stc=      : When invoked with lvl=, represents percentage slack in ratio/cSpeed allowed for a solution to be considered (Default 100%)
              : In normal operation, represents percentage slack in choosing viable starting strategy selection in choosing the default parameters
                    (Lower value will begin with stronger strategies) (Default 90%)
    speedRatio=   (accepts decimals)
              : determines value of gains in speed vs gains in ratio
                    when determining overall winner (default 5 (1% ratio = 5% speed)).
    tries=    : Maximum number of random restarts on a single strategy before switching (Default 5)
                    Higher values will make optimizer run longer, more chances to find better solution.
    memLog    : Limits the log of the size of each memotable (1 per strategy). Will use hash tables when state space is larger than max size.
                    Setting memLog = 0 turns off memoization
 --display=   : specifiy which parameters are included in the output
                    can use all --zstd parameter names and 'cParams' as a shorthand for all parameters used in ZSTD_compressionParameters
                    (Default: display all params available)
 -P#          : generated sample compressibility (when no file is provided)
 -t#          : Caps runtime of operation in seconds (default : 99999 seconds (about 27 hours ))
 -v           : Prints Benchmarking output
 -D           : Next argument dictionary file
 -s           : Benchmark all files separately
 -q           : Quiet, repeat for more quiet
                  -q Prints parameters + results whenever a new best is found
                  -qq Only prints parameters whenever a new best is found, prints final parameters + results
                  -qqq Only print final parameters + results
                  -qqqq Only prints final parameter set in the form --zstd=
 -v           : Verbose, cancels quiet, repeat for more volume
                  -v Prints all candidate parameters and results

```
 Any inputs afterwards are treated as files to benchmark.
