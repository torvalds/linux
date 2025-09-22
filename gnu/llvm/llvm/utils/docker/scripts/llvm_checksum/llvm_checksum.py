#!/usr/bin/env python
""" A small program to compute checksums of LLVM checkout.
"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import hashlib
import logging
import re
import sys
from argparse import ArgumentParser
from project_tree import *

SVN_DATES_REGEX = re.compile(r"\$(Date|LastChangedDate)[^\$]+\$")


def main():
    parser = ArgumentParser()
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="enable debug logging"
    )
    parser.add_argument(
        "-c",
        "--check",
        metavar="reference_file",
        help="read checksums from reference_file and "
        + "check they match checksums of llvm_path.",
    )
    parser.add_argument(
        "--partial",
        action="store_true",
        help="ignore projects from reference_file "
        + "that are not checked out in llvm_path.",
    )
    parser.add_argument(
        "--multi_dir",
        action="store_true",
        help="indicates llvm_path contains llvm, checked out "
        + "into multiple directories, as opposed to a "
        + "typical single source tree checkout.",
    )
    parser.add_argument("llvm_path")

    args = parser.parse_args()
    if args.check is not None:
        with open(args.check, "r") as f:
            reference_checksums = ReadLLVMChecksums(f)
    else:
        reference_checksums = None

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    llvm_projects = CreateLLVMProjects(not args.multi_dir)
    checksums = ComputeLLVMChecksums(args.llvm_path, llvm_projects)

    if reference_checksums is None:
        WriteLLVMChecksums(checksums, sys.stdout)
        sys.exit(0)

    if not ValidateChecksums(reference_checksums, checksums, args.partial):
        sys.stdout.write("Checksums differ.\nNew checksums:\n")
        WriteLLVMChecksums(checksums, sys.stdout)
        sys.stdout.write("Reference checksums:\n")
        WriteLLVMChecksums(reference_checksums, sys.stdout)
        sys.exit(1)
    else:
        sys.stdout.write("Checksums match.")


def ComputeLLVMChecksums(root_path, projects):
    """Compute checksums for LLVM sources checked out using svn.

    Args:
      root_path: a directory of llvm checkout.
      projects: a list of LLVMProject instances, which describe checkout paths,
        relative to root_path.

    Returns:
      A dict mapping from project name to project checksum.
    """
    hash_algo = hashlib.sha256

    def collapse_svn_substitutions(contents):
        # Replace svn substitutions for $Date$ and $LastChangedDate$.
        # Unfortunately, these are locale-specific.
        return SVN_DATES_REGEX.sub("$\1$", contents)

    def read_and_collapse_svn_subsitutions(file_path):
        with open(file_path, "rb") as f:
            contents = f.read()
            new_contents = collapse_svn_substitutions(contents)
            if contents != new_contents:
                logging.debug("Replaced svn keyword substitutions in %s", file_path)
                logging.debug("\n\tBefore\n%s\n\tAfter\n%s", contents, new_contents)
            return new_contents

    project_checksums = dict()
    # Hash each project.
    for proj in projects:
        project_root = os.path.join(root_path, proj.relpath)
        if not os.path.exists(project_root):
            logging.info(
                "Folder %s doesn't exist, skipping project %s", proj.relpath, proj.name
            )
            continue

        files = list()

        def add_file_hash(file_path):
            if os.path.islink(file_path) and not os.path.exists(file_path):
                content = os.readlink(file_path)
            else:
                content = read_and_collapse_svn_subsitutions(file_path)
            hasher = hash_algo()
            hasher.update(content)
            file_digest = hasher.hexdigest()
            logging.debug("Checksum %s for file %s", file_digest, file_path)
            files.append((file_path, file_digest))

        logging.info("Computing checksum for %s", proj.name)
        WalkProjectFiles(root_path, projects, proj, add_file_hash)

        # Compute final checksum.
        files.sort(key=lambda x: x[0])
        hasher = hash_algo()
        for file_path, file_digest in files:
            file_path = os.path.relpath(file_path, project_root)
            hasher.update(file_path)
            hasher.update(file_digest)
        project_checksums[proj.name] = hasher.hexdigest()
    return project_checksums


def WriteLLVMChecksums(checksums, f):
    """Writes checksums to a text file.

    Args:
      checksums: a dict mapping from project name to project checksum (result of
        ComputeLLVMChecksums).
      f: a file object to write into.
    """

    for proj in sorted(checksums.keys()):
        f.write("{} {}\n".format(checksums[proj], proj))


def ReadLLVMChecksums(f):
    """Reads checksums from a text file, produced by WriteLLVMChecksums.

    Returns:
      A dict, mapping from project name to project checksum.
    """
    checksums = {}
    while True:
        line = f.readline()
        if line == "":
            break
        checksum, proj = line.split()
        checksums[proj] = checksum
    return checksums


def ValidateChecksums(reference_checksums, new_checksums, allow_missing_projects=False):
    """Validates that reference_checksums and new_checksums match.

    Args:
      reference_checksums: a dict of reference checksums, mapping from a project
        name to a project checksum.
      new_checksums: a dict of checksums to be checked, mapping from a project
        name to a project checksum.
      allow_missing_projects:
        When True, reference_checksums may contain more projects than
          new_checksums. Projects missing from new_checksums are ignored.
        When False, new_checksums and reference_checksums must contain checksums
          for the same set of projects. If there is a project in
          reference_checksums, missing from new_checksums, ValidateChecksums
          will return False.

    Returns:
      True, if checksums match with regards to allow_missing_projects flag value.
      False, otherwise.
    """
    if not allow_missing_projects:
        if len(new_checksums) != len(reference_checksums):
            return False

    for proj, checksum in new_checksums.items():
        # We never computed a checksum for this project.
        if proj not in reference_checksums:
            return False
        # Checksum did not match.
        if reference_checksums[proj] != checksum:
            return False

    return True


if __name__ == "__main__":
    main()
