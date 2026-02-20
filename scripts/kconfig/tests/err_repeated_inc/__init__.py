# SPDX-License-Identifier: GPL-2.0
"""
Detect repeated inclusion error.

If repeated inclusion is detected, it should fail with error message.
"""

def test(conf):
    assert conf.oldaskconfig() != 0
    assert conf.stderr_contains('expected_stderr')
