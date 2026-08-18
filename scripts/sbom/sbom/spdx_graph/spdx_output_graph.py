# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass
import os
from typing import Protocol
from sbom.environment import Environment
from sbom.path_utils import PathStr
from sbom.spdx.build import Build
from sbom.spdx.core import DictionaryEntry, NamespaceMap, Relationship, SpdxDocument
from sbom.spdx.simplelicensing import LicenseExpression
from sbom.spdx.software import File, Package, Sbom
from sbom.spdx.spdxId import SpdxIdGenerator
from sbom.spdx_graph.kernel_file import KernelFile
from sbom.spdx_graph.shared_spdx_elements import SharedSpdxElements
from sbom.spdx_graph.spdx_graph_model import SpdxGraph, SpdxIdGeneratorCollection


class SpdxOutputGraphConfig(Protocol):
    obj_tree: PathStr
    src_tree: PathStr
    build_type: str
    build_id: str | None
    package_license: str
    package_version: str | None
    package_copyright_text: str | None


@dataclass
class SpdxOutputGraph(SpdxGraph):
    """SPDX graph representing distributable output files"""

    high_level_build_element: Build

    @classmethod
    def create(
        cls,
        root_files: list[KernelFile],
        shared_elements: SharedSpdxElements,
        spdx_id_generators: SpdxIdGeneratorCollection,
        config: SpdxOutputGraphConfig,
    ) -> "SpdxOutputGraph":
        """
        Args:
            root_files: List of distributable output files which act as roots
                of the dependency graph.
            shared_elements: Shared SPDX elements used across multiple documents.
            spdx_id_generators: Collection of SPDX ID generators.
            config: Configuration options.

        Returns:
            SpdxOutputGraph: The SPDX output graph.
        """
        # SpdxDocument
        spdx_document = SpdxDocument(
            spdxId=spdx_id_generators.output.generate(),
            profileConformance=["core", "software", "build", "simpleLicensing"],
            namespaceMap=[
                NamespaceMap(prefix=generator.prefix, namespace=generator.namespace)
                for generator in [spdx_id_generators.output, spdx_id_generators.base]
                if generator.prefix is not None
            ],
        )

        # Sbom
        sbom = Sbom(
            spdxId=spdx_id_generators.output.generate(),
            software_sbomType=["build"],
        )

        # High-level Build elements
        config_source_element = KernelFile.create(
            absolute_path=os.path.join(config.obj_tree, ".config"),
            obj_tree=config.obj_tree,
            src_tree=config.src_tree,
            spdx_id_generators=spdx_id_generators,
            is_output=True,
        ).spdx_file_element
        high_level_build_element, high_level_build_element_hasOutput_relationship = _high_level_build_elements(
            config.build_type,
            config.build_id,
            config_source_element,
            spdx_id_generators.output,
        )

        # Root file elements
        root_file_elements: list[File] = [file.spdx_file_element for file in root_files]

        # Package elements
        package_elements = [
            Package(
                spdxId=spdx_id_generators.output.generate(),
                name=_get_package_name(file.name),
                software_packageVersion=config.package_version,
                software_copyrightText=config.package_copyright_text,
                comment=f"Architecture={arch}" if (arch := Environment.ARCH() or Environment.SRCARCH()) else None,
                software_primaryPurpose=file.software_primaryPurpose,
            )
            for file in root_file_elements
        ]
        package_hasDistributionArtifact_file_relationships = [
            Relationship(
                spdxId=spdx_id_generators.output.generate(),
                relationshipType="hasDistributionArtifact",
                from_=package,
                to=[file],
            )
            for package, file in zip(package_elements, root_file_elements)
        ]
        package_license_expression = LicenseExpression(
            spdxId=spdx_id_generators.output.generate(),
            simplelicensing_licenseExpression=config.package_license,
        )
        package_hasDeclaredLicense_relationships = [
            Relationship(
                spdxId=spdx_id_generators.output.generate(),
                relationshipType="hasDeclaredLicense",
                from_=package,
                to=[package_license_expression],
            )
            for package in package_elements
        ]

        # Update relationships
        spdx_document.rootElement = [sbom]

        sbom.rootElement = [*package_elements]
        sbom.element = [
            config_source_element,
            high_level_build_element,
            high_level_build_element_hasOutput_relationship,
            *root_file_elements,
            *package_elements,
            *package_hasDistributionArtifact_file_relationships,
            package_license_expression,
            *package_hasDeclaredLicense_relationships,
        ]

        high_level_build_element_hasOutput_relationship.to = [*root_file_elements]

        output_graph = SpdxOutputGraph(
            spdx_document,
            shared_elements.agent,
            shared_elements.creation_info,
            sbom,
            high_level_build_element,
        )
        return output_graph


def _get_package_name(filename: str) -> str:
    """
    Generates a SPDX package name from a filename.
    Kernel images (bzImage, Image) get a descriptive name, others use the basename of the file.
    """
    KERNEL_FILENAMES = ["bzImage", "Image"]
    basename = os.path.basename(filename)
    return f"Linux Kernel ({basename})" if basename in KERNEL_FILENAMES else basename


def _high_level_build_elements(
    build_type: str,
    build_id: str | None,
    config_source_element: File,
    spdx_id_generator: SpdxIdGenerator,
) -> tuple[Build, Relationship]:
    build_spdxId = spdx_id_generator.generate()
    high_level_build_element = Build(
        spdxId=build_spdxId,
        build_buildType=build_type,
        build_buildId=build_id if build_id is not None else build_spdxId,
        build_environment=[
            DictionaryEntry(key=key, value=value)
            for key, value in Environment.KERNEL_BUILD_VARIABLES().items()
            if value
        ],
        build_configSourceUri=[config_source_element.spdxId],
        build_configSourceDigest=config_source_element.verifiedUsing,
    )

    high_level_build_element_hasOutput_relationship = Relationship(
        spdxId=spdx_id_generator.generate(),
        relationshipType="hasOutput",
        from_=high_level_build_element,
        to=[],
    )
    return high_level_build_element, high_level_build_element_hasOutput_relationship
