#!/usr/bin/env python3
#
# ======- pre-push - LLVM Git Help Integration ---------*- python -*--========#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ==------------------------------------------------------------------------==#

"""
pre-push git hook integration
=============================

This script is intended to be setup as a pre-push hook, from the root of the
repo run:

   ln -sf ../../llvm/utils/git/pre-push.py .git/hooks/pre-push

From the git doc:

  The pre-push hook runs during git push, after the remote refs have been
  updated but before any objects have been transferred. It receives the name
  and location of the remote as parameters, and a list of to-be-updated refs
  through stdin. You can use it to validate a set of ref updates before a push
  occurs (a non-zero exit code will abort the push).
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from shlex import quote

VERBOSE = False
QUIET = False
dev_null_fd = None
z40 = "0000000000000000000000000000000000000000"


def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def log(*args, **kwargs):
    if QUIET:
        return
    print(*args, **kwargs)


def log_verbose(*args, **kwargs):
    if not VERBOSE:
        return
    print(*args, **kwargs)


def die(msg):
    eprint(msg)
    sys.exit(1)


def ask_confirm(prompt):
    while True:
        query = input("%s (y/N): " % (prompt))
        if query.lower() not in ["y", "n", ""]:
            print("Expect y or n!")
            continue
        return query.lower() == "y"


def get_dev_null():
    """Lazily create a /dev/null fd for use in shell()"""
    global dev_null_fd
    if dev_null_fd is None:
        dev_null_fd = open(os.devnull, "w")
    return dev_null_fd


def shell(
    cmd,
    strip=True,
    cwd=None,
    stdin=None,
    die_on_failure=True,
    ignore_errors=False,
    text=True,
    print_raw_stderr=False,
):
    # Escape args when logging for easy repro.
    quoted_cmd = [quote(arg) for arg in cmd]
    cwd_msg = ""
    if cwd:
        cwd_msg = " in %s" % cwd
    log_verbose("Running%s: %s" % (cwd_msg, " ".join(quoted_cmd)))

    err_pipe = subprocess.PIPE
    if ignore_errors:
        # Silence errors if requested.
        err_pipe = get_dev_null()

    start = time.time()
    p = subprocess.Popen(
        cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=err_pipe,
        stdin=subprocess.PIPE,
        universal_newlines=text,
    )
    stdout, stderr = p.communicate(input=stdin)
    elapsed = time.time() - start

    log_verbose("Command took %0.1fs" % elapsed)

    if p.returncode == 0 or ignore_errors:
        if stderr and not ignore_errors:
            if not print_raw_stderr:
                eprint("`%s` printed to stderr:" % " ".join(quoted_cmd))
            eprint(stderr.rstrip())
        if strip:
            if text:
                stdout = stdout.rstrip("\r\n")
            else:
                stdout = stdout.rstrip(b"\r\n")
        if VERBOSE:
            for l in stdout.splitlines():
                log_verbose("STDOUT: %s" % l)
        return stdout
    err_msg = "`%s` returned %s" % (" ".join(quoted_cmd), p.returncode)
    eprint(err_msg)
    if stderr:
        eprint(stderr.rstrip())
    if die_on_failure:
        sys.exit(2)
    raise RuntimeError(err_msg)


def git(*cmd, **kwargs):
    return shell(["git"] + list(cmd), **kwargs)


def get_revs_to_push(range):
    commits = git("rev-list", range).splitlines()
    # Reverse the order so we print the oldest commit first
    commits.reverse()
    return commits


def handle_push(args, local_ref, local_sha, remote_ref, remote_sha):
    """Check a single push request (which can include multiple revisions)"""
    log_verbose(
        "Handle push, reproduce with "
        "`echo %s %s %s %s | pre-push.py %s %s"
        % (local_ref, local_sha, remote_ref, remote_sha, args.remote, args.url)
    )
    # Handle request to delete
    if local_sha == z40:
        if not ask_confirm(
            'Are you sure you want to delete "%s" on remote "%s"?'
            % (remote_ref, args.url)
        ):
            die("Aborting")
        return

    # Push a new branch
    if remote_sha == z40:
        if not ask_confirm(
            'Are you sure you want to push a new branch/tag "%s" on remote "%s"?'
            % (remote_ref, args.url)
        ):
            die("Aborting")
        range = local_sha
        return
    else:
        # Update to existing branch, examine new commits
        range = "%s..%s" % (remote_sha, local_sha)
        # Check that the remote commit exists, otherwise let git proceed
        if "commit" not in git("cat-file", "-t", remote_sha, ignore_errors=True):
            return

    revs = get_revs_to_push(range)
    if not revs:
        # This can happen if someone is force pushing an older revision to a branch
        return

    # Print the revision about to be pushed commits
    print('Pushing to "%s" on remote "%s"' % (remote_ref, args.url))
    for sha in revs:
        print(" - " + git("show", "--oneline", "--quiet", sha))

    if len(revs) > 1:
        if not ask_confirm("Are you sure you want to push %d commits?" % len(revs)):
            die("Aborting")

    for sha in revs:
        msg = git("log", "--format=%B", "-n1", sha)
        if "Differential Revision" not in msg:
            continue
        for line in msg.splitlines():
            for tag in ["Summary", "Reviewers", "Subscribers", "Tags"]:
                if line.startswith(tag + ":"):
                    eprint(
                        'Please remove arcanist tags from the commit message (found "%s" tag in %s)'
                        % (tag, sha[:12])
                    )
                    if len(revs) == 1:
                        eprint("Try running: llvm/utils/git/arcfilter.sh")
                    die('Aborting (force push by adding "--no-verify")')

    return


if __name__ == "__main__":
    if not shutil.which("git"):
        die("error: cannot find git command")

    argv = sys.argv[1:]
    p = argparse.ArgumentParser(
        prog="pre-push",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    verbosity_group = p.add_mutually_exclusive_group()
    verbosity_group.add_argument(
        "-q", "--quiet", action="store_true", help="print less information"
    )
    verbosity_group.add_argument(
        "-v", "--verbose", action="store_true", help="print more information"
    )

    p.add_argument("remote", type=str, help="Name of the remote")
    p.add_argument("url", type=str, help="URL for the remote")

    args = p.parse_args(argv)
    VERBOSE = args.verbose
    QUIET = args.quiet

    lines = sys.stdin.readlines()
    sys.stdin = open("/dev/tty", "r")
    for line in lines:
        local_ref, local_sha, remote_ref, remote_sha = line.split()
        handle_push(args, local_ref, local_sha, remote_ref, remote_sha)
