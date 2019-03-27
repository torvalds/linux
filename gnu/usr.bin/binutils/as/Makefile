# $FreeBSD$


# BINDIR
.include "${.CURDIR}/../../Makefile.inc"
.include "${.CURDIR}/../Makefile.inc0"
.include <src.opts.mk>

.PATH: ${SRCDIR}/gas ${SRCDIR}/gas/config

.if ${TARGET_ARCH:Marm*} || ${TARGET_ARCH} == "powerpc64"
NO_WERROR.clang=
.endif

PROG=	as
SRCS+=	app.c \
	as.c \
	atof-generic.c \
	atof-ieee.c \
	cond.c \
	depend.c \
	dw2gencfi.c \
	dwarf2dbg.c \
	ecoff.c \
	ehopt.c \
	expr.c \
	flonum-copy.c \
	flonum-konst.c \
	flonum-mult.c \
	frags.c \
	hash.c \
	input-file.c \
	input-scrub.c \
	listing.c \
	literal.c \
	macro.c \
	messages.c \
	obj-elf.c \
	output-file.c \
	read.c \
	sb.c \
	stabs.c \
	subsegs.c \
	symbols.c \
	write.c
# DEO: why not used?
#SRCS+=	itbl-ops.c

.if ${TARGET_ARCH:Marmv6*} != ""
CFLAGS+=	-DCPU_DEFAULT=ARM_ARCH_V6K
.endif
.if ${TARGET_ARCH:Marmv7*} != ""
CFLAGS+=	-DCPU_DEFAULT=ARM_ARCH_V7A
.endif

.if ${TARGET_CPUARCH} == "mips"
SRCS+=	itbl-ops.c itbl-parse.y itbl-lex.l
.if ${TARGET_ARCH:Mmips64*} != ""
CFLAGS+=	-DMIPS_DEFAULT_ABI=N64_ABI -DMIPS_DEFAULT_64BIT=1
.elif ${TARGET_ARCH:Mmipsn32*} != ""
CFLAGS+=	-DMIPS_DEFAULT_ABI=N32_ABI
.else
MIPS_ABI_DEFAULT=ABI_32
.endif
.endif

.if ${TARGET_ARCH} == "amd64"
SRCS+=	tc-i386.c
.elif ${TARGET_CPUARCH} == "powerpc"
SRCS+=	tc-ppc.c
.elif ${TARGET_ARCH} == "sparc64"
SRCS+=	tc-sparc.c
.else
SRCS+=	tc-${TARGET_CPUARCH}.c
.endif

.if ${TARGET_ARCH} == "sparc64"
CFLAGS+= -DDEFAULT_ARCH=\"v9-64\"
.else
CFLAGS+= -DDEFAULT_ARCH=\"${BINUTILS_ARCH}\"
.endif
.if defined(TARGET_BIG_ENDIAN)
CFLAGS+= -DTARGET_BYTES_BIG_ENDIAN=1
.endif
CFLAGS+= -DTARGET_CPU=\"${BINUTILS_ARCH}\"
CFLAGS+= -DTARGET_OS=\"${TARGET_OS}\"
CFLAGS+= -DTARGET_CANONICAL=\"${TARGET_TUPLE}\"
CFLAGS+= -DTARGET_ALIAS=\"${TARGET_TUPLE}\"
CFLAGS+= -DVERSION=\"${VERSION}\"
CFLAGS+= -D_GNU_SOURCE
CFLAGS+= -I${SRCDIR}/gas -I${SRCDIR}/bfd -I${SRCDIR}/gas/config -I${SRCDIR}
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/${TARGET_CPUARCH}-freebsd

.if ${MK_SHARED_TOOLCHAIN} == "no"
NO_SHARED?=	yes
.endif

DPADD=	${GNURELTOP}/libbfd/libbfd${PIE_SUFFIX}.a
DPADD+=	${GNURELTOP}/libiberty/libiberty${PIE_SUFFIX}.a
DPADD+=	${GNURELTOP}/libopcodes/libopcodes${PIE_SUFFIX}.a
LDADD=	${DPADD}

.include <bsd.prog.mk>
