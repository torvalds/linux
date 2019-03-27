# $FreeBSD$

.PATH: ${.CURDIR:H}
CFLAGS+=	-DDSO_BASE
DPADD+=		${.OBJDIR:H}/dso/libh_csu.so
LDFLAGS+=	-Wl,-rpath,${TESTSDIR} -L${.OBJDIR:H}/dso
LDADD+=		-lh_csu

.include "../Makefile.tests"

.for test in ${ATF_TESTS_C}
ATF_TESTS_CXX+=	${test}
SRCS.${test}=	${test}.c
.endfor
ATF_TESTS_C:=

.include <bsd.test.mk>
