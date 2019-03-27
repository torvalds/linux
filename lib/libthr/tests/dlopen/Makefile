# $FreeBSD$

TESTSRC=	${SRCTOP}/contrib/netbsd-tests/lib/libpthread/dlopen

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/lib/libthr/dlopen

CFLAGS+=	-DTESTDIR=\"${TESTSDIR:Q}/\"
LDFLAGS+=	-L${.OBJDIR}/dso -Wl,-rpath=${TESTDIR}

.if !defined(NO_PIC)
SUBDIR+=	dso

NETBSD_ATF_TESTS_C=	dlopen_test
NETBSD_ATF_TESTS_C+=	main_pthread_create_test
# XXX: this blocks running the testcase
#NETBSD_ATF_TESTS_C+=	dso_pthread_create_test

.for t in dlopen_test main_pthread_create_test
LIBADD.${t}+=	pthread
.endfor
.endif

.include <netbsd-tests.test.mk>

.include <bsd.test.mk>
