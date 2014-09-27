#!/usr/bin/env python

"""Find Kconfig identifieres that are referenced but not defined."""

# Copyright (C) 2014 Valentin Rothberg <valentinrothberg@gmail.com>
# Copyright (C) 2014 Stefan Hengelein <stefan.hengelein@fau.de>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.


import os
import re
from subprocess import Popen, PIPE, STDOUT

# REGEX EXPRESSIONS
OPERATORS = r"&|\(|\)|\||\!"
FEATURE = r"\w*[A-Z]{1}\w*"
CONFIG_DEF = r"^\s*(?:menu){,1}config\s+(" + FEATURE + r")\s*"
EXPR = r"(?:" + OPERATORS + r"|\s|" + FEATURE + r")+"
STMT = r"^\s*(?:if|select|depends\s+on)\s+" + EXPR

# REGEX OBJECTS
REGEX_FILE_KCONFIG = re.compile(r".*Kconfig[\.\w+\-]*$")
REGEX_FEATURE = re.compile(r"(" + FEATURE + r")")
REGEX_SOURCE_FEATURE = re.compile(r"(?:D|\W|\b)+CONFIG_(" + FEATURE + r")")
REGEX_KCONFIG_DEF = re.compile(CONFIG_DEF)
REGEX_KCONFIG_EXPR = re.compile(EXPR)
REGEX_KCONFIG_STMT = re.compile(STMT)
REGEX_KCONFIG_HELP = re.compile(r"^\s+(help|---help---)\s*$")
REGEX_FILTER_FEATURES = re.compile(r"[A-Za-z0-9]$")


def main():
    """Main function of this module."""
    source_files = []
    kconfig_files = []
    defined_features = set()
    referenced_features = dict()

    # use 'git ls-files' to get the worklist
    pop = Popen("git ls-files", stdout=PIPE, stderr=STDOUT, shell=True)
    (stdout, _) = pop.communicate()  # wait until finished
    if len(stdout) > 0 and stdout[-1] == "\n":
        stdout = stdout[:-1]

    for gitfile in stdout.rsplit("\n"):
        if ".git" in gitfile or "ChangeLog" in gitfile or \
                os.path.isdir(gitfile):
            continue
        if REGEX_FILE_KCONFIG.match(gitfile):
            kconfig_files.append(gitfile)
        else:
            # All non-Kconfig files are checked for consistency
            source_files.append(gitfile)

    for sfile in source_files:
        parse_source_file(sfile, referenced_features)

    for kfile in kconfig_files:
        parse_kconfig_file(kfile, defined_features, referenced_features)

    print "Undefined symbol used\tFile list"
    for feature in sorted(referenced_features):
        if feature not in defined_features:
            if feature.endswith("_MODULE"):
                # Avoid false positives for kernel modules
                if feature[:-len("_MODULE")] in defined_features:
                    continue
            if "FOO" in feature or "BAR" in feature:
                continue
            files = referenced_features.get(feature)
            print "%s:\t%s" % (feature, ", ".join(files))


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
            paths = referenced_features.get(feature, set())
            paths.add(sfile)
            referenced_features[feature] = paths


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
        line = line.split("#")[0]  # Ignore Kconfig comments

        if REGEX_KCONFIG_DEF.match(line):
            feature_def = REGEX_KCONFIG_DEF.findall(line)
            defined_features.add(feature_def[0])
            skip = False
        elif REGEX_KCONFIG_HELP.match(line):
            skip = True
        elif skip:
            # Ignore content of help messages
            pass
        elif REGEX_KCONFIG_STMT.match(line):
            features = get_features_in_line(line)
            # Multi-line statements
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
