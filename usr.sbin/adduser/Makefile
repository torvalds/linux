#	$OpenBSD: Makefile,v 1.9 2016/03/30 06:38:46 jmc Exp $

SCRIPTS= adduser.perl rmuser.perl rmgroup.sh
MAN=	 adduser.8 rmgroup.8

beforeinstall:
.for script in ${SCRIPTS}
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/${script} ${DESTDIR}${BINDIR}/${script:R}
.endfor

.include <bsd.prog.mk>
