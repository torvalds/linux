#! /usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
#
# Copyright © 2025, Oracle and/or its affiliates.
# Author: Vegard Nossum <vegard.nossum@oracle.com>

"""Generate HTML redirects for renamed Documentation/**.rst files using
the output of tools/docs/gen-renames.py.

Example:

    tools/docs/gen-redirects.py --output Documentation/output/ < Documentation/.renames.txt
"""

import argparse
import os
import sys

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('-o', '--output', help='output directory')

args = parser.parse_args()

for line in sys.stdin:
    line = line.rstrip('\n')

    old_name, new_name = line.split(' ', 2)

    old_html_path = os.path.join(args.output, old_name + '.html')
    new_html_path = os.path.join(args.output, new_name + '.html')

    if not os.path.exists(new_html_path):
        print(f"warning: target does not exist: {new_html_path} (redirect from {old_html_path})")
        continue

    old_html_dir = os.path.dirname(old_html_path)
    if not os.path.exists(old_html_dir):
        os.makedirs(old_html_dir)

    relpath = os.path.relpath(new_name, os.path.dirname(old_name)) + '.html'

    with open(old_html_path, 'w') as f:
        print(f"""\
<!DOCTYPE html>

<html lang="en">
<head>
    <title>This page has moved</title>
    <meta http-equiv="refresh" content="0; url={relpath}">
</head>
<body>
<p>This page has moved to <a href="{relpath}">{new_name}</a>.</p>
</body>
</html>""", file=f)
