"""
Warn recursive inclusion.

Recursive dependency should be warned.
"""

def test(conf):
    assert conf.oldaskconfig() == 0
    assert conf.stderr_contains('expected_stderr')
