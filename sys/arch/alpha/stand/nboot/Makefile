#	$OpenBSD: Makefile,v 1.3 2024/01/28 01:07:26 deraadt Exp $
#	$NetBSD: Makefile,v 1.17 1997/04/17 07:27:46 thorpej Exp $

MAN?=	boot.8
MANSUBDIR=alpha

.if ${MACHINE} == "alpha"
S=	${.CURDIR}/../../../..

PROG=	boot
INSTALL_STRIP=
BINMODE= 444

# Needs to be first
SRCS=	start.S

SRCS+=	conf.c devopen.c disk.c machdep.c prom.c promcons.c \
	prom_swpal.S rpcc.S

BOOT_RELOC = ${SECONDARY_LOAD_ADDRESS}

CLEANFILES+= ${PROG}.sym ${PROG}.nosym

.PATH: ${.CURDIR}/..
SRCS+=	OSFpal.c loadfile_subr.c prom_disp.S

.PATH: ${S}/stand/boot
SRCS+=	boot.c cmd.c vars.c

.PATH:	${S}/lib/libkern/arch/alpha ${S}/lib/libkern
SRCS+=	__divlu.S __remqu.S __reml.S strlen.c bzero.c __divl.S __divqu.S \
	__remlu.S __divq.S __remq.S
SRCS+=	strlcpy.c

.PATH:	${S}/lib/libkern/arch/alpha ${S}/lib/libsa
SRCS+=	loadfile.c arc4.c ctime.c hexdump.c strtol.c strtoll.c ufs2.c

DEFNS= -DCOMPAT_UFS

AFLAGS += -DASSEMBLER ${DEFNS} -fno-pie
CPPFLAGS += -I${.CURDIR}/../.. -I${.CURDIR} -I${S} ${DEFNS}
CFLAGS = -Oz -Werror -mno-fp-regs -fno-pie -msmall-data -msmall-text \
	-fno-stack-protector

${PROG}.nosym: ${PROG}.sym
	cp ${PROG}.sym ${PROG}.nosym
	${STRIP} ${PROG}.nosym

${PROG}: ${PROG}.nosym
	${OBJCOPY} --output-target=binary ${PROG}.nosym ${PROG}

.include <bsd.prog.mk>

### find out what to use for libz
Z_AS=		library
ZDST=		${.CURDIR}/../libz/${__objdir}
.include "${.CURDIR}/../libz/Makefile.inc"
LIBZ=		${ZLIB}

### find out what to use for libsa
SA_AS=		library
SADST=		${.CURDIR}/../libsa/${__objdir}
SAREL=
SA_ZLIB=	yes
.include "${.CURDIR}/../libsa/Makefile.inc"
LIBSA=		${SALIB}

${PROG}.sym: ${OBJS} ${LIBSA} ${LIBZ}
	${LD} -nopie -znorelro -Ttext ${BOOT_RELOC} -N -e start \
	    -o ${PROG}.sym ${OBJS} ${LIBSA} ${LIBZ} ${LIBSA}
	size ${PROG}.sym

.else
NOPROG=
.include <bsd.prog.mk>
.endif

