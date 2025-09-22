#	$OpenBSD: Makefile,v 1.1 2015/07/20 23:52:29 yasuoka Exp $

PROG=			radius_test
REGRESS_TARGETS=	run-radius_test

SRCS=		main.c
SRCS+=		test00.c test01.c test02.c test03.c test04.c test05.c test06.c
SRCS+=		test10.c test11.c
SRCS+=		test20.c test21.c test22.c test23.c test24.c test25.c

CFLAGS+=	-std=gnu99 -Wall -g -O0
LDFLAGS+=	-g

DPADD+=		${LIBRADIUS} ${LIBCRYPTO}
LDADD+=		-lradius -lcrypto

NOMAN=		#defined

run-radius_test: radius_test
	./radius_test

.include <bsd.regress.mk>
