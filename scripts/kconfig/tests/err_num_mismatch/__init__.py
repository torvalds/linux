# SPDX-License-Identifier: GPL-2.0
"""
Reject direct references ('default' or 'range') between int and hex options.
"""


def test(conf):
    assert conf.olddefconfig() == 1
    assert conf.stderr_matches('expected_stderr')
