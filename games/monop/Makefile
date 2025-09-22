#	$OpenBSD: Makefile,v 1.8 2024/02/08 20:28:53 miod Exp $

PROG=	monop
SRCS=	monop.c cards.c execute.c getinp.c houses.c jail.c misc.c morg.c \
	print.c prop.c rent.c roll.c spec.c trade.c
MAN=	monop.6
CLEANFILES+=initdeck initdeck.d cards.pck

monop: cards.pck

cards.pck: initdeck
	${.OBJDIR}/initdeck ${.CURDIR}/cards.inp

initdeck: initdeck.c
	${HOSTCC} ${CFLAGS} ${LDFLAGS} ${LDSTATIC} -o ${.TARGET} ${.CURDIR}/initdeck.c ${LDADD}

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 cards.pck \
	    ${DESTDIR}/usr/share/games

.include <bsd.prog.mk>
