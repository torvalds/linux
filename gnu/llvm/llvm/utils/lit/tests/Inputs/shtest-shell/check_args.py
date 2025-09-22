#!/usr/bin/env python

import argparse
import platform

parser = argparse.ArgumentParser()
parser.add_argument("--my_arg", "-a")

args = parser.parse_args()

answer = (
    platform.system() == "Windows" and args.my_arg == "/dev/null" and "ERROR"
) or "OK"

print(answer)
