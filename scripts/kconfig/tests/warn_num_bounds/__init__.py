# SPDX-License-Identifier: GPL-2.0
"""Test user values outside the numeric type bounds."""


def test(conf):
    in_keys = (
        '-9223372036854775809\n'
        '-1\n'
        '9223372036854775808\n'
        '1\n'
        '0x10000000000000000\n'
        '0x1\n'
        '-9223372036854775808\n'
        '9223372036854775807\n'
        '0x0\n'
        '0xffffffffffffffff\n'
    )

    assert conf.oldaskconfig(in_keys=in_keys) == 0
    assert conf.stderr_matches('expected_frontend_stderr')
    assert conf.config_matches('expected_frontend_config')

    assert conf.olddefconfig('config') == 0
    assert conf.stderr_matches('expected_config_stderr')
    assert conf.config_matches('expected_config')
