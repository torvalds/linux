# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass
from typing import Mapping
from sbom.cmd_graph import CmdGraph
from sbom.path_utils import PathStr
from sbom.spdx import SpdxIdGenerator
from sbom.spdx.build import Build
from sbom.spdx.core import ExternalMap, NamespaceMap, Relationship, SpdxDocument
from sbom.spdx.software import File, Sbom
from sbom.spdx_graph.kernel_file import KernelFileCollection
from sbom.spdx_graph.shared_spdx_elements import SharedSpdxElements
from sbom.spdx_graph.spdx_graph_model import SpdxGraph, SpdxIdGeneratorCollection
from sbom.spdx_graph.spdx_source_graph import source_file_license_elements


@dataclass
class SpdxBuildGraph(SpdxGraph):
    """SPDX graph representing build dependencies connecting source files and
    distributable output files"""

    @classmethod
    def create(
        cls,
        cmd_graph: CmdGraph,
        kernel_files: KernelFileCollection,
        shared_elements: SharedSpdxElements,
        high_level_build_element: Build,
        spdx_id_generators: SpdxIdGeneratorCollection,
    ) -> "SpdxBuildGraph":
        if len(kernel_files.source) > 0:
            return _create_spdx_build_graph(
                cmd_graph,
                kernel_files,
                shared_elements,
                high_level_build_element,
                spdx_id_generators,
            )
        else:
            return _create_spdx_build_graph_with_mixed_sources(
                cmd_graph,
                kernel_files,
                shared_elements,
                high_level_build_element,
                spdx_id_generators,
            )


def _create_spdx_build_graph(
    cmd_graph: CmdGraph,
    kernel_files: KernelFileCollection,
    shared_elements: SharedSpdxElements,
    high_level_build_element: Build,
    spdx_id_generators: SpdxIdGeneratorCollection,
) -> SpdxBuildGraph:
    """
    Creates an SPDX build graph where source and output files are referenced
    from external documents.

    Args:
        cmd_graph: The dependency graph of a kernel build.
        kernel_files: Collection of categorized kernel files involved in the build.
        shared_elements: SPDX elements shared across multiple documents.
        high_level_build_element: The high-level Build element referenced by the build graph.
        spdx_id_generators: Collection of generators for SPDX element IDs.

    Returns:
        SpdxBuildGraph: The SPDX build graph connecting source files and distributable output files.
    """
    # SpdxDocument
    build_spdx_document = SpdxDocument(
        spdxId=spdx_id_generators.build.generate(),
        profileConformance=["core", "software", "build"],
        namespaceMap=[
            NamespaceMap(prefix=generator.prefix, namespace=generator.namespace)
            for generator in [
                spdx_id_generators.build,
                spdx_id_generators.source,
                spdx_id_generators.output,
                spdx_id_generators.base,
            ]
            if generator.prefix is not None
        ],
    )

    # Sbom
    build_sbom = Sbom(
        spdxId=spdx_id_generators.build.generate(),
        software_sbomType=["build"],
    )

    # Src and object tree elements
    obj_tree_element = File(
        spdxId=spdx_id_generators.build.generate(),
        name="$(obj_tree)",
        software_fileKind="directory",
    )
    obj_tree_contains_relationship = Relationship(
        spdxId=spdx_id_generators.build.generate(),
        relationshipType="contains",
        from_=obj_tree_element,
        to=[],
    )

    # File elements
    build_file_elements = [file.spdx_file_element for file in kernel_files.build.values()]
    file_relationships = _file_relationships(
        cmd_graph=cmd_graph,
        file_elements={key: file.spdx_file_element for key, file in kernel_files.to_dict().items()},
        high_level_build_element=high_level_build_element,
        spdx_id_generator=spdx_id_generators.build,
    )

    # Update relationships
    build_spdx_document.rootElement = [build_sbom]

    build_spdx_document.import_ = [
        *(
            ExternalMap(externalSpdxId=file.spdx_file_element.spdxId)
            for file in (*kernel_files.source.values(), *kernel_files.external.values())
        ),
        ExternalMap(externalSpdxId=high_level_build_element.spdxId),
        *(ExternalMap(externalSpdxId=file.spdx_file_element.spdxId) for file in kernel_files.output.values()),
    ]

    build_sbom.rootElement = [obj_tree_element]
    build_sbom.element = [
        obj_tree_element,
        obj_tree_contains_relationship,
        *build_file_elements,
        *file_relationships,
    ]

    obj_tree_contains_relationship.to = [
        *build_file_elements,
        *(file.spdx_file_element for file in kernel_files.output.values()),
    ]

    # create Spdx graphs
    build_graph = SpdxBuildGraph(
        build_spdx_document,
        shared_elements.agent,
        shared_elements.creation_info,
        build_sbom,
    )
    return build_graph


def _create_spdx_build_graph_with_mixed_sources(
    cmd_graph: CmdGraph,
    kernel_files: KernelFileCollection,
    shared_elements: SharedSpdxElements,
    high_level_build_element: Build,
    spdx_id_generators: SpdxIdGeneratorCollection,
) -> SpdxBuildGraph:
    """
    Creates an SPDX build graph where only output files are referenced from
    an external document. Source files are included directly in the build graph.

    Args:
        cmd_graph: The dependency graph of a kernel build.
        kernel_files: Collection of categorized kernel files involved in the build.
        shared_elements: SPDX elements shared across multiple documents.
        high_level_build_element: The high-level Build element referenced by the build graph.
        spdx_id_generators: Collection of generators for SPDX element IDs.

    Returns:
        SpdxBuildGraph: The SPDX build graph connecting source files and distributable output files.
    """
    # SpdxDocument
    build_spdx_document = SpdxDocument(
        spdxId=spdx_id_generators.build.generate(),
        profileConformance=["core", "software", "build"],
        namespaceMap=[
            NamespaceMap(prefix=generator.prefix, namespace=generator.namespace)
            for generator in [
                spdx_id_generators.build,
                spdx_id_generators.output,
                spdx_id_generators.base,
            ]
            if generator.prefix is not None
        ],
    )

    # Sbom
    build_sbom = Sbom(
        spdxId=spdx_id_generators.build.generate(),
        software_sbomType=["build"],
    )

    # File elements
    build_file_elements = [file.spdx_file_element for file in kernel_files.build.values()]
    external_file_elements = [file.spdx_file_element for file in kernel_files.external.values()]
    file_relationships = _file_relationships(
        cmd_graph=cmd_graph,
        file_elements={key: file.spdx_file_element for key, file in kernel_files.to_dict().items()},
        high_level_build_element=high_level_build_element,
        spdx_id_generator=spdx_id_generators.build,
    )

    # Source file license elements
    source_file_license_identifiers, source_file_license_relationships = source_file_license_elements(
        list(kernel_files.build.values()), spdx_id_generators.build
    )

    # Update relationships
    build_spdx_document.rootElement = [build_sbom]
    root_file_elements = [file.spdx_file_element for file in kernel_files.output.values()]
    build_spdx_document.import_ = [
        ExternalMap(externalSpdxId=high_level_build_element.spdxId),
        *(ExternalMap(externalSpdxId=file.spdxId) for file in root_file_elements),
    ]

    build_sbom.rootElement = [*root_file_elements]
    build_sbom.element = [
        *build_file_elements,
        *external_file_elements,
        *source_file_license_identifiers,
        *source_file_license_relationships,
        *file_relationships,
    ]

    build_graph = SpdxBuildGraph(
        build_spdx_document,
        shared_elements.agent,
        shared_elements.creation_info,
        build_sbom,
    )
    return build_graph


def _file_relationships(
    cmd_graph: CmdGraph,
    file_elements: Mapping[PathStr, File],
    high_level_build_element: Build,
    spdx_id_generator: SpdxIdGenerator,
) -> list[Build | Relationship]:
    """
    Construct SPDX Build and Relationship elements representing dependency
    relationships in the cmd graph.

    Args:
        cmd_graph: The dependency graph of a kernel build.
        file_elements: Mapping of filesystem paths (PathStr) to their
            corresponding SPDX File elements.
        high_level_build_element: The SPDX Build element representing the overall build process/root.
        spdx_id_generator: Generator for unique SPDX IDs.

    Returns:
        list[Build | Relationship]: List of SPDX Build and Relationship elements
    """
    high_level_build_ancestorOf_relationship = Relationship(
        spdxId=spdx_id_generator.generate(),
        relationshipType="ancestorOf",
        from_=high_level_build_element,
        completeness="complete",
        to=[],
    )

    # Create a relationship between each node (output file)
    # and its children (input files)
    build_and_relationship_elements: list[Build | Relationship] = [high_level_build_ancestorOf_relationship]
    for node in cmd_graph:
        # .cmd file dependencies
        if node.cmd_file is not None:
            build_element = Build(
                spdxId=spdx_id_generator.generate(),
                build_buildType=high_level_build_element.build_buildType,
                build_buildId=high_level_build_element.build_buildId,
                comment=node.cmd_file.savedcmd,
            )
            build_and_relationship_elements.append(build_element)

            if node.cmd_file_dependencies:
                hasInput_relationship = Relationship(
                    spdxId=spdx_id_generator.generate(),
                    relationshipType="hasInput",
                    from_=build_element,
                    to=[file_elements[dep.absolute_path] for dep in node.cmd_file_dependencies],
                )
                build_and_relationship_elements.append(hasInput_relationship)

            hasOutput_relationship = Relationship(
                spdxId=spdx_id_generator.generate(),
                relationshipType="hasOutput",
                from_=build_element,
                to=[file_elements[node.absolute_path]],
            )
            build_and_relationship_elements.append(hasOutput_relationship)

            high_level_build_ancestorOf_relationship.to.append(build_element)

        # incbin dependencies
        if len(node.incbin_dependencies) > 0:
            incbin_dependsOn_relationship = Relationship(
                spdxId=spdx_id_generator.generate(),
                relationshipType="dependsOn",
                comment="\n".join([incbin_dependency.full_statement for incbin_dependency in node.incbin_dependencies]),
                from_=file_elements[node.absolute_path],
                to=[
                    file_elements[incbin_dependency.node.absolute_path]
                    for incbin_dependency in node.incbin_dependencies
                ],
            )
            build_and_relationship_elements.append(incbin_dependsOn_relationship)

        # hardcoded dependencies
        if len(node.hardcoded_dependencies) > 0:
            hardcoded_dependency_relationship = Relationship(
                spdxId=spdx_id_generator.generate(),
                relationshipType="dependsOn",
                from_=file_elements[node.absolute_path],
                to=[file_elements[n.absolute_path] for n in node.hardcoded_dependencies],
            )
            build_and_relationship_elements.append(hardcoded_dependency_relationship)

    return build_and_relationship_elements
