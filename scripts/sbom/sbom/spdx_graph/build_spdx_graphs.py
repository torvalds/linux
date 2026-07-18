# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from datetime import datetime
from typing import Protocol

import logging
from sbom.config import KernelSpdxDocumentKind
from sbom.cmd_graph import CmdGraph
from sbom.path_utils import PathStr
from sbom.spdx_graph.kernel_file import KernelFileCollection
from sbom.spdx_graph.spdx_graph_model import SpdxGraph, SpdxIdGeneratorCollection
from sbom.spdx_graph.shared_spdx_elements import SharedSpdxElements
from sbom.spdx_graph.spdx_source_graph import SpdxSourceGraph
from sbom.spdx_graph.spdx_build_graph import SpdxBuildGraph
from sbom.spdx_graph.spdx_output_graph import SpdxOutputGraph


class SpdxGraphConfig(Protocol):
    obj_tree: PathStr
    src_tree: PathStr
    created: datetime
    build_type: str
    build_id: str | None
    package_license: str
    package_version: str | None
    package_copyright_text: str | None


def build_spdx_graphs(
    cmd_graph: CmdGraph,
    spdx_id_generators: SpdxIdGeneratorCollection,
    config: SpdxGraphConfig,
) -> dict[KernelSpdxDocumentKind, SpdxGraph]:
    """
    Builds SPDX graphs (output, source, and build) based on a cmd dependency graph.
    If the source and object trees are identical, no dedicated source graph can be created.
    In that case the source files are added to the build graph instead.

    Args:
        cmd_graph: The dependency graph of a kernel build.
        spdx_id_generators: Collection of SPDX ID generators.
        config: Configuration options.

    Returns:
        Dictionary of SPDX graphs
    """
    shared_elements = SharedSpdxElements.create(spdx_id_generators.base, config.created)
    kernel_files = KernelFileCollection.create(cmd_graph, config.obj_tree, config.src_tree, spdx_id_generators)
    output_graph = SpdxOutputGraph.create(
        root_files=list(kernel_files.output.values()),
        shared_elements=shared_elements,
        spdx_id_generators=spdx_id_generators,
        config=config,
    )
    spdx_graphs: dict[KernelSpdxDocumentKind, SpdxGraph] = {
        KernelSpdxDocumentKind.OUTPUT: output_graph,
    }

    if len(kernel_files.source) > 0:
        spdx_graphs[KernelSpdxDocumentKind.SOURCE] = SpdxSourceGraph.create(
            source_files=list(kernel_files.source.values()),
            external_files=list(kernel_files.external.values()),
            shared_elements=shared_elements,
            spdx_id_generators=spdx_id_generators,
        )
    else:
        logging.info(
            "Skipped creating a dedicated source SBOM because source files cannot be "
            "reliably classified when the source and object trees are identical. "
            "Added source files to the build SBOM instead."
        )

    build_graph = SpdxBuildGraph.create(
        cmd_graph,
        kernel_files,
        shared_elements,
        output_graph.high_level_build_element,
        spdx_id_generators,
    )
    spdx_graphs[KernelSpdxDocumentKind.BUILD] = build_graph

    return spdx_graphs
