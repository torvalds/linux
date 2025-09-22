# $OpenBSD: Makefile,v 1.6 2021/10/03 00:48:45 deraadt Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}
LDADD=-lz
DPADD=${LIBZ}

FIRM=	myx-ethp_z8e myx-eth_z8e

PROG=	build

CLEANFILES+= ${FIRM} ${PROG}

all: ${FIRM}

${FIRM}: build
	${.OBJDIR}/build

realinstall:

afterinstall:
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${FIRM} ${DESTDIR}/etc/firmware
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${.CURDIR}/myx-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
