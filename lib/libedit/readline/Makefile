#	$OpenBSD: Makefile,v 1.2 2016/03/20 23:48:27 schwarze Exp $
#	$NetBSD: Makefile,v 1.7 2003/08/03 09:23:15 lukem Exp $

NOOBJ=          # defined

.include <bsd.own.mk>

includes:
	 ${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
		${DESTDIR}/usr/include/readline
	-cd ${.CURDIR}; cmp -s readline.h ${DESTDIR}/usr/include/readline/readline.h > \
		/dev/null 2>&1 || \
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 readline.h \
		${DESTDIR}/usr/include/readline
	rm -f ${DESTDIR}/usr/include/readline/history.h
	cd ${DESTDIR}/usr/include/readline && ln -s readline.h history.h

.include <bsd.prog.mk>
