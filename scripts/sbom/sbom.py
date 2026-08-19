#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

"""
Compute software bill of materials in SPDX format describing a kernel build.
"""

import json
import logging
import os
import sys
import time
import uuid
import sbom.sbom_logging as sbom_logging
from sbom.config import get_config
from sbom.path_utils import is_relative_to
from sbom.spdx import JsonLdSpdxDocument, SpdxIdGenerator
from sbom.spdx.core import CreationInfo, SpdxDocument
from sbom.spdx_graph import SpdxIdGeneratorCollection, build_spdx_graphs
from sbom.cmd_graph import CmdGraph


def _exit_with_summary(write_output_on_error: bool = False) -> None:
    warning_summary = sbom_logging.summarize_warnings()
    error_summary = sbom_logging.summarize_errors()
    if warning_summary:
        logging.warning(warning_summary)
    if error_summary:
        logging.error(error_summary)
        if not write_output_on_error:
            logging.info(
                "Use --write-output-on-error to generate output documents even when errors occur. "
                "Note that in this case the generated documents may be incomplete."
            )
        sys.exit(1)


def main():
    # Read config
    config = get_config()

    # Configure logging
    logging.basicConfig(
        level=logging.DEBUG if config.debug else logging.INFO,
        format="[%(levelname)s] %(message)s",
    )

    # Build cmd graph
    logging.debug("Start building cmd graph")
    start_time = time.time()
    cmd_graph = CmdGraph.create(config.root_paths, config)
    logging.debug(f"Built cmd graph in {time.time() - start_time} seconds")

    # Save used files document
    if config.generate_used_files:
        if config.src_tree == config.obj_tree:
            logging.info(
                f"Extracting all files from the cmd graph to {config.used_files_file_name} "
                "instead of only source files because source files cannot be "
                "reliably classified when the source and object trees are identical.",
            )
            used_files = [os.path.relpath(node.absolute_path, config.src_tree) for node in cmd_graph]
            logging.debug(f"Found {len(used_files)} files in cmd graph.")
        else:
            used_files = [
                os.path.relpath(node.absolute_path, config.src_tree)
                for node in cmd_graph
                if is_relative_to(node.absolute_path, config.src_tree)
                and not is_relative_to(node.absolute_path, config.obj_tree)
            ]
            logging.debug(f"Found {len(used_files)} source files in cmd graph")
        if not sbom_logging.has_errors() or config.write_output_on_error:
            used_files_path = os.path.join(config.output_directory, config.used_files_file_name)
            with open(used_files_path, "w", encoding="utf-8") as f:
                f.write("\n".join(str(file_path) for file_path in used_files))
            logging.debug(f"Successfully saved {used_files_path}")

    if config.generate_spdx is False:
        _exit_with_summary(config.write_output_on_error)
        return

    # Build SPDX Documents
    logging.debug("Start generating SPDX graph based on cmd graph")
    start_time = time.time()

    # The real uuid will be generated based on the content of the SPDX graphs
    # to ensure that the same SPDX document is always assigned the same uuid.
    PLACEHOLDER_UUID = "00000000-0000-0000-0000-000000000000"
    spdx_id_base_namespace = f"{config.spdxId_prefix}{PLACEHOLDER_UUID}/"
    spdx_id_generators = SpdxIdGeneratorCollection(
        base=SpdxIdGenerator(prefix="p", namespace=spdx_id_base_namespace),
        source=SpdxIdGenerator(prefix="s", namespace=f"{spdx_id_base_namespace}source/"),
        build=SpdxIdGenerator(prefix="b", namespace=f"{spdx_id_base_namespace}build/"),
        output=SpdxIdGenerator(prefix="o", namespace=f"{spdx_id_base_namespace}output/"),
    )

    spdx_graphs = build_spdx_graphs(
        cmd_graph,
        spdx_id_generators,
        config,
    )
    spdx_id_uuid = uuid.uuid5(
        uuid.NAMESPACE_URL,
        "".join(
            json.dumps(element.to_dict()) for spdx_graph in spdx_graphs.values() for element in spdx_graph.to_list()
        ),
    )
    logging.debug(f"Generated SPDX graph in {time.time() - start_time} seconds")

    if not sbom_logging.has_errors() or config.write_output_on_error:
        for kernel_sbom_kind, spdx_graph in spdx_graphs.items():
            spdx_graph_objects = spdx_graph.to_list()
            # Add warning and error summary to creation info comment
            creation_info = next(element for element in spdx_graph_objects if isinstance(element, CreationInfo))
            creation_info.comment = "\n".join([
                sbom_logging.summarize_warnings(),
                sbom_logging.summarize_errors(),
            ]).strip()
            # Replace Placeholder uuid with real uuid for spdxIds
            spdx_document = next(element for element in spdx_graph_objects if isinstance(element, SpdxDocument))
            for namespaceMap in spdx_document.namespaceMap:
                namespaceMap.namespace = namespaceMap.namespace.replace(PLACEHOLDER_UUID, str(spdx_id_uuid))
            # Serialize SPDX graph to JSON-LD
            spdx_doc = JsonLdSpdxDocument(graph=spdx_graph_objects)
            save_path = os.path.join(config.output_directory, config.spdx_file_names[kernel_sbom_kind])
            spdx_doc.save(save_path, config.prettify_json)
            logging.debug(f"Successfully saved {save_path}")

    _exit_with_summary(config.write_output_on_error)


# Call main method
if __name__ == "__main__":
    main()
