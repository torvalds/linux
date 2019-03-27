# $FreeBSD$

TESTSDIR=	${TESTSBASE}/sys/fifo

PLAIN_TESTS_C+=	fifo_create
PLAIN_TESTS_C+=	fifo_io
PLAIN_TESTS_C+=	fifo_misc
PLAIN_TESTS_C+=	fifo_open

TEST_METADATA.fifo_create+=	required_user="root"
TEST_METADATA.fifo_open+=	required_user="root"

.include <bsd.test.mk>
