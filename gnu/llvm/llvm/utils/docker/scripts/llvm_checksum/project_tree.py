"""Contains helper functions to compute checksums for LLVM checkouts.
"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import logging
import os
import os.path
import sys


class LLVMProject(object):
    """An LLVM project with a descriptive name and a relative checkout path."""

    def __init__(self, name, relpath):
        self.name = name
        self.relpath = relpath

    def is_subproject(self, other_project):
        """Check if self is checked out as a subdirectory of other_project."""
        return self.relpath.startswith(other_project.relpath)


def WalkProjectFiles(checkout_root, all_projects, project, visitor):
    """Walk over all files inside a project without recursing into subprojects, '.git' and '.svn' subfolders.

    checkout_root: root of the LLVM checkout.
    all_projects: projects in the LLVM checkout.
    project: a project to walk the files of. Must be inside all_projects.
    visitor: a function called on each visited file.
    """
    assert project in all_projects

    ignored_paths = set()
    for other_project in all_projects:
        if other_project != project and other_project.is_subproject(project):
            ignored_paths.add(os.path.join(checkout_root, other_project.relpath))

    def raise_error(err):
        raise err

    project_root = os.path.join(checkout_root, project.relpath)
    for root, dirs, files in os.walk(project_root, onerror=raise_error):
        dirs[:] = [
            d
            for d in dirs
            if d != ".svn"
            and d != ".git"
            and os.path.join(root, d) not in ignored_paths
        ]
        for f in files:
            visitor(os.path.join(root, f))


def CreateLLVMProjects(single_tree_checkout):
    """Returns a list of LLVMProject instances, describing relative paths of a typical LLVM checkout.

    Args:
      single_tree_checkout:
        When True, relative paths for each project points to a typical single
          source tree checkout.
        When False, relative paths for each projects points to a separate
          directory. However, clang-tools-extra is an exception, its relative path
          will always be 'clang/tools/extra'.
    """
    # FIXME: cover all of llvm projects.

    # Projects that reside inside 'projects/' in a single source tree checkout.
    ORDINARY_PROJECTS = [
        "compiler-rt",
        "dragonegg",
        "libcxx",
        "libcxxabi",
        "libunwind",
        "test-suite",
    ]
    # Projects that reside inside 'tools/' in a single source tree checkout.
    TOOLS_PROJECTS = ["clang", "lld", "lldb"]

    if single_tree_checkout:
        projects = [LLVMProject("llvm", "")]
        projects += [
            LLVMProject(p, os.path.join("projects", p)) for p in ORDINARY_PROJECTS
        ]
        projects += [LLVMProject(p, os.path.join("tools", p)) for p in TOOLS_PROJECTS]
        projects.append(
            LLVMProject(
                "clang-tools-extra", os.path.join("tools", "clang", "tools", "extra")
            )
        )
    else:
        projects = [LLVMProject("llvm", "llvm")]
        projects += [LLVMProject(p, p) for p in ORDINARY_PROJECTS]
        projects += [LLVMProject(p, p) for p in TOOLS_PROJECTS]
        projects.append(
            LLVMProject("clang-tools-extra", os.path.join("clang", "tools", "extra"))
        )
    return projects
