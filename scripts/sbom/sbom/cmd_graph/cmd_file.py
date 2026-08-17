# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import os
import re
from dataclasses import dataclass, field
from sbom.cmd_graph.deps_parser import parse_cmd_file_deps
from sbom.cmd_graph.savedcmd_parser import parse_inputs_from_commands
import sbom.sbom_logging as sbom_logging
from sbom.path_utils import PathStr

SAVEDCMD_PATTERN = re.compile(r"^(saved)?cmd_.*?:=\s*(?P<full_command>.+)$")
SOURCE_PATTERN = re.compile(r"^source.*?:=\s*(?P<source_file>.+)$")


@dataclass
class CmdFile:
    cmd_file_path: PathStr
    savedcmd: str
    source: PathStr | None = None
    deps: list[str] = field(default_factory=list)
    make_rules: list[str] = field(default_factory=list)

    @classmethod
    def create(cls, cmd_file_path: PathStr) -> "CmdFile | None":
        """
        Parses a .cmd file.
        .cmd files are assumed to have one of the following structures:
        1. Full Cmd File
            (saved)?cmd_<output> := <command>
            source_<output> := <main_input>
            deps_<output> := \
            <dependencies>
            <output> := $(deps_<output>)
            $(deps_<output>):

        2. Command Only Cmd File
            (saved)?cmd_<output> := <command>

        3. Single Dependency Cmd File
            (saved)?cmd_<output> := <command>
            <output> : <dependency>

        Args:
            cmd_file_path (Path): absolute Path to a .cmd file

        Returns:
            cmd_file (CmdFile): Parsed cmd file.
        """
        with open(cmd_file_path, "rt", encoding="utf-8") as f:
            lines = [line.strip() for line in f.readlines() if line.strip() != "" and not line.startswith("#")]

        # savedcmd
        match = SAVEDCMD_PATTERN.match(lines[0] if lines else "")
        if match is None:
            sbom_logging.error(
                "Skip parsing '{cmd_file_path}' because no 'savedcmd_' command was found.", cmd_file_path=cmd_file_path
            )
            return None
        savedcmd = match.group("full_command")

        # Command Only Cmd File
        if len(lines) == 1:
            return CmdFile(cmd_file_path, savedcmd)

        # Single Dependency Cmd File
        if len(lines) == 2:
            parts = lines[1].split(":", 1)
            if len(parts) != 2:
                sbom_logging.error(
                    "Skip parsing '{cmd_file_path}'. Expected dependency line '<output>: <dependency>' but got {second_line}", cmd_file_path=cmd_file_path, second_line=lines[1]
                )
                return None
            dep = parts[1].strip()
            return CmdFile(cmd_file_path, savedcmd, deps=[dep])

        # Full Cmd File
        # source
        line1 = SOURCE_PATTERN.match(lines[1])
        if line1 is None:
            sbom_logging.error(
                "Skip parsing '{cmd_file_path}' because no 'source_' entry was found.", cmd_file_path=cmd_file_path
            )
            return CmdFile(cmd_file_path, savedcmd)
        source = line1.group("source_file")

        # deps
        deps: list[str] = []
        i = 3  # lines[2] includes the variable assignment but no actual dependency, so we need to start at lines[3].
        while i < len(lines):
            if not lines[i].endswith("\\"):
                break
            deps.append(lines[i][:-1].strip())
            i += 1

        # make_rules
        make_rules = lines[i:]

        return CmdFile(cmd_file_path, savedcmd, source, deps, make_rules)

    def get_dependencies(
        self: "CmdFile", target_path: PathStr, obj_tree: PathStr, fail_on_unknown_build_command: bool
    ) -> list[PathStr]:
        """
        Parses all dependencies required to build a target file from its cmd file.

        Args:
            target_path: path to the target file relative to `obj_tree`.
            obj_tree: absolute path to the object tree.
            fail_on_unknown_build_command: Whether to fail if an unknown build command is encountered.

        Returns:
            list[PathStr]: dependency file paths relative to `obj_tree`.
        """
        input_files: list[PathStr] = [
            str(p) for p in parse_inputs_from_commands(self.savedcmd, fail_on_unknown_build_command)
        ]
        if self.deps:
            input_files += [str(p) for p in parse_cmd_file_deps(self.deps)]
        input_files = _expand_resolve_files(input_files, obj_tree)

        cmd_file_dependencies: list[PathStr] = []
        for input_file in input_files:
            # input files are either absolute or relative to the object tree
            if os.path.isabs(input_file):
                input_file = os.path.relpath(input_file, obj_tree)
            if input_file == target_path:
                # Skip target file to prevent cycles. This is necessary because some multi stage commands first create an output and then pass it as input to the next command, e.g., objcopy.
                continue
            cmd_file_dependencies.append(input_file)
        unique_cmd_file_dependencies = list(dict.fromkeys(cmd_file_dependencies))
        return unique_cmd_file_dependencies


def _expand_resolve_files(input_files: list[PathStr], obj_tree: PathStr) -> list[PathStr]:
    """
    Expands resolve files which may reference additional files via '@' notation.

    Args:
        input_files (list[PathStr]): List of file paths relative to the object tree, where paths starting with '@' refer to files
                                     containing further file paths, each on a separate line.
        obj_tree: Absolute path to the root of the object tree.

    Returns:
        list[PathStr]: Flattened list of all input file paths, with any nested '@' file references resolved recursively.
    """
    expanded_input_files: list[PathStr] = []
    for input_file in input_files:
        if not input_file.startswith("@"):
            expanded_input_files.append(input_file)
            continue
        resolve_file_path = os.path.join(obj_tree, input_file.removeprefix("@"))
        if not os.path.exists(resolve_file_path):
            sbom_logging.error(
                "Skip resolving '{resolve_file_path}' because the response file does not exist.",
                resolve_file_path=resolve_file_path,
            )
            continue
        with open(resolve_file_path, "rt", encoding="utf-8") as f:
            resolve_file_content = [line_stripped for line in f.readlines() if (line_stripped := line.strip())]
        expanded_input_files += _expand_resolve_files(resolve_file_content, obj_tree)
    return expanded_input_files
