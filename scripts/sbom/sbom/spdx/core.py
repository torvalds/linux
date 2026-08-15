# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass, field

from typing import Any, Literal
from sbom.spdx.spdxId import SpdxId

SPDX_SPEC_VERSION = "3.0.1"

ExternalIdentifierType = Literal["email", "gitoid", "urlScheme"]
HashAlgorithm = Literal["sha256", "sha512"]
ProfileIdentifierType = Literal["core", "software", "build", "lite", "simpleLicensing"]
RelationshipType = Literal[
    "contains",
    "generates",
    "hasDeclaredLicense",
    "hasInput",
    "hasOutput",
    "ancestorOf",
    "hasDistributionArtifact",
    "dependsOn",
]
RelationshipCompleteness = Literal["complete", "incomplete", "noAssertion"]


@dataclass
class SpdxObject:
    def to_dict(self) -> dict[str, Any]:
        def _to_dict(v: Any):
            return v.to_dict() if hasattr(v, "to_dict") else v

        d: dict[str, Any] = {}
        for field_name in self.__dataclass_fields__:
            value = getattr(self, field_name)
            if value is None or value == [] or value == "":
                continue

            if isinstance(value, Element):
                d[field_name] = value.spdxId
            elif isinstance(value, list) and len(value) > 0 and isinstance(value[0], Element):  # type: ignore
                value: list[Element] = value
                d[field_name] = [v.spdxId for v in value]
            else:
                d[field_name] = [_to_dict(v) for v in value] if isinstance(value, list) else _to_dict(value)  # type: ignore
        return d


@dataclass(kw_only=True)
class IntegrityMethod(SpdxObject):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/IntegrityMethod/"""


@dataclass(kw_only=True)
class Hash(IntegrityMethod):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/Hash/"""

    type: str = field(init=False, default="Hash")
    hashValue: str
    algorithm: HashAlgorithm


@dataclass(kw_only=True)
class Element(SpdxObject):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/Element/"""

    type: str = field(init=False, default="Element")
    spdxId: SpdxId
    creationInfo: str = "_:creationinfo"
    name: str | None = None
    verifiedUsing: list[Hash] = field(default_factory=list)
    comment: str | None = None


@dataclass(kw_only=True)
class ExternalMap(SpdxObject):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/ExternalMap/"""

    type: str = field(init=False, default="ExternalMap")
    externalSpdxId: SpdxId


@dataclass(kw_only=True)
class NamespaceMap(SpdxObject):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/NamespaceMap/"""

    type: str = field(init=False, default="NamespaceMap")
    prefix: str
    namespace: str


@dataclass(kw_only=True)
class ElementCollection(Element):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/ElementCollection/"""

    type: str = field(init=False, default="ElementCollection")
    element: list[Element] = field(default_factory=list)
    rootElement: list[Element] = field(default_factory=list)
    profileConformance: list[ProfileIdentifierType] = field(default_factory=list)


@dataclass(kw_only=True)
class SpdxDocument(ElementCollection):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/SpdxDocument/"""

    type: str = field(init=False, default="SpdxDocument")
    import_: list[ExternalMap] = field(default_factory=list)
    namespaceMap: list[NamespaceMap] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {("import" if k == "import_" else k): v for k, v in super().to_dict().items()}


@dataclass(kw_only=True)
class Agent(Element):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/Agent/"""

    type: str = field(init=False, default="Agent")


@dataclass(kw_only=True)
class SoftwareAgent(Agent):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/SoftwareAgent/"""

    type: str = field(init=False, default="SoftwareAgent")


@dataclass(kw_only=True)
class CreationInfo(SpdxObject):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/CreationInfo/"""

    type: str = field(init=False, default="CreationInfo")
    id: SpdxId = "_:creationinfo"
    specVersion: str = SPDX_SPEC_VERSION
    createdBy: list[Agent]
    created: str
    comment: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {("@id" if k == "id" else k): v for k, v in super().to_dict().items()}


@dataclass(kw_only=True)
class Relationship(Element):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/Relationship/"""

    type: str = field(init=False, default="Relationship")
    relationshipType: RelationshipType
    from_: Element  # underscore because 'from' is a reserved keyword
    to: list[Element]
    completeness: RelationshipCompleteness | None = None

    def to_dict(self) -> dict[str, Any]:
        return {("from" if k == "from_" else k): v for k, v in super().to_dict().items()}


@dataclass(kw_only=True)
class Artifact(Element):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/Artifact/"""

    type: str = field(init=False, default="Artifact")


@dataclass(kw_only=True)
class DictionaryEntry(SpdxObject):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Core/Classes/DictionaryEntry/"""

    type: str = field(init=False, default="DictionaryEntry")
    key: str
    value: str
