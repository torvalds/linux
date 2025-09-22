# ===- perf-helper.py - Clang Python Bindings -----------------*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#

from __future__ import absolute_import, division, print_function

import sys
import os
import subprocess
import argparse
import time
import bisect
import shlex
import tempfile

test_env = {"PATH": os.environ["PATH"]}


def findFilesWithExtension(path, extension):
    filenames = []
    for root, dirs, files in os.walk(path):
        for filename in files:
            if filename.endswith(f".{extension}"):
                filenames.append(os.path.join(root, filename))
    return filenames


def clean(args):
    if len(args) < 2:
        print(
            "Usage: %s clean <paths> <extension>\n" % __file__
            + "\tRemoves all files with extension from <path>."
        )
        return 1
    for path in args[0:-1]:
        for filename in findFilesWithExtension(path, args[-1]):
            os.remove(filename)
    return 0


def merge(args):
    if len(args) < 3:
        print(
            "Usage: %s merge <llvm-profdata> <output> <paths>\n" % __file__
            + "\tMerges all profraw files from path into output."
        )
        return 1
    cmd = [args[0], "merge", "-o", args[1]]
    for path in args[2:]:
        cmd.extend(findFilesWithExtension(path, "profraw"))
    subprocess.check_call(cmd)
    return 0


def merge_fdata(args):
    if len(args) != 3:
        print(
            "Usage: %s merge-fdata <merge-fdata> <output> <path>\n" % __file__
            + "\tMerges all fdata files from path into output."
        )
        return 1
    cmd = [args[0], "-o", args[1]]
    cmd.extend(findFilesWithExtension(args[2], "fdata"))
    subprocess.check_call(cmd)
    return 0


def perf(args):
    parser = argparse.ArgumentParser(
        prog="perf-helper perf", description="perf wrapper for BOLT profile collection"
    )
    parser.add_argument(
        "--lbr", action="store_true", help="Use perf with branch stacks"
    )
    parser.add_argument("cmd", nargs=argparse.REMAINDER, help="")

    opts = parser.parse_args(args)
    cmd = opts.cmd[1:]

    perf_args = [
        "perf",
        "record",
        "--event=cycles:u",
        "--freq=max",
        "--output=%d.perf.data" % os.getpid(),
    ]
    if opts.lbr:
        perf_args += ["--branch-filter=any,u"]
    perf_args.extend(cmd)

    start_time = time.time()
    subprocess.check_call(perf_args)

    elapsed = time.time() - start_time
    print("... data collection took %.4fs" % elapsed)
    return 0


def perf2bolt(args):
    parser = argparse.ArgumentParser(
        prog="perf-helper perf2bolt",
        description="perf2bolt conversion wrapper for perf.data files",
    )
    parser.add_argument("bolt", help="Path to llvm-bolt")
    parser.add_argument("path", help="Path containing perf.data files")
    parser.add_argument("binary", help="Input binary")
    parser.add_argument("--lbr", action="store_true", help="Use LBR perf2bolt mode")
    opts = parser.parse_args(args)

    p2b_args = [
        opts.bolt,
        opts.binary,
        "--aggregate-only",
        "--profile-format=yaml",
    ]
    if not opts.lbr:
        p2b_args += ["-nl"]
    p2b_args += ["-p"]
    for filename in findFilesWithExtension(opts.path, "perf.data"):
        subprocess.check_call(p2b_args + [filename, "-o", filename + ".fdata"])
    return 0


def dtrace(args):
    parser = argparse.ArgumentParser(
        prog="perf-helper dtrace",
        description="dtrace wrapper for order file generation",
    )
    parser.add_argument(
        "--buffer-size",
        metavar="size",
        type=int,
        required=False,
        default=1,
        help="dtrace buffer size in MB (default 1)",
    )
    parser.add_argument(
        "--use-oneshot",
        required=False,
        action="store_true",
        help="Use dtrace's oneshot probes",
    )
    parser.add_argument(
        "--use-ustack",
        required=False,
        action="store_true",
        help="Use dtrace's ustack to print function names",
    )
    parser.add_argument(
        "--cc1",
        required=False,
        action="store_true",
        help="Execute cc1 directly (don't profile the driver)",
    )
    parser.add_argument("cmd", nargs="*", help="")

    # Use python's arg parser to handle all leading option arguments, but pass
    # everything else through to dtrace
    first_cmd = next(arg for arg in args if not arg.startswith("--"))
    last_arg_idx = args.index(first_cmd)

    opts = parser.parse_args(args[:last_arg_idx])
    cmd = args[last_arg_idx:]

    if opts.cc1:
        cmd = get_cc1_command_for_args(cmd, test_env)

    if opts.use_oneshot:
        target = "oneshot$target:::entry"
    else:
        target = "pid$target:::entry"
    predicate = '%s/probemod=="%s"/' % (target, os.path.basename(cmd[0]))
    log_timestamp = 'printf("dtrace-TS: %d\\n", timestamp)'
    if opts.use_ustack:
        action = "ustack(1);"
    else:
        action = 'printf("dtrace-Symbol: %s\\n", probefunc);'
    dtrace_script = "%s { %s; %s }" % (predicate, log_timestamp, action)

    dtrace_args = []
    if not os.geteuid() == 0:
        print(
            "Script must be run as root, or you must add the following to your sudoers:"
            + "%%admin ALL=(ALL) NOPASSWD: /usr/sbin/dtrace"
        )
        dtrace_args.append("sudo")

    dtrace_args.extend(
        (
            "dtrace",
            "-xevaltime=exec",
            "-xbufsize=%dm" % (opts.buffer_size),
            "-q",
            "-n",
            dtrace_script,
            "-c",
            " ".join(cmd),
        )
    )

    if sys.platform == "darwin":
        dtrace_args.append("-xmangled")

    start_time = time.time()

    with open("%d.dtrace" % os.getpid(), "w") as f:
        f.write("### Command: %s" % dtrace_args)
        subprocess.check_call(dtrace_args, stdout=f, stderr=subprocess.PIPE)

    elapsed = time.time() - start_time
    print("... data collection took %.4fs" % elapsed)

    return 0


def get_cc1_command_for_args(cmd, env):
    # Find the cc1 command used by the compiler. To do this we execute the
    # compiler with '-###' to figure out what it wants to do.
    cmd = cmd + ["-###"]
    cc_output = subprocess.check_output(
        cmd, stderr=subprocess.STDOUT, env=env, universal_newlines=True
    ).strip()
    cc_commands = []
    for ln in cc_output.split("\n"):
        # Filter out known garbage.
        if (
            ln == "Using built-in specs."
            or ln.startswith("Configured with:")
            or ln.startswith("Target:")
            or ln.startswith("Thread model:")
            or ln.startswith("InstalledDir:")
            or ln.startswith("LLVM Profile Note")
            or ln.startswith(" (in-process)")
            or " version " in ln
        ):
            continue
        cc_commands.append(ln)

    if len(cc_commands) != 1:
        print("Fatal error: unable to determine cc1 command: %r" % cc_output)
        exit(1)

    cc1_cmd = shlex.split(cc_commands[0])
    if not cc1_cmd:
        print("Fatal error: unable to determine cc1 command: %r" % cc_output)
        exit(1)

    return cc1_cmd


def cc1(args):
    parser = argparse.ArgumentParser(
        prog="perf-helper cc1", description="cc1 wrapper for order file generation"
    )
    parser.add_argument("cmd", nargs="*", help="")

    # Use python's arg parser to handle all leading option arguments, but pass
    # everything else through to dtrace
    first_cmd = next(arg for arg in args if not arg.startswith("--"))
    last_arg_idx = args.index(first_cmd)

    opts = parser.parse_args(args[:last_arg_idx])
    cmd = args[last_arg_idx:]

    # clear the profile file env, so that we don't generate profdata
    # when capturing the cc1 command
    cc1_env = test_env
    cc1_env["LLVM_PROFILE_FILE"] = os.devnull
    cc1_cmd = get_cc1_command_for_args(cmd, cc1_env)

    subprocess.check_call(cc1_cmd)
    return 0


def parse_dtrace_symbol_file(path, all_symbols, all_symbols_set, missing_symbols, opts):
    def fix_mangling(symbol):
        if sys.platform == "darwin":
            if symbol[0] != "_" and symbol != "start":
                symbol = "_" + symbol
        return symbol

    def get_symbols_with_prefix(symbol):
        start_index = bisect.bisect_left(all_symbols, symbol)
        for s in all_symbols[start_index:]:
            if not s.startswith(symbol):
                break
            yield s

    # Extract the list of symbols from the given file, which is assumed to be
    # the output of a dtrace run logging either probefunc or ustack(1) and
    # nothing else. The dtrace -xdemangle option needs to be used.
    #
    # This is particular to OS X at the moment, because of the '_' handling.
    with open(path) as f:
        current_timestamp = None
        for ln in f:
            # Drop leading and trailing whitespace.
            ln = ln.strip()
            if not ln.startswith("dtrace-"):
                continue

            # If this is a timestamp specifier, extract it.
            if ln.startswith("dtrace-TS: "):
                _, data = ln.split(": ", 1)
                if not data.isdigit():
                    print(
                        "warning: unrecognized timestamp line %r, ignoring" % ln,
                        file=sys.stderr,
                    )
                    continue
                current_timestamp = int(data)
                continue
            elif ln.startswith("dtrace-Symbol: "):

                _, ln = ln.split(": ", 1)
                if not ln:
                    continue

                # If there is a '`' in the line, assume it is a ustack(1) entry in
                # the form of <modulename>`<modulefunc>, where <modulefunc> is never
                # truncated (but does need the mangling patched).
                if "`" in ln:
                    yield (current_timestamp, fix_mangling(ln.split("`", 1)[1]))
                    continue

                # Otherwise, assume this is a probefunc printout. DTrace on OS X
                # seems to have a bug where it prints the mangled version of symbols
                # which aren't C++ mangled. We just add a '_' to anything but start
                # which doesn't already have a '_'.
                symbol = fix_mangling(ln)

                # If we don't know all the symbols, or the symbol is one of them,
                # just return it.
                if not all_symbols_set or symbol in all_symbols_set:
                    yield (current_timestamp, symbol)
                    continue

                # Otherwise, we have a symbol name which isn't present in the
                # binary. We assume it is truncated, and try to extend it.

                # Get all the symbols with this prefix.
                possible_symbols = list(get_symbols_with_prefix(symbol))
                if not possible_symbols:
                    continue

                # If we found too many possible symbols, ignore this as a prefix.
                if len(possible_symbols) > 100:
                    print(
                        "warning: ignoring symbol %r " % symbol
                        + "(no match and too many possible suffixes)",
                        file=sys.stderr,
                    )
                    continue

                # Report that we resolved a missing symbol.
                if opts.show_missing_symbols and symbol not in missing_symbols:
                    print(
                        "warning: resolved missing symbol %r" % symbol, file=sys.stderr
                    )
                    missing_symbols.add(symbol)

                # Otherwise, treat all the possible matches as having occurred. This
                # is an over-approximation, but it should be ok in practice.
                for s in possible_symbols:
                    yield (current_timestamp, s)


def uniq(list):
    seen = set()
    for item in list:
        if item not in seen:
            yield item
            seen.add(item)


def form_by_call_order(symbol_lists):
    # Simply strategy, just return symbols in order of occurrence, even across
    # multiple runs.
    return uniq(s for symbols in symbol_lists for s in symbols)


def form_by_call_order_fair(symbol_lists):
    # More complicated strategy that tries to respect the call order across all
    # of the test cases, instead of giving a huge preference to the first test
    # case.

    # First, uniq all the lists.
    uniq_lists = [list(uniq(symbols)) for symbols in symbol_lists]

    # Compute the successors for each list.
    succs = {}
    for symbols in uniq_lists:
        for a, b in zip(symbols[:-1], symbols[1:]):
            succs[a] = items = succs.get(a, [])
            if b not in items:
                items.append(b)

    # Emit all the symbols, but make sure to always emit all successors from any
    # call list whenever we see a symbol.
    #
    # There isn't much science here, but this sometimes works better than the
    # more naive strategy. Then again, sometimes it doesn't so more research is
    # probably needed.
    return uniq(
        s
        for symbols in symbol_lists
        for node in symbols
        for s in ([node] + succs.get(node, []))
    )


def form_by_frequency(symbol_lists):
    # Form the order file by just putting the most commonly occurring symbols
    # first. This assumes the data files didn't use the oneshot dtrace method.

    counts = {}
    for symbols in symbol_lists:
        for a in symbols:
            counts[a] = counts.get(a, 0) + 1

    by_count = list(counts.items())
    by_count.sort(key=lambda __n: -__n[1])
    return [s for s, n in by_count]


def form_by_random(symbol_lists):
    # Randomize the symbols.
    merged_symbols = uniq(s for symbols in symbol_lists for s in symbols)
    random.shuffle(merged_symbols)
    return merged_symbols


def form_by_alphabetical(symbol_lists):
    # Alphabetize the symbols.
    merged_symbols = list(set(s for symbols in symbol_lists for s in symbols))
    merged_symbols.sort()
    return merged_symbols


methods = dict(
    (name[len("form_by_") :], value)
    for name, value in locals().items()
    if name.startswith("form_by_")
)


def genOrderFile(args):
    parser = argparse.ArgumentParser("%prog  [options] <dtrace data file directories>]")
    parser.add_argument("input", nargs="+", help="")
    parser.add_argument(
        "--binary",
        metavar="PATH",
        type=str,
        dest="binary_path",
        help="Path to the binary being ordered (for getting all symbols)",
        default=None,
    )
    parser.add_argument(
        "--output",
        dest="output_path",
        help="path to output order file to write",
        default=None,
        required=True,
        metavar="PATH",
    )
    parser.add_argument(
        "--show-missing-symbols",
        dest="show_missing_symbols",
        help="show symbols which are 'fixed up' to a valid name (requires --binary)",
        action="store_true",
        default=None,
    )
    parser.add_argument(
        "--output-unordered-symbols",
        dest="output_unordered_symbols_path",
        help="write a list of the unordered symbols to PATH (requires --binary)",
        default=None,
        metavar="PATH",
    )
    parser.add_argument(
        "--method",
        dest="method",
        help="order file generation method to use",
        choices=list(methods.keys()),
        default="call_order",
    )
    opts = parser.parse_args(args)

    # If the user gave us a binary, get all the symbols in the binary by
    # snarfing 'nm' output.
    if opts.binary_path is not None:
        output = subprocess.check_output(
            ["nm", "-P", opts.binary_path], universal_newlines=True
        )
        lines = output.split("\n")
        all_symbols = [ln.split(" ", 1)[0] for ln in lines if ln.strip()]
        print("found %d symbols in binary" % len(all_symbols))
        all_symbols.sort()
    else:
        all_symbols = []
    all_symbols_set = set(all_symbols)

    # Compute the list of input files.
    input_files = []
    for dirname in opts.input:
        input_files.extend(findFilesWithExtension(dirname, "dtrace"))

    # Load all of the input files.
    print("loading from %d data files" % len(input_files))
    missing_symbols = set()
    timestamped_symbol_lists = [
        list(
            parse_dtrace_symbol_file(
                path, all_symbols, all_symbols_set, missing_symbols, opts
            )
        )
        for path in input_files
    ]

    # Reorder each symbol list.
    symbol_lists = []
    for timestamped_symbols_list in timestamped_symbol_lists:
        timestamped_symbols_list.sort()
        symbol_lists.append([symbol for _, symbol in timestamped_symbols_list])

    # Execute the desire order file generation method.
    method = methods.get(opts.method)
    result = list(method(symbol_lists))

    # Report to the user on what percentage of symbols are present in the order
    # file.
    num_ordered_symbols = len(result)
    if all_symbols:
        print(
            "note: order file contains %d/%d symbols (%.2f%%)"
            % (
                num_ordered_symbols,
                len(all_symbols),
                100.0 * num_ordered_symbols / len(all_symbols),
            ),
            file=sys.stderr,
        )

    if opts.output_unordered_symbols_path:
        ordered_symbols_set = set(result)
        with open(opts.output_unordered_symbols_path, "w") as f:
            f.write("\n".join(s for s in all_symbols if s not in ordered_symbols_set))

    # Write the order file.
    with open(opts.output_path, "w") as f:
        f.write("\n".join(result))
        f.write("\n")

    return 0


commands = {
    "clean": clean,
    "merge": merge,
    "dtrace": dtrace,
    "cc1": cc1,
    "gen-order-file": genOrderFile,
    "merge-fdata": merge_fdata,
    "perf": perf,
    "perf2bolt": perf2bolt,
}


def main():
    f = commands[sys.argv[1]]
    sys.exit(f(sys.argv[2:]))


if __name__ == "__main__":
    main()
