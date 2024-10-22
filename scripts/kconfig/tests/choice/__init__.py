# SPDX-License-Identifier: GPL-2.0
"""
Basic choice tests.
"""


def test_oldask0(conf):
    assert conf.oldaskconfig() == 0
    assert conf.stdout_contains('oldask0_expected_stdout')


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
