#	$OpenBSD: Makefile,v 1.1 2001/09/25 09:42:02 espie Exp $
#

FILES= config.guess config.sub configure install-sh mkdirs.sh \
	move-if-change ltconfig ltmain.sh config.if

NOOBJ=	noobj

install:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		-m ${BINMODE} ${FILES} ${DESTDIR}${GNUSYSTEM_AUX_DIR}

.include <bsd.prog.mk>
