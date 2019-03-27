# $FreeBSD$
# $Id: progs.mk,v 1.11 2012/11/06 17:18:54 sjg Exp $
#
#	@(#) Copyright (c) 2006, Simon J. Gerraty
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

.MAIN: all

.if defined(PROGS) || defined(PROGS_CXX)
# we really only use PROGS below...
PROGS += ${PROGS_CXX}

.if defined(PROG)
# just one of many
PROG_OVERRIDE_VARS +=	BINDIR BINGRP BINOWN BINMODE DPSRCS MAN NO_WERROR \
			PROGNAME SRCS STRIP WARNS
PROG_VARS +=	CFLAGS CXXFLAGS DEBUG_FLAGS DPADD INTERNALPROG LDADD LIBADD \
		LINKS LDFLAGS MLINKS ${PROG_OVERRIDE_VARS}
.for v in ${PROG_VARS:O:u}
.if empty(${PROG_OVERRIDE_VARS:M$v})
.if defined(${v}.${PROG})
$v += ${${v}.${PROG}}
.elif defined(${v}_${PROG})
$v += ${${v}_${PROG}}
.endif
.else
$v ?=
.endif
.endfor

.if ${MK_DIRDEPS_BUILD} == "yes"
# Leave updating the Makefile.depend to the parent.
UPDATE_DEPENDFILE = NO

# Record our meta files for the parent to use.
CLEANFILES+= ${PROG}.meta_files
${PROG}.meta_files: .NOMETA $${.MAKE.META.CREATED} ${_this}
	@echo "Updating ${.TARGET}: ${.OODATE:T:[1..8]}"
	@echo ${.MAKE.META.FILES} > ${.TARGET}

.if !defined(_SKIP_BUILD)
.END: ${PROG}.meta_files
.endif
.endif	# ${MK_DIRDEPS_BUILD} == "yes"

# prog.mk will do the rest
.else # !defined(PROG)
.if !defined(_SKIP_BUILD)
all: ${PROGS}
.endif

META_XTRAS+=	${cat ${PROGS:S/$/*.meta_files/} 2>/dev/null || true:L:sh}

.if ${MK_STAGING} != "no" && !empty(PROGS)
# Stage from parent while respecting PROGNAME and BINDIR overrides.
.for _prog in ${PROGS}
STAGE_DIR.prog.${_prog}= ${STAGE_OBJTOP}${BINDIR.${_prog}:UBINDIR_${_prog}:U${BINDIR}}
STAGE_AS_SETS+=	prog.${_prog}
STAGE_AS_prog.${_prog}=	${PROGNAME.${_prog}:UPROGNAME_${_prog}:U${_prog}}
stage_as.prog.${_prog}: ${_prog}
.endfor
.endif	# ${MK_STAGING} != "no" && !empty(PROGS)
.endif
.endif	# PROGS || PROGS_CXX

# These are handled by the main make process.
.ifdef _RECURSING_PROGS
MK_STAGING= no

_PROGS_GLOBAL_VARS= CLEANFILES CLEANDIRS CONFGROUPS DIRS FILESGROUPS INCSGROUPS \
		    SCRIPTS
.for v in ${_PROGS_GLOBAL_VARS}
$v =
.endfor
.endif

# handle being called [bsd.]progs.mk
.include <bsd.prog.mk>

# Find common sources among the PROGS to depend on them before building
# anything.  This allows parallelization without them each fighting over
# the same objects.
_PROGS_COMMON_SRCS=
_PROGS_ALL_SRCS=
.for p in ${PROGS}
.for s in ${SRCS.${p}}
.if ${_PROGS_ALL_SRCS:M${s}} && !${_PROGS_COMMON_SRCS:M${s}}
_PROGS_COMMON_SRCS+=	${s}
.else
_PROGS_ALL_SRCS+=	${s}
.endif
.endfor
.endfor
.if !empty(_PROGS_COMMON_SRCS)
_PROGS_COMMON_OBJS=	${_PROGS_COMMON_SRCS:M*.[dhly]}
.if !empty(_PROGS_COMMON_SRCS:N*.[dhly])
_PROGS_COMMON_OBJS+=	${_PROGS_COMMON_SRCS:N*.[dhly]:${OBJS_SRCS_FILTER:ts:}:S/$/.o/g}
.endif
.endif

# When recursing, ensure common sources are not rebuilt in META_MODE.
.if defined(_RECURSING_PROGS) && !empty(_PROGS_COMMON_OBJS) && \
    !empty(.MAKE.MODE:Mmeta)
${_PROGS_COMMON_OBJS}: .NOMETA
.endif

.if !empty(PROGS) && !defined(_RECURSING_PROGS) && !defined(PROG)
# tell progs.mk we might want to install things
PROGS_TARGETS+= checkdpadd clean depend install
# Only handle removing depend files from the main process.
_PROG_MK.cleandir=	CLEANDEPENDFILES= CLEANDEPENDDIRS=
_PROG_MK.cleanobj=	CLEANDEPENDFILES= CLEANDEPENDDIRS=
# Only recurse on these if there is no objdir, meaning a normal
# 'clean' gets ran via the target defined in bsd.obj.mk.
# Same check from cleanobj: in bsd.obj.mk
.if ${CANONICALOBJDIR} == ${.CURDIR} || !exists(${CANONICALOBJDIR}/)
PROGS_TARGETS+=	cleandir cleanobj
.endif

# Ensure common objects are built before recursing.
.if !empty(_PROGS_COMMON_OBJS)
${PROGS}: ${_PROGS_COMMON_OBJS}
.endif

.for p in ${PROGS}
.if defined(PROGS_CXX) && !empty(PROGS_CXX:M$p)
# bsd.prog.mk may need to know this
x.$p= PROG_CXX=$p
.endif

# Main PROG target
$p ${p}_p: .PHONY .MAKE
	(cd ${.CURDIR} && \
	    DEPENDFILE=.depend.$p \
	    NO_SUBDIR=1 ${MAKE} -f ${MAKEFILE} _RECURSING_PROGS=t \
	    PROG=$p ${x.$p})

# Pseudo targets for PROG, such as 'install'.
.for t in ${PROGS_TARGETS:O:u}
$p.$t: .PHONY .MAKE
	(cd ${.CURDIR} && \
	    DEPENDFILE=.depend.$p \
	    NO_SUBDIR=1 ${MAKE} -f ${MAKEFILE} _RECURSING_PROGS=t \
	    ${_PROG_MK.${t}} PROG=$p ${x.$p} ${@:E})
.endfor
.endfor

# Depend main pseudo targets on all PROG.pseudo targets too.
.for t in ${PROGS_TARGETS:O:u}
.if make(${t})
$t: ${PROGS:%=%.$t}
.endif
.endfor
.endif	# !empty(PROGS) && !defined(_RECURSING_PROGS) && !defined(PROG)
