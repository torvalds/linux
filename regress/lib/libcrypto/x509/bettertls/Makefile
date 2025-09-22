#	$OpenBSD: Makefile,v 1.7 2025/07/23 07:46:12 tb Exp $

PROGS =		verify

.ifdef EOPENSSL35
LDADD +=	-Wl,-rpath,/usr/local/lib/eopenssl35 -L/usr/local/lib/eopenssl35
CFLAGS +=	-I/usr/local/include/eopenssl35/
.endif

LDADD +=	-lcrypto
DPADD +=	${LIBCRYPTO}
WARNINGS ?=	Yes
CFLAGS +=	-g -DLIBRESSL_INTERNAL -Wundef -Werror

REGRESS_TARGETS =  bettertls-test
CLEANFILES +=	test.out

bettertls-test: bettertls-verify
	${.CURDIR}/check.perl ${.CURDIR}/expected ${.CURDIR}/known ${.OBJDIR}/test.out

bettertls-verify: verify
	./verify ${.CURDIR}/certificates > test.out

.include <bsd.regress.mk>
