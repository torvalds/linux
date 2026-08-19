# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from collections import deque
from dataclasses import dataclass, field
from typing import Iterator

from sbom.cmd_graph.cmd_graph_node import CmdGraphNode, CmdGraphNodeConfig
from sbom.path_utils import PathStr


@dataclass
class CmdGraph:
    """Directed acyclic graph of build dependencies primarily inferred from .cmd files produced during kernel builds"""

    roots: list[CmdGraphNode] = field(default_factory=list)

    @classmethod
    def create(cls, root_paths: list[PathStr], config: CmdGraphNodeConfig) -> "CmdGraph":
        """
        Recursively builds a dependency graph starting from `root_paths`.
        Dependencies are mainly discovered by parsing the `.cmd` files.

        Args:
            root_paths (list[PathStr]): List of paths to root outputs relative to obj_tree
            config (CmdGraphNodeConfig): Configuration options

        Returns:
            CmdGraph: A graph of all build dependencies for the given root files.
        """
        node_cache: dict[PathStr, CmdGraphNode] = {}
        root_nodes = [CmdGraphNode.create(root_path, config, node_cache) for root_path in root_paths]
        return CmdGraph(root_nodes)

    def __iter__(self) -> Iterator[CmdGraphNode]:
        """Traverse the graph in breadth-first order, yielding each unique node."""
        visited: set[PathStr] = set()
        node_stack: deque[CmdGraphNode] = deque(self.roots)
        while len(node_stack) > 0:
            node = node_stack.popleft()
            if node.absolute_path in visited:
                continue

            visited.add(node.absolute_path)
            node_stack.extend(node.children)
            yield node
