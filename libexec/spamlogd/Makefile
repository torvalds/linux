#	$OpenBSD: Makefile,v 1.9 2019/07/25 14:53:21 brynet Exp $

PROG=	spamlogd
SRCS=	spamlogd.c sync.c gdcopy.c
MAN=	spamlogd.8

CFLAGS+= -Wall -Wstrict-prototypes -I${.CURDIR}/../spamd
# for pcap-int.h
CFLAGS+=-I${.CURDIR}/../../lib/libpcap
LDADD+= -lpcap -lcrypto
DPADD+=	${LIBPCAP} ${LIBCRYPTO}
.PATH:  ${.CURDIR}/../spamd

.include <bsd.prog.mk>
