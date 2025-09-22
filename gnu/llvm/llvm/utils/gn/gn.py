#!/usr/bin/env python3
"""Calls `gn` with the right --dotfile= and --root= arguments for LLVM."""

# GN normally expects a file called '.gn' at the root of the repository.
# Since LLVM's GN build isn't supported, putting that file at the root
# is deemed inappropriate, which requires passing --dotfile= and -root= to GN.
# Since that gets old fast, this script automatically passes these arguments.

import os
import subprocess
import sys


THIS_DIR = os.path.dirname(__file__)
ROOT_DIR = os.path.join(THIS_DIR, "..", "..", "..")


def get_platform():
    import platform

    if sys.platform == "darwin":
        return "mac-amd64" if platform.machine() != "arm64" else "mac-arm64"
    if platform.machine() not in ("AMD64", "x86_64"):
        return None
    if sys.platform.startswith("linux"):
        return "linux-amd64"
    if sys.platform == "win32":
        return "windows-amd64"


def print_no_gn(mention_get):
    print("gn binary not found in PATH")
    if mention_get:
        print("run llvm/utils/gn/get.py to download a binary and try again, or")
    print("follow https://gn.googlesource.com/gn/#getting-started")
    return 1


def main():
    # Find real gn executable.
    gn = "gn"
    if (
        subprocess.call(
            "gn --version",
            stdout=open(os.devnull, "w"),
            stderr=subprocess.STDOUT,
            shell=True,
        )
        != 0
    ):
        # Not on path. See if get.py downloaded a prebuilt binary and run that
        # if it's there, or suggest to run get.py if it isn't.
        platform = get_platform()
        if not platform:
            return print_no_gn(mention_get=False)
        gn = os.path.join(os.path.dirname(__file__), "bin", platform, "gn")
        if not os.path.exists(gn + (".exe" if sys.platform == "win32" else "")):
            return print_no_gn(mention_get=True)

    # Compute --dotfile= and --root= args to add.
    extra_args = []
    gn_main_arg = next((x for x in sys.argv[1:] if not x.startswith("-")), None)
    if gn_main_arg != "help":  # `gn help` gets confused by the switches.
        cwd = os.getcwd()
        dotfile = os.path.relpath(os.path.join(THIS_DIR, ".gn"), cwd)
        root = os.path.relpath(ROOT_DIR, cwd)
        extra_args = ["--dotfile=" + dotfile, "--root=" + root]

    # Run GN command with --dotfile= and --root= added.
    cmd = [gn] + extra_args + sys.argv[1:]
    sys.exit(subprocess.call(cmd))


if __name__ == "__main__":
    main()
