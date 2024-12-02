# SPDX-License-Identifier: GPL-2.0
"""
Do not write choice values to .config if the dependency is unmet.

"# CONFIG_... is not set" should not be written into the .config file
for symbols with unmet dependency.

This was not working correctly for choice values because choice needs
a bit different symbol computation.

This checks that no unneeded "# COFIG_... is not set" is contained in
the .config file.

Related Linux commit: cb67ab2cd2b8abd9650292c986c79901e3073a59
"""


def test(conf):
    assert conf.oldaskconfig('config', 'n') == 0
    assert conf.config_matches('expected_config')
