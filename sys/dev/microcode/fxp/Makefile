# $OpenBSD: Makefile,v 1.9 2021/10/03 00:48:45 deraadt Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM=  fxp-d101a fxp-d101b0 fxp-d101ma fxp-d101s fxp-d102 fxp-d102c fxp-d102e

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
	    ${.CURDIR}/fxp-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
