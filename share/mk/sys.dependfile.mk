# $FreeBSD$
# $Id: sys.dependfile.mk,v 1.7 2016/02/20 01:57:39 sjg Exp $
#
#	@(#) Copyright (c) 2012, Simon J. Gerraty
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

# This only makes sense in meta mode.
# This allows a mixture of auto generated as well as manually edited
# dependency files, which can be differentiated by their names.
# As per dirdeps.mk we only require:
# 1. a common prefix
# 2. that machine specific files end in .${MACHINE}
#
# The .MAKE.DEPENDFILE_PREFERENCE below is an example.

# All depend file names should start with this
.MAKE.DEPENDFILE_PREFIX ?= Makefile.depend

.if !empty(.MAKE.DEPENDFILE) && \
	${.MAKE.DEPENDFILE:M${.MAKE.DEPENDFILE_PREFIX}*} == ""
# let us do our thing below...
.undef .MAKE.DEPENDFILE
.endif

# The order of preference: we will use the first one of these we find.
# It usually makes sense to order from most specific to least.
.MAKE.DEPENDFILE_PREFERENCE ?= \
	${.CURDIR}/${.MAKE.DEPENDFILE_PREFIX}.${MACHINE} \
	${.CURDIR}/${.MAKE.DEPENDFILE_PREFIX}

# Normally the 1st entry is our default choice
# Another useful default is ${.MAKE.DEPENDFILE_PREFIX}
.MAKE.DEPENDFILE_DEFAULT ?= ${.MAKE.DEPENDFILE_PREFERENCE:[1]}

_e := ${.MAKE.DEPENDFILE_PREFERENCE:@m@${exists($m):?$m:}@}
.if !empty(_e)
.MAKE.DEPENDFILE := ${_e:[1]}
.elif ${.MAKE.DEPENDFILE_PREFERENCE:M*${MACHINE}} != "" && ${.MAKE.DEPENDFILE_DEFAULT:E} != ${MACHINE}
# MACHINE specific depend files are supported, but *not* default.
# If any already exist, we should follow suit.
_aml = ${ALL_MACHINE_LIST:Uarm amd64 i386 powerpc:N${MACHINE}} ${MACHINE}
# make sure we restore MACHINE
_m := ${MACHINE}
_e := ${_aml:@MACHINE@${.MAKE.DEPENDFILE_PREFERENCE:@m@${exists($m):?$m:}@}@}
MACHINE := ${_m}
.if !empty(_e)
.MAKE.DEPENDFILE ?= ${.MAKE.DEPENDFILE_PREFERENCE:M*${MACHINE}:[1]}
.endif
.endif
.MAKE.DEPENDFILE ?= ${.MAKE.DEPENDFILE_DEFAULT}
