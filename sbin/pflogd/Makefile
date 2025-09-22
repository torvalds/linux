#	$OpenBSD: Makefile,v 1.10 2022/08/29 17:00:30 deraadt Exp $

CFLAGS+=-Wall -Wmissing-prototypes -Wshadow

# for pcap-int.h
CFLAGS+=-I${.CURDIR}/../../lib/libpcap

LDADD+= -lpcap
DPADD+=	${LIBPCAP}

PROG=	pflogd
SRCS=	pflogd.c privsep.c privsep_fdpass.c
MAN=	pflogd.8

.include <bsd.prog.mk>

# Don't compile pflogd as static binary by default
LDSTATIC=       
