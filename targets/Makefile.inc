# $FreeBSD$

.if !target(__${_this}__)
__${_this}__:

_CURDIR ?= ${.CURDIR}
_OBJDIR ?= ${.OBJDIR}

.if ${RELDIR:Mtargets/*} != "" || ${RELDIR} == "."

.if ${.MAKE.LEVEL} == 0 && ${RELDIR:Mtargets/*} != ""
.if make(bootstrap*) || !exists(${_CURDIR}/${.MAKE.DEPENDFILE:T})
_bootstrap_dirdeps = yes
.else
# BUILD_AT_LEVEL0=no tells dirdeps.mk to use a sub-make for .CURDIR
# This is useful if we need to leverage DIRDEPS
BUILD_AT_LEVEL0 = no
# this renders the rest harmless
PKG_METHOD = no
.endif
.endif

# The makefile in subdirs should set this to something useful
# the default should do nothing.
PKG_METHOD ?= none

.MAIN: all

.if ${build-*:${M_L_TARGETS}} != ""
# just build the bits, skip packaging
all:
SHIPDIR = no
.if ${RELDIR} != "."
PKG_METHOD = no
.endif
.endif

.if ${PKG_METHOD:Mno*} != ""
all: ${PKG_METHOD}

# set this to "no" for manual control
UPDATE_DEPENDFILE ?= yes
.endif

.if ${UPDATE_DEPENDFILE:Uno} == "yes"
.include <meta.autodep.mk>
.endif

# all the clever packaging methods go here....

.endif					# level 0

.endif					# _this
