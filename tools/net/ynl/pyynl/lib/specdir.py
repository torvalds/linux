# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

"""
Locating YNL spec and schema files on disk.

Resolves the directory holding the YAML specs (preferring an in-tree copy
over the installed system path) and maps family names to spec files.
"""

import os

SYS_SCHEMA_DIR='/usr/share/ynl'
RELATIVE_SCHEMA_DIR='../../../../../Documentation/netlink'


def schema_dir():
    """
    Return the effective schema directory, preferring in-tree before
    system schema directory.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    schema_dir_ = os.path.abspath(f"{script_dir}/{RELATIVE_SCHEMA_DIR}")
    if not os.path.isdir(schema_dir_):
        schema_dir_ = SYS_SCHEMA_DIR
    if not os.path.isdir(schema_dir_):
        raise FileNotFoundError(f"Schema directory {schema_dir_} does not exist")
    return schema_dir_

def spec_dir():
    """
    Return the effective spec directory, relative to the effective
    schema directory.
    """
    spec_dir_ = schema_dir() + '/specs'
    if not os.path.isdir(spec_dir_):
        raise FileNotFoundError(f"Spec directory {spec_dir_} does not exist")
    return spec_dir_


def find_spec(family):
    """ Return the path to the YAML spec file for a family by name. """
    spec = f"{spec_dir()}/{family}.yaml"
    if not os.path.isfile(spec):
        raise FileNotFoundError(f"Spec for family '{family}' not found at {spec}")
    return spec


def list_families():
    """ Return the sorted names of all families with an installed spec. """
    return sorted(f.removesuffix('.yaml')
                  for f in os.listdir(spec_dir()) if f.endswith('.yaml'))
