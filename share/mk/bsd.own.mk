#	$OpenBSD: bsd.own.mk,v 1.213 2022/07/12 21:01:37 jca Exp $
#	$NetBSD: bsd.own.mk,v 1.24 1996/04/13 02:08:09 thorpej Exp $

# Host-specific overrides
.if defined(MAKECONF) && exists(${MAKECONF})
.include "${MAKECONF}"
.elif exists(/etc/mk.conf)
.include "/etc/mk.conf"
.endif

# Set `WARNINGS' to `yes' to add appropriate warnings to each compilation
WARNINGS?=	no
# Set `SKEY' to `yes' to build with support for S/key authentication.
SKEY?=		yes
# Set `YP' to `yes' to build with support for NIS/YP.
YP?=		yes

CLANG_ARCH=aarch64 amd64 arm i386 mips64 mips64el powerpc powerpc64 riscv64 sparc64
GCC4_ARCH=alpha hppa sh sparc64
GCC3_ARCH=m88k
LLD_ARCH=aarch64 amd64 arm i386 powerpc powerpc64 riscv64
LLDB_ARCH=aarch64 amd64

# m88k: ?
PIE_ARCH=aarch64 alpha amd64 arm hppa i386 mips64 mips64el powerpc powerpc64 riscv64 sh sparc64
STATICPIE_ARCH=aarch64 alpha amd64 arm hppa i386 mips64 mips64el powerpc powerpc64 riscv64 sh sparc64

.for _arch in ${MACHINE_ARCH}
.if !empty(GCC3_ARCH:M${_arch})
COMPILER_VERSION?=gcc3
.elif !empty(GCC4_ARCH:M${_arch})
COMPILER_VERSION?=gcc4
.elif !empty(CLANG_ARCH:M${_arch})
COMPILER_VERSION?=clang
.endif

.if !empty(GCC3_ARCH:M${_arch})
BUILD_GCC3?=yes
.else
BUILD_GCC3?=no
.endif
.if !empty(GCC4_ARCH:M${_arch})
BUILD_GCC4?=yes
.else
BUILD_GCC4?=no
.endif
.if !empty(CLANG_ARCH:M${_arch})
BUILD_CLANG?=yes
.else
BUILD_CLANG?=no
.endif

.if !empty(LLD_ARCH:M${_arch})
LINKER_VERSION?=lld
AR_VERSION?=llvm
.else
LINKER_VERSION?=bfd
AR_VERSION?=binutils
.endif

.if !empty(LLDB_ARCH:M${_arch})
BUILD_LLDB?=yes
.else
BUILD_LLDB?=no
.endif

.if !empty(STATICPIE_ARCH:M${_arch})
STATICPIE?=-pie
.endif

# Executables are always PIC on mips64.
# Do not pass -fno-pie to the compiler because clang does not accept it.
.if ${MACHINE_ARCH} == "mips64" || ${MACHINE_ARCH} == "mips64el"
NOPIE_FLAGS?=
.endif

.if !empty(PIE_ARCH:M${_arch})
NOPIE_FLAGS?=-fno-pie
NOPIE_LDFLAGS?=-nopie
PIE_DEFAULT?=${DEFAULT_PIE_DEF}
.else
NOPIE_FLAGS?=
PIE_DEFAULT?=
.endif
.endfor

# where the system object and source trees are kept; can be configurable
# by the user in case they want them in ~/foosrc and ~/fooobj, for example
BSDSRCDIR?=	/usr/src
BSDOBJDIR?=	/usr/obj

BINGRP?=	bin
BINOWN?=	root
BINMODE?=	555
NONBINMODE?=	444
DIRMODE?=	755

SHAREDIR?=	/usr/share
SHAREGRP?=	bin
SHAREOWN?=	root
SHAREMODE?=	${NONBINMODE}

MANDIR?=	/usr/share/man/man
MANGRP?=	bin
MANOWN?=	root
MANMODE?=	${NONBINMODE}

LIBDIR?=	/usr/lib
LIBGRP?=	${BINGRP}
LIBOWN?=	${BINOWN}
LIBMODE?=	${NONBINMODE}

DOCDIR?=	/usr/share/doc
DOCGRP?=	bin
DOCOWN?=	root
DOCMODE?=	${NONBINMODE}

LOCALEDIR?=	/usr/share/locale
LOCALEGRP?=	wheel
LOCALEOWN?=	root
LOCALEMODE?=	${NONBINMODE}

.if !defined(CDIAGFLAGS)
CDIAGFLAGS=	-Wall -Wpointer-arith -Wuninitialized -Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes -Wunused -Wsign-compare
CDIAGFLAGS+=	-Wshadow
.  if ${COMPILER_VERSION} == "gcc4"
CDIAGFLAGS+=	-Wdeclaration-after-statement
.  endif
.endif

# Shared files for system gnu configure, not used yet
GNUSYSTEM_AUX_DIR?=${BSDSRCDIR}/share/gnu

INSTALL_COPY?=	-c
.ifndef DEBUG
INSTALL_STRIP?=	-s
.endif

STATIC?=	-static ${STATICPIE}

# Define SYS_INCLUDE to indicate whether you want symbolic links to the system
# source (``symlinks''), or a separate copy (``copies''); (latter useful
# in environments where it's not possible to keep /sys publicly readable)
#SYS_INCLUDE= 	symlinks

# pic relocation flags.
.if ${MACHINE_ARCH} == "alpha" || ${MACHINE_ARCH} == "powerpc" || \
    ${MACHINE_ARCH} == "sparc64"
PICFLAG?=-fPIC
.else
PICFLAG?=-fpic
.endif

.if ${MACHINE_ARCH} == "alpha" || ${MACHINE_ARCH} == "powerpc" || \
    ${MACHINE_ARCH} == "sparc64"
# big PIE
DEFAULT_PIE_DEF=-DPIE_DEFAULT=2
.else
# small pie
DEFAULT_PIE_DEF=-DPIE_DEFAULT=1
.endif

# don't try to generate PROFILED versions of libraries on machines
# which don't support profiling.
.if 0
NOPROFILE=
.endif

BUILDUSER?= build
WOBJGROUP?= wobj
WOBJUMASK?= 007

BSD_OWN_MK=Done

.PHONY: spell clean cleandir obj manpages print all \
	depend beforedepend afterdepend cleandepend subdirdepend \
	all cleanman includes \
	beforeinstall realinstall maninstall afterinstall install
