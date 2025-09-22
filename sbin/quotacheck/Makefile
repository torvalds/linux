#	$OpenBSD: Makefile,v 1.7 2012/02/08 20:38:50 krw Exp $

PROG=	quotacheck
SRCS=	quotacheck.c preen.c fsutil.c
CFLAGS+=-I${.CURDIR}/../fsck
MAN=	quotacheck.8
.PATH:	${.CURDIR}/../fsck

LDADD+=-lutil
DPADD+=${LIBUTIL}

.include <bsd.prog.mk>
