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
        #
        # Just save everything else into our own dict so that the output
        # side can grab it directly as before.  As we move things into more
        # structured data, this will, hopefully, fade away.
        #
        self.other_stuff = other_stuff

    def get(self, key, default = None):
        ret = self.other_stuff.get(key, default)
        if ret == default:
            return self.__dict__.get(key, default)
        return ret

    def __getitem__(self, key):
        return self.get(key)
