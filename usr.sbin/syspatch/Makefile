#	$OpenBSD: Makefile,v 1.1 2016/09/05 11:04:45 ajacoutot Exp $

MAN=	syspatch.8

SCRIPT=	syspatch.sh

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/${SCRIPT} ${DESTDIR}${BINDIR}/syspatch

.include <bsd.prog.mk>
