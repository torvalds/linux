"""
Create submenu for symbols that depend on the preceding one.

If a symbols has dependency on the preceding symbol, the menu entry
should become the submenu of the preceding one, and displayed with
deeper indentation.
"""


def test(conf):
    assert conf.oldaskconfig() == 0
    assert conf.stdout_contains('expected_stdout')
