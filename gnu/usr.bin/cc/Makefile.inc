# $FreeBSD$

.include <src.opts.mk>
.include "../Makefile.inc"

# Sometimes this is .include'd several times...
.if !defined(__CC_MAKEFILE_INC__)
__CC_MAKEFILE_INC__= ${MFILE}

GCCVER=	4.2
GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs

.include "Makefile.tgt"

# Machine description.
MD_FILE=	${GCCDIR}/config/${GCC_CPU}/${GCC_CPU}.md
GCC_TARGET=	${TARGET_ARCH}-undermydesk-freebsd

CFLAGS+=	-DGCCVER=\"${GCCVER}\"
CFLAGS+=	-DIN_GCC -DHAVE_CONFIG_H
CFLAGS+=	-DPREFIX=\"${TOOLS_PREFIX}/usr\"
#CFLAGS+=	-DWANT_COMPILER_INVARIANTS
CSTD?=	gnu89

.if ${TARGET_ARCH} != ${MACHINE_ARCH}
CFLAGS+=	-DCROSS_DIRECTORY_STRUCTURE
CFLAGS+=	-DTARGET_SYSTEM_ROOT=\"${TOOLS_PREFIX}\"
.endif

.if ${TARGET_CPUARCH} == "arm"
CFLAGS+=	-DTARGET_ARM_EABI
.endif

.if ${TARGET_ARCH:Marm*eb} != ""
CFLAGS += -DTARGET_ENDIAN_DEFAULT=MASK_BIG_END
.endif
.if ${TARGET_ARCH:Marmv6*} != ""
CFLAGS += -DFREEBSD_ARCH_armv6
.endif
.if ${TARGET_ARCH:Marmv7*} != ""
CFLAGS += -DFREEBSD_ARCH_armv7
.endif

.if ${TARGET_CPUARCH} == "mips"
.if ${TARGET_ARCH:Mmips*el*} != ""
CFLAGS += -DTARGET_ENDIAN_DEFAULT=0
.endif

.if ${TARGET_ARCH:Mmips64*} != ""
MIPS_ABI_DEFAULT=ABI_64
.elif ${TARGET_ARCH:Mmipsn32*} != ""
MIPS_ABI_DEFAULT=ABI_N32
.else
MIPS_ABI_DEFAULT=ABI_32
.endif
CFLAGS += -DMIPS_ABI_DEFAULT=${MIPS_ABI_DEFAULT}

# If we are compiling for the O32 ABI, we need to default to MIPS-III rather
# than taking the ISA from the ABI requirements, since FreeBSD is built with
# a number of MIPS-III features/instructions and that is the minimum ISA we
# support, not the O32 default MIPS-I.
.if ${MIPS_ABI_DEFAULT} == "ABI_32"
TARGET_CPUTYPE?=mips3
.endif

# GCC by default takes the ISA from the ABI's requirements.  If world is built
# with a superior ISA, since we lack multilib, we have to set the right
# default ISA to be able to link against what's in /usr/lib.  Terrible stuff.
.if defined(TARGET_CPUTYPE)
CFLAGS += -DMIPS_CPU_STRING_DEFAULT=\"${TARGET_CPUTYPE}\"
.endif
.endif

.if defined(WANT_FORCE_OPTIMIZATION_DOWNGRADE)
CFLAGS+= -DFORCE_OPTIMIZATION_DOWNGRADE=${WANT_FORCE_OPTIMIZATION_DOWNGRADE}
.endif

.if exists(${.OBJDIR}/../cc_tools)
CFLAGS+=	-I${.OBJDIR}/../cc_tools
.endif
CFLAGS+=	-I${.CURDIR}/../cc_tools
# This must go after the -I for cc_tools to resolve ambiguities for hash.h
# correctly.
CFLAGS+=	-I${GCCDIR} -I${GCCDIR}/config

CFLAGS+=	-I${GCCLIB}/include
CFLAGS+=	-I${GCCLIB}/libcpp/include
CFLAGS+=	-I${GCCLIB}/libdecnumber

.if exists(${.OBJDIR}/../cc_int)
LIBBACKEND=	${.OBJDIR}/../cc_int/libbackend.a
.else
LIBBACKEND=	${.CURDIR}/../cc_int/libbackend.a
.endif

.if exists(${.OBJDIR}/../libiberty)
LIBIBERTY=	${.OBJDIR}/../libiberty/libiberty.a
.else
LIBIBERTY=	${.CURDIR}/../libiberty/libiberty.a
.endif

.if exists(${.OBJDIR}/../libcpp)
LIBCPP=		${.OBJDIR}/../libcpp/libcpp.a
.else
LIBCPP=		${.CURDIR}/../libcpp/libcpp.a
.endif

.if exists(${.OBJDIR}/../libdecnumber)
LIBDECNUMBER=	${.OBJDIR}/../libdecnumber/libdecnumber.a
.else
LIBDECNUMBER=	${.CURDIR}/../libdecnumber/libdecnumber.a
.endif

.endif # !__CC_MAKEFILE_INC__
