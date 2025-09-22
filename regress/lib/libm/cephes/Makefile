#	$OpenBSD: Makefile,v 1.3 2023/01/27 16:39:22 miod Exp $

PROG = cephes
SRCS = cephes.c const.c constll.c monot.c monotl.c monotll.c polevll.c \
       testvect.c testvectl.c testvectll.c
LDADD = -lm
DPADD = ${LIBM}
CFLAGS+= -fno-builtin

.include <bsd.regress.mk>
