# $OpenBSD: Makefile,v 1.7 2021/10/03 00:48:46 deraadt Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM= 	yds

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
	    ${.CURDIR}/yds-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
