#	$OpenBSD: Makefile,v 1.2 2010/02/19 01:43:49 deraadt Exp $

.if ${MACHINE} == "loongson"

.include	"${.CURDIR}/../Makefile.inc"
CLEANFILES= mbr

all: mbr

mbr: mbr.uu
	uudecode ${.CURDIR}/mbr.uu

realinstall:
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 444 mbr \
		${DESTDIR}/usr/mdec

.endif

.include <bsd.prog.mk>
