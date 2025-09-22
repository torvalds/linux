#!/usr/bin/env python3

from sphinx.application import Sphinx
import re

__version__ = "1.0"


def subst_gh_links(app: Sphinx, docname, source):
    regex = re.compile("#GH([0-9]+)")
    out_pattern = r"`#\1 <https://github.com/llvm/llvm-project/issues/\1>`_"
    result = source[0]
    result = regex.sub(out_pattern, result)
    source[0] = result


def setup(app: Sphinx):
    app.connect("source-read", subst_gh_links)
    return dict(version=__version__, parallel_read_safe=True, parallel_write_safe=True)
