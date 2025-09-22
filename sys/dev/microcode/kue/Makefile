# $OpenBSD: Makefile,v 1.19 2021/10/03 00:48:45 deraadt Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM= 	kue

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
	    ${.CURDIR}/kue-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
