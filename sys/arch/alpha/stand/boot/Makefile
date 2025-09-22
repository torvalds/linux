#	$OpenBSD: Makefile,v 1.27 2024/01/28 01:07:26 deraadt Exp $
#	$NetBSD: Makefile,v 1.17 1997/04/17 07:27:46 thorpej Exp $

S=	${.CURDIR}/../../../..

.PATH: ${.CURDIR}/..

PROG = boot

SRCS = start.S boot.c disk.c conf.c prom.c prom_disp.S OSFpal.c
SRCS+= devopen.c filesystem.c loadfile_subr.c prom_swpal.S
NOMAN=
INSTALL_STRIP=
BINMODE= 444

BOOT_RELOC = ${SECONDARY_LOAD_ADDRESS}

CLEANFILES+= vers.c vers.o ${PROG}.sym ${PROG}.nosym

.PATH:	${S}/lib/libkern/arch/alpha ${S}/lib/libkern
SRCS+=	__divlu.S __remqu.S __reml.S strlen.c bzero.c __divl.S __divqu.S \
	__remlu.S __divq.S __remq.S

.PATH:	${S}/lib/libkern/arch/alpha ${S}/lib/libsa
SRCS+=	loadfile.c arc4.c ufs2.c

DEFNS= -DCOMPAT_UFS

AFLAGS += -DASSEMBLER ${DEFNS} -fno-pie
CPPFLAGS += -I${.CURDIR}/../.. -I${S} ${DEFNS}
CFLAGS = -Oz -Werror -mno-fp-regs -fno-pie \
	-msmall-data -msmall-text -fno-stack-protector

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
	sh ${.CURDIR}/newvers.sh ${.CURDIR}/version
	${COMPILE.c} vers.c
	${LD} -nopie -znorelro -Ttext ${BOOT_RELOC} -N -e start \
	    -o ${PROG}.sym ${OBJS} vers.o ${LIBSA} ${LIBZ} ${LIBSA}
	size ${PROG}.sym
