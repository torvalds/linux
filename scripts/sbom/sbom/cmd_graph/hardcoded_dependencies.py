# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import os
from typing import Callable
import sbom.sbom_logging as sbom_logging
from sbom.path_utils import PathStr, is_relative_to
from sbom.environment import Environment

HARDCODED_DEPENDENCIES: dict[str, list[str]] = {
    # defined in linux/Kbuild
    "include/generated/rq-offsets.h": ["kernel/sched/rq-offsets.s"],
    "kernel/sched/rq-offsets.s": ["include/generated/asm-offsets.h"],
    "include/generated/bounds.h": ["kernel/bounds.s"],
    "include/generated/asm-offsets.h": ["arch/{arch}/kernel/asm-offsets.s"],
}
"""
Maps file paths to the list of dependencies required to build them
which are not tracked by the .cmd dependency mechanism.
Paths are relative to either the source tree or the object tree.
"""

def get_hardcoded_dependencies(path: PathStr, obj_tree: PathStr, src_tree: PathStr) -> list[PathStr]:
    """
    Some files in the kernel build process are not tracked by the .cmd dependency mechanism.
    Parsing these dependencies programmatically is too complex for the scope of this project.
    Therefore, this function provides manually defined dependencies to be added to the build graph.

    Args:
        path: absolute path to a file within the src tree or object tree.
        obj_tree: absolute Path to the base directory of the object tree.
        src_tree: absolute Path to the `linux` source directory.

    Returns:
        list[PathStr]: A list of dependency file paths (relative to the object tree) required to build the file at the given path.
    """
    if is_relative_to(path, obj_tree):
        path = os.path.relpath(path, obj_tree)
    elif is_relative_to(path, src_tree):
        path = os.path.relpath(path, src_tree)

    if path not in HARDCODED_DEPENDENCIES:
        return []

    template_variables: dict[str, Callable[[], str | None]] = {
        "arch": lambda: _get_arch(path),
    }

    dependencies: list[PathStr] = []
    for dependency_template in HARDCODED_DEPENDENCIES[path]:
        dependency = _evaluate_template(dependency_template, template_variables)
        if dependency is None:
            continue
        if os.path.exists(os.path.join(obj_tree, dependency)):
            dependencies.append(dependency)
        elif os.path.exists(dependency_absolute := os.path.join(src_tree, dependency)):
            dependencies.append(os.path.relpath(dependency_absolute, obj_tree))
        else:
            sbom_logging.error(
                "Skip hardcoded dependency '{dependency}' for '{path}' because the dependency lies neither in the src tree nor the object tree.",
                dependency=dependency,
                path=path,
            )

    return dependencies


def _evaluate_template(template: str, variables: dict[str, Callable[[], str | None]]) -> str | None:
    for key, value_function in variables.items():
        template_key = "{" + key + "}"
        if template_key in template:
            value = value_function()
            if value is None:
                return None
            template = template.replace(template_key, value)
    return template


def _get_arch(path: PathStr):
    srcarch = Environment.SRCARCH()
    if srcarch is None:
        sbom_logging.error(
            "Skipped architecture specific hardcoded dependency for '{path}' because the SRCARCH environment variable was not set.",
            path=path,
        )
        return None
    return srcarch
