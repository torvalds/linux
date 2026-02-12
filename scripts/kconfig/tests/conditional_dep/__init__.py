# SPDX-License-Identifier: GPL-2.0
"""
Correctly handle conditional dependencies.
"""

def test(conf):
    assert conf.oldconfig('test_config1') == 0
    assert conf.config_matches('expected_config1')

    assert conf.oldconfig('test_config2') == 0
    assert conf.config_matches('expected_config2')

    assert conf.oldconfig('test_config3') == 0
    assert conf.config_matches('expected_config3')
