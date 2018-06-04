"""
Hide tristate choice values with mod dependency in y choice.

If tristate choice values depend on symbols set to 'm', they should be
hidden when the choice containing them is changed from 'm' to 'y'
(i.e. exclusive choice).

Related Linux commit: fa64e5f6a35efd5e77d639125d973077ca506074
"""


def test(conf):
    assert conf.oldaskconfig('config', 'y') == 0
    assert conf.config_contains('expected_config')
    assert conf.stdout_contains('expected_stdout')
