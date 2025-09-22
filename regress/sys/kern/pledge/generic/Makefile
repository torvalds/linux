#	$OpenBSD: Makefile,v 1.11 2024/06/03 08:02:22 anton Exp $
PROG=	generic
SRCS=	main.c manager.c test_stdio.c test_tty.c pty.c
NOMAN=	yes

LDADD+=		-lutil
CFLAGS+=	-Wall -Werror

REGRESS_TARGETS+=	test_normal

test_normal: ${PROG}
	ulimit -c unlimited && \
	./${PROG} | diff -I OpenBSD -u ${.CURDIR}/tests.out -

.include <bsd.regress.mk>
