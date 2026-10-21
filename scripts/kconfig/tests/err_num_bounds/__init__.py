# SPDX-License-Identifier: GPL-2.0
"""
Detect constants outside the 'int' and 'hex' bounds.

An int constant must fit in a signed 64-bit integer, and a hex constant must
fit in an unsigned 64-bit integer.
"""


def test(conf):
    assert conf.olddefconfig() == 1
    assert conf.stderr_matches('expected_stderr')
