#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0
# Author: Julian Sun <sunjunchao2870@gmail.com>

""" Find macro definitions with unused parameters. """

import argparse
import os
import re

parser = argparse.ArgumentParser()

parser.add_argument("path", type=str, help="The file or dir path that needs check")
parser.add_argument("-v", "--verbose", action="store_true",
                    help="Check conditional macros, but may lead to more false positives")
args = parser.parse_args()

macro_pattern = r"#define\s+(\w+)\(([^)]*)\)"
# below vars were used to reduce false positives
fp_patterns = [r"\s*do\s*\{\s*\}\s*while\s*\(\s*0\s*\)",
               r"\(?0\)?", r"\(?1\)?"]
correct_macros = []
cond_compile_mark = "#if"
cond_compile_end = "#endif"

def check_macro(macro_line, report):
    match = re.match(macro_pattern, macro_line)
    if match:
        macro_def = re.sub(macro_pattern, '', macro_line)
        identifier = match.group(1)
        content = match.group(2)
        arguments = [item.strip() for item in content.split(',') if item.strip()]

        macro_def = macro_def.strip()
        if not macro_def:
            return
        # used to reduce false positives, like #define endfor_nexthops(rt) }
        if len(macro_def) == 1:
            return

        for fp_pattern in fp_patterns:
            if (re.match(fp_pattern, macro_def)):
                return

        for arg in arguments:
            # used to reduce false positives
            if "..." in arg:
                return
        for arg in arguments:
            if not arg in macro_def and report == False:
                return
            # if there is a correct macro with the same name, do not report it.
            if not arg in macro_def and identifier not in correct_macros:
                print(f"Argument {arg} is not used in function-line macro {identifier}")
                return

        correct_macros.append(identifier)


# remove comment and whitespace
def macro_strip(macro):
    comment_pattern1 = r"\/\/*"
    comment_pattern2 = r"\/\**\*\/"

    macro = macro.strip()
    macro = re.sub(comment_pattern1, '', macro)
    macro = re.sub(comment_pattern2, '', macro)

    return macro

def file_check_macro(file_path, report):
    # number of conditional compiling
    cond_compile = 0
    # only check .c and .h file
    if not file_path.endswith(".c") and not file_path.endswith(".h"):
        return

    with open(file_path, "r") as f:
        while True:
            line = f.readline()
            if not line:
                break
            line = line.strip()
            if line.startswith(cond_compile_mark):
                cond_compile += 1
                continue
            if line.startswith(cond_compile_end):
                cond_compile -= 1
                continue

            macro = re.match(macro_pattern, line)
            if macro:
                macro = macro_strip(macro.string)
                while macro[-1] == '\\':
                    macro = macro[0:-1]
                    macro = macro.strip()
                    macro += f.readline()
                    macro = macro_strip(macro)
                if not args.verbose:
                    if file_path.endswith(".c")  and cond_compile != 0:
                        continue
                    # 1 is for #ifdef xxx at the beginning of the header file
                    if file_path.endswith(".h") and cond_compile != 1:
                        continue
                check_macro(macro, report)

def get_correct_macros(path):
    file_check_macro(path, False)

def dir_check_macro(dir_path):

    for dentry in os.listdir(dir_path):
        path = os.path.join(dir_path, dentry)
        if os.path.isdir(path):
            dir_check_macro(path)
        elif os.path.isfile(path):
            get_correct_macros(path)
            file_check_macro(path, True)


def main():
    if os.path.isfile(args.path):
        get_correct_macros(args.path)
        file_check_macro(args.path, True)
    elif os.path.isdir(args.path):
        dir_check_macro(args.path)
    else:
        print(f"{args.path} doesn't exit or is neither a file nor a dir")

if __name__ == "__main__":
    main()