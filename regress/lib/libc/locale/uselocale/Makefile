#	$OpenBSD: Makefile,v 1.3 2017/08/16 01:40:30 schwarze Exp $

PROG =		uselocale
CFLAGS +=	-Wno-macro-redefined -Wno-int-to-pointer-cast
LDFLAGS +=	-pthread

run-regress-${PROG}: ${PROG}
	./${PROG}

.include <bsd.regress.mk>
