# $FreeBSD$

PACKAGE=	tests

TESTSDIR=	${TESTSBASE}/sys/netmap
TEST_METADATA+=	required_user="root"
TEST_METADATA+=	is_exclusive=true

LDFLAGS+= 	-lpthread
PLAIN_TESTS_C+=	ctrl-api-test

WARNS?=		6

.include <bsd.test.mk>
