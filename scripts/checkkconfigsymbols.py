#!/usr/bin/env python2

"""Find Kconfig symbols that are referenced but not defined."""

# (c) 2014-2015 Valentin Rothberg <valentinrothberg@gmail.com>
# (c) 2014 Stefan Hengelein <stefan.hengelein@fau.de>
#
# Licensed under the terms of the GNU GPL License version 2


import os
import re
import sys
from subprocess import Popen, PIPE, STDOUT
from optparse import OptionParser


# regex expressions
OPERATORS = r"&|\(|\)|\||\!"
FEATURE = r"(?:\w*[A-Z0-9]\w*){2,}"
DEF = r"^\s*(?:menu){,1}config\s+(" + FEATURE + r")\s*"
EXPR = r"(?:" + OPERATORS + r"|\s|" + FEATURE + r")+"
DEFAULT = r"default\s+.*?(?:if\s.+){,1}"
STMT = r"^\s*(?:if|select|depends\s+on|(?:" + DEFAULT + r"))\s+" + EXPR
SOURCE_FEATURE = r"(?:\W|\b)+[D]{,1}CONFIG_(" + FEATURE + r")"

# regex objects
REGEX_FILE_KCONFIG = re.compile(r".*Kconfig[\.\w+\-]*$")
REGEX_FEATURE = re.compile(r'(?!\B"[^"]*)' + FEATURE + r'(?![^"]*"\B)')
REGEX_SOURCE_FEATURE = re.compile(SOURCE_FEATURE)
REGEX_KCONFIG_DEF = re.compile(DEF)
REGEX_KCONFIG_EXPR = re.compile(EXPR)
REGEX_KCONFIG_STMT = re.compile(STMT)
REGEX_KCONFIG_HELP = re.compile(r"^\s+(help|---help---)\s*$")
REGEX_FILTER_FEATURES = re.compile(r"[A-Za-z0-9]$")
REGEX_NUMERIC = re.compile(r"0[xX][0-9a-fA-F]+|[0-9]+")


def parse_options():
    """The user interface of this module."""
    usage = "%prog [options]\n\n"                                              \
            "Run this tool to detect Kconfig symbols that are referenced but " \
            "not defined in\nKconfig.  The output of this tool has the "       \
            "format \'Undefined symbol\\tFile list\'\n\n"                      \
            "If no option is specified, %prog will default to check your\n"    \
            "current tree.  Please note that specifying commits will "         \
            "\'git reset --hard\'\nyour current tree!  You may save "          \
            "uncommitted changes to avoid losing data."

    parser = OptionParser(usage=usage)

    parser.add_option('-c', '--commit', dest='commit', action='store',
                      default="",
                      help="Check if the specified commit (hash) introduces "
                           "undefined Kconfig symbols.")

    parser.add_option('-d', '--diff', dest='diff', action='store',
                      default="",
                      help="Diff undefined symbols between two commits.  The "
                           "input format bases on Git log's "
                           "\'commmit1..commit2\'.")

    parser.add_option('-f', '--find', dest='find', action='store_true',
                      default=False,
                      help="Find and show commits that may cause symbols to be "
                           "missing.  Required to run with --diff.")

    parser.add_option('-i', '--ignore', dest='ignore', action='store',
                      default="",
                      help="Ignore files matching this pattern.  Note that "
                           "the pattern needs to be a Python regex.  To "
                           "ignore defconfigs, specify -i '.*defconfig'.")

    parser.add_option('', '--force', dest='force', action='store_true',
                      default=False,
                      help="Reset current Git tree even when it's dirty.")

    (opts, _) = parser.parse_args()

    if opts.commit and opts.diff:
        sys.exit("Please specify only one option at once.")

    if opts.diff and not re.match(r"^[\w\-\.]+\.\.[\w\-\.]+$", opts.diff):
        sys.exit("Please specify valid input in the following format: "
                 "\'commmit1..commit2\'")

    if opts.commit or opts.diff:
        if not opts.force and tree_is_dirty():
            sys.exit("The current Git tree is dirty (see 'git status').  "
                     "Running this script may\ndelete important data since it "
                     "calls 'git reset --hard' for some performance\nreasons. "
                     " Please run this script in a clean Git tree or pass "
                     "'--force' if you\nwant to ignore this warning and "
                     "continue.")

    if opts.commit:
        opts.find = False

    if opts.ignore:
        try:
            re.match(opts.ignore, "this/is/just/a/test.c")
        except:
            sys.exit("Please specify a valid Python regex.")

    return opts


def main():
    """Main function of this module."""
    opts = parse_options()

    if opts.commit or opts.diff:
        head = get_head()

        # get commit range
        commit_a = None
        commit_b = None
        if opts.commit:
            commit_a = opts.commit + "~"
            commit_b = opts.commit
        elif opts.diff:
            split = opts.diff.split("..")
            commit_a = split[0]
            commit_b = split[1]
            undefined_a = {}
            undefined_b = {}

        # get undefined items before the commit
        execute("git reset --hard %s" % commit_a)
        undefined_a = check_symbols(opts.ignore)

        # get undefined items for the commit
        execute("git reset --hard %s" % commit_b)
        undefined_b = check_symbols(opts.ignore)

        # report cases that are present for the commit but not before
        for feature in sorted(undefined_b):
            # feature has not been undefined before
            if not feature in undefined_a:
                files = sorted(undefined_b.get(feature))
                print "%s\t%s" % (yel(feature), ", ".join(files))
                if opts.find:
                    commits = find_commits(feature, opts.diff)
                    print red(commits)
            # check if there are new files that reference the undefined feature
            else:
                files = sorted(undefined_b.get(feature) -
                               undefined_a.get(feature))
                if files:
                    print "%s\t%s" % (yel(feature), ", ".join(files))
                    if opts.find:
                        commits = find_commits(feature, opts.diff)
                        print red(commits)

        # reset to head
        execute("git reset --hard %s" % head)

    # default to check the entire tree
    else:
        undefined = check_symbols(opts.ignore)
        for feature in sorted(undefined):
            files = sorted(undefined.get(feature))
            print "%s\t%s" % (yel(feature), ", ".join(files))


def yel(string):
    """
    Color %string yellow.
    """
    return "\033[33m%s\033[0m" % string


def red(string):
    """
    Color %string red.
    """
    return "\033[31m%s\033[0m" % string


def execute(cmd):
    """Execute %cmd and return stdout.  Exit in case of error."""
    pop = Popen(cmd, stdout=PIPE, stderr=STDOUT, shell=True)
    (stdout, _) = pop.communicate()  # wait until finished
    if pop.returncode != 0:
        sys.exit(stdout)
    return stdout


def find_commits(symbol, diff):
    """Find commits changing %symbol in the given range of %diff."""
    commits = execute("git log --pretty=oneline --abbrev-commit -G %s %s"
                      % (symbol, diff))
    return commits


def tree_is_dirty():
    """Return true if the current working tree is dirty (i.e., if any file has
    been added, deleted, modified, renamed or copied but not committed)."""
    stdout = execute("git status --porcelain")
    for line in stdout:
        if re.findall(r"[URMADC]{1}", line[:2]):
            return True
    return False


def get_head():
    """Return commit hash of current HEAD."""
    stdout = execute("git rev-parse HEAD")
    return stdout.strip('\n')


def check_symbols(ignore):
    """Find undefined Kconfig symbols and return a dict with the symbol as key
    and a list of referencing files as value.  Files matching %ignore are not
    checked for undefined symbols."""
    source_files = []
    kconfig_files = []
    defined_features = set()
    referenced_features = dict()  # {feature: [files]}

    # use 'git ls-files' to get the worklist
    stdout = execute("git ls-files")
    if len(stdout) > 0 and stdout[-1] == "\n":
        stdout = stdout[:-1]

    for gitfile in stdout.rsplit("\n"):
        if ".git" in gitfile or "ChangeLog" in gitfile or      \
                ".log" in gitfile or os.path.isdir(gitfile) or \
                gitfile.startswith("tools/"):
            continue
        if REGEX_FILE_KCONFIG.match(gitfile):
            kconfig_files.append(gitfile)
        else:
            # all non-Kconfig files are checked for consistency
            source_files.append(gitfile)

    for sfile in source_files:
        if ignore and re.match(ignore, sfile):
            # do not check files matching %ignore
            continue
        parse_source_file(sfile, referenced_features)

    for kfile in kconfig_files:
        if ignore and re.match(ignore, kfile):
            # do not collect references for files matching %ignore
            parse_kconfig_file(kfile, defined_features, dict())
        else:
            parse_kconfig_file(kfile, defined_features, referenced_features)

    undefined = {}  # {feature: [files]}
    for feature in sorted(referenced_features):
        # filter some false positives
        if feature == "FOO" or feature == "BAR" or \
                feature == "FOO_BAR" or feature == "XXX":
            continue
        if feature not in defined_features:
            if feature.endswith("_MODULE"):
                # avoid false positives for kernel modules
                if feature[:-len("_MODULE")] in defined_features:
                    continue
            undefined[feature] = referenced_features.get(feature)
    return undefined


def parse_source_file(sfile, referenced_features):
    """Parse @sfile for referenced Kconfig features."""
    lines = []
    with open(sfile, "r") as stream:
        lines = stream.readlines()

    for line in lines:
        if not "CONFIG_" in line:
            continue
        features = REGEX_SOURCE_FEATURE.findall(line)
        for feature in features:
            if not REGEX_FILTER_FEATURES.search(feature):
                continue
            sfiles = referenced_features.get(feature, set())
            sfiles.add(sfile)
            referenced_features[feature] = sfiles


def get_features_in_line(line):
    """Return mentioned Kconfig features in @line."""
    return REGEX_FEATURE.findall(line)


def parse_kconfig_file(kfile, defined_features, referenced_features):
    """Parse @kfile and update feature definitions and references."""
    lines = []
    skip = False

    with open(kfile, "r") as stream:
        lines = stream.readlines()

    for i in range(len(lines)):
        line = lines[i]
        line = line.strip('\n')
        line = line.split("#")[0]  # ignore comments

        if REGEX_KCONFIG_DEF.match(line):
            feature_def = REGEX_KCONFIG_DEF.findall(line)
            defined_features.add(feature_def[0])
            skip = False
        elif REGEX_KCONFIG_HELP.match(line):
            skip = True
        elif skip:
            # ignore content of help messages
            pass
        elif REGEX_KCONFIG_STMT.match(line):
            features = get_features_in_line(line)
            # multi-line statements
            while line.endswith("\\"):
                i += 1
                line = lines[i]
                line = line.strip('\n')
                features.extend(get_features_in_line(line))
            for feature in set(features):
                if REGEX_NUMERIC.match(feature):
                    # ignore numeric values
                    continue
                paths = referenced_features.get(feature, set())
                paths.add(kfile)
                referenced_features[feature] = paths


if __name__ == "__main__":
    main()
