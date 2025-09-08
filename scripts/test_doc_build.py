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
PYTHON_VER_CHANGES = {(8, 0, 0): PYTHON}

DEFAULT_VERSIONS_TO_TEST = [
    (3, 4, 3),   # Minimal supported version
    (5, 3, 0),   # CentOS Stream 9 / AlmaLinux 9
    (6, 1, 1),   # Debian 12
    (7, 2, 1),   # openSUSE Leap 15.6
    (7, 2, 6),   # Ubuntu 24.04 LTS
    (7, 4, 7),   # Ubuntu 24.10
    (7, 3, 0),   # openSUSE Tumbleweed
    (8, 1, 3),   # Fedora 42
    (8, 2, 3)    # Latest version - covers rolling distros
]

# Sphinx versions to be installed and their incremental requirements
SPHINX_REQUIREMENTS = {
    # Oldest versions we support for each package required by Sphinx 3.4.3
    (3, 4, 3): {
        "docutils": "0.16",
        "alabaster": "0.7.12",
        "babel": "2.8.0",
        "certifi": "2020.6.20",
        "docutils": "0.16",
        "idna": "2.10",
        "imagesize": "1.2.0",
        "Jinja2": "2.11.2",
        "MarkupSafe": "1.1.1",
        "packaging": "20.4",
        "Pygments": "2.6.1",
        "PyYAML": "5.1",
        "requests": "2.24.0",
        "snowballstemmer": "2.0.0",
        "sphinxcontrib-applehelp": "1.0.2",
        "sphinxcontrib-devhelp": "1.0.2",
        "sphinxcontrib-htmlhelp": "1.0.3",
        "sphinxcontrib-jsmath": "1.0.1",
        "sphinxcontrib-qthelp": "1.0.3",
        "sphinxcontrib-serializinghtml": "1.1.4",
        "urllib3": "1.25.9",
    },

    # Update package dependencies to a more modern base. The goal here
    # is to avoid to many incremental changes for the next entries
    (3, 5, 0): {
        "alabaster": "0.7.13",
        "babel": "2.17.0",
        "certifi": "2025.6.15",
        "idna": "3.10",
        "imagesize": "1.4.1",
        "packaging": "25.0",
        "Pygments": "2.8.1",
        "requests": "2.32.4",
        "snowballstemmer": "3.0.1",
        "sphinxcontrib-applehelp": "1.0.4",
        "sphinxcontrib-htmlhelp": "2.0.1",
        "sphinxcontrib-serializinghtml": "1.1.5",
        "urllib3": "2.0.0",
    },

    # Starting from here, ensure all docutils versions are covered with
    # supported Sphinx versions. Other packages are upgraded only when
    # required by pip
    (4, 0, 0): {
        "PyYAML": "5.1",
    },
    (4, 1, 0): {
        "docutils": "0.17",
        "Pygments": "2.19.1",
        "Jinja2": "3.0.3",
        "MarkupSafe": "2.0",
    },
    (4, 3, 0): {},
    (4, 4, 0): {},
    (4, 5, 0): {
        "docutils": "0.17.1",
    },
    (5, 0, 0): {},
    (5, 1, 0): {},
    (5, 2, 0): {
        "docutils": "0.18",
        "Jinja2": "3.1.2",
        "MarkupSafe": "2.0",
        "PyYAML": "5.3.1",
    },
    (5, 3, 0): {
        "docutils": "0.18.1",
    },
    (6, 0, 0): {},
    (6, 1, 0): {},
    (6, 2, 0): {
        "PyYAML": "5.4.1",
    },
    (7, 0, 0): {},
    (7, 1, 0): {},
    (7, 2, 0): {
        "docutils": "0.19",
        "PyYAML": "6.0.1",
        "sphinxcontrib-serializinghtml": "1.1.9",
    },
    (7, 2, 6): {
        "docutils": "0.20",
    },
    (7, 3, 0): {
        "alabaster": "0.7.14",
        "PyYAML": "6.0.1",
        "tomli": "2.0.1",
    },
    (7, 4, 0): {
        "docutils": "0.20.1",
        "PyYAML": "6.0.1",
    },
    (8, 0, 0): {
        "docutils": "0.21",
    },
    (8, 1, 0): {
        "docutils": "0.21.1",
        "PyYAML": "6.0.1",
        "sphinxcontrib-applehelp": "1.0.7",
        "sphinxcontrib-devhelp": "1.0.6",
        "sphinxcontrib-htmlhelp": "2.0.6",
        "sphinxcontrib-qthelp": "1.0.6",
    },
    (8, 2, 0): {
        "docutils": "0.21.2",
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
        out = out.removesuffix('\n')

        if verbose:
            if is_info:
                print(out)
            else:
                print(out, file=sys.stderr)

        if self.fp:
            self.fp.write(out + "\n")

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

        if not self.first_run and args.wait_input and args.build:
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
        if args.req_file:
            with open(req_file, "w", encoding="utf-8") as fp:
                fp.write(result.stdout)

        if args.build:
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
            make = ["make"]

            if args.output:
                sphinx_build = os.path.realpath(f"{bin_dir}/sphinx-build")
                make += [f"O={args.output}", f"SPHINXBUILD={sphinx_build}"]

            if args.make_args:
                make += args.make_args

            make += args.targets

            if args.verbose:
                cmd.log(f". {bin_dir}/activate", verbose=True)
            await cmd.run(make, env=env, check=True, verbose=True)
            if args.verbose:
                cmd.log("deactivate", verbose=True)

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

        vers = set(SPHINX_REQUIREMENTS.keys()) | set(args.versions)

        for cur_ver in sorted(vers):
            if cur_ver in SPHINX_REQUIREMENTS:
                new_reqs = SPHINX_REQUIREMENTS[cur_ver]
                cur_requirements.update(new_reqs)

            if cur_ver in PYTHON_VER_CHANGES:          # pylint: disable=R1715
                python_bin = PYTHON_VER_CHANGES[cur_ver]

            if cur_ver not in args.versions:
                continue

            if args.min_version:
                if cur_ver < args.min_version:
                    continue

            if args.max_version:
                if cur_ver > args.max_version:
                    break

            await self._handle_version(args, fp, cur_ver, cur_requirements,
                                       python_bin)

        if args.build:
            cmd = AsyncCommands(fp)
            cmd.log("\nSummary:", verbose=True)
            for ver, elapsed_time in sorted(self.built_time.items()):
                cmd.log(f"\tSphinx {ver} elapsed time: {elapsed_time}",
                        verbose=True)

        if fp:
            fp.close()

def parse_version(ver_str):
    """Convert a version string into a tuple."""

    return tuple(map(int, ver_str.split(".")))


DEFAULT_VERS = "    - "
DEFAULT_VERS += "\n    - ".join(map(lambda v: f"{v[0]}.{v[1]}.{v[2]}",
                                    DEFAULT_VERSIONS_TO_TEST))

SCRIPT = os.path.relpath(__file__)

DESCRIPTION = f"""
This tool allows creating Python virtual environments for different
Sphinx versions that are supported by the Linux Kernel build system.

Besides creating the virtual environment, it can also test building
the documentation using "make htmldocs" (and/or other doc targets).

If called without "--versions" argument, it covers the versions shipped
on major distros, plus the lowest supported version:

{DEFAULT_VERS}

A typical usage is to run:

   {SCRIPT} -m -l sphinx_builds.log

This will create one virtual env for the default version set and run
"make htmldocs" for each version, creating a log file with the
excecuted commands on it.

NOTE: The build time can be very long, specially on old versions. Also, there
is a known bug with Sphinx version 6.0.x: each subprocess uses a lot of
memory. That, together with "-jauto" may cause OOM killer to cause
failures at the doc generation. To minimize the risk, you may use the
"-a" command line parameter to constrain the built directories and/or
reduce the number of threads from "-jauto" to, for instance, "-j4":

    {SCRIPT} -m -V 6.0.1 -a "SPHINXDIRS=process" "SPHINXOPTS='-j4'"

"""

MAKE_TARGETS = [
    "htmldocs",
    "texinfodocs",
    "infodocs",
    "latexdocs",
    "pdfdocs",
    "epubdocs",
    "xmldocs",
]

async def main():
    """Main program"""

    parser = argparse.ArgumentParser(description=DESCRIPTION,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)

    ver_group = parser.add_argument_group("Version range options")

    ver_group.add_argument('-V', '--versions', nargs="*",
                           default=DEFAULT_VERSIONS_TO_TEST,type=parse_version,
                           help='Sphinx versions to test')
    ver_group.add_argument('--min-version', "--min", type=parse_version,
                           help='Sphinx minimal version')
    ver_group.add_argument('--max-version', "--max", type=parse_version,
                           help='Sphinx maximum version')
    ver_group.add_argument('-f', '--full', action='store_true',
                           help='Add all Sphinx (major,minor) supported versions to the version range')

    build_group = parser.add_argument_group("Build options")

    build_group.add_argument('-b', '--build', action='store_true',
                             help='Build documentation')
    build_group.add_argument('-a', '--make-args', nargs="*",
                             help='extra arguments for make, like SPHINXDIRS=netlink/specs',
                        )
    build_group.add_argument('-t', '--targets', nargs="+", choices=MAKE_TARGETS,
                             default=[MAKE_TARGETS[0]],
                             help="make build targets. Default: htmldocs.")
    build_group.add_argument("-o", '--output',
                             help="output directory for the make O=OUTPUT")

    other_group = parser.add_argument_group("Other options")

    other_group.add_argument('-r', '--req-file', action='store_true',
                             help='write a requirements.txt file')
    other_group.add_argument('-l', '--log',
                             help='Log command output on a file')
    other_group.add_argument('-v', '--verbose', action='store_true',
                             help='Verbose all commands')
    other_group.add_argument('-i', '--wait-input', action='store_true',
                        help='Wait for an enter before going to the next version')

    args = parser.parse_args()

    if not args.make_args:
        args.make_args = []

    sphinx_versions = sorted(list(SPHINX_REQUIREMENTS.keys()))

    if args.full:
        args.versions += list(SPHINX_REQUIREMENTS.keys())

    venv = SphinxVenv()
    await venv.run(args)


# Call main method
if __name__ == "__main__":
    asyncio.run(main())
