#	$OpenBSD: Makefile,v 1.2 2023/01/27 16:39:22 miod Exp $

PROG=nextafter
LDADD=-lm
DPADD=${LIBM}
CFLAGS+= -fno-builtin

.include <bsd.regress.mk>
