#!/usr/bin/env python

"""Find Kconfig identifiers that are referenced but not defined."""

# (c) 2014 Valentin Rothberg <valentinrothberg@gmail.com>
# (c) 2014 Stefan Hengelein <stefan.hengelein@fau.de>
#
# Licensed under the terms of the GNU GPL License version 2


import os
import re
from subprocess import Popen, PIPE, STDOUT


# regex expressions
OPERATORS = r"&|\(|\)|\||\!"
FEATURE = r"(?:\w*[A-Z0-9]\w*){2,}"
DEF = r"^\s*(?:menu){,1}config\s+(" + FEATURE + r")\s*"
EXPR = r"(?:" + OPERATORS + r"|\s|" + FEATURE + r")+"
STMT = r"^\s*(?:if|select|depends\s+on)\s+" + EXPR
SOURCE_FEATURE = r"(?:\W|\b)+[D]{,1}CONFIG_(" + FEATURE + r")"

# regex objects
REGEX_FILE_KCONFIG = re.compile(r".*Kconfig[\.\w+\-]*$")
REGEX_FEATURE = re.compile(r"(" + FEATURE + r")")
REGEX_SOURCE_FEATURE = re.compile(SOURCE_FEATURE)
REGEX_KCONFIG_DEF = re.compile(DEF)
REGEX_KCONFIG_EXPR = re.compile(EXPR)
REGEX_KCONFIG_STMT = re.compile(STMT)
REGEX_KCONFIG_HELP = re.compile(r"^\s+(help|---help---)\s*$")
REGEX_FILTER_FEATURES = re.compile(r"[A-Za-z0-9]$")


def main():
    """Main function of this module."""
    source_files = []
    kconfig_files = []
    defined_features = set()
    referenced_features = dict()  # {feature: [files]}

    # use 'git ls-files' to get the worklist
    pop = Popen("git ls-files", stdout=PIPE, stderr=STDOUT, shell=True)
    (stdout, _) = pop.communicate()  # wait until finished
    if len(stdout) > 0 and stdout[-1] == "\n":
        stdout = stdout[:-1]

    for gitfile in stdout.rsplit("\n"):
        if ".git" in gitfile or "ChangeLog" in gitfile or \
                ".log" in gitfile or os.path.isdir(gitfile):
            continue
        if REGEX_FILE_KCONFIG.match(gitfile):
            kconfig_files.append(gitfile)
        else:
            # all non-Kconfig files are checked for consistency
            source_files.append(gitfile)

    for sfile in source_files:
        parse_source_file(sfile, referenced_features)

    for kfile in kconfig_files:
        parse_kconfig_file(kfile, defined_features, referenced_features)

    print "Undefined symbol used\tFile list"
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
            files = referenced_features.get(feature)
            print "%s\t%s" % (feature, ", ".join(files))


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
                paths = referenced_features.get(feature, set())
                paths.add(kfile)
                referenced_features[feature] = paths


if __name__ == "__main__":
    main()
