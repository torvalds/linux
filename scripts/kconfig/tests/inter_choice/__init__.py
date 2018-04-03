"""
Do not affect user-assigned choice value by another choice.

Handling of state flags for choices is complecated.  In old days,
the defconfig result of a choice could be affected by another choice
if those choices interact by 'depends on', 'select', etc.

Related Linux commit: fbe98bb9ed3dae23e320c6b113e35f129538d14a
"""


def test(conf):
    assert conf.defconfig('defconfig') == 0
    assert conf.config_contains('expected_config')
