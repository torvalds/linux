# $FreeBSD$

TESTSDIR=	${TESTSBASE}/sys/kern/pipe

PLAIN_TESTS_C+= big_pipe_test
PLAIN_TESTS_C+= pipe_fstat_bug_test
PLAIN_TESTS_C+= pipe_ino_test
PLAIN_TESTS_C+= pipe_overcommit1_test
PLAIN_TESTS_C+= pipe_overcommit2_test
PLAIN_TESTS_C+= pipe_reverse2_test
PLAIN_TESTS_C+= pipe_reverse_test
PLAIN_TESTS_C+= pipe_wraparound_test

WARNS?=		6

.include <bsd.test.mk>
