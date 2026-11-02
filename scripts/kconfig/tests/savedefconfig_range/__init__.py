# SPDX-License-Identifier: GPL-2.0
"""Test savedefconfig with numerical defaults outside of ranges."""


def test(conf):
    assert conf._run_conf('--savedefconfig=defconfig', dot_config='config',
                          out_file='defconfig') == 0
    assert conf.config_matches('expected_defconfig')
