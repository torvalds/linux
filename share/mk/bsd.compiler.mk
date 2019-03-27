# $FreeBSD$

# Setup variables for the compiler
#
# COMPILER_TYPE is the major type of compiler. Currently gcc and clang support
# automatic detection. Other compiler types can be shoe-horned in, but require
# explicit setting of the compiler type. The compiler type can also be set
# explicitly if, say, you install gcc as clang...
#
# COMPILER_VERSION is a numeric constant equal to:
#     major * 10000 + minor * 100 + tiny
# It too can be overridden on the command line. When testing it, be sure to
# make sure that you are limiting the test to a specific compiler. Testing
# against 30300 for gcc likely isn't  what you wanted (since versions of gcc
# prior to 4.2 likely have no prayer of working).
#
# COMPILER_FREEBSD_VERSION is the compiler's __FreeBSD_cc_version value.
#
# COMPILER_FEATURES will contain one or more of the following, based on
# compiler support for that feature:
#
# - c++11:     supports full (or nearly full) C++11 programming environment.
# - retpoline: supports the retpoline speculative execution vulnerability
#              mitigation.
#
# These variables with an X_ prefix will also be provided if XCC is set.
#
# This file may be included multiple times, but only has effect the first time.
#

.if !target(__<bsd.compiler.mk>__)
__<bsd.compiler.mk>__:

.include <bsd.opts.mk>

.if defined(_NO_INCLUDE_COMPILERMK)
# If _NO_INCLUDE_COMPILERMK is set we are doing a make obj/cleandir/cleanobj
# and might not have a valid compiler in $PATH yet. In this case just set the
# variables that are expected by the other .mk files and return
COMPILER_TYPE=none
X_COMPILER_TYPE=none
COMPILER_VERSION=0
X_COMPILER_VERSION=0
COMPILER_FEATURES=none
.else
# command = /usr/local/bin/ccache cc ...
# wrapper = /usr/local/libexec/ccache/cc ...
CCACHE_BUILD_TYPE?=	command
# Handle ccache after CC is determined, but not if CC/CXX are already
# overridden with a manual setup.
.if ${MK_CCACHE_BUILD:Uno} == "yes" && \
    !make(test-system-*) && !make(print-dir) && !make(showconfig) && \
    (${CC:M*ccache/world/*} == "" || ${CXX:M*ccache/world/*} == "")
# CC is always prepended with the ccache wrapper rather than modifying
# PATH since it is more clear that ccache is used and avoids wasting time
# for mkdep/linking/asm builds.
LOCALBASE?=		/usr/local
CCACHE_WRAPPER_PATH?=	${LOCALBASE}/libexec/ccache
CCACHE_BIN?=		${LOCALBASE}/bin/ccache
.if exists(${CCACHE_BIN})
# Export to ensure sub-makes can filter it out for mkdep/linking and
# to chain down into kernel build which won't include this file.
.export CCACHE_BIN
# Expand and export some variables so they may be based on make vars.
# This allows doing something like the following in the environment:
# CCACHE_BASEDIR='${SRCTOP:H}' MAKEOBJDIRPREFIX='${SRCTOP:H}/obj'
.for var in CCACHE_LOGFILE CCACHE_BASEDIR
.if defined(${var})
${var}:=	${${var}}
.export		${var}
.endif
.endfor
# Handle bootstrapped compiler changes properly by hashing their content
# rather than checking mtime.  For external compilers it should be safe
# to use the more optimal mtime check.
# XXX: CCACHE_COMPILERCHECK= string:<compiler_version, compiler_build_rev, compiler_patch_rev, compiler_default_target, compiler_default_sysroot>
.if ${CC:N${CCACHE_BIN}:[1]:M/*} == ""
CCACHE_COMPILERCHECK?=	content
.else
CCACHE_COMPILERCHECK?=	mtime
.endif
.export CCACHE_COMPILERCHECK
# Ensure no bogus CCACHE_PATH leaks in which might avoid the in-tree compiler.
.if !empty(CCACHE_PATH)
CCACHE_PATH=
.export CCACHE_PATH
.endif
.if ${CCACHE_BUILD_TYPE} == "command"
# Remove ccache from the PATH to prevent double calls and wasted CPP/LD time.
PATH:=	${PATH:C,:?${CCACHE_WRAPPER_PATH}(/world)?(:$)?,,g}
# Override various toolchain vars.
.for var in CC CXX HOST_CC HOST_CXX
.if defined(${var}) && ${${var}:M${CCACHE_BIN}} == ""
${var}:=	${CCACHE_BIN} ${${var}}
.endif
.endfor
.else
# Need to ensure CCACHE_WRAPPER_PATH is the first in ${PATH}
PATH:=	${PATH:C,:?${CCACHE_WRAPPER_PATH}(/world)?(:$)?,,g}
PATH:=	${CCACHE_WRAPPER_PATH}:${PATH}
CCACHE_WRAPPER_PATH_PFX=	${CCACHE_WRAPPER_PATH}:
.endif	# ${CCACHE_BUILD_TYPE} == "command"
# GCC does not need the CCACHE_CPP2 hack enabled by default in devel/ccache.
# The port enables it due to ccache passing preprocessed C to clang
# which fails with -Wparentheses-equality, -Wtautological-compare, and
# -Wself-assign on macro-expanded lines.
.if defined(COMPILER_TYPE) && ${COMPILER_TYPE} == "gcc"
CCACHE_NOCPP2=	1
.export CCACHE_NOCPP2
.endif
# Canonicalize CCACHE_DIR for meta mode usage.
.if !defined(CCACHE_DIR)
CCACHE_DIR!=	${CCACHE_BIN} -p | awk '$$2 == "cache_dir" {print $$4}'
.export CCACHE_DIR
.endif
.if !empty(CCACHE_DIR) && empty(.MAKE.META.IGNORE_PATHS:M${CCACHE_DIR})
CCACHE_DIR:=	${CCACHE_DIR:tA}
.MAKE.META.IGNORE_PATHS+= ${CCACHE_DIR}
.export CCACHE_DIR
.endif
# ccache doesn't affect build output so let it slide for meta mode
# comparisons.
.MAKE.META.IGNORE_PATHS+= ${CCACHE_BIN}
ccache-print-options: .PHONY
	@${CCACHE_BIN} -p
.endif	# exists(${CCACHE_BIN})
.endif	# ${MK_CCACHE_BUILD} == "yes"

_cc_vars=CC $${_empty_var_}
.if !empty(_WANT_TOOLCHAIN_CROSS_VARS)
# Only the toplevel makefile needs to compute the X_COMPILER_* variables.
# Skipping the computation of the unused X_COMPILER_* in the subdirectory
# makefiles can save a noticeable amount of time when walking the whole source
# tree (e.g. during make includes, etc.).
_cc_vars+=XCC X_
.endif

.for cc X_ in ${_cc_vars}
.if ${cc} == "CC" || !empty(XCC)
# Try to import COMPILER_TYPE and COMPILER_VERSION from parent make.
# The value is only used/exported for the same environment that impacts
# CC and COMPILER_* settings here.
_exported_vars=	${X_}COMPILER_TYPE ${X_}COMPILER_VERSION \
		${X_}COMPILER_FREEBSD_VERSION
${X_}_cc_hash=	${${cc}}${MACHINE}${PATH}
${X_}_cc_hash:=	${${X_}_cc_hash:hash}
# Only import if none of the vars are set somehow else.
_can_export=	yes
.for var in ${_exported_vars}
.if defined(${var})
_can_export=	no
.endif
.endfor
.if ${_can_export} == yes
.for var in ${_exported_vars}
.if defined(${var}.${${X_}_cc_hash})
${var}=	${${var}.${${X_}_cc_hash}}
.endif
.endfor
.endif

.if ${cc} == "CC" || (${cc} == "XCC" && ${XCC} != ${CC})
.if ${MACHINE} == "common"
# common is a pseudo machine for architecture independent
# generated files - thus there is no compiler.
${X_}COMPILER_TYPE= none
${X_}COMPILER_VERSION= 0
${X_}COMPILER_FREEBSD_VERSION= 0
.elif !defined(${X_}COMPILER_TYPE) || !defined(${X_}COMPILER_VERSION)
_v!=	${${cc}:N${CCACHE_BIN}} --version || echo 0.0.0

.if !defined(${X_}COMPILER_TYPE)
. if ${${cc}:T:M*gcc*}
${X_}COMPILER_TYPE:=	gcc
. elif ${${cc}:T:M*clang*}
${X_}COMPILER_TYPE:=	clang
. elif ${_v:Mgcc}
${X_}COMPILER_TYPE:=	gcc
. elif ${_v:M\(GCC\)} || ${_v:M*GNU}
${X_}COMPILER_TYPE:=	gcc
. elif ${_v:Mclang} || ${_v:M(clang-*.*.*)}
${X_}COMPILER_TYPE:=	clang
. else
.error Unable to determine compiler type for ${cc}=${${cc}}.  Consider setting ${X_}COMPILER_TYPE.
. endif
.endif
.if !defined(${X_}COMPILER_VERSION)
${X_}COMPILER_VERSION!=echo "${_v:M[1-9].[0-9]*}" | awk -F. '{print $$1 * 10000 + $$2 * 100 + $$3;}'
.endif
.undef _v
.endif
.if !defined(${X_}COMPILER_FREEBSD_VERSION)
${X_}COMPILER_FREEBSD_VERSION!=	{ echo "__FreeBSD_cc_version" | ${${cc}:N${CCACHE_BIN}} -E - 2>/dev/null || echo __FreeBSD_cc_version; } | sed -n '$$p'
# If we get a literal "__FreeBSD_cc_version" back then the compiler
# is a non-FreeBSD build that doesn't support it or some other error
# occurred.
.if ${${X_}COMPILER_FREEBSD_VERSION} == "__FreeBSD_cc_version"
${X_}COMPILER_FREEBSD_VERSION=	unknown
.endif
.endif

${X_}COMPILER_FEATURES=
.if ${${X_}COMPILER_TYPE} == "clang" || \
	(${${X_}COMPILER_TYPE} == "gcc" && ${${X_}COMPILER_VERSION} >= 40800)
${X_}COMPILER_FEATURES+=	c++11
.endif
.if ${${X_}COMPILER_TYPE} == "clang" && ${${X_}COMPILER_VERSION} >= 60000
${X_}COMPILER_FEATURES+=	retpoline
.endif

.else
# Use CC's values
X_COMPILER_TYPE=	${COMPILER_TYPE}
X_COMPILER_VERSION=	${COMPILER_VERSION}
X_COMPILER_FREEBSD_VERSION=	${COMPILER_FREEBSD_VERSION}
X_COMPILER_FEATURES=	${COMPILER_FEATURES}
.endif	# ${cc} == "CC" || (${cc} == "XCC" && ${XCC} != ${CC})

# Export the values so sub-makes don't have to look them up again, using the
# hash key computed above.
.for var in ${_exported_vars}
${var}.${${X_}_cc_hash}:=	${${var}}
.export-env ${var}.${${X_}_cc_hash}
.undef ${var}.${${X_}_cc_hash}
.endfor

.endif	# ${cc} == "CC" || !empty(XCC)
.endfor	# .for cc in CC XCC

.if !defined(_NO_INCLUDE_LINKERMK)
.include <bsd.linker.mk>
.endif
.endif	# defined(_NO_INCLUDE_COMPILERMK)
.endif	# !target(__<bsd.compiler.mk>__)
