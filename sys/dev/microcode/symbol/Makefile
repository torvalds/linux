# $OpenBSD: Makefile,v 1.14 2021/05/01 16:11:16 visa Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

# pcmcia capable systems only
.if (${MACHINE} == "i386") || (${MACHINE} == "amd64") || \
    (${MACHINE} == "macppc") || \
    (${MACHINE} == "sparc64") || (${MACHINE} == "hppa")

afterinstall:
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${.CURDIR}/eprimsym ${DESTDIR}/etc/firmware/symbol-eprim
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${.CURDIR}/esecsym ${DESTDIR}/etc/firmware/symbol-esec
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${.CURDIR}/symbol-license ${DESTDIR}/etc/firmware
.endif

.include <bsd.prog.mk>
