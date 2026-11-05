# SPDX-License-Identifier: GPL-2.0
"""
Reject nonnumeric symbol references from int and hex properties.
"""


def test(conf):
    assert conf.olddefconfig() == 1
    assert conf.stderr_matches('expected_stderr')
