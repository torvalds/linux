# SPDX-License-Identifier: GPL-2.0
"""
Escape sequence tests.
"""

def test(conf):
    assert conf.oldaskconfig() == 0
    assert conf.stderr_matches('expected_stderr')
