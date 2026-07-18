# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from dataclasses import dataclass
from datetime import datetime, timezone
from sbom.spdx.core import CreationInfo, SoftwareAgent
from sbom.spdx.spdxId import SpdxIdGenerator


@dataclass(frozen=True)
class SharedSpdxElements:
    agent: SoftwareAgent
    creation_info: CreationInfo

    @classmethod
    def create(cls, spdx_id_generator: SpdxIdGenerator, created: datetime) -> "SharedSpdxElements":
        """
        Creates shared SPDX elements used across multiple documents.

        Args:
            spdx_id_generator: Generator for creating SPDX IDs.
            created: SPDX 'created' property used for the creation info.

        Returns:
            SharedSpdxElements with agent and creation info.
        """
        agent = SoftwareAgent(
            spdxId=spdx_id_generator.generate(),
            name="KernelSbom",
        )
        creation_info = CreationInfo(createdBy=[agent], created=created.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"))
        return SharedSpdxElements(agent=agent, creation_info=creation_info)
