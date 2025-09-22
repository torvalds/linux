# $OpenBSD: Makefile,v 1.12 2016/03/30 06:38:43 jmc Exp $

LIB=	skey
SRCS=	skeylogin.c skeysubr.c put.c
HDRS=	skey.h
MAN+=	skey.3 skey.5

includes:
	@cd ${.CURDIR}; for i in $(HDRS); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/`basename $$i` || \
		${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 $$i \
		${DESTDIR}/usr/include"; \
	    echo $$j; \
	    eval "$$j"; \
	done

.include <bsd.lib.mk>
