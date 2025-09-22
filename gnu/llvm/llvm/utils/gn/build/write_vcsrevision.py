#!/usr/bin/env python3

"""Gets the current revision and writes it to VCSRevision.h."""

import argparse
import os
import subprocess
import sys


THIS_DIR = os.path.abspath(os.path.dirname(__file__))
LLVM_DIR = os.path.dirname(os.path.dirname(os.path.dirname(THIS_DIR)))


def which(program):
    # distutils.spawn.which() doesn't find .bat files,
    # https://bugs.python.org/issue2200
    for path in os.environ["PATH"].split(os.pathsep):
        candidate = os.path.join(path, program)
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-d",
        "--depfile",
        help="if set, writes a depfile that causes this script "
        "to re-run each time the current revision changes",
    )
    parser.add_argument(
        "--write-git-rev",
        action="store_true",
        help="if set, writes git revision, else writes #undef",
    )
    parser.add_argument(
        "--name",
        action="append",
        help="if set, writes a depfile that causes this script "
        "to re-run each time the current revision changes",
    )
    parser.add_argument("vcs_header", help="path to the output file to write")
    args = parser.parse_args()

    vcsrevision_contents = ""
    if args.write_git_rev:
        git, use_shell = which("git"), False
        if not git:
            git = which("git.exe")
        if not git:
            git, use_shell = which("git.bat"), True
        git_dir = (
            subprocess.check_output(
                [git, "rev-parse", "--git-dir"], cwd=LLVM_DIR, shell=use_shell
            )
            .decode()
            .strip()
        )
        if not os.path.isdir(git_dir):
            print('.git dir not found at "%s"' % git_dir, file=sys.stderr)
            return 1

        rev = (
            subprocess.check_output(
                [git, "rev-parse", "--short", "HEAD"], cwd=git_dir, shell=use_shell
            )
            .decode()
            .strip()
        )
        url = (
            subprocess.check_output(
                [git, "remote", "get-url", "origin"], cwd=git_dir, shell=use_shell
            )
            .decode()
            .strip()
        )
        for name in args.name:
            vcsrevision_contents += '#define %s_REVISION "%s"\n' % (name, rev)
            vcsrevision_contents += '#define %s_REPOSITORY "%s"\n' % (name, url)
    else:
        for name in args.name:
            vcsrevision_contents += "#undef %s_REVISION\n" % name
            vcsrevision_contents += "#undef %s_REPOSITORY\n" % name

    # If the output already exists and is identical to what we'd write,
    # return to not perturb the existing file's timestamp.
    if (
        os.path.exists(args.vcs_header)
        and open(args.vcs_header).read() == vcsrevision_contents
    ):
        return 0

    # http://neugierig.org/software/blog/2014/11/binary-revisions.html
    if args.depfile:
        build_dir = os.getcwd()
        with open(args.depfile, "w") as depfile:
            depfile.write(
                "%s: %s\n"
                % (
                    args.vcs_header,
                    os.path.relpath(os.path.join(git_dir, "logs", "HEAD"), build_dir),
                )
            )
    open(args.vcs_header, "w").write(vcsrevision_contents)


if __name__ == "__main__":
    sys.exit(main())
