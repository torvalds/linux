#	$OpenBSD: Makefile,v 1.9 2015/03/12 02:19:10 bentley Exp $

PROG=	cribbage
DPADD=	${LIBCURSES}
LDADD=	-lcurses
SRCS=	extern.c crib.c cards.c instr.c io.c score.c support.c
MAN=	cribbage.6

beforeinstall: 
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/cribbage.n ${DESTDIR}/usr/share/games/cribbage.instr

.include <bsd.prog.mk>
