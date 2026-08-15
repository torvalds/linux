# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass, field
from typing import Literal
from sbom.spdx.core import Artifact, ElementCollection, IntegrityMethod


SbomType = Literal["source", "build"]
FileKindType = Literal["file", "directory"]
SoftwarePurpose = Literal[
    "source",
    "archive",
    "library",
    "file",
    "data",
    "configuration",
    "executable",
    "module",
    "application",
    "documentation",
    "other",
]
ContentIdentifierType = Literal["gitoid", "swhid"]


@dataclass(kw_only=True)
class Sbom(ElementCollection):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Software/Classes/Sbom/"""

    type: str = field(init=False, default="software_Sbom")
    software_sbomType: list[SbomType] = field(default_factory=list)


@dataclass(kw_only=True)
class ContentIdentifier(IntegrityMethod):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Software/Classes/ContentIdentifier/"""

    type: str = field(init=False, default="software_ContentIdentifier")
    software_contentIdentifierType: ContentIdentifierType
    software_contentIdentifierValue: str


@dataclass(kw_only=True)
class SoftwareArtifact(Artifact):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Software/Classes/SoftwareArtifact/"""

    type: str = field(init=False, default="software_Artifact")
    software_primaryPurpose: SoftwarePurpose | None = None
    software_copyrightText: str | None = None
    software_contentIdentifier: list[ContentIdentifier] = field(default_factory=list)


@dataclass(kw_only=True)
class Package(SoftwareArtifact):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Software/Classes/Package/"""

    type: str = field(init=False, default="software_Package")
    name: str  # type: ignore
    software_packageVersion: str | None = None


@dataclass(kw_only=True)
class File(SoftwareArtifact):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Software/Classes/File/"""

    type: str = field(init=False, default="software_File")
    name: str  # type: ignore
    software_fileKind: FileKindType | None = None
