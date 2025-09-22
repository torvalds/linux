# $OpenBSD: Makefile,v 1.5 2025/07/23 07:47:23 tb Exp $

EOPENSSL =	eopenssl35

LDADD +=	-Wl,-rpath,/usr/local/lib/${EOPENSSL} -L/usr/local/lib/${EOPENSSL}
CFLAGS +=	-I${.CURDIR}/ -I/usr/local/include/${EOPENSSL}/
CFLAGS +=	-DOPENSSL_SUPPRESS_DEPRECATED

.PATH:		${.CURDIR}/..

.include <bsd.regress.mk>
