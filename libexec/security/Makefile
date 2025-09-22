#	$OpenBSD: Makefile,v 1.1 2011/03/23 21:13:27 schwarze Exp $

realinstall: 
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
		${.CURDIR}/security ${DESTDIR}${BINDIR}/security

.include <bsd.prog.mk>
