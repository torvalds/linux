#	$OpenBSD: Makefile,v 1.13 2017/04/15 13:12:08 schwarze Exp $
#	from: @(#)Makefile	5.9 (Berkeley) 7/1/91

SUBDIR=	man1 man3 man4 man5 man6 man7 man8 man9

afterinstall:
	${INSTALL} ${INSTALL_COPY} -o ${MANOWN} -g ${MANGRP} -m ${MANMODE} \
	    man0/COPYRIGHT ${DESTDIR}/usr/share/man/COPYRIGHT

makedb:
	/usr/sbin/makewhatis ${DESTDIR}/usr/share/man
	chown root:wheel ${DESTDIR}/usr/share/man/mandoc.db
	chmod 644 ${DESTDIR}/usr/share/man/mandoc.db

.include <bsd.subdir.mk>
