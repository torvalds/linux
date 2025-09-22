#!/usr/bin/env python

# This script extracts the VPlan digraphs from the vectoriser debug messages
# and saves them in individual dot files (one for each plan). Optionally, and
# providing 'dot' is installed, it can also render the dot into a PNG file.

from __future__ import print_function

import sys
import re
import argparse
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--png", action="store_true")
args = parser.parse_args()

dot = shutil.which("dot")
if args.png and not dot:
    raise RuntimeError("Can't export to PNG without 'dot' in the system")

pattern = re.compile(r"(digraph VPlan {.*?\n})", re.DOTALL)
matches = re.findall(pattern, sys.stdin.read())

for vplan in matches:
    m = re.search("graph \[.+(VF=.+,UF.+)", vplan)
    if not m:
        raise ValueError("Can't get the right VPlan name")
    name = re.sub("[^a-zA-Z0-9]", "", m.group(1))

    if args.png:
        filename = "VPlan" + name + ".png"
        print("Exporting " + name + " to PNG via dot: " + filename)
        p = subprocess.Popen(
            [dot, "-Tpng", "-o", filename],
            encoding="utf-8",
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        out, err = p.communicate(input=vplan)
        if err:
            raise RuntimeError("Error running dot: " + err)

    else:
        filename = "VPlan" + name + ".dot"
        print("Exporting " + name + " to DOT: " + filename)
        with open(filename, "w") as out:
            out.write(vplan)
