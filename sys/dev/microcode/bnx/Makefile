# $OpenBSD: Makefile,v 1.7 2021/10/03 00:48:45 deraadt Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM=	bnx-b06 bnx-b09 bnx-rv2p bnx-xi-rv2p bnx-xi90-rv2p

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
	    ${.CURDIR}/bnx-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
