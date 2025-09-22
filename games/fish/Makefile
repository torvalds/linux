#	$OpenBSD: Makefile,v 1.5 2002/05/23 18:43:00 deraadt Exp $

PROG=	fish
MAN=	fish.6

beforeinstall: 
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/fish.instr ${DESTDIR}/usr/share/games/fish.instr

.include <bsd.prog.mk>
