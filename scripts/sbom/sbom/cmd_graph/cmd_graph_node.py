# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass, field
from itertools import chain
import logging
import os
from typing import Iterator, Protocol

from sbom import sbom_logging
from sbom.cmd_graph.cmd_file import CmdFile
from sbom.cmd_graph.hardcoded_dependencies import get_hardcoded_dependencies
from sbom.cmd_graph.incbin_parser import parse_incbin_statements
from sbom.path_utils import PathStr, has_link, is_relative_to


@dataclass
class IncbinDependency:
    node: "CmdGraphNode"
    full_statement: str


class CmdGraphNodeConfig(Protocol):
    obj_tree: PathStr
    src_tree: PathStr
    fail_on_unknown_build_command: bool


@dataclass
class CmdGraphNode:
    """A node in the cmd graph representing a single file and its dependencies."""

    absolute_path: PathStr
    """Absolute path to the file this node represents."""

    cmd_file: CmdFile | None = None
    """Parsed .cmd file describing how the file at absolute_path was built, or None if not available."""

    cmd_file_dependencies: list["CmdGraphNode"] = field(default_factory=list)
    incbin_dependencies: list[IncbinDependency] = field(default_factory=list)
    hardcoded_dependencies: list["CmdGraphNode"] = field(default_factory=list)

    @property
    def children(self) -> Iterator["CmdGraphNode"]:
        seen: set[PathStr] = set()
        for node in chain(
            self.cmd_file_dependencies,
            (dep.node for dep in self.incbin_dependencies),
            self.hardcoded_dependencies,
        ):
            if node.absolute_path not in seen:
                seen.add(node.absolute_path)
                yield node

    @classmethod
    def create(
        cls,
        target_path: PathStr,
        config: CmdGraphNodeConfig,
        cache: dict[PathStr, "CmdGraphNode"] | None = None,
        depth: int = 0,
    ) -> "CmdGraphNode":
        """
        Recursively builds a dependency graph starting from `target_path`.
        Dependencies are mainly discovered by parsing the `.<target_path.name>.cmd` file.

        Args:
            target_path: Path to the target file relative to obj_tree.
            config: Config options
            cache: Tracks processed nodes to prevent cycles.
            depth: Internal parameter to track the current recursion depth.

        Returns:
            CmdGraphNode: cmd graph node representing the target file
        """
        if cache is None:
            cache = {}

        target_path_absolute = (
            os.path.realpath(p)
            if has_link(p:=os.path.join(config.obj_tree, target_path))
            else os.path.normpath(p)
        )

        if target_path_absolute in cache:
            return cache[target_path_absolute]

        if depth == 0:
            logging.debug(f"Build node: {target_path}")

        cmd_file_path = _to_cmd_path(target_path_absolute)
        cmd_file = CmdFile.create(cmd_file_path) if os.path.exists(cmd_file_path) else None
        node = CmdGraphNode(target_path_absolute, cmd_file)
        cache[target_path_absolute] = node

        if not os.path.exists(target_path_absolute):
            error_or_warning = (
                sbom_logging.error
                if is_relative_to(target_path_absolute, config.obj_tree)
                or is_relative_to(target_path_absolute, config.src_tree)
                else sbom_logging.warning
            )
            error_or_warning(
                "Skip parsing '{target_path_absolute}' because file does not exist",
                target_path_absolute=target_path_absolute,
            )
            return node

        # Search for dependencies to add to the graph as child nodes. Child paths are always relative to the output tree.
        def _build_child_node(child_path: PathStr) -> "CmdGraphNode":
            return CmdGraphNode.create(child_path, config, cache, depth + 1)

        node.hardcoded_dependencies = [
            _build_child_node(hardcoded_dependency_path)
            for hardcoded_dependency_path in get_hardcoded_dependencies(
                target_path_absolute, config.obj_tree, config.src_tree
            )
        ]

        if cmd_file is not None:
            node.cmd_file_dependencies = [
                _build_child_node(cmd_file_dependency_path)
                for cmd_file_dependency_path in cmd_file.get_dependencies(
                    target_path, config.obj_tree, config.fail_on_unknown_build_command
                )
            ]

        if node.absolute_path.endswith(".S"):
            node.incbin_dependencies = [
                IncbinDependency(
                    node=_build_child_node(incbin_statement.path),
                    full_statement=incbin_statement.full_statement,
                )
                for incbin_statement in parse_incbin_statements(node.absolute_path)
            ]

        return node


def _to_cmd_path(path: PathStr) -> PathStr:
    name = os.path.basename(path)
    return path.removesuffix(name) + f".{name}.cmd"
