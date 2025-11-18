#! /usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright © 2025, Oracle and/or its affiliates.
# Author: Vegard Nossum <vegard.nossum@oracle.com>

"""Trawl repository history for renames of Documentation/**.rst files.

Example:

    tools/docs/gen-renames.py --rev HEAD > Documentation/.renames.txt
"""

import argparse
import itertools
import os
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--rev', default='HEAD', help='generate renames up to this revision')

args = parser.parse_args()

def normalize(path):
    prefix = 'Documentation/'
    suffix = '.rst'

    assert path.startswith(prefix)
    assert path.endswith(suffix)

    return path[len(prefix):-len(suffix)]

class Name(object):
    def __init__(self, name):
        self.names = [name]

    def rename(self, new_name):
        self.names.append(new_name)

names = {
}

for line in subprocess.check_output([
    'git', 'log',
    '--reverse',
    '--oneline',
    '--find-renames',
    '--diff-filter=RD',
    '--name-status',
    '--format=commit %H',
    # ~v4.8-ish is when Sphinx/.rst was added in the first place
    f'v4.8..{args.rev}',
    '--',
    'Documentation/'
], text=True).splitlines():
    # rename
    if line.startswith('R'):
        _, old, new = line[1:].split('\t', 2)

        if old.endswith('.rst') and new.endswith('.rst'):
            old = normalize(old)
            new = normalize(new)

            name = names.get(old)
            if name is None:
                name = Name(old)
            else:
                del names[old]

            name.rename(new)
            names[new] = name

        continue

    # delete
    if line.startswith('D'):
        _, old = line.split('\t', 1)

        if old.endswith('.rst'):
            old = normalize(old)

            # TODO: we could save added/modified files as well and propose
            # them as alternatives
            name = names.get(old)
            if name is None:
                pass
            else:
                del names[old]

        continue

#
# Get the set of current files so we can sanity check that we aren't
# redirecting any of those
#

current_files = set()
for line in subprocess.check_output([
    'git', 'ls-tree',
    '-r',
    '--name-only',
    args.rev,
    'Documentation/',
], text=True).splitlines():
    if line.endswith('.rst'):
        current_files.add(normalize(line))

#
# Format/group/output result
#

result = []
for _, v in names.items():
    old_names = v.names[:-1]
    new_name = v.names[-1]

    for old_name in old_names:
        if old_name == new_name:
            # A file was renamed to its new name twice; don't redirect that
            continue

        if old_name in current_files:
            # A file was recreated with a former name; don't redirect those
            continue

        result.append((old_name, new_name))

for old_name, new_name in sorted(result):
    print(f"{old_name} {new_name}")
