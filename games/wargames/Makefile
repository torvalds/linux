#	$OpenBSD: Makefile,v 1.6 2019/12/21 21:40:00 espie Exp $

NOOBJ=	noobj
all wargames clean cleandir tags:

install:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/wargames.sh ${DESTDIR}/usr/games/wargames

.include <bsd.prog.mk>
