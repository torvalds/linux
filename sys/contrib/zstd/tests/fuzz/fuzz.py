#!/usr/bin/env python

# ################################################################
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ##########################################################################

import argparse
import contextlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


def abs_join(a, *p):
    return os.path.abspath(os.path.join(a, *p))


# Constants
FUZZ_DIR = os.path.abspath(os.path.dirname(__file__))
CORPORA_DIR = abs_join(FUZZ_DIR, 'corpora')
TARGETS = [
    'simple_round_trip',
    'stream_round_trip',
    'block_round_trip',
    'simple_decompress',
    'stream_decompress',
    'block_decompress',
]
ALL_TARGETS = TARGETS + ['all']
FUZZ_RNG_SEED_SIZE = 4

# Standard environment variables
CC = os.environ.get('CC', 'cc')
CXX = os.environ.get('CXX', 'c++')
CPPFLAGS = os.environ.get('CPPFLAGS', '')
CFLAGS = os.environ.get('CFLAGS', '-O3')
CXXFLAGS = os.environ.get('CXXFLAGS', CFLAGS)
LDFLAGS = os.environ.get('LDFLAGS', '')
MFLAGS = os.environ.get('MFLAGS', '-j')

# Fuzzing environment variables
LIB_FUZZING_ENGINE = os.environ.get('LIB_FUZZING_ENGINE', 'libregression.a')
AFL_FUZZ = os.environ.get('AFL_FUZZ', 'afl-fuzz')
DECODECORPUS = os.environ.get('DECODECORPUS',
                              abs_join(FUZZ_DIR, '..', 'decodecorpus'))

# Sanitizer environment variables
MSAN_EXTRA_CPPFLAGS = os.environ.get('MSAN_EXTRA_CPPFLAGS', '')
MSAN_EXTRA_CFLAGS = os.environ.get('MSAN_EXTRA_CFLAGS', '')
MSAN_EXTRA_CXXFLAGS = os.environ.get('MSAN_EXTRA_CXXFLAGS', '')
MSAN_EXTRA_LDFLAGS = os.environ.get('MSAN_EXTRA_LDFLAGS', '')


def create(r):
    d = os.path.abspath(r)
    if not os.path.isdir(d):
        os.mkdir(d)
    return d


def check(r):
    d = os.path.abspath(r)
    if not os.path.isdir(d):
        return None
    return d


@contextlib.contextmanager
def tmpdir():
    dirpath = tempfile.mkdtemp()
    try:
        yield dirpath
    finally:
        shutil.rmtree(dirpath, ignore_errors=True)


def parse_targets(in_targets):
    targets = set()
    for target in in_targets:
        if not target:
            continue
        if target == 'all':
            targets = targets.union(TARGETS)
        elif target in TARGETS:
            targets.add(target)
        else:
            raise RuntimeError('{} is not a valid target'.format(target))
    return list(targets)


def targets_parser(args, description):
    parser = argparse.ArgumentParser(prog=args.pop(0), description=description)
    parser.add_argument(
        'TARGET',
        nargs='*',
        type=str,
        help='Fuzz target(s) to build {{{}}}'.format(', '.join(ALL_TARGETS)))
    args, extra = parser.parse_known_args(args)
    args.extra = extra

    args.TARGET = parse_targets(args.TARGET)

    return args


def parse_env_flags(args, flags):
    """
    Look for flags set by environment variables.
    """
    san_flags = ','.join(re.findall('-fsanitize=((?:[a-z]+,?)+)', flags))
    nosan_flags = ','.join(re.findall('-fno-sanitize=((?:[a-z]+,?)+)', flags))

    def set_sanitizer(sanitizer, default, san, nosan):
        if sanitizer in san and sanitizer in nosan:
            raise RuntimeError('-fno-sanitize={s} and -fsanitize={s} passed'.
                               format(s=sanitizer))
        if sanitizer in san:
            return True
        if sanitizer in nosan:
            return False
        return default

    san = set(san_flags.split(','))
    nosan = set(nosan_flags.split(','))

    args.asan = set_sanitizer('address', args.asan, san, nosan)
    args.msan = set_sanitizer('memory', args.msan, san, nosan)
    args.ubsan = set_sanitizer('undefined', args.ubsan, san, nosan)

    args.sanitize = args.asan or args.msan or args.ubsan

    return args


def compiler_version(cc, cxx):
    """
    Determines the compiler and version.
    Only works for clang and gcc.
    """
    cc_version_bytes = subprocess.check_output([cc, "--version"])
    cxx_version_bytes = subprocess.check_output([cxx, "--version"])
    compiler = None
    version = None
    if b'clang' in cc_version_bytes:
        assert(b'clang' in cxx_version_bytes)
        compiler = 'clang'
    elif b'gcc' in cc_version_bytes:
        assert(b'gcc' in cxx_version_bytes)
        compiler = 'gcc'
    if compiler is not None:
        version_regex = b'([0-9])+\.([0-9])+\.([0-9])+'
        version_match = re.search(version_regex, cc_version_bytes)
        version = tuple(int(version_match.group(i)) for i in range(1, 4))
    return compiler, version


def overflow_ubsan_flags(cc, cxx):
    compiler, version = compiler_version(cc, cxx)
    if compiler == 'gcc':
        return ['-fno-sanitize=signed-integer-overflow']
    if compiler == 'clang' and version >= (5, 0, 0):
        return ['-fno-sanitize=pointer-overflow']
    return []


def build_parser(args):
    description = """
    Cleans the repository and builds a fuzz target (or all).
    Many flags default to environment variables (default says $X='y').
    Options that aren't enabling features default to the correct values for
    zstd.
    Enable sanitizers with --enable-*san.
    For regression testing just build.
    For libFuzzer set LIB_FUZZING_ENGINE and pass --enable-coverage.
    For AFL set CC and CXX to AFL's compilers and set
    LIB_FUZZING_ENGINE='libregression.a'.
    """
    parser = argparse.ArgumentParser(prog=args.pop(0), description=description)
    parser.add_argument(
        '--lib-fuzzing-engine',
        dest='lib_fuzzing_engine',
        type=str,
        default=LIB_FUZZING_ENGINE,
        help=('The fuzzing engine to use e.g. /path/to/libFuzzer.a '
              "(default: $LIB_FUZZING_ENGINE='{})".format(LIB_FUZZING_ENGINE)))
    parser.add_argument(
        '--enable-coverage',
        dest='coverage',
        action='store_true',
        help='Enable coverage instrumentation (-fsanitize-coverage)')
    parser.add_argument(
        '--enable-asan', dest='asan', action='store_true', help='Enable UBSAN')
    parser.add_argument(
        '--enable-ubsan',
        dest='ubsan',
        action='store_true',
        help='Enable UBSAN')
    parser.add_argument(
        '--enable-ubsan-pointer-overflow',
        dest='ubsan_pointer_overflow',
        action='store_true',
        help='Enable UBSAN pointer overflow check (known failure)')
    parser.add_argument(
        '--enable-msan', dest='msan', action='store_true', help='Enable MSAN')
    parser.add_argument(
        '--enable-msan-track-origins', dest='msan_track_origins',
        action='store_true', help='Enable MSAN origin tracking')
    parser.add_argument(
        '--msan-extra-cppflags',
        dest='msan_extra_cppflags',
        type=str,
        default=MSAN_EXTRA_CPPFLAGS,
        help="Extra CPPFLAGS for MSAN (default: $MSAN_EXTRA_CPPFLAGS='{}')".
        format(MSAN_EXTRA_CPPFLAGS))
    parser.add_argument(
        '--msan-extra-cflags',
        dest='msan_extra_cflags',
        type=str,
        default=MSAN_EXTRA_CFLAGS,
        help="Extra CFLAGS for MSAN (default: $MSAN_EXTRA_CFLAGS='{}')".format(
            MSAN_EXTRA_CFLAGS))
    parser.add_argument(
        '--msan-extra-cxxflags',
        dest='msan_extra_cxxflags',
        type=str,
        default=MSAN_EXTRA_CXXFLAGS,
        help="Extra CXXFLAGS for MSAN (default: $MSAN_EXTRA_CXXFLAGS='{}')".
        format(MSAN_EXTRA_CXXFLAGS))
    parser.add_argument(
        '--msan-extra-ldflags',
        dest='msan_extra_ldflags',
        type=str,
        default=MSAN_EXTRA_LDFLAGS,
        help="Extra LDFLAGS for MSAN (default: $MSAN_EXTRA_LDFLAGS='{}')".
        format(MSAN_EXTRA_LDFLAGS))
    parser.add_argument(
        '--enable-sanitize-recover',
        dest='sanitize_recover',
        action='store_true',
        help='Non-fatal sanitizer errors where possible')
    parser.add_argument(
        '--debug',
        dest='debug',
        type=int,
        default=1,
        help='Set DEBUGLEVEL (default: 1)')
    parser.add_argument(
        '--force-memory-access',
        dest='memory_access',
        type=int,
        default=0,
        help='Set MEM_FORCE_MEMORY_ACCESS (default: 0)')
    parser.add_argument(
        '--fuzz-rng-seed-size',
        dest='fuzz_rng_seed_size',
        type=int,
        default=4,
        help='Set FUZZ_RNG_SEED_SIZE (default: 4)')
    parser.add_argument(
        '--disable-fuzzing-mode',
        dest='fuzzing_mode',
        action='store_false',
        help='Do not define FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION')
    parser.add_argument(
        '--enable-stateful-fuzzing',
        dest='stateful_fuzzing',
        action='store_true',
        help='Reuse contexts between runs (makes reproduction impossible)')
    parser.add_argument(
        '--cc',
        dest='cc',
        type=str,
        default=CC,
        help="CC (default: $CC='{}')".format(CC))
    parser.add_argument(
        '--cxx',
        dest='cxx',
        type=str,
        default=CXX,
        help="CXX (default: $CXX='{}')".format(CXX))
    parser.add_argument(
        '--cppflags',
        dest='cppflags',
        type=str,
        default=CPPFLAGS,
        help="CPPFLAGS (default: $CPPFLAGS='{}')".format(CPPFLAGS))
    parser.add_argument(
        '--cflags',
        dest='cflags',
        type=str,
        default=CFLAGS,
        help="CFLAGS (default: $CFLAGS='{}')".format(CFLAGS))
    parser.add_argument(
        '--cxxflags',
        dest='cxxflags',
        type=str,
        default=CXXFLAGS,
        help="CXXFLAGS (default: $CXXFLAGS='{}')".format(CXXFLAGS))
    parser.add_argument(
        '--ldflags',
        dest='ldflags',
        type=str,
        default=LDFLAGS,
        help="LDFLAGS (default: $LDFLAGS='{}')".format(LDFLAGS))
    parser.add_argument(
        '--mflags',
        dest='mflags',
        type=str,
        default=MFLAGS,
        help="Extra Make flags (default: $MFLAGS='{}')".format(MFLAGS))
    parser.add_argument(
        'TARGET',
        nargs='*',
        type=str,
        help='Fuzz target(s) to build {{{}}}'.format(', '.join(ALL_TARGETS))
    )
    args = parser.parse_args(args)
    args = parse_env_flags(args, ' '.join(
        [args.cppflags, args.cflags, args.cxxflags, args.ldflags]))

    # Check option sanitiy
    if args.msan and (args.asan or args.ubsan):
        raise RuntimeError('MSAN may not be used with any other sanitizers')
    if args.msan_track_origins and not args.msan:
        raise RuntimeError('--enable-msan-track-origins requires MSAN')
    if args.ubsan_pointer_overflow and not args.ubsan:
        raise RuntimeError('--enable-ubsan-pointer-overlow requires UBSAN')
    if args.sanitize_recover and not args.sanitize:
        raise RuntimeError('--enable-sanitize-recover but no sanitizers used')

    return args


def build(args):
    try:
        args = build_parser(args)
    except Exception as e:
        print(e)
        return 1
    # The compilation flags we are setting
    targets = args.TARGET
    cc = args.cc
    cxx = args.cxx
    cppflags = shlex.split(args.cppflags)
    cflags = shlex.split(args.cflags)
    ldflags = shlex.split(args.ldflags)
    cxxflags = shlex.split(args.cxxflags)
    mflags = shlex.split(args.mflags)
    # Flags to be added to both cflags and cxxflags
    common_flags = []

    cppflags += [
        '-DDEBUGLEVEL={}'.format(args.debug),
        '-DMEM_FORCE_MEMORY_ACCESS={}'.format(args.memory_access),
        '-DFUZZ_RNG_SEED_SIZE={}'.format(args.fuzz_rng_seed_size),
    ]

    mflags += ['LIB_FUZZING_ENGINE={}'.format(args.lib_fuzzing_engine)]

    # Set flags for options
    if args.coverage:
        common_flags += [
            '-fsanitize-coverage=trace-pc-guard,indirect-calls,trace-cmp'
        ]

    if args.sanitize_recover:
        recover_flags = ['-fsanitize-recover=all']
    else:
        recover_flags = ['-fno-sanitize-recover=all']
    if args.sanitize:
        common_flags += recover_flags

    if args.msan:
        msan_flags = ['-fsanitize=memory']
        if args.msan_track_origins:
            msan_flags += ['-fsanitize-memory-track-origins']
        common_flags += msan_flags
        # Append extra MSAN flags (it might require special setup)
        cppflags += [args.msan_extra_cppflags]
        cflags += [args.msan_extra_cflags]
        cxxflags += [args.msan_extra_cxxflags]
        ldflags += [args.msan_extra_ldflags]

    if args.asan:
        common_flags += ['-fsanitize=address']

    if args.ubsan:
        ubsan_flags = ['-fsanitize=undefined']
        if not args.ubsan_pointer_overflow:
            ubsan_flags += overflow_ubsan_flags(cc, cxx)
        common_flags += ubsan_flags

    if args.stateful_fuzzing:
        cppflags += ['-DSTATEFUL_FUZZING']

    if args.fuzzing_mode:
        cppflags += ['-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION']

    if args.lib_fuzzing_engine == 'libregression.a':
        targets = ['libregression.a'] + targets

    # Append the common flags
    cflags += common_flags
    cxxflags += common_flags

    # Prepare the flags for Make
    cc_str = "CC={}".format(cc)
    cxx_str = "CXX={}".format(cxx)
    cppflags_str = "CPPFLAGS={}".format(' '.join(cppflags))
    cflags_str = "CFLAGS={}".format(' '.join(cflags))
    cxxflags_str = "CXXFLAGS={}".format(' '.join(cxxflags))
    ldflags_str = "LDFLAGS={}".format(' '.join(ldflags))

    # Print the flags
    print('MFLAGS={}'.format(' '.join(mflags)))
    print(cc_str)
    print(cxx_str)
    print(cppflags_str)
    print(cflags_str)
    print(cxxflags_str)
    print(ldflags_str)

    # Clean and build
    clean_cmd = ['make', 'clean'] + mflags
    print(' '.join(clean_cmd))
    subprocess.check_call(clean_cmd)
    build_cmd = [
        'make',
        cc_str,
        cxx_str,
        cppflags_str,
        cflags_str,
        cxxflags_str,
        ldflags_str,
    ] + mflags + targets
    print(' '.join(build_cmd))
    subprocess.check_call(build_cmd)
    return 0


def libfuzzer_parser(args):
    description = """
    Runs a libfuzzer binary.
    Passes all extra arguments to libfuzzer.
    The fuzzer should have been build with LIB_FUZZING_ENGINE pointing to
    libFuzzer.a.
    Generates output in the CORPORA directory, puts crashes in the ARTIFACT
    directory, and takes extra input from the SEED directory.
    To merge AFL's output pass the SEED as AFL's output directory and pass
    '-merge=1'.
    """
    parser = argparse.ArgumentParser(prog=args.pop(0), description=description)
    parser.add_argument(
        '--corpora',
        type=str,
        help='Override the default corpora dir (default: {})'.format(
            abs_join(CORPORA_DIR, 'TARGET')))
    parser.add_argument(
        '--artifact',
        type=str,
        help='Override the default artifact dir (default: {})'.format(
            abs_join(CORPORA_DIR, 'TARGET-crash')))
    parser.add_argument(
        '--seed',
        type=str,
        help='Override the default seed dir (default: {})'.format(
            abs_join(CORPORA_DIR, 'TARGET-seed')))
    parser.add_argument(
        'TARGET',
        type=str,
        help='Fuzz target(s) to build {{{}}}'.format(', '.join(TARGETS)))
    args, extra = parser.parse_known_args(args)
    args.extra = extra

    if args.TARGET and args.TARGET not in TARGETS:
        raise RuntimeError('{} is not a valid target'.format(args.TARGET))

    return args


def libfuzzer(target, corpora=None, artifact=None, seed=None, extra_args=None):
    if corpora is None:
        corpora = abs_join(CORPORA_DIR, target)
    if artifact is None:
        artifact = abs_join(CORPORA_DIR, '{}-crash'.format(target))
    if seed is None:
        seed = abs_join(CORPORA_DIR, '{}-seed'.format(target))
    if extra_args is None:
        extra_args = []

    target = abs_join(FUZZ_DIR, target)

    corpora = [create(corpora)]
    artifact = create(artifact)
    seed = check(seed)

    corpora += [artifact]
    if seed is not None:
        corpora += [seed]

    cmd = [target, '-artifact_prefix={}/'.format(artifact)]
    cmd += corpora + extra_args
    print(' '.join(cmd))
    subprocess.check_call(cmd)


def libfuzzer_cmd(args):
    try:
        args = libfuzzer_parser(args)
    except Exception as e:
        print(e)
        return 1
    libfuzzer(args.TARGET, args.corpora, args.artifact, args.seed, args.extra)
    return 0


def afl_parser(args):
    description = """
    Runs an afl-fuzz job.
    Passes all extra arguments to afl-fuzz.
    The fuzzer should have been built with CC/CXX set to the AFL compilers,
    and with LIB_FUZZING_ENGINE='libregression.a'.
    Takes input from CORPORA and writes output to OUTPUT.
    Uses AFL_FUZZ as the binary (set from flag or environment variable).
    """
    parser = argparse.ArgumentParser(prog=args.pop(0), description=description)
    parser.add_argument(
        '--corpora',
        type=str,
        help='Override the default corpora dir (default: {})'.format(
            abs_join(CORPORA_DIR, 'TARGET')))
    parser.add_argument(
        '--output',
        type=str,
        help='Override the default AFL output dir (default: {})'.format(
            abs_join(CORPORA_DIR, 'TARGET-afl')))
    parser.add_argument(
        '--afl-fuzz',
        type=str,
        default=AFL_FUZZ,
        help='AFL_FUZZ (default: $AFL_FUZZ={})'.format(AFL_FUZZ))
    parser.add_argument(
        'TARGET',
        type=str,
        help='Fuzz target(s) to build {{{}}}'.format(', '.join(TARGETS)))
    args, extra = parser.parse_known_args(args)
    args.extra = extra

    if args.TARGET and args.TARGET not in TARGETS:
        raise RuntimeError('{} is not a valid target'.format(args.TARGET))

    if not args.corpora:
        args.corpora = abs_join(CORPORA_DIR, args.TARGET)
    if not args.output:
        args.output = abs_join(CORPORA_DIR, '{}-afl'.format(args.TARGET))

    return args


def afl(args):
    try:
        args = afl_parser(args)
    except Exception as e:
        print(e)
        return 1
    target = abs_join(FUZZ_DIR, args.TARGET)

    corpora = create(args.corpora)
    output = create(args.output)

    cmd = [args.afl_fuzz, '-i', corpora, '-o', output] + args.extra
    cmd += [target, '@@']
    print(' '.join(cmd))
    subprocess.call(cmd)
    return 0


def regression(args):
    try:
        description = """
        Runs one or more regression tests.
        The fuzzer should have been built with with
        LIB_FUZZING_ENGINE='libregression.a'.
        Takes input from CORPORA.
        """
        args = targets_parser(args, description)
    except Exception as e:
        print(e)
        return 1
    for target in args.TARGET:
        corpora = create(abs_join(CORPORA_DIR, target))
        target = abs_join(FUZZ_DIR, target)
        cmd = [target, corpora]
        print(' '.join(cmd))
        subprocess.check_call(cmd)
    return 0


def gen_parser(args):
    description = """
    Generate a seed corpus appropiate for TARGET with data generated with
    decodecorpus.
    The fuzz inputs are prepended with a seed before the zstd data, so the
    output of decodecorpus shouldn't be used directly.
    Generates NUMBER samples prepended with FUZZ_RNG_SEED_SIZE random bytes and
    puts the output in SEED.
    DECODECORPUS is the decodecorpus binary, and must already be built.
    """
    parser = argparse.ArgumentParser(prog=args.pop(0), description=description)
    parser.add_argument(
        '--number',
        '-n',
        type=int,
        default=100,
        help='Number of samples to generate')
    parser.add_argument(
        '--max-size-log',
        type=int,
        default=13,
        help='Maximum sample size to generate')
    parser.add_argument(
        '--seed',
        type=str,
        help='Override the default seed dir (default: {})'.format(
            abs_join(CORPORA_DIR, 'TARGET-seed')))
    parser.add_argument(
        '--decodecorpus',
        type=str,
        default=DECODECORPUS,
        help="decodecorpus binary (default: $DECODECORPUS='{}')".format(
            DECODECORPUS))
    parser.add_argument(
        '--fuzz-rng-seed-size',
        type=int,
        default=4,
        help="FUZZ_RNG_SEED_SIZE used for generate the samples (must match)"
    )
    parser.add_argument(
        'TARGET',
        type=str,
        help='Fuzz target(s) to build {{{}}}'.format(', '.join(TARGETS)))
    args, extra = parser.parse_known_args(args)
    args.extra = extra

    if args.TARGET and args.TARGET not in TARGETS:
        raise RuntimeError('{} is not a valid target'.format(args.TARGET))

    if not args.seed:
        args.seed = abs_join(CORPORA_DIR, '{}-seed'.format(args.TARGET))

    if not os.path.isfile(args.decodecorpus):
        raise RuntimeError("{} is not a file run 'make -C {} decodecorpus'".
                           format(args.decodecorpus, abs_join(FUZZ_DIR, '..')))

    return args


def gen(args):
    try:
        args = gen_parser(args)
    except Exception as e:
        print(e)
        return 1

    seed = create(args.seed)
    with tmpdir() as compressed:
        with tmpdir() as decompressed:
            cmd = [
                args.decodecorpus,
                '-n{}'.format(args.number),
                '-p{}/'.format(compressed),
                '-o{}'.format(decompressed),
            ]

            if 'block_' in args.TARGET:
                cmd += [
                    '--gen-blocks',
                    '--max-block-size-log={}'.format(args.max_size_log)
                ]
            else:
                cmd += ['--max-content-size-log={}'.format(args.max_size_log)]

            print(' '.join(cmd))
            subprocess.check_call(cmd)

            if '_round_trip' in args.TARGET:
                print('using decompressed data in {}'.format(decompressed))
                samples = decompressed
            elif '_decompress' in args.TARGET:
                print('using compressed data in {}'.format(compressed))
                samples = compressed

            # Copy the samples over and prepend the RNG seeds
            for name in os.listdir(samples):
                samplename = abs_join(samples, name)
                outname = abs_join(seed, name)
                rng_seed = os.urandom(args.fuzz_rng_seed_size)
                with open(samplename, 'rb') as sample:
                    with open(outname, 'wb') as out:
                        out.write(rng_seed)
                        CHUNK_SIZE = 131072
                        chunk = sample.read(CHUNK_SIZE)
                        while len(chunk) > 0:
                            out.write(chunk)
                            chunk = sample.read(CHUNK_SIZE)
    return 0


def minimize(args):
    try:
        description = """
        Runs a libfuzzer fuzzer with -merge=1 to build a minimal corpus in
        TARGET_seed_corpus. All extra args are passed to libfuzzer.
        """
        args = targets_parser(args, description)
    except Exception as e:
        print(e)
        return 1

    for target in args.TARGET:
        # Merge the corpus + anything else into the seed_corpus
        corpus = abs_join(CORPORA_DIR, target)
        seed_corpus = abs_join(CORPORA_DIR, "{}_seed_corpus".format(target))
        extra_args = [corpus, "-merge=1"] + args.extra
        libfuzzer(target, corpora=seed_corpus, extra_args=extra_args)
        seeds = set(os.listdir(seed_corpus))
        # Copy all crashes directly into the seed_corpus if not already present
        crashes = abs_join(CORPORA_DIR, '{}-crash'.format(target))
        for crash in os.listdir(crashes):
            if crash not in seeds:
                shutil.copy(abs_join(crashes, crash), seed_corpus)
                seeds.add(crash)


def zip_cmd(args):
    try:
        description = """
        Zips up the seed corpus.
        """
        args = targets_parser(args, description)
    except Exception as e:
        print(e)
        return 1

    for target in args.TARGET:
        # Zip the seed_corpus
        seed_corpus = abs_join(CORPORA_DIR, "{}_seed_corpus".format(target))
        zip_file = "{}.zip".format(seed_corpus)
        cmd = ["zip", "-r", "-q", "-j", "-9", zip_file, "."]
        print(' '.join(cmd))
        subprocess.check_call(cmd, cwd=seed_corpus)


def list_cmd(args):
    print("\n".join(TARGETS))


def short_help(args):
    name = args[0]
    print("Usage: {} [OPTIONS] COMMAND [ARGS]...\n".format(name))


def help(args):
    short_help(args)
    print("\tfuzzing helpers (select a command and pass -h for help)\n")
    print("Options:")
    print("\t-h, --help\tPrint this message")
    print("")
    print("Commands:")
    print("\tbuild\t\tBuild a fuzzer")
    print("\tlibfuzzer\tRun a libFuzzer fuzzer")
    print("\tafl\t\tRun an AFL fuzzer")
    print("\tregression\tRun a regression test")
    print("\tgen\t\tGenerate a seed corpus for a fuzzer")
    print("\tminimize\tMinimize the test corpora")
    print("\tzip\t\tZip the minimized corpora up")
    print("\tlist\t\tList the available targets")


def main():
    args = sys.argv
    if len(args) < 2:
        help(args)
        return 1
    if args[1] == '-h' or args[1] == '--help' or args[1] == '-H':
        help(args)
        return 1
    command = args.pop(1)
    args[0] = "{} {}".format(args[0], command)
    if command == "build":
        return build(args)
    if command == "libfuzzer":
        return libfuzzer_cmd(args)
    if command == "regression":
        return regression(args)
    if command == "afl":
        return afl(args)
    if command == "gen":
        return gen(args)
    if command == "minimize":
        return minimize(args)
    if command == "zip":
        return zip_cmd(args)
    if command == "list":
        return list_cmd(args)
    short_help(args)
    print("Error: No such command {} (pass -h for help)".format(command))
    return 1


if __name__ == "__main__":
    sys.exit(main())
