# SPDX-License-Identifier: GPL-2.0
"""
Set a symbol's type and its default value in one line.

"def_bool", "def_tristate", "def_string", "def_int" and "def_hex" are
shorthand for a type definition plus a "default" property.  Check that
each one sets the type, and that an "if" on the shorthand does not
disturb the usual default cascade: the shorthand is only the first arm
of the list, so a later "default" still applies when its condition is
not met.
"""

def test(conf):
    assert conf.olddefconfig(dot_config='guard_y.config') == 0
    assert conf.config_matches('expected_guard_y')

    assert conf.olddefconfig(dot_config='guard_n.config') == 0
    assert conf.config_matches('expected_guard_n')
