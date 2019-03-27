# $FreeBSD$

TESTSDIR= ${TESTSBASE}/lib/libdevdctl

.PATH:	${.CURDIR:H}

PLAIN_TESTS_CXX= libdevdctl_unittest

SRCS.libdevdctl_unittest+= 	event_factory.cc	\
				libdevdctl_unittest.cc	\
				event.cc exception.cc	\
				guid.cc
CFLAGS.libdevdctl_unittest+= -I ${LOCALBASE}/include -D_THREAD_SAFE -pthread
DPADD.libdevdctl_unittest+= ${LIBDEVDCTL}
LDADD.libdevdctl_unittest+= -L ${LOCALBASE}/lib -D_THREAD_SAFE -pthread -lgtest -lgtest_main

# Googletest options
LOCALBASE?=	/usr/local

WARNS?= 3
.include <bsd.test.mk>
