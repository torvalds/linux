# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import re
import sbom.sbom_logging as sbom_logging
from sbom.path_utils import PathStr

# Match dependencies on config files
# Example match: "$(wildcard include/config/CONFIG_SOMETHING)"
CONFIG_PATTERN = re.compile(r"\$\(wildcard (include/config/[^)]+)\)")

# Match dependencies on the objtool binary
# Example match: "$(wildcard ./tools/objtool/objtool)"
OBJTOOL_PATTERN = re.compile(r"\$\(wildcard \./tools/objtool/objtool\)")

# Match any Makefile wildcard reference
# Example match: "$(wildcard path/to/file)"
WILDCARD_PATTERN = re.compile(r"\$\(wildcard (?P<path>[^)]+)\)")

# Match ordinary paths:
# - ^(\/)?: Optionally starts with a '/'
# - (([\w\-\.,+~=@ ]*)\/)*: Zero or more directory levels
# - [\w\-\.,+~=@ ]+$: Path component (file or directory)
# Example matches: "/foo/bar.c", "dir1/dir2/file.txt", "plainfile"
VALID_PATH_PATTERN = re.compile(r"^(\/)?(([\w\-\.,+~=@ ]*)\/)*[\w\-\.,+~=@ ]+$")


def parse_cmd_file_deps(deps: list[str]) -> list[PathStr]:
    """
    Parse dependency strings of a .cmd file and return valid input file paths.

    Args:
        deps: List of dependency strings as found in `.cmd` files.

    Returns:
        input_files: List of input file paths
    """
    input_files: list[PathStr] = []
    for dep in deps:
        dep = dep.strip()
        match dep:
            case _ if CONFIG_PATTERN.match(dep) or OBJTOOL_PATTERN.match(dep):
                # config paths like include/config/<CONFIG_NAME> should not be included in the graph
                continue
            case _ if match := WILDCARD_PATTERN.match(dep):
                path = match.group("path")
                input_files.append(path)
            case _ if VALID_PATH_PATTERN.match(dep):
                input_files.append(dep)
            case _:
                sbom_logging.error("Skip parsing dependency {dep} because of unrecognized format", dep=dep)
    return input_files
