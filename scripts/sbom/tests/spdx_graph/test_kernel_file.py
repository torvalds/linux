# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import unittest
from pathlib import Path
import tempfile
from sbom.spdx_graph.kernel_file import _parse_spdx_license_identifier  # type: ignore


class TestKernelFile(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.src_tree = Path(self.tmpdir.name)

    def tearDown(self):
        self.tmpdir.cleanup()

    def test_parse_spdx_license_identifier(self):
        # REUSE-IgnoreStart
        test_cases: list[tuple[str, str | None]] = [
            ("/* SPDX-License-Identifier: MIT*/", "MIT"),
            ("// SPDX-License-Identifier: GPL-2.0-only", "GPL-2.0-only"),
            ("# SPDX-License-Identifier: GPL-2.0-only", "GPL-2.0-only"),
            ("#!/bin/bash\n# SPDX-License-Identifier: GPL-2.0-only", "GPL-2.0-only"),
            ("/* SPDX-License-Identifier: GPL-2.0-or-later OR MIT */", "GPL-2.0-or-later OR MIT"),
            ("/* SPDX-License-Identifier: Apache-2.0 */\n extra text", "Apache-2.0"),
            ("<!-- SPDX-License-Identifier: GPL-2.0 -->", "GPL-2.0"),
            ("int main() { return 0; }", None),
        ]
        # REUSE-IgnoreEnd

        for i, (file_content, expected_identifier) in enumerate(test_cases):
            file_path = self.src_tree / f"file_{i}.c"
            file_path.write_text(file_content)
            self.assertEqual(_parse_spdx_license_identifier(str(file_path)), expected_identifier)
