#	$OpenBSD: Makefile,v 1.20 2024/01/28 01:07:26 deraadt Exp $
#	$NetBSD: Makefile,v 1.11 1997/04/17 07:27:50 thorpej Exp $

S=	${.CURDIR}/../../../..

.PATH: ${.CURDIR}/.. ${.CURDIR}/../boot

PROG = netboot

SRCS = start.S boot.c prom.c prom_disp.S OSFpal.c prom_swpal.S
SRCS+= rpcc.S
SRCS+= dev_net.c conf.c devopen.c
SRCS+= if_prom.c getsecs.c
SRCS+= loadfile_subr.c
NOMAN=
INSTALL_STRIP=
BINMODE= 444

BOOT_RELOC = ${PRIMARY_LOAD_ADDRESS}

CLEANFILES+= vers.c vers.o ${PROG}.sym ${PROG}.nosym ${PROG}.mop

.PATH:  ${S}/lib/libkern/arch/alpha ${S}/lib/libkern
SRCS+=  bzero.c __divqu.S strlen.c __remqu.S __remq.S __remlu.S

.PATH:	${S}/lib/libkern/arch/alpha ${S}/lib/libsa
SRCS+=	loadfile.c arc4.c

DEFNS= -DPRIMARY_BOOTBLOCK -DSUN_BOOTPARAMS

AFLAGS += -DASSEMBLER ${DEFNS} -fno-pie
CPPFLAGS += -I${.CURDIR}/../.. -I${S} ${DEFNS}
CFLAGS = -Werror -mno-fp-regs -fno-pie -fno-stack-protector

${PROG}.nosym: ${PROG}.sym
	cp ${PROG}.sym ${PROG}.nosym
	${STRIP} ${PROG}.nosym

${PROG} ${PROG}.mop: ${PROG}.nosym
	${OBJCOPY} --output-target=binary ${PROG}.nosym ${PROG}
	mopa.out ${PROG}.nosym ${PROG}.mop

realinstall:
	${INSTALL} ${INSTALL_COPY} ${INSTALL_STRIP} \
	    -o ${BINOWN} -g ${BINGRP} \
	    -m ${BINMODE} ${PROG} ${DESTDIR}${BINDIR}/${PROG}
	${INSTALL} ${INSTALL_COPY} ${INSTALL_STRIP} \
	    -o ${BINOWN} -g ${BINGRP} \
	    -m ${BINMODE} ${PROG}.mop ${DESTDIR}${BINDIR}/${PROG}.mop

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
