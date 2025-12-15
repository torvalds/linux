#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) Akira Yokosawa, 2024
#
# Ported to Python by (c) Mauro Carvalho Chehab, 2025
#
# pylint: disable=C0103

"""
Detect problematic Noto CJK variable fonts.

or more details, see .../tools/lib/python/kdoc/latex_fonts.py.
"""

import argparse
import sys
import os.path

src_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(src_dir, '../lib/python'))

from kdoc.latex_fonts import LatexFontChecker

checker = LatexFontChecker()

parser=argparse.ArgumentParser(description=checker.description(),
                               formatter_class=argparse.RawTextHelpFormatter)
parser.add_argument("--deny-vf",
                    help="XDG_CONFIG_HOME dir containing fontconfig/fonts.conf file")

args=parser.parse_args()

msg = LatexFontChecker(args.deny_vf).check()
if msg:
    print(msg)

sys.exit(1)
