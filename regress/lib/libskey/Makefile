#       $OpenBSD: Makefile,v 1.3 2014/07/16 20:04:21 okan Exp $

PROG=	skeytest
CFLAGS+=-Wall
LDADD+=	-lskey
DPADD+=	${LIBSKEY}

skey-regress: skeytest
	./${PROG}

REGRESS_TARGETS=skey-regress
.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
