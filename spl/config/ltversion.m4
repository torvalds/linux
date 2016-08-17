# ltversion.m4 -- version numbers			-*- Autoconf -*-
#
#   Copyright (C) 2004, 2011-2015 Free Software Foundation, Inc.
#   Written by Scott James Remnant, 2004
#
# This file is free software; the Free Software Foundation gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.

# @configure_input@

# serial 4179 ltversion.m4
# This file is part of GNU Libtool

m4_define([LT_PACKAGE_VERSION], [2.4.6])
m4_define([LT_PACKAGE_REVISION], [2.4.6])

AC_DEFUN([LTVERSION_VERSION],
[macro_version='2.4.6'
macro_revision='2.4.6'
_LT_DECL(, macro_version, 0, [Which release of libtool.m4 was used?])
_LT_DECL(, macro_revision, 0)
])
