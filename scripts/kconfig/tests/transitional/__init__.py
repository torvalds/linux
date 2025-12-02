# SPDX-License-Identifier: GPL-2.0
"""
Test transitional symbol migration functionality for all Kconfig types.

This tests that:
- OLD_* options in existing .config cause NEW_* options to be set
- OLD_* options are not written to the new .config file
- NEW_* options appear in the new .config file with correct values
- NEW_* options with defaults from transitional symbols are not prompted
- All Kconfig types work correctly: bool, tristate, string, hex, int
- User-set NEW values take precedence over conflicting OLD transitional values
"""

def test(conf):
    # Run olddefconfig to process the migration with the initial config
    assert conf.olddefconfig(dot_config='initial_config') == 0

    # Check that the configuration matches expected output
    assert conf.config_contains('expected_config')

    # Test oldconfig to ensure symbols with transitional defaults are not prompted
    assert conf.oldconfig(dot_config='initial_config', in_keys='n\n') == 0

    # Except for when conditional default evaluates to 'no'
    assert conf.stdout_contains('expected_stdout')
