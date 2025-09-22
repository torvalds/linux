#       $OpenBSD: Makefile,v 1.4 2014/07/13 21:13:30 jasper Exp $

NOMACHINE=
FILES=	bsd.tbxi
NOOBJ=	noobj

install:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 ${FILES} \
	    ${DESTDIR}/usr/mdec

.include <bsd.prog.mk>
