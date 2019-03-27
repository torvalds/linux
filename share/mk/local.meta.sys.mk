# $FreeBSD$

# local configuration specific to meta mode
# XXX some of this should be in meta.sys.mk
# we assume that MK_DIRDEPS_BUILD=yes

# we need this until there is an alternative
MK_INSTALL_AS_USER= yes

.if !defined(HOST_TARGET) || !defined(HOST_MACHINE)
# we need HOST_TARGET etc below.
.include <host-target.mk>
.export HOST_TARGET
.endif

# from src/Makefile (for universe)
TARGET_ARCHES_arm?=     arm armv6 armv7
TARGET_ARCHES_arm64?=   aarch64
TARGET_ARCHES_mips?=    mipsel mips mips64el mips64 mipsn32 mipsn32el
TARGET_ARCHES_powerpc?= powerpc powerpc64 powerpcspe
TARGET_ARCHES_riscv?=   riscv64 riscv64sf

# some corner cases
BOOT_MACHINE_DIR.amd64 = boot/i386
MACHINE_ARCH.host = ${_HOST_ARCH}

# the list of machines we support
ALL_MACHINE_LIST?= amd64 arm arm64 i386 mips powerpc riscv sparc64
.for m in ${ALL_MACHINE_LIST:O:u}
MACHINE_ARCH_LIST.$m?= ${TARGET_ARCHES_${m}:U$m}
MACHINE_ARCH.$m?= ${MACHINE_ARCH_LIST.$m:[1]}
BOOT_MACHINE_DIR.$m ?= boot/$m
.endfor

.ifndef _TARGET_SPEC
.if empty(MACHINE_ARCH)
.if !empty(TARGET_ARCH)
MACHINE_ARCH= ${TARGET_ARCH}
.else
MACHINE_ARCH= ${MACHINE_ARCH.${MACHINE}}
.endif
.endif
MACHINE_ARCH?= ${MACHINE_ARCH.${MACHINE}}
MACHINE_ARCH:= ${MACHINE_ARCH}
.else
# we got here via dirdeps
MACHINE_ARCH:= ${MACHINE_ARCH.${MACHINE}}
.endif

# now because for universe we want to potentially
# build for multiple MACHINE_ARCH per MACHINE
# we need more than MACHINE in TARGET_SPEC
TARGET_SPEC_VARS= MACHINE MACHINE_ARCH
# see dirdeps.mk
.if ${TARGET_SPEC:Uno:M*,*} != ""
_tspec := ${TARGET_SPEC:S/,/ /g}
MACHINE := ${_tspec:[1]}
MACHINE_ARCH := ${_tspec:[2]}
# etc.
# We need to stop that TARGET_SPEC affecting any submakes
# and deal with MACHINE=${TARGET_SPEC} in the environment.
TARGET_SPEC=
# export but do not track
.export-env TARGET_SPEC 
.export ${TARGET_SPEC_VARS}
.for v in ${TARGET_SPEC_VARS:O:u}
.if empty($v)
.undef $v
.endif
.endfor
.endif
# make sure we know what TARGET_SPEC is
# as we may need it to find Makefile.depend*
TARGET_SPEC = ${TARGET_SPEC_VARS:@v@${$v:U}@:ts,}

# to be consistent with src/Makefile just concatenate with '.'s
TARGET_OBJ_SPEC:= ${TARGET_SPEC:S;,;.;g}
OBJTOP:= ${OBJROOT}${TARGET_OBJ_SPEC}

.if defined(MAKEOBJDIR)
.if ${MAKEOBJDIR:M/*} == ""
.error Cannot use MAKEOBJDIR=${MAKEOBJDIR}${.newline}Unset MAKEOBJDIR to get default:  MAKEOBJDIR='${_default_makeobjdir}'
.endif
.endif

HOST_OBJTOP ?= ${OBJROOT}${HOST_TARGET}

.if ${OBJTOP} == ${HOST_OBJTOP} || ${REQUESTED_MACHINE:U${MACHINE}} == "host"
MACHINE= host
.if ${TARGET_MACHINE:Uno} == ${HOST_TARGET}
# not what we want
TARGET_MACHINE= host
.endif
.endif
.if ${MACHINE} == "host"
OBJTOP := ${HOST_OBJTOP}
.endif

.if ${.MAKE.LEVEL} == 0
PYTHON ?= /usr/local/bin/python
.export PYTHON
# this works best if share/mk is ready for it.
BUILD_AT_LEVEL0= no
# _SKIP_BUILD is not 100% as it requires wrapping all 'all:' targets to avoid
# building in MAKELEVEL0.  Just prohibit 'all' entirely in this case to avoid
# problems.
.if ${MK_DIRDEPS_BUILD} == "yes" && \
    ${.MAKE.LEVEL} == 0 && ${BUILD_AT_LEVEL0:Uyes:tl} == "no"
.MAIN: dirdeps
.if make(all)
.error DIRDEPS_BUILD: Please run '${MAKE}' instead of '${MAKE} all'.
.endif
.endif
.endif

# we want to end up with a singe stage tree for all machines
.if ${MK_STAGING} == "yes"
.if empty(STAGE_ROOT)
STAGE_ROOT?= ${OBJROOT}stage
.export STAGE_ROOT
.endif
.endif

.if ${MK_STAGING} == "yes"
.if ${MACHINE} == "host"
STAGE_MACHINE= ${HOST_TARGET}
.else
STAGE_MACHINE:= ${TARGET_OBJ_SPEC}
.endif
STAGE_OBJTOP:= ${STAGE_ROOT}/${STAGE_MACHINE}
STAGE_COMMON_OBJTOP:= ${STAGE_ROOT}/common
STAGE_TARGET_OBJTOP:= ${STAGE_ROOT}/${TARGET_OBJ_SPEC}
STAGE_HOST_OBJTOP:= ${STAGE_ROOT}/${HOST_TARGET}
# These are exported for hooking in out-of-tree builds.  They will always
# be overridden in sub-makes above when building in-tree.
.export STAGE_OBJTOP STAGE_TARGET_OBJTOP STAGE_HOST_OBJTOP

# Use tools/install.sh which can avoid the need for xinstall for simple cases.
INSTALL?=	sh ${SRCTOP}/tools/install.sh
# This is for stage-install to pickup from the environment.
REAL_INSTALL:=	${INSTALL}
.export REAL_INSTALL
STAGE_INSTALL=	sh ${.PARSEDIR:tA}/stage-install.sh OBJDIR=${.OBJDIR:tA}

STAGE_LIBDIR= ${STAGE_OBJTOP}${_LIBDIR:U${LIBDIR:U/lib}}
STAGE_INCLUDEDIR= ${STAGE_OBJTOP}${INCLUDEDIR:U/usr/include}
# this is not the same as INCLUDEDIR
STAGE_INCSDIR= ${STAGE_OBJTOP}${INCSDIR:U/include}
# the target is usually an absolute path
STAGE_SYMLINKS_DIR= ${STAGE_OBJTOP}

#LDFLAGS_LAST+= -Wl,-rpath-link,${STAGE_LIBDIR}
.if ${MK_SYSROOT} == "yes"
SYSROOT?= ${STAGE_OBJTOP}
.else
LDFLAGS_LAST+= -L${STAGE_LIBDIR}
.endif

.endif				# MK_STAGING

.-include "local.toolchain.mk"

# this is sufficient for most of the tree.
.MAKE.DEPENDFILE_DEFAULT = ${.MAKE.DEPENDFILE_PREFIX}

# but if we have a machine qualified file it should be used in preference
.MAKE.DEPENDFILE_PREFERENCE = \
	${.MAKE.DEPENDFILE_PREFIX}.${MACHINE} \
	${.MAKE.DEPENDFILE_PREFIX}

.undef .MAKE.DEPENDFILE

.include "sys.dependfile.mk"

.if ${.MAKE.LEVEL} > 0 && ${MACHINE} == "host" && ${.MAKE.DEPENDFILE:E} != "host"
# we can use this but should not update it.
UPDATE_DEPENDFILE= NO
.endif

# define the list of places that contain files we are responsible for
.MAKE.META.BAILIWICK = ${SB} ${OBJROOT} ${STAGE_ROOT}

CSU_DIR.${MACHINE_ARCH} ?= csu/${MACHINE_ARCH}
CSU_DIR := ${CSU_DIR.${MACHINE_ARCH}}

.if !empty(TIME_STAMP)
TRACER= ${TIME_STAMP} ${:U}
.endif
.if !defined(_RECURSING_PROGS) && !defined(_RECURSING_CRUNCH) && \
    !make(print-dir)
WITH_META_STATS= t
.endif

# toolchains can be a pain - especially bootstrappping them
.if ${MACHINE} == "host"
MK_SHARED_TOOLCHAIN= no
.endif
TOOLCHAIN_VARS=	AS AR CC CLANG_TBLGEN CXX CPP LD NM OBJCOPY RANLIB \
		STRINGS SIZE LLVM_TBLGEN
_toolchain_bin_CLANG_TBLGEN=	/usr/bin/clang-tblgen
_toolchain_bin_LLVM_TBLGEN=	/usr/bin/llvm-tblgen
_toolchain_bin_CXX=		/usr/bin/c++
.ifdef WITH_TOOLSDIR
TOOLSDIR?= ${HOST_OBJTOP}/tools
.elif defined(STAGE_HOST_OBJTOP)
TOOLSDIR?= ${STAGE_HOST_OBJTOP}
.endif
# Only define if it exists in case user didn't run bootstrap-tools.  Otherwise
# the tool will be built during the build.  Building it assumes it is
# TARGET==MACHINE.
.if exists(${HOST_OBJTOP}/tools${.CURDIR})
BTOOLSPATH= ${HOST_OBJTOP}/tools${.CURDIR}
.endif

# Don't use the bootstrap tools logic on itself.
.if ${.TARGETS:Mbootstrap-tools} == "" && \
    !make(test-system-*) && !make(showconfig) && !make(print-dir) && \
    !defined(BOOTSTRAPPING_TOOLS) && !empty(TOOLSDIR) && ${.MAKE.LEVEL} == 0
.for dir in /sbin /bin /usr/sbin /usr/bin
PATH:= ${TOOLSDIR}${dir}:${PATH}
.endfor
.export PATH
# Prefer the TOOLSDIR version of the toolchain if present vs the host version.
.for var in ${TOOLCHAIN_VARS}
_toolchain_bin.${var}=	${TOOLSDIR}${_toolchain_bin_${var}:U/usr/bin/${var:tl}}
.if exists(${_toolchain_bin.${var}})
HOST_${var}?=	${_toolchain_bin.${var}}
.export		HOST_${var}
.endif
.endfor
.endif

.for var in ${TOOLCHAIN_VARS}
HOST_${var}?=	${_toolchain_bin_${var}:U/usr/bin/${var:tl}}
.endfor

.if ${MACHINE} == "host"
.for var in ${TOOLCHAIN_VARS}
${var}=		${HOST_${var}}
.endfor
.endif

.if ${MACHINE:Nhost:Ncommon} != "" && ${MACHINE} != ${HOST_MACHINE}
# cross-building
.if !defined(FREEBSD_REVISION)
FREEBSD_REVISION!= sed -n '/^REVISION=/{s,.*=,,;s,",,g;p; }' ${SRCTOP}/sys/conf/newvers.sh
.export FREEBSD_REVISION
.endif
CROSS_TARGET_FLAGS= -target ${MACHINE_ARCH}-unknown-freebsd${FREEBSD_REVISION}
CFLAGS+= ${CROSS_TARGET_FLAGS}
ACFLAGS+= ${CROSS_TARGET_FLAGS}
LDFLAGS+= -Wl,-m -Wl,elf_${MACHINE_ARCH}_fbsd
.endif

META_MODE+=	missing-meta=yes
.if empty(META_MODE:Mnofilemon)
META_MODE+=	missing-filemon=yes
.endif
