"""
Basic choice tests.

The handling of 'choice' is a bit complicated part in Kconfig.

The behavior of 'y' choice is intuitive.  If choice values are tristate,
the choice can be 'm' where each value can be enabled independently.
Also, if a choice is marked as 'optional', the whole choice can be
invisible.
"""


def test_oldask0(conf):
    assert conf.oldaskconfig() == 0
    assert conf.stdout_contains('oldask0_expected_stdout')


def test_oldask1(conf):
    assert conf.oldaskconfig('oldask1_config') == 0
    assert conf.stdout_contains('oldask1_expected_stdout')


def test_allyes(conf):
    assert conf.allyesconfig() == 0
    assert conf.config_contains('allyes_expected_config')


def test_allmod(conf):
    assert conf.allmodconfig() == 0
    assert conf.config_contains('allmod_expected_config')


def test_allno(conf):
    assert conf.allnoconfig() == 0
    assert conf.config_contains('allno_expected_config')


def test_alldef(conf):
    assert conf.alldefconfig() == 0
    assert conf.config_contains('alldef_expected_config')
