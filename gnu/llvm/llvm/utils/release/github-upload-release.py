#!/usr/bin/env python3
# ===-- github-upload-release.py  ------------------------------------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#
#
# Create and manage releases in the llvm github project.
#
# This script requires python3 and the PyGithub module.
#
# Example Usage:
#
# You will need to obtain a personal access token for your github account in
# order to use this script.  Instructions for doing this can be found here:
# https://help.github.com/en/articles/creating-a-personal-access-token-for-the-command-line
#
# Create a new release from an existing tag:
# ./github-upload-release.py --token $github_token --release 8.0.1-rc4 create
#
# Upload files for a release
# ./github-upload-release.py --token $github_token --release 8.0.1-rc4 upload --files llvm-8.0.1rc4.src.tar.xz
#
# You can upload as many files as you want at a time and use wildcards e.g.
# ./github-upload-release.py --token $github_token --release 8.0.1-rc4 upload --files *.src.*
# ===------------------------------------------------------------------------===#


import argparse
import github
import sys
from textwrap import dedent


def create_release(repo, release, tag=None, name=None, message=None):
    if not tag:
        tag = "llvmorg-{}".format(release)

    if not name:
        name = "LLVM {}".format(release)

    if not message:
        message = dedent(
            """\
            LLVM {} Release

            # A note on binaries

            Volunteers make binaries for the LLVM project, which will be uploaded
            when they have had time to test and build these binaries. They might
            not be available directly or not at all for each release. We suggest
            you use the binaries from your distribution or build your own if you
            rely on a specific platform or configuration."""
        ).format(release)

    prerelease = True if "rc" in release else False

    repo.create_git_release(tag=tag, name=name, message=message, prerelease=prerelease)


def upload_files(repo, release, files):
    release = repo.get_release("llvmorg-{}".format(release))
    for f in files:
        print("Uploading {}".format(f))
        release.upload_asset(f)
        print("Done")


parser = argparse.ArgumentParser()
parser.add_argument(
    "command", type=str, choices=["create", "upload", "check-permissions"]
)

# All args
parser.add_argument("--token", type=str)
parser.add_argument("--release", type=str)
parser.add_argument("--user", type=str)
parser.add_argument("--user-token", type=str)

# Upload args
parser.add_argument("--files", nargs="+", type=str)

args = parser.parse_args()

gh = github.Github(args.token)
llvm_org = gh.get_organization("llvm")
llvm_repo = llvm_org.get_repo("llvm-project")

if args.user:
    if not args.user_token:
        print("--user-token option required when --user is used")
        sys.exit(1)
    # Validate that this user is allowed to modify releases.
    user = gh.get_user(args.user)
    team = (
        github.Github(args.user_token)
        .get_organization("llvm")
        .get_team_by_slug("llvm-release-managers")
    )
    if not team.has_in_members(user):
        print("User {} is not a allowed to modify releases".format(args.user))
        sys.exit(1)
elif args.command == "check-permissions":
    print("--user option required for check-permissions")
    sys.exit(1)

if args.command == "create":
    create_release(llvm_repo, args.release)
if args.command == "upload":
    upload_files(llvm_repo, args.release, args.files)
