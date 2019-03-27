# $FreeBSD$

.include <bsd.own.mk>

.if !defined(NO_PIC)
SUBDIR+=	dso
.endif

# TODO: doesn't link properly (for some odd reason it's trying to link in
#       libatf.so)
#NETBSD_ATF_TESTS_C=	tls_static_test
.if !defined(NO_PIC)
NETBSD_ATF_TESTS_C+=	tls_dlopen_test
NETBSD_ATF_TESTS_C+=	tls_dynamic_test
.endif

.include "../Makefile.netbsd-tests"

DSODIR=		${.OBJDIR:H}/tls_dso

LIBADD.tls_static_test+=	pthread
LDFLAGS.tls_static_test+=	-static
SRCS.tls_static_test=		t_tls_static.c t_tls_static_helper.c

DPADD.tls_dynamic_test+=	${DSODIR}/libh_tls_dynamic.so
LDADD.tls_dynamic_test+=	-lh_tls_dynamic
LDFLAGS.tls_dynamic_test+=	-Wl,-rpath,${TESTSDIR} -L${DSODIR}
LIBADD.tls_dynamic_test+=	pthread

LIBADD.tls_dlopen_test+=	pthread
LDFLAGS.tls_dlopen_test+=	-Wl,-rpath,${TESTSDIR} -Wl,-export-dynamic

.include <bsd.test.mk>
