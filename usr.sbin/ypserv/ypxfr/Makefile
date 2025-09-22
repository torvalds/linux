#	$OpenBSD: Makefile,v 1.5 2016/03/30 06:38:47 jmc Exp $

PROG=	ypxfr
SRCS=	ypxfr.c ypdb.c yplog.c yplib_host.c
MAN=	ypxfr.8
LDADD=	-lrpcsvc
DPADD=	${LIBRPCSVC}
SCRIPTS= ypxfr_1perhour.sh ypxfr_2perday.sh ypxfr_1perday.sh

.PATH:	${.CURDIR}/../common
CFLAGS+=-I${.CURDIR}/../common

beforeinstall:
.for script in ${SCRIPTS}
	${INSTALL} ${COPY} -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	    ${.CURDIR}/${script} ${DESTDIR}${BINDIR}/${script:R}
.endfor

.include <bsd.prog.mk>
