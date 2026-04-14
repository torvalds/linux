# SPDX-License-Identifier: GPL-2.0
#
# A class that will, eventually, encapsulate all of the parsed data that we
# then pass into the output modules.
#

"""
Data class to store a kernel-doc Item.
"""

class KdocItem:
    """
    A class that will, eventually, encapsulate all of the parsed data that we
    then pass into the output modules.
    """

    def __init__(self, name, fname, type, start_line,
                 **other_stuff):
        self.name = name
        self.fname = fname
        self.type = type
        self.declaration_start_line = start_line
        self.sections = {}
        self.sections_start_lines = {}
        self.parameterlist = []
        self.parameterdesc_start_lines = {}
        self.parameterdescs = {}
        self.parametertypes = {}

        self.warnings = []

        #
        # Just save everything else into our own dict so that the output
        # side can grab it directly as before.  As we move things into more
        # structured data, this will, hopefully, fade away.
        #
        known_keys = {
            'declaration_start_line',
            'sections',
            'sections_start_lines',
            'parameterlist',
            'parameterdesc_start_lines',
            'parameterdescs',
            'parametertypes',
            'warnings',
        }

        self.other_stuff = {}
        for k, v in other_stuff.items():
            if k in known_keys:
                setattr(self, k, v)           # real attribute
            else:
                self.other_stuff[k] = v

    def get(self, key, default = None):
        """
        Get a value from optional keys.
        """
        return self.other_stuff.get(key, default)

    def __getitem__(self, key):
        return self.get(key)

    def __repr__(self):
        return f"KdocItem({self.name}, {self.fname}, {self.type}, {self.declaration_start_line})"

    @classmethod
    def from_dict(cls, d):
        """Create a KdocItem from a plain dict."""

        cp = d.copy()
        name        = cp.pop('name', None)
        fname       = cp.pop('fname', None)
        type        = cp.pop('type', None)
        start_line  = cp.pop('start_line', 1)
        other_stuff = cp.pop('other_stuff', {})

        # Everything that’s left goes straight to __init__
        return cls(name, fname, type, start_line, **cp, **other_stuff)

    #
    # Tracking of section and parameter information.
    #
    def set_sections(self, sections, start_lines):
        """
        Set sections and start lines.
        """
        self.sections = sections
        self.sections_start_lines = start_lines

    def set_params(self, names, descs, types, starts):
        """
        Set parameter list: names, descriptions, types and start lines.
        """
        self.parameterlist = names
        self.parameterdescs = descs
        self.parametertypes = types
        self.parameterdesc_start_lines = starts
