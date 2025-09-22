#	$OpenBSD: Makefile,v 1.2 2023/05/12 14:34:00 claudio Exp $

FILES=	kprofile.bt runqlat.bt uprofile.bt
NOOBJ=	noobj

install:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 ${FILES} \
	    ${DESTDIR}${BINDIR}/btrace

.include <bsd.prog.mk>
