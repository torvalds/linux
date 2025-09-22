#	$OpenBSD: Makefile,v 1.8 2016/03/30 06:38:43 jmc Exp $
#	$NetBSD: Makefile,v 1.5 1999/07/23 09:44:38 mrg Exp $

LIB=	usbhid
MAN=	usbhid.3

SRCS=	descr.c parse.c usage.c data.c

CPPFLAGS+= -I${.CURDIR}

includes:
	@cd ${.CURDIR}; cmp -s usbhid.h ${DESTDIR}/usr/include/usbhid.h || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) usbhid.h \
	  ${DESTDIR}/usr/include

.include <bsd.lib.mk>
