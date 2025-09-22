#	$OpenBSD: Makefile,v 1.5 2002/05/23 18:43:00 deraadt Exp $

PROG=	wump
MAN=	wump.6

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/wump.info ${DESTDIR}/usr/share/games

.include <bsd.prog.mk>
