#!/usr/bin/env python3
"""Generate test body using split-file and a custom script.

The script will prepare extra files with `split-file`, invoke `gen`, and then
rewrite the part after `gen` with its stdout.

https://llvm.org/docs/TestingGuide.html#elaborated-tests

Example:
PATH=/path/to/clang_build/bin:$PATH llvm/utils/update_test_body.py path/to/test.s
"""
import argparse
import contextlib
import os
import re
import subprocess
import sys
import tempfile


@contextlib.contextmanager
def cd(directory):
    cwd = os.getcwd()
    os.chdir(directory)
    try:
        yield
    finally:
        os.chdir(cwd)


def process(args, path):
    prolog = []
    seen_gen = False
    with open(path) as f:
        for line in f.readlines():
            line = line.rstrip()
            prolog.append(line)
            if (seen_gen and re.match(r"(.|//)---", line)) or line.startswith(".endif"):
                break
            if re.match(r"(.|//)--- gen", line):
                seen_gen = True
        else:
            print(
                "'gen' should be followed by another part (---) or .endif",
                file=sys.stderr,
            )
            return 1

    if not seen_gen:
        print("'gen' does not exist", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="update_test_body_") as dir:
        try:
            # If the last line starts with ".endif", remove it.
            sub = subprocess.run(
                ["split-file", "-", dir],
                input="\n".join(
                    prolog[:-1] if prolog[-1].startswith(".endif") else prolog
                ).encode(),
                capture_output=True,
                check=True,
            )
        except subprocess.CalledProcessError as ex:
            sys.stderr.write(ex.stderr.decode())
            return 1
        with cd(dir):
            if args.shell:
                print(f"invoke shell in the temporary directory '{dir}'")
                subprocess.run([os.environ.get("SHELL", "sh")])
                return 0

            sub = subprocess.run(
                ["sh", "-eu", "gen"],
                capture_output=True,
                # Don't encode the directory information to the Clang output.
                # Remove unneeded details (.ident) as well.
                env=dict(
                    os.environ,
                    CCC_OVERRIDE_OPTIONS="#^-fno-ident",
                    PWD="/proc/self/cwd",
                ),
            )
            sys.stderr.write(sub.stderr.decode())
            if sub.returncode != 0:
                print("'gen' failed", file=sys.stderr)
                return sub.returncode
            if not sub.stdout:
                print("stdout is empty; forgot -o - ?", file=sys.stderr)
                return 1
            content = sub.stdout.decode()

    with open(path, "w") as f:
        # Print lines up to '.endif'.
        print("\n".join(prolog), file=f)
        # Then print the stdout of 'gen'.
        f.write(content)


parser = argparse.ArgumentParser(
    description="Generate test body using split-file and a custom script"
)
parser.add_argument("files", nargs="+")
parser.add_argument(
    "--shell", action="store_true", help="invoke shell instead of 'gen'"
)
args = parser.parse_args()
for path in args.files:
    retcode = process(args, path)
    if retcode != 0:
        sys.exit(retcode)
