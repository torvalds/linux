# $OpenBSD: Makefile,v 1.14 2021/10/03 00:48:45 deraadt Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM=	ral-rt2561 ral-rt2561s ral-rt2661 ral-rt2860 ral-rt3290

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
	    ${.CURDIR}/ral-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
