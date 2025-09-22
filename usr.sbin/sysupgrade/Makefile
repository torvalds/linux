#	$OpenBSD: Makefile,v 1.2 2025/05/22 04:34:18 bluhm Exp $

MAN=	sysupgrade.8

SCRIPT=	sysupgrade.sh

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/${SCRIPT} ${DESTDIR}${BINDIR}/sysupgrade

.include <bsd.prog.mk>
