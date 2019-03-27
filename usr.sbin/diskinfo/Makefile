# $FreeBSD$

PROG=	diskinfo
MAN=	diskinfo.8

LIBADD=	util

.include <bsd.prog.mk>

test:	${PROG}
	./${PROG} /dev/ad4 md50
	./${PROG} -v /dev/ad4 md50
	./${PROG} -t /dev/ad4
