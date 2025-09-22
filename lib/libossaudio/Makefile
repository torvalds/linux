#	$OpenBSD: Makefile,v 1.6 2020/04/02 19:57:10 ratchov Exp $
#	$NetBSD: Makefile,v 1.5 1998/04/13 14:18:45 lukem Exp $

LIB=	ossaudio
MAN=	ossaudio.3

SRCS=	ossaudio.c aucat.c debug.c sioctl.c sioctl_aucat.c sioctl_sun.c

CPPFLAGS+= -I${.CURDIR}

.PATH:	${.CURDIR}/../libsndio

includes:
	@cd ${.CURDIR}; cmp -s soundcard.h ${DESTDIR}/usr/include/soundcard.h || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) soundcard.h \
	  ${DESTDIR}/usr/include

.include <bsd.lib.mk>
