# $FreeBSD$

.PATH: ${.CURDIR:H}
SHLIB=		h_csu
SHLIB_NAME=	libh_csu.so
SHLIB_MAJOR=	1

WITHOUT_STATIC=
WITHOUT_PROFILE=
WITHOUT_PIC=

CFLAGS+=	-DDSO_LIB

.include "../Makefile.tests"
SRCS=
.for src in ${ATF_TESTS_C}
SRCS+=	${src}.c
.endfor
.for src in ${ATF_TESTS_CXX}
SRCS+=	${src}.cc
.endfor

LIBDIR=		${TESTSBASE}/lib/csu/dynamiclib/

.include <bsd.lib.mk>
