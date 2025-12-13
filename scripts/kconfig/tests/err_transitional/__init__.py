# SPDX-License-Identifier: GPL-2.0
"""
Test that transitional symbols with invalid properties are rejected.

Transitional symbols can only have help sections. Any other properties
(default, select, depends, etc.) should cause a parser error.
"""

def test(conf):
    # This should fail with exit code 1 due to invalid transitional symbol
    assert conf.olddefconfig() == 1

    # Check that the error message is about transitional symbols
    assert conf.stderr_contains('expected_stderr')
