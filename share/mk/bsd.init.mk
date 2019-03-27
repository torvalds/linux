# $FreeBSD$

# The include file <bsd.init.mk> includes <bsd.opts.mk>,
# ../Makefile.inc and <bsd.own.mk>; this is used at the
# top of all <bsd.*.mk> files that actually "build something".
# bsd.opts.mk is included early so Makefile.inc can use the
# MK_FOO variables.

.if !target(__<bsd.init.mk>__)
__<bsd.init.mk>__:
.include <bsd.opts.mk>
.-include "local.init.mk"

.if ${MK_AUTO_OBJ} == "yes"
# This is also done in bsd.obj.mk
.if defined(NO_OBJ) && ${.OBJDIR} != ${.CURDIR}
.OBJDIR: ${.CURDIR}
.endif
.endif

.if exists(${.CURDIR}/../Makefile.inc)
.include "${.CURDIR}/../Makefile.inc"
.endif
.include <bsd.own.mk>
.MAIN: all

# This is used in bsd.{dep,lib,prog}.mk as ${OBJS_SRCS_FILTER:ts:}
# Some makefiles may want T as well to avoid nested objdirs.
OBJS_SRCS_FILTER+= R

# Handle INSTALL_AS_USER here to maximize the chance that
# it has final authority over fooOWN and fooGRP.
.if ${MK_INSTALL_AS_USER} != "no"
.if !defined(_uid)
_uid!=	id -u
.export _uid
.endif
.if ${_uid} != 0
.if !defined(_gid)
_gid!=	id -g
.export _gid
.endif
.for x in BIN CONF DOC DTB INFO KMOD LIB MAN NLS SHARE
$xOWN=	${_uid}
$xGRP=	${_gid}
.endfor
.endif
.endif

# Some targets need to know when something may build.  This is used to
# optimize targets that are only needed when building something, such as
# (not) reading in depend files.  For DIRDEPS_BUILD, it will only calculate
# the dependency graph at .MAKE.LEVEL==0, so nothing should be built there.
# Skip "build" logic if:
# - DIRDEPS_BUILD at MAKELEVEL 0
# - make -V is used without an override
# - make install is used without other targets.  This is to avoid breaking
#   things like 'make all install' or 'make foo install'.
# - non-build targets are called
.if ${MK_DIRDEPS_BUILD} == "yes" && ${.MAKE.LEVEL:U1} == 0 && \
    ${BUILD_AT_LEVEL0:Uyes:tl} == "no" && !make(clean*)
_SKIP_BUILD=	not building at level 0
.elif !empty(.MAKEFLAGS:M-V${_V_DO_BUILD}) || \
    ${.TARGETS:M*install*} == ${.TARGETS} || \
    ${.TARGETS:Mclean*} == ${.TARGETS} || \
    ${.TARGETS:Mdestroy*} == ${.TARGETS} || \
    ${.TARGETS:Mobj} == ${.TARGETS} || \
    make(analyze) || make(print-dir)
# Skip building, but don't show a warning.
_SKIP_BUILD=
.endif
.if ${MK_DIRDEPS_BUILD} == "yes" && ${.MAKE.LEVEL} > 0 && !empty(_SKIP_BUILD)
.warning ${_SKIP_BUILD}
.endif

beforebuild: .PHONY .NOTMAIN
.if !defined(_SKIP_BUILD)
all: beforebuild .WAIT
.endif

.if ${MK_META_MODE} == "yes"
.if !exists(/dev/filemon) && \
    ${UPDATE_DEPENDFILE:Uyes:tl} != "no" && !defined(NO_FILEMON) && \
    !make(test-system-*) && !make(showconfig) && !make(print-dir) && \
    ${.MAKEFLAGS:M-V} == ""
.warning The filemon module (/dev/filemon) is not loaded.
.warning META_MODE is less useful for incremental builds without filemon.
.warning 'kldload filemon' or pass -DNO_FILEMON to suppress this warning.
.endif
.endif	# ${MK_META_MODE} == "yes"

.endif	# !target(__<bsd.init.mk>__)
