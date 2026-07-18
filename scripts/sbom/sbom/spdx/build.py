# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass, field
from sbom.spdx.core import DictionaryEntry, Element, Hash


@dataclass(kw_only=True)
class Build(Element):
    """https://spdx.github.io/spdx-spec/v3.0.1/model/Build/Classes/Build/"""

    type: str = field(init=False, default="build_Build")
    build_buildType: str
    build_buildId: str
    build_environment: list[DictionaryEntry] = field(default_factory=list)
    build_configSourceUri: list[str] = field(default_factory=list)
    build_configSourceDigest: list[Hash] = field(default_factory=list)
