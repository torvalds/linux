#	$OpenBSD: Makefile,v 1.4 2012/10/14 04:12:05 deraadt Exp $

CLEANFILES= mbr

all: mbr

mbr: mbr.uu
	uudecode ${.CURDIR}/mbr.uu

realinstall:
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 444 mbr ${DESTDIR}/usr/mdec


.include <bsd.prog.mk>
