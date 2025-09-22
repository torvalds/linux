#	$OpenBSD: Makefile,v 1.43 2016/03/30 06:38:42 jmc Exp $

.include <bsd.own.mk>

LIB=	event
SRCS=	buffer.c evbuffer.c event.c event_tagging.c evutil.c kqueue.c \
	log.c poll.c select.c signal.c
HDRS=	event.h evutil.h
MAN=	event.3 evbuffer_new.3

CFLAGS+= -I${.CURDIR} -DNDEBUG

# use more warnings than defined in bsd.own.mk
CDIAGFLAGS+=	-Wbad-function-cast
CDIAGFLAGS+=	-Wcast-align
CDIAGFLAGS+=	-Wcast-qual
CDIAGFLAGS+=	-Wextra
CDIAGFLAGS+=	-Wmissing-declarations
CDIAGFLAGS+=	-Wuninitialized
CDIAGFLAGS+=	-Wno-unused-parameter

includes:
	@cd ${.CURDIR}; for i in ${HDRS}; do \
	  cmp -s $$i ${DESTDIR}/usr/include/$$i || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) $$i \
	  ${DESTDIR}/usr/include; done

.include <bsd.lib.mk>
