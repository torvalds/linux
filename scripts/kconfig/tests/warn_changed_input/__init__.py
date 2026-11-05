# SPDX-License-Identifier: GPL-2.0
"""
Test optional warnings for user-provided values changed by Kconfig.

Warnings should stay disabled by default, and should only appear when
KCONFIG_WARN_CHANGED_INPUT is enabled.
"""


def test(conf):
    warn_changed_input = {
        "KCONFIG_WARN_CHANGED_INPUT": "1",
    }

    assert conf.olddefconfig('config') == 0
    assert 'user-provided values changed by Kconfig' not in conf.stderr

    assert conf.olddefconfig('config', extra_env=warn_changed_input) == 0
    assert conf.stderr_contains('expected_stderr')
    assert conf.config_matches('expected_config')

    assert conf.olddefconfig('config', extra_env=warn_changed_input,
                             silent=True) == 0
    assert conf.stderr_contains('expected_stderr')

    assert conf.savedefconfig('config', extra_env=warn_changed_input) == 0
    assert conf.stderr_contains('expected_stderr')
    assert conf.config_matches('expected_defconfig')
