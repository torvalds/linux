# -*- coding: utf-8 -*-
#
# LLVM documentation anchor slug formatting

# Some of our markdown documentation numbers section titles
# This helpers is used by myst to remove that numbering from the anchor links.

from docutils.nodes import make_id


def make_slug(str):
    import re

    str = re.sub(r"^\s*(\w\.)+\w\s", "", str)
    str = re.sub(r"^\s*\w\.\s", "", str)
    return make_id(str)
