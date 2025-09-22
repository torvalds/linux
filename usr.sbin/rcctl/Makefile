#	$OpenBSD: Makefile,v 1.1 2014/08/19 14:08:20 ajacoutot Exp $

MAN=	rcctl.8

SCRIPT=	rcctl.sh

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/${SCRIPT} ${DESTDIR}${BINDIR}/rcctl

.include <bsd.prog.mk>
