#	$OpenBSD: Makefile,v 1.1 2008/04/22 20:53:16 ajacoutot Exp $

MAN=	sysmerge.8

SCRIPT=	sysmerge.sh

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/${SCRIPT} ${DESTDIR}${BINDIR}/sysmerge

.include <bsd.prog.mk>
