# $FreeBSD$
#
# Early setup of MAKEOBJDIR
#
# Default format is: /usr/obj/usr/src/[${TARGET}.${TARGET_ARCH}/]bin/sh
#  MAKEOBJDIRPREFIX is	/usr/obj
#  OBJROOT is		/usr/obj/usr/src/
#  OBJTOP is		/usr/obj/usr/src/[${TARGET}.${TARGET_ARCH}/]
#  MAKEOBJDIR is	/usr/obj/usr/src/[${TARGET}.${TARGET_ARCH}/]bin/sh
#
#  MAKEOBJDIRPREFIX will override the default pattern above and internally
#  set MAKEOBJDIR.  If OBJROOT is set then MAKEOBJDIRPREFIX is rooted inside
#  of there.
#
#  If MK_UNIFIED_OBJDIR is no then OBJROOT will always match OBJTOP.
#
#  If .MAKE.LEVEL == 0 then the TARGET.TARGET_ARCH is potentially added on.
#  If .MAKE.LEVEL >  0 and MAKEOBJDIRPREFIX is set then it will not get
#  TARGET.TARGET_ARCH added in as it assumes that MAKEOBJDIRPREFIX is
#  nested in the existing OBJTOP with TARGET.TARGET_ARCH in it.
#
#  The expected OBJDIR is stored in __objdir for auto.obj.mk to use.
#
#  AUTO_OBJ is opportunistically enabled if the computed .OBJDIR is writable
#  by the current user.  Some top-level targets disable this behavior in
#  Makefile.sys.inc.
#

_default_makeobjdirprefix?=	/usr/obj
_default_makeobjdir=	$${.CURDIR:S,^$${SRCTOP},$${OBJTOP},}

.include <bsd.mkopt.mk>

.if ${.MAKE.LEVEL} == 0 || empty(OBJROOT)
.if ${MK_UNIFIED_OBJDIR} == "no" && ${MK_DIRDEPS_BUILD} == "no"
# Fall back to historical behavior.
# We always want to set a default MAKEOBJDIRPREFIX...
MAKEOBJDIRPREFIX?=	${_default_makeobjdirprefix}
# but don't enforce TARGET.TARGET_ARCH unless we're at the top-level directory.
.if ${.CURDIR} == ${SRCTOP} && \
    !(defined(TARGET) && defined(TARGET_ARCH) && \
    ${MACHINE} == ${TARGET} && ${MACHINE_ARCH} == ${TARGET_ARCH} && \
    !defined(CROSS_BUILD_TESTING))
MAKEOBJDIRPREFIX:=	${MAKEOBJDIRPREFIX}${TARGET:D/${TARGET}.${TARGET_ARCH}}
.endif
.endif	# ${MK_UNIFIED_OBJDIR} == "no"

.if !empty(MAKEOBJDIRPREFIX)
# put things approximately where they want
OBJROOT:=	${MAKEOBJDIRPREFIX}${SRCTOP}/
MAKEOBJDIRPREFIX=
# export but do not track
.export-env MAKEOBJDIRPREFIX
.endif
.if empty(MAKEOBJDIR)
# OBJTOP set below
MAKEOBJDIR=	${_default_makeobjdir}
# export but do not track
.export-env MAKEOBJDIR
# Expand for our own use
MAKEOBJDIR:=	${MAKEOBJDIR}
.endif
# SB documented at http://www.crufty.net/sjg/docs/sb-tools.htm
.if !empty(SB)
SB_OBJROOT?=	${SB}/obj/
# this is what we use below
OBJROOT?=	${SB_OBJROOT}
.endif
OBJROOT?=	${_default_makeobjdirprefix}${SRCTOP}/
.if ${OBJROOT:M*/} != ""
OBJROOT:=	${OBJROOT:H:tA}/
.else
OBJROOT:=	${OBJROOT:H:tA}/${OBJROOT:T}
.endif
# Must export since OBJDIR will dynamically be based on it
.export OBJROOT SRCTOP
.endif

.if ${MK_DIRDEPS_BUILD} == "no"
.if empty(OBJTOP)
# SRCTOP == OBJROOT only happens with clever MAKEOBJDIRPREFIX=/.  Don't
# append TARGET.TARGET_ARCH for that case since the user wants to build
# in the source tree.
.if ${MK_UNIFIED_OBJDIR} == "yes" && ${SRCTOP} != ${OBJROOT:tA}
.if defined(TARGET) && defined(TARGET_ARCH)
OBJTOP:=	${OBJROOT}${TARGET}.${TARGET_ARCH}
.elif defined(TARGET) && ${.CURDIR} == ${SRCTOP}
# Not enough information, just use basic OBJDIR.  This can happen with some
# 'make universe' targets or if TARGET is not being used as expected.
OBJTOP:=	${OBJROOT:H}
.else
OBJTOP:=	${OBJROOT}${MACHINE}.${MACHINE_ARCH}
.endif
.else
# TARGET.TARGET_ARCH handled in OBJROOT already.
OBJTOP:=	${OBJROOT:H}
.endif	# ${MK_UNIFIED_OBJDIR} == "yes"
.endif	# empty(OBJTOP)

# Fixup OBJROOT/OBJTOP if using MAKEOBJDIRPREFIX.
# This intenionally comes after adding TARGET.TARGET_ARCH so that is truncated
# away for nested objdirs.  This logic also will not trigger if the OBJROOT
# block above unsets MAKEOBJDIRPREFIX.
.if !empty(MAKEOBJDIRPREFIX)
OBJTOP:=	${MAKEOBJDIRPREFIX}${SRCTOP}
OBJROOT:=	${OBJTOP}/
.endif

# Wait to validate MAKEOBJDIR until OBJTOP is set.
.if defined(MAKEOBJDIR)
.if ${MAKEOBJDIR:M/*} == ""
.error Cannot use MAKEOBJDIR=${MAKEOBJDIR}${.newline}Unset MAKEOBJDIR to get default:  MAKEOBJDIR='${_default_makeobjdir}'
.endif
.endif

# __objdir is the expected .OBJDIR we want to use and that auto.obj.mk will
# try to create.
.if !empty(MAKEOBJDIRPREFIX)
.if ${.CURDIR:M${MAKEOBJDIRPREFIX}/*} != ""
# we are already in obj tree!
__objdir=	${.CURDIR}
.else
__objdir:=	${MAKEOBJDIRPREFIX}${.CURDIR}
.endif
.elif !empty(MAKEOBJDIR)
__objdir:=	${MAKEOBJDIR}
.endif

# Try to enable MK_AUTO_OBJ by default if we can write to the __objdir.  Only
# do this if AUTO_OBJ is not disabled by the user, and this is the first make
# ran.
.if ${.MAKE.LEVEL} == 0 && \
    ${MK_AUTO_OBJ} == "no" && empty(.MAKEOVERRIDES:MMK_AUTO_OBJ) && \
    !defined(WITHOUT_AUTO_OBJ) && !make(showconfig) && !make(print-dir) && \
    !make(test-system-*) && \
    !defined(NO_OBJ) && \
    empty(RELDIR:Msys/*/compile/*)
# Find the last existing directory component and check if we can write to it.
# If the last component is a symlink then recurse on the new path.
CheckAutoObj= \
DirIsCreatable() { \
	if [ -w "$${1}" ]; then \
		[ -d "$${1}" ] || return 1; \
		return 0; \
	fi; \
	d="$${1}"; \
	IFS=/; \
	set -- $${d}; \
	unset dir; \
	while [ $$\# -gt 0 ]; do \
		d="$${1}"; \
		shift; \
		if [ ! -d "$${dir}$${d}/" ]; then \
			if [ -L "$${dir}$${d}" ]; then \
				dir="$$(readlink "$${dir}$${d}")/"; \
				for d in "$${@}"; do \
					dir="$${dir}$${d}/"; \
				done; \
				ret=0; \
				DirIsCreatable "$${dir%/}" || ret=$$?; \
				return $${ret}; \
			elif [ -e "/$${dir}$${d}" ]; then \
				return 1; \
			else \
				break; \
			fi; \
		fi; \
		dir="$${dir}$${d}/"; \
	done; \
	[ -w "$${dir}" ] && [ -d "$${dir}" ] && return 0; \
	return 1; \
}; \
CheckAutoObj() { \
	if DirIsCreatable "$${1}"; then \
		echo yes; \
	else \
		echo no; \
	fi; \
}
.if !empty(__objdir)
.if ${.CURDIR} == ${__objdir} || \
    (exists(${__objdir}) && ${.TARGETS:M*install*} == ${.TARGETS})
__objdir_writable?= yes
.elif empty(__objdir_writable)
__objdir_writable!= \
	${CheckAutoObj}; CheckAutoObj "${__objdir}" || echo no
.endif
.endif
__objdir_writable?= no
# Export the decision to sub-makes.
MK_AUTO_OBJ:=	${__objdir_writable}
.export MK_AUTO_OBJ
.elif make(showconfig)
# Need to export for showconfig internally running make -dg1.  It is enabled
# in sys.mk by default.
.export MK_AUTO_OBJ
.endif	# ${MK_AUTO_OBJ} == "no" && ...

# Assign this directory as .OBJDIR if possible.
#
# The expected OBJDIR already exists, set it as .OBJDIR.
.if !empty(__objdir) && exists(${__objdir})
.OBJDIR: ${__objdir}
.else
# The OBJDIR we wanted does not yet exist, ensure we default to safe .CURDIR
# in case make started with a bogus MAKEOBJDIR, that expanded before OBJTOP
# was set, that happened to match some unexpected directory.  Either
# auto.obj.mk or bsd.obj.mk will create the directory and fix .OBJDIR later.
.OBJDIR: ${.CURDIR}
.endif

# Ensure .OBJDIR=.CURDIR cases have a proper OBJTOP and .OBJDIR
.if defined(NO_OBJ) || ${__objdir_writable:Uunknown} == "no" || \
    ${__objdir} == ${.CURDIR}
OBJTOP=		${SRCTOP}
OBJROOT=	${SRCTOP}/
# Compare only to avoid an unneeded chdir(2), :tA purposely left out.
.if ${.OBJDIR} != ${.CURDIR}
.OBJDIR:	${.CURDIR}
.endif
.endif	# defined(NO_OBJ)

.if !defined(HOST_TARGET)
# we need HOST_TARGET etc below.
.include <host-target.mk>
.export HOST_TARGET
.endif
HOST_OBJTOP?=	${OBJROOT}${HOST_TARGET}

.endif	# ${MK_DIRDEPS_BUILD} == "no"
