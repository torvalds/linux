#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2025: Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
#
# pylint: disable=R0903,R0912,R0913,R0914,R0917,C0301

"""
Install minimal supported requirements for different Sphinx versions
and optionally test the build.
"""

import argparse
import asyncio
import os.path
import shutil
import sys
import time
import subprocess

# Minimal python version supported by the building system.

PYTHON = os.path.basename(sys.executable)

min_python_bin = None

for i in range(9, 13):
    p = f"python3.{i}"
    if shutil.which(p):
        min_python_bin = p
        break

if not min_python_bin:
    min_python_bin = PYTHON

# Starting from 8.0, Python 3.9 is not supported anymore.
PYTHON_VER_CHANGES = {(8, 0, 2): PYTHON}

# Sphinx versions to be installed and their incremental requirements
SPHINX_REQUIREMENTS = {
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


class AsyncCommands:
    """Excecute command synchronously"""

    def __init__(self, fp=None):

        self.stdout = None
        self.stderr = None
        self.output = None
        self.fp = fp

    def log(self, out, verbose, is_info=True):
        if verbose:
            if is_info:
                print(out.rstrip("\n"))
            else:
                print(out.rstrip("\n"), file=sys.stderr)

        if self.fp:
            self.fp.write(out.rstrip("\n") + "\n")

    async def _read(self, stream, verbose, is_info):
        """Ancillary routine to capture while displaying"""

        while stream is not None:
            line = await stream.readline()
            if line:
                out = line.decode("utf-8", errors="backslashreplace")
                self.log(out, verbose, is_info)
                if is_info:
                    self.stdout += out
                else:
                    self.stderr += out
            else:
                break

    async def run(self, cmd, capture_output=False, check=False,
                  env=None, verbose=True):

        """
        Execute an arbitrary command, handling errors.

        Please notice that this class is not thread safe
        """

        self.stdout = ""
        self.stderr = ""

        self.log("$ " + " ".join(cmd), verbose)

        proc = await asyncio.create_subprocess_exec(cmd[0],
                                                    *cmd[1:],
                                                    env=env,
                                                    stdout=asyncio.subprocess.PIPE,
                                                    stderr=asyncio.subprocess.PIPE)

        # Handle input and output in realtime
        await asyncio.gather(
            self._read(proc.stdout, verbose, True),
            self._read(proc.stderr, verbose, False),
        )

        await proc.wait()

        if check and proc.returncode > 0:
            raise subprocess.CalledProcessError(returncode=proc.returncode,
                                                cmd=" ".join(cmd),
                                                output=self.stdout,
                                                stderr=self.stderr)

        if capture_output:
            if proc.returncode > 0:
                self.log(f"Error {proc.returncode}", verbose=True, is_info=False)
                return ""

            return self.output

        ret = subprocess.CompletedProcess(args=cmd,
                                          returncode=proc.returncode,
                                          stdout=self.stdout,
                                          stderr=self.stderr)

        return ret


class SphinxVenv:
    """
    Installs Sphinx on one virtual env per Sphinx version with a minimal
    set of dependencies, adjusting them to each specific version.
    """

    def __init__(self):
        """Initialize instance variables"""

        self.built_time = {}
        self.first_run = True

    async def _handle_version(self, args, fp,
                              cur_ver, cur_requirements, python_bin):
        """Handle a single Sphinx version"""

        cmd = AsyncCommands(fp)

        ver = ".".join(map(str, cur_ver))

        if not self.first_run and args.wait_input and args.make:
            ret = input("Press Enter to continue or 'a' to abort: ").strip().lower()
            if ret == "a":
                print("Aborted.")
                sys.exit()
        else:
            self.first_run = False

        venv_dir = f"Sphinx_{ver}"
        req_file = f"requirements_{ver}.txt"

        cmd.log(f"\nSphinx {ver} with {python_bin}", verbose=True)

        # Create venv
        await cmd.run([python_bin, "-m", "venv", venv_dir],
                      verbose=args.verbose, check=True)
        pip = os.path.join(venv_dir, "bin/pip")

        # Create install list
        reqs = []
        for pkg, verstr in cur_requirements.items():
            reqs.append(f"{pkg}=={verstr}")

        reqs.append(f"Sphinx=={ver}")

        await cmd.run([pip, "install"] + reqs, check=True, verbose=args.verbose)

        # Freeze environment
        result = await cmd.run([pip, "freeze"], verbose=False, check=True)

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
            await cmd.run(["make", "cleandocs"], env=env, check=True)
            make = ["make"] + args.make_args + ["htmldocs"]

            if args.verbose:
                print(f". {bin_dir}/activate")
            await cmd.run(make, env=env, check=True, verbose=True)
            if args.verbose:
                print("deactivate")

            end_time = time.time()
            elapsed_time = end_time - start_time
            hours, minutes = divmod(elapsed_time, 3600)
            minutes, seconds = divmod(minutes, 60)

            hours = int(hours)
            minutes = int(minutes)
            seconds = int(seconds)

            self.built_time[ver] = f"{hours:02d}:{minutes:02d}:{seconds:02d}"

            cmd.log(f"Finished doc build for Sphinx {ver}. Elapsed time: {self.built_time[ver]}", verbose=True)

    async def run(self, args):
        """
        Navigate though multiple Sphinx versions, handling each of them
        on a loop.
        """

        if args.log:
            fp = open(args.log, "w", encoding="utf-8")
            if not args.verbose:
                args.verbose = False
        else:
            fp = None
            if not args.verbose:
                args.verbose = True

        cur_requirements = {}
        python_bin = min_python_bin

        for cur_ver, new_reqs in SPHINX_REQUIREMENTS.items():
            cur_requirements.update(new_reqs)

            if cur_ver in PYTHON_VER_CHANGES:          # pylint: disable=R1715

                python_bin = PYTHON_VER_CHANGES[cur_ver]

            if args.min_version:
                if cur_ver < args.min_version:
                    continue

            if args.max_version:
                if cur_ver > args.max_version:
                    break

            await self._handle_version(args, fp, cur_ver, cur_requirements,
                                       python_bin)

        if args.make:
            print()
            print("Summary:")
            for ver, elapsed_time in sorted(self.built_time.items()):
                print(f"\tSphinx {ver} elapsed time: {elapsed_time}")

        if fp:
            fp.close()

def parse_version(ver_str):
    """Convert a version string into a tuple."""

    return tuple(map(int, ver_str.split(".")))


async def main():
    """Main program"""

    parser = argparse.ArgumentParser(description="Build docs for different sphinx_versions.")

    parser.add_argument('-V', '--version', help='Sphinx single version',
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
    parser.add_argument('-v', '--verbose',
                        help='Verbose all commands',
                        action='store_true')
    parser.add_argument('-l', '--log',
                        help='Log command output on a file')

    args = parser.parse_args()

    if not args.make_args:
        args.make_args = []

    if args.version:
        if args.min_version or args.max_version:
            sys.exit("Use either --version or --min-version/--max-version")
        else:
            args.min_version = args.version
            args.max_version = args.version

    sphinx_versions = sorted(list(SPHINX_REQUIREMENTS.keys()))

    if not args.min_version:
        args.min_version = sphinx_versions[0]

    if not args.max_version:
        args.max_version = sphinx_versions[-1]

    venv = SphinxVenv()
    await venv.run(args)


# Call main method
if __name__ == "__main__":
    asyncio.run(main())
