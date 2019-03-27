# $FreeBSD$

# early setup only see also src.sys.mk

# bmake-20170301 started taking '-C' "as is" for some cases, notably absolute
# paths.  Some later comparisons will assume .CURDIR is resolved and matches
# what we would get with 'cd'.  So just force resolve it now if it is an
# absolute path.
.if ${MAKE_VERSION} >= 20170301 && !empty(.CURDIR:M/*)
.CURDIR:= ${.CURDIR:tA}
.endif

# make sure this is defined in a consistent manner
SRCTOP:= ${.PARSEDIR:tA:H:H}

.if ${.CURDIR} == ${SRCTOP}
RELDIR= .
RELTOP= .
.elif ${.CURDIR:M${SRCTOP}/*}
RELDIR:= ${.CURDIR:S,${SRCTOP}/,,}
.endif
RELTOP?= 	${RELDIR:C,[^/]+,..,g}
RELOBJTOP?=	${RELTOP}
RELSRCTOP?=	${RELTOP}

# site customizations that do not depend on anything!

# Save MAKEOBJDIRPREFIX and don't let src-env.conf modify it.
# MAKEOBJDIRPREFIX is only needed in MAKELEVEL=0.  In sub-makes it will
# either be read from environment or OBJDIR/MAKEOBJDIR according to
# src.sys.obj.mk.
.if !(${.MAKE.LEVEL} == 0 || empty(OBJROOT))
.if defined(MAKEOBJDIRPREFIX)
_saveMAKEOBJDIRPREFIX:=	${MAKEOBJDIRPREFIX}
.else
_undefMAKEOBJDIRPREFIX=	t
.endif
.endif

SRC_ENV_CONF?= /etc/src-env.conf
.if !empty(SRC_ENV_CONF) && !target(_src_env_conf_included_)
.-include "${SRC_ENV_CONF}"
_src_env_conf_included_:	.NOTMAIN
.endif

.if defined(_saveMAKEOBJDIRPREFIX) || defined(_undefMAKEOBJDIRPREFIX)
.if defined(MAKEOBJDIRPREFIX) && ((defined(_saveMAKEOBJDIRPREFIX) && \
    ${_saveMAKEOBJDIRPREFIX} != ${MAKEOBJDIRPREFIX}) || \
    defined(_undefMAKEOBJDIRPREFIX))
.warning ${SRC_ENV_CONF}: Ignoring MAKEOBJDIRPREFIX entry in sub-make.  Use '?=' to avoid this warning.
.endif
.if defined(_saveMAKEOBJDIRPREFIX)
MAKEOBJDIRPREFIX:=	${_saveMAKEOBJDIRPREFIX}
.undef _saveMAKEOBJDIRPREFIX
.elif defined(_undefMAKEOBJDIRPREFIX)
.undef MAKEOBJDIRPREFIX
.undef _undefMAKEOBJDIRPREFIX
.endif
.endif

.include <bsd.mkopt.mk>

# Top-level installs should not use meta mode as it may prevent installing
# based on cookies.
.if make(*install*) && ${.MAKE.LEVEL} == 0
META_MODE=	normal
MK_META_MODE=	no
.export MK_META_MODE
.endif

# If we were found via .../share/mk we need to replace that
# with ${.PARSEDIR:tA} so that we can be found by
# sub-makes launched from objdir.
.if ${.MAKEFLAGS:M.../share/mk} != ""
.MAKEFLAGS:= ${.MAKEFLAGS:S,.../share/mk,${.PARSEDIR:tA},}
.endif
.if ${MAKESYSPATH:Uno:M*.../*} != ""
MAKESYSPATH:= ${MAKESYSPATH:S,.../share/mk,${.PARSEDIR:tA},}
.export MAKESYSPATH
.elif empty(MAKESYSPATH)
MAKESYSPATH:=	${.PARSEDIR:tA}
.export MAKESYSPATH
.endif

.if ${RELDIR:U} == "." && ${.MAKE.LEVEL} == 0
.sinclude "${.CURDIR}/Makefile.sys.inc"
.endif
.include <src.sys.obj.mk>
