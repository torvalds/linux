"""
Ask new choice values when they become visible.

If new choice values are added with new dependency, and they become
visible during user configuration, oldconfig should recognize them
as (NEW), and ask the user for choice.

Related Linux commit: 5d09598d488f081e3be23f885ed65cbbe2d073b5
"""


def test(conf):
    assert conf.oldconfig('config', 'y') == 0
    assert conf.stdout_contains('expected_stdout')
