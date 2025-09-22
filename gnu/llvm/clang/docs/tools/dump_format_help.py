#!/usr/bin/env python3
# A tool to parse the output of `clang-format --help` and update the
# documentation in ../ClangFormat.rst automatically.

import os
import re
import subprocess
import sys

PARENT_DIR = os.path.join(os.path.dirname(__file__), "..")
DOC_FILE = os.path.join(PARENT_DIR, "ClangFormat.rst")


def substitute(text, tag, contents):
    replacement = "\n.. START_%s\n\n%s\n\n.. END_%s\n" % (tag, contents, tag)
    pattern = r"\n\.\. START_%s\n.*\n\.\. END_%s\n" % (tag, tag)
    return re.sub(pattern, replacement, text, flags=re.S)


def indent(text, columns, indent_first_line=True):
    indent_str = " " * columns
    s = re.sub(r"\n([^\n])", "\n" + indent_str + "\\1", text, flags=re.S)
    if not indent_first_line or s.startswith("\n"):
        return s
    return indent_str + s


def get_help_output():
    args = ["clang-format", "--help"]
    cmd = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out, _ = cmd.communicate()
    out = out.decode(sys.stdout.encoding)
    return out


def get_help_text():
    out = get_help_output()
    out = re.sub(r" clang-format\.exe ", " clang-format ", out)

    out = (
        """.. code-block:: console

$ clang-format --help
"""
        + out
    )
    out = indent(out, 2, indent_first_line=False)
    return out


def validate(text, columns):
    for line in text.splitlines():
        if len(line) > columns:
            print("warning: line too long:\n", line, file=sys.stderr)


help_text = get_help_text()
validate(help_text, 100)

with open(DOC_FILE) as f:
    contents = f.read()

contents = substitute(contents, "FORMAT_HELP", help_text)

with open(DOC_FILE, "wb") as output:
    output.write(contents.encode())
