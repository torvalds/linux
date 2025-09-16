# SPDX-License-Identifier: GPL-2.0
#
# A class that will, eventually, encapsulate all of the parsed data that we
# then pass into the output modules.
#

class KdocItem:
    def __init__(self, name, type, start_line, **other_stuff):
        self.name = name
        self.type = type
        self.declaration_start_line = start_line
        self.sections = {}
        self.sections_start_lines = {}
        self.parameterlist = []
        self.parameterdesc_start_lines = []
        self.parameterdescs = {}
        self.parametertypes = {}
        #
        # Just save everything else into our own dict so that the output
        # side can grab it directly as before.  As we move things into more
        # structured data, this will, hopefully, fade away.
        #
        self.other_stuff = other_stuff

    def get(self, key, default = None):
        return self.other_stuff.get(key, default)

    def __getitem__(self, key):
        return self.get(key)

    #
    # Tracking of section and parameter information.
    #
    def set_sections(self, sections, start_lines):
        self.sections = sections
        self.section_start_lines = start_lines

    def set_params(self, names, descs, types, starts):
        self.parameterlist = names
        self.parameterdescs = descs
        self.parametertypes = types
        self.parameterdesc_start_lines = starts
