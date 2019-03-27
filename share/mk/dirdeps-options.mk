# $FreeBSD$
# $Id: dirdeps-options.mk,v 1.9 2018/09/20 00:07:19 sjg Exp $
#
#	@(#) Copyright (c) 2018, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that
#	the above copyright notice and this notice are
#	left intact.
#
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

##
#
# This makefile is used to deal with optional DIRDEPS.
#
# It is to be included by Makefile.depend.options in a
# directory which has DIRDEPS affected by optional features.
# Makefile.depend.options should set DIRDEPS_OPTIONS and
# may also set specific DIRDEPS.* for those options.
#
# If a Makefile.depend.options file exists, it will be included by
# dirdeps.mk and meta.autodep.mk
#
# We include local.dirdeps-options.mk which may also define DIRDEPS.*
# for options.
#
# Thus a directory, that is affected by an option FOO would have
# a Makefile.depend.options that sets
# DIRDEPS_OPTIONS= FOO
# It can also set either/both of
# DIRDEPS.FOO.yes
# DIRDEPS.FOO.no
# to whatever applies for that dir, or it can rely on globals
# set in local.dirdeps-options.mk
# Either way, we will .undef DIRDEPS.* when done.

# This should have been set by Makefile.depend.options
# before including us
DIRDEPS_OPTIONS ?=

# pickup any DIRDEPS.* we need
.-include <local.dirdeps-options.mk>

.if ${.MAKE.LEVEL} == 0
# :U below avoids potential errors when we :=
.for o in ${DIRDEPS_OPTIONS:tu}
DIRDEPS += ${DIRDEPS.$o.${MK_$o:U}:U}
.endfor
DIRDEPS := ${DIRDEPS:O:u}
# avoid cross contamination
.for o in ${DIRDEPS_OPTIONS:tu}
.undef DIRDEPS.$o.yes
.undef DIRDEPS.$o.no
.endfor
.else
# whether options are enabled or not,
# we want to filter out the relevant DIRDEPS.*
# we should only be included by meta.autodep.mk
# if dependencies are to be updated
.for o in ${DIRDEPS_OPTIONS:tu}
.for d in ${DIRDEPS.$o.yes} ${DIRDEPS.$o.no}
.if exists(${SRCTOP}/$d)
GENDIRDEPS_FILTER += N$d*
.elif exists(${SRCTOP}/${d:R})
GENDIRDEPS_FILTER += N${d:R}*
.endif
.endfor
.endfor
.endif
