# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

from .build_spdx_graphs import build_spdx_graphs
from .spdx_graph_model import SpdxIdGeneratorCollection

__all__ = ["build_spdx_graphs", "SpdxIdGeneratorCollection"]
