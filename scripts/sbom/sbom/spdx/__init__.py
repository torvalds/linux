# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from .spdxId import SpdxId, SpdxIdGenerator
from .serialization import JsonLdSpdxDocument

__all__ = ["JsonLdSpdxDocument", "SpdxId", "SpdxIdGenerator"]
