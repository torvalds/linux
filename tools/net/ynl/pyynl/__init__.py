# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

""" Python YNL (YAML Netlink) library. """

# Re-export the public library API so it can be imported straight from the
# package, e.g. `from pyynl import YnlFamily`.
# pylint: disable=wildcard-import,unused-wildcard-import
from .lib import *
from .lib import __all__
