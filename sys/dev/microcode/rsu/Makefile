# $OpenBSD: Makefile,v 1.1 2021/10/04 01:33:42 kevlo Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM=	rsu-rtl8712

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
	    ${.CURDIR}/rsu-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
