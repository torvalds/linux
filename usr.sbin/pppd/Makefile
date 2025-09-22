#	$OpenBSD: Makefile,v 1.19 2019/05/12 21:01:46 tedu Exp $

PCAPDIR=${.CURDIR}/../../lib/libpcap

PROG=	pppd
SRCS=	auth.c cbcp.c ccp.c chap.c demand.c fsm.c ipcp.c \
	lcp.c magic.c main.c options.c sys-bsd.c upap.c

MAN=	pppd.8
SUBDIR=	pppstats chat
BINMODE=4550
BINOWN=	root
BINGRP=	network

LDADD=	-lpcap -lutil
DPADD=	${LIBPCAP} ${LIBUTIL}
CFLAGS+= -I. -I${PCAPDIR} -DHAVE_PATHS_H -DPPP_FILTER

.include <bsd.prog.mk>
