# SPDX-License-Identifier: GPL-2.0
"""
Test optional warnings for user-provided values changed by Kconfig.

Warnings should stay disabled by default, and should only appear when
KCONFIG_WARN_CHANGED_INPUT is enabled.
"""


def test(conf):
    assert conf.olddefconfig('config') == 0
    assert 'user-provided values changed by Kconfig' not in conf.stderr

    assert conf._run_conf('--olddefconfig', dot_config='config',
                          extra_env={
                              'KCONFIG_WARN_CHANGED_INPUT': '1',
                          }) == 0
    assert conf.stderr_contains('expected_stderr')
    assert conf.config_matches('expected_config')

    assert conf._run_conf('--olddefconfig', dot_config='config',
                          extra_env={
                              'KCONFIG_WARN_CHANGED_INPUT': '1',
                          }, silent=True) == 0
    assert conf.stderr_contains('expected_stderr')

    assert conf._run_conf('--savedefconfig=defconfig', dot_config='config',
                          out_file='defconfig',
                          extra_env={
                              'KCONFIG_WARN_CHANGED_INPUT': '1',
                          }) == 0
    assert conf.stderr_contains('expected_stderr')
    assert conf.config_matches('expected_defconfig')
