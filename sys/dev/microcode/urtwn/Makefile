# $OpenBSD: Makefile,v 1.2 2023/04/28 01:24:14 kevlo Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

FIRM=	urtwn-rtl8188eu urtwn-rtl8188ftv urtwn-rtl8192cT urtwn-rtl8192cU \
	urtwn-rtl8192eu

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
	    ${.CURDIR}/urtwn-license ${DESTDIR}/etc/firmware

.include <bsd.prog.mk>
