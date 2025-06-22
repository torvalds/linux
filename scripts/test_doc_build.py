#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
#
# pylint: disable=C0103,R1715

"""
Install minimal supported requirements for different Sphinx versions
and optionally test the build.
"""

import argparse
import os.path
import sys
import time

from subprocess import run

# Minimal python version supported by the building system
python_bin = "python3.9"

# Starting from 8.0.2, Python 3.9 becomes too old
python_changes = {(8, 0, 2): "python3"}

# Sphinx versions to be installed and their incremental requirements
sphinx_requirements = {
    (3, 4, 3): {
        "alabaster": "0.7.13",
        "babel": "2.17.0",
        "certifi": "2025.6.15",
        "charset-normalizer": "3.4.2",
        "docutils": "0.15",
        "idna": "3.10",
        "imagesize": "1.4.1",
        "Jinja2": "3.0.3",
        "MarkupSafe": "2.0",
        "packaging": "25.0",
        "Pygments": "2.19.1",
        "PyYAML": "5.1",
        "requests": "2.32.4",
        "snowballstemmer": "3.0.1",
        "sphinxcontrib-applehelp": "1.0.4",
        "sphinxcontrib-devhelp": "1.0.2",
        "sphinxcontrib-htmlhelp": "2.0.1",
        "sphinxcontrib-jsmath": "1.0.1",
        "sphinxcontrib-qthelp": "1.0.3",
        "sphinxcontrib-serializinghtml": "1.1.5",
        "urllib3": "2.4.0",
    },
    (3, 5, 4): {},
    (4, 0, 3): {
        "docutils": "0.17.1",
        "PyYAML": "5.1",
    },
    (4, 1, 2): {},
    (4, 3, 2): {},
    (4, 4, 0): {},
    (4, 5, 0): {},
    (5, 0, 2): {},
    (5, 1, 1): {},
    (5, 2, 3): {
        "Jinja2": "3.1.2",
        "MarkupSafe": "2.0",
        "PyYAML": "5.3.1",
    },
    (5, 3, 0): {
        "docutils": "0.18.1",
        "PyYAML": "5.3.1",
    },
    (6, 0, 1): {},
    (6, 1, 3): {},
    (6, 2, 1): {
        "PyYAML": "5.4.1",
    },
    (7, 0, 1): {},
    (7, 1, 2): {},
    (7, 2, 3): {
        "PyYAML": "6.0.1",
        "sphinxcontrib-serializinghtml": "1.1.9",
    },
    (7, 3, 7): {
        "alabaster": "0.7.14",
        "PyYAML": "6.0.1",
    },
    (7, 4, 7): {
        "docutils": "0.20",
        "PyYAML": "6.0.1",
    },
    (8, 0, 2): {},
    (8, 1, 3): {
        "PyYAML": "6.0.1",
        "sphinxcontrib-applehelp": "1.0.7",
        "sphinxcontrib-devhelp": "1.0.6",
        "sphinxcontrib-htmlhelp": "2.0.6",
        "sphinxcontrib-qthelp": "1.0.6",
    },
    (8, 2, 3): {
        "PyYAML": "6.0.1",
        "sphinxcontrib-serializinghtml": "1.1.9",
    },
}


def parse_version(ver_str):
    """Convert a version string into a tuple."""

    return tuple(map(int, ver_str.split(".")))


parser = argparse.ArgumentParser(description="Build docs for different sphinx_versions.")

parser.add_argument('-v', '--version', help='Sphinx single version',
                    type=parse_version)
parser.add_argument('--min-version', "--min", help='Sphinx minimal version',
                    type=parse_version)
parser.add_argument('--max-version', "--max", help='Sphinx maximum version',
                    type=parse_version)
parser.add_argument('-a', '--make_args',
                    help='extra arguments for make htmldocs, like SPHINXDIRS=netlink/specs',
                    nargs="*")
parser.add_argument('-w', '--write', help='write a requirements.txt file',
                    action='store_true')
parser.add_argument('-m', '--make',
                    help='Make documentation',
                    action='store_true')
parser.add_argument('-i', '--wait-input',
                    help='Wait for an enter before going to the next version',
                    action='store_true')

args = parser.parse_args()

if not args.make_args:
    args.make_args = []

if args.version:
    if args.min_version or args.max_version:
        sys.exit("Use either --version or --min-version/--max-version")
    else:
        args.min_version = args.version
        args.max_version = args.version

sphinx_versions = sorted(list(sphinx_requirements.keys()))

if not args.min_version:
    args.min_version = sphinx_versions[0]

if not args.max_version:
    args.max_version = sphinx_versions[-1]

first_run = True
cur_requirements = {}
built_time = {}

for cur_ver, new_reqs in sphinx_requirements.items():
    cur_requirements.update(new_reqs)

    if cur_ver in python_changes:
        python_bin = python_changes[cur_ver]

    ver = ".".join(map(str, cur_ver))

    if args.min_version:
        if cur_ver < args.min_version:
            continue

    if args.max_version:
        if cur_ver > args.max_version:
            break

    if not first_run and args.wait_input and args.make:
        ret = input("Press Enter to continue or 'a' to abort: ").strip().lower()
        if ret == "a":
            print("Aborted.")
            sys.exit()
    else:
        first_run = False

    venv_dir = f"Sphinx_{ver}"
    req_file = f"requirements_{ver}.txt"

    print(f"\nSphinx {ver} with {python_bin}")

    # Create venv
    run([python_bin, "-m", "venv", venv_dir], check=True)
    pip = os.path.join(venv_dir, "bin/pip")

    # Create install list
    reqs = []
    for pkg, verstr in cur_requirements.items():
        reqs.append(f"{pkg}=={verstr}")

    reqs.append(f"Sphinx=={ver}")

    run([pip, "install"] + reqs, check=True)

    # Freeze environment
    result = run([pip, "freeze"], capture_output=True, text=True, check=True)

    # Pip install succeeded. Write requirements file
    if args.write:
        with open(req_file, "w", encoding="utf-8") as fp:
            fp.write(result.stdout)

    if args.make:
        start_time = time.time()

        # Prepare a venv environment
        env = os.environ.copy()
        bin_dir = os.path.join(venv_dir, "bin")
        env["PATH"] = bin_dir + ":" + env["PATH"]
        env["VIRTUAL_ENV"] = venv_dir
        if "PYTHONHOME" in env:
            del env["PYTHONHOME"]

        # Test doc build
        run(["make", "cleandocs"], env=env, check=True)
        make = ["make"] + args.make_args + ["htmldocs"]

        print(f". {bin_dir}/activate")
        print(" ".join(make))
        print("deactivate")
        run(make, env=env, check=True)

        end_time = time.time()
        elapsed_time = end_time - start_time
        hours, minutes = divmod(elapsed_time, 3600)
        minutes, seconds = divmod(minutes, 60)

        hours = int(hours)
        minutes = int(minutes)
        seconds = int(seconds)

        built_time[ver] = f"{hours:02d}:{minutes:02d}:{seconds:02d}"

        print(f"Finished doc build for Sphinx {ver}. Elapsed time: {built_time[ver]}")

if args.make:
    print()
    print("Summary:")
    for ver, elapsed_time in sorted(built_time.items()):
        print(f"\tSphinx {ver} elapsed time: {elapsed_time}")
