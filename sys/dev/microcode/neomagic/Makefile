# $OpenBSD: Makefile,v 1.3 2007/04/05 23:41:20 drahn Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

.if (${MACHINE} == "i386")

FIRM= 	neo-coefficients

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
	    ${.CURDIR}/neo-license ${DESTDIR}/etc/firmware
.endif

.include <bsd.prog.mk>
