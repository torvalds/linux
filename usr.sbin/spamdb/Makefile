#	$OpenBSD: Makefile,v 1.4 2013/08/21 16:13:29 millert Exp $

PROG=	spamdb
SRCS=	spamdb.c gdcopy.c
MAN=	spamdb.8

CFLAGS+= -Wall -Wstrict-prototypes -I${.CURDIR}/../../libexec/spamd

.PATH: ${.CURDIR}/../../libexec/spamd

.include <bsd.prog.mk>
