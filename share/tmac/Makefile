#	$OpenBSD: Makefile,v 1.8 2019/12/21 21:40:01 espie Exp $
#	from: @(#)Makefile	6.13 (Berkeley) 3/20/91

FILES=	tmac.andoc
LINKS=	${BINDIR}/tmac/tmac.andoc ${BINDIR}/tmac/tmac.an
NOOBJ=	noobj

SUBDIR=	mdoc

all clean cleandir tags:

realinstall:
	install -c -o ${BINOWN} -g ${BINGRP} -m 444 ${FILES} \
		${DESTDIR}${BINDIR}/tmac

.include <bsd.prog.mk>
