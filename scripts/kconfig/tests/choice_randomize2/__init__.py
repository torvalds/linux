# SPDX-License-Identifier: GPL-2.0-only
"""
Randomize choices with correct dependencies

When shuffling a choice may potentially disrupt certain dependencies, symbol
values must be recalculated.

Related Linux commits:
  - c8fb7d7e48d11520ad24808cfce7afb7b9c9f798
"""


def test(conf):
    for i in range(20):
        assert conf.randconfig(seed=i) == 0
        assert (conf.config_matches('expected_config0') or
                conf.config_matches('expected_config1') or
                conf.config_matches('expected_config2'))
