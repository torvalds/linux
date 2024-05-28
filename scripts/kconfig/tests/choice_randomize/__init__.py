# SPDX-License-Identifier: GPL-2.0-only
"""
Randomize all dependent choices

This is a somewhat tricky case for randconfig; the visibility of one choice is
determined by a member of another choice. Randconfig should be able to generate
all possible patterns.
"""


def test(conf):

    expected0 = False
    expected1 = False
    expected2 = False

    for i in range(100):
        assert conf.randconfig(seed=i) == 0

        if conf.config_matches('expected_config0'):
            expected0 = True
        elif conf.config_matches('expected_config1'):
            expected1 = True
        elif conf.config_matches('expected_config2'):
            expected2 = True
        else:
            assert False

        if expected0 and expected1 and expected2:
            break

    assert expected0
    assert expected1
    assert expected2
