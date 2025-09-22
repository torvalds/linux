#!/usr/bin/env python
#
# Given a previous good compile narrow down miscompiles.
# Expects two directories named "before" and "after" each containing a set of
# assembly or object files where the "after" version is assumed to be broken.
# You also have to provide a script called "link_test". It is called with a
# list of files which should be linked together and result tested. "link_test"
# should returns with exitcode 0 if the linking and testing succeeded.
#
# If a response file is provided, only the object files that are listed in the
# file are inspected. In addition, the "link_test" is called with a temporary
# response file representing one iteration of bisection.
#
# abtest.py operates by taking all files from the "before" directory and
# in each step replacing one of them with a file from the "bad" directory.
#
# Additionally you can perform the same steps with a single .s file. In this
# mode functions are identified by " -- Begin function FunctionName" and
# " -- End function" markers. The abtest.py then takes all
# function from the file in the "before" directory and replaces one function
# with the corresponding function from the "bad" file in each step.
#
# Example usage to identify miscompiled files:
#    1. Create a link_test script, make it executable. Simple Example:
#          clang "$@" -o /tmp/test && /tmp/test || echo "PROBLEM"
#    2. Run the script to figure out which files are miscompiled:
#       > ./abtest.py
#       somefile.s: ok
#       someotherfile.s: skipped: same content
#       anotherfile.s: failed: './link_test' exitcode != 0
#       ...
# Example usage to identify miscompiled functions inside a file:
#    3. Run the tests on a single file (assuming before/file.s and
#       after/file.s exist)
#       > ./abtest.py file.s
#       funcname1 [0/XX]: ok
#       funcname2 [1/XX]: ok
#       funcname3 [2/XX]: skipped: same content
#       funcname4 [3/XX]: failed: './link_test' exitcode != 0
#       ...
from fnmatch import filter
from sys import stderr
import argparse
import filecmp
import os
import subprocess
import sys
import tempfile

# Specify LINKTEST via `--test`. Default value is './link_test'.
LINKTEST = ""
ESCAPE = "\033[%sm"
BOLD = ESCAPE % "1"
RED = ESCAPE % "31"
NORMAL = ESCAPE % "0"
FAILED = RED + "failed" + NORMAL


def find(dir, file_filter=None):
    files = [walkdir[0] + "/" + file for walkdir in os.walk(dir) for file in walkdir[2]]
    if file_filter is not None:
        files = filter(files, file_filter)
    return sorted(files)


def error(message):
    stderr.write("Error: %s\n" % (message,))


def warn(message):
    stderr.write("Warning: %s\n" % (message,))


def info(message):
    stderr.write("Info: %s\n" % (message,))


def announce_test(name):
    stderr.write("%s%s%s: " % (BOLD, name, NORMAL))
    stderr.flush()


def announce_result(result):
    stderr.write(result)
    stderr.write("\n")
    stderr.flush()


def format_namelist(l):
    result = ", ".join(l[0:3])
    if len(l) > 3:
        result += "... (%d total)" % len(l)
    return result


def check_sanity(choices, perform_test):
    announce_test("sanity check A")
    all_a = {name: a_b[0] for name, a_b in choices}
    res_a = perform_test(all_a)
    if res_a is not True:
        error("Picking all choices from A failed to pass the test")
        sys.exit(1)

    announce_test("sanity check B (expecting failure)")
    all_b = {name: a_b[1] for name, a_b in choices}
    res_b = perform_test(all_b)
    if res_b is not False:
        error("Picking all choices from B did unexpectedly pass the test")
        sys.exit(1)


def check_sequentially(choices, perform_test):
    known_good = set()
    all_a = {name: a_b[0] for name, a_b in choices}
    n = 1
    for name, a_b in sorted(choices):
        picks = dict(all_a)
        picks[name] = a_b[1]
        announce_test("checking %s [%d/%d]" % (name, n, len(choices)))
        n += 1
        res = perform_test(picks)
        if res is True:
            known_good.add(name)
    return known_good


def check_bisect(choices, perform_test):
    known_good = set()
    if len(choices) == 0:
        return known_good

    choice_map = dict(choices)
    all_a = {name: a_b[0] for name, a_b in choices}

    def test_partition(partition, upcoming_partition):
        # Compute the maximum number of checks we have to do in the worst case.
        max_remaining_steps = len(partition) * 2 - 1
        if upcoming_partition is not None:
            max_remaining_steps += len(upcoming_partition) * 2 - 1
        for x in partitions_to_split:
            max_remaining_steps += (len(x) - 1) * 2

        picks = dict(all_a)
        for x in partition:
            picks[x] = choice_map[x][1]
        announce_test(
            "checking %s [<=%d remaining]"
            % (format_namelist(partition), max_remaining_steps)
        )
        res = perform_test(picks)
        if res is True:
            known_good.update(partition)
        elif len(partition) > 1:
            partitions_to_split.insert(0, partition)

    # TODO:
    # - We could optimize based on the knowledge that when splitting a failed
    #   partition into two and one side checks out okay then we can deduce that
    #   the other partition must be a failure.
    all_choice_names = [name for name, _ in choices]
    partitions_to_split = [all_choice_names]
    while len(partitions_to_split) > 0:
        partition = partitions_to_split.pop()

        middle = len(partition) // 2
        left = partition[0:middle]
        right = partition[middle:]

        if len(left) > 0:
            test_partition(left, right)
        assert len(right) > 0
        test_partition(right, None)

    return known_good


def extract_functions(file):
    functions = []
    in_function = None
    for line in open(file):
        marker = line.find(" -- Begin function ")
        if marker != -1:
            if in_function is not None:
                warn("Missing end of function %s" % (in_function,))
            funcname = line[marker + 19 : -1]
            in_function = funcname
            text = line
            continue

        marker = line.find(" -- End function")
        if marker != -1:
            text += line
            functions.append((in_function, text))
            in_function = None
            continue

        if in_function is not None:
            text += line
    return functions


def replace_functions(source, dest, replacements):
    out = open(dest, "w")
    skip = False
    in_function = None
    for line in open(source):
        marker = line.find(" -- Begin function ")
        if marker != -1:
            if in_function is not None:
                warn("Missing end of function %s" % (in_function,))
            funcname = line[marker + 19 : -1]
            in_function = funcname
            replacement = replacements.get(in_function)
            if replacement is not None:
                out.write(replacement)
                skip = True
        else:
            marker = line.find(" -- End function")
            if marker != -1:
                in_function = None
                if skip:
                    skip = False
                    continue

        if not skip:
            out.write(line)


def testrun(files):
    linkline = "%s %s" % (
        LINKTEST,
        " ".join(files),
    )
    res = subprocess.call(linkline, shell=True)
    if res != 0:
        announce_result(FAILED + ": '%s' exitcode != 0" % LINKTEST)
        return False
    else:
        announce_result("ok")
        return True


def prepare_files(gooddir, baddir, rspfile):
    files_a = []
    files_b = []

    if rspfile is not None:

        def get_basename(name):
            # remove prefix
            if name.startswith(gooddir):
                return name[len(gooddir) :]
            if name.startswith(baddir):
                return name[len(baddir) :]
            assert False, ""

        with open(rspfile, "r") as rf:
            for line in rf.read().splitlines():
                for obj in line.split():
                    assert not os.path.isabs(obj), "TODO: support abs path"
                    files_a.append(gooddir + "/" + obj)
                    files_b.append(baddir + "/" + obj)
    else:
        get_basename = lambda name: os.path.basename(name)
        files_a = find(gooddir, "*")
        files_b = find(baddir, "*")

    basenames_a = set(map(get_basename, files_a))
    basenames_b = set(map(get_basename, files_b))

    for name in files_b:
        basename = get_basename(name)
        if basename not in basenames_a:
            warn("There is no corresponding file to '%s' in %s" % (name, gooddir))
    choices = []
    skipped = []
    for name in files_a:
        basename = get_basename(name)
        if basename not in basenames_b:
            warn("There is no corresponding file to '%s' in %s" % (name, baddir))

        file_a = gooddir + "/" + basename
        file_b = baddir + "/" + basename
        if filecmp.cmp(file_a, file_b):
            skipped.append(basename)
            continue

        choice = (basename, (file_a, file_b))
        choices.append(choice)

    if len(skipped) > 0:
        info("Skipped (same content): %s" % format_namelist(skipped))

    def perform_test(picks):
        files = []
        # Note that we iterate over files_a so we don't change the order
        # (cannot use `picks` as it is a dictionary without order)
        for x in files_a:
            basename = get_basename(x)
            picked = picks.get(basename)
            if picked is None:
                assert basename in skipped
                files.append(x)
            else:
                files.append(picked)

        # If response file is used, create a temporary response file for the
        # picked files.
        if rspfile is not None:
            with tempfile.NamedTemporaryFile("w", suffix=".rsp", delete=False) as tf:
                tf.write(" ".join(files))
                tf.flush()
            ret = testrun([tf.name])
            os.remove(tf.name)
            return ret

        return testrun(files)

    return perform_test, choices


def prepare_functions(to_check, gooddir, goodfile, badfile):
    files_good = find(gooddir, "*")

    functions_a = extract_functions(goodfile)
    functions_a_map = dict(functions_a)
    functions_b_map = dict(extract_functions(badfile))

    for name in functions_b_map.keys():
        if name not in functions_a_map:
            warn("Function '%s' missing from good file" % name)
    choices = []
    skipped = []
    for name, candidate_a in functions_a:
        candidate_b = functions_b_map.get(name)
        if candidate_b is None:
            warn("Function '%s' missing from bad file" % name)
            continue
        if candidate_a == candidate_b:
            skipped.append(name)
            continue
        choice = name, (candidate_a, candidate_b)
        choices.append(choice)

    if len(skipped) > 0:
        info("Skipped (same content): %s" % format_namelist(skipped))

    combined_file = "/tmp/combined2.s"
    files = []
    found_good_file = False
    for c in files_good:
        if os.path.basename(c) == to_check:
            found_good_file = True
            files.append(combined_file)
            continue
        files.append(c)
    assert found_good_file

    def perform_test(picks):
        for name, x in picks.items():
            assert x == functions_a_map[name] or x == functions_b_map[name]
        replace_functions(goodfile, combined_file, picks)
        return testrun(files)

    return perform_test, choices


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--a", dest="dir_a", default="before")
    parser.add_argument("--b", dest="dir_b", default="after")
    parser.add_argument("--rsp", default=None)
    parser.add_argument("--test", default="./link_test")
    parser.add_argument("--insane", help="Skip sanity check", action="store_true")
    parser.add_argument(
        "--seq", help="Check sequentially instead of bisection", action="store_true"
    )
    parser.add_argument("file", metavar="file", nargs="?")
    config = parser.parse_args()

    gooddir = config.dir_a
    baddir = config.dir_b
    rspfile = config.rsp
    global LINKTEST
    LINKTEST = config.test

    # Preparation phase: Creates a dictionary mapping names to a list of two
    # choices each. The bisection algorithm will pick one choice for each name
    # and then run the perform_test function on it.
    if config.file is not None:
        goodfile = gooddir + "/" + config.file
        badfile = baddir + "/" + config.file
        perform_test, choices = prepare_functions(
            config.file, gooddir, goodfile, badfile
        )
    else:
        perform_test, choices = prepare_files(gooddir, baddir, rspfile)

    info("%d bisection choices" % len(choices))

    # "Checking whether build environment is sane ..."
    if not config.insane:
        if not os.access(LINKTEST, os.X_OK):
            error("Expect '%s' to be present and executable" % (LINKTEST,))
            exit(1)

        check_sanity(choices, perform_test)

    if config.seq:
        known_good = check_sequentially(choices, perform_test)
    else:
        known_good = check_bisect(choices, perform_test)

    stderr.write("")
    if len(known_good) != len(choices):
        stderr.write("== Failing ==\n")
        for name, _ in choices:
            if name not in known_good:
                stderr.write("%s\n" % name)
    else:
        # This shouldn't happen when the sanity check works...
        # Maybe link_test isn't deterministic?
        stderr.write("Could not identify failing parts?!?")


if __name__ == "__main__":
    main()
