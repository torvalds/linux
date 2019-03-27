# $FreeBSD$

.include <src.opts.mk>

PACKAGE=runtime
PROG=	dumpon

.if ${MK_OPENSSL} != "no"
LIBADD=	crypto
CFLAGS+=-DHAVE_CRYPTO
.endif

MAN=	dumpon.8

.include <bsd.prog.mk>
