#	$OpenBSD: Makefile,v 1.2 2021/09/13 11:49:21 robert Exp $

SCRIPT=	reorder_kernel.sh
MAN=	bsd.re-config.5

realinstall:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/${SCRIPT} ${DESTDIR}${BINDIR}/reorder_kernel

.include <bsd.prog.mk>
