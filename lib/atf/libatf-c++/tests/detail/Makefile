# $FreeBSD$

.include <bsd.init.mk>

TESTSDIR=	${TESTSBASE}/lib/atf/libatf-c++/detail

ATF=		${SRCTOP}/contrib/atf
.PATH:		${ATF}/atf-c++/detail

CFLAGS+=	-DATF_C_TESTS_BASE='"${TESTSBASE}/lib/atf/libatf-c"'
CFLAGS+=	-DATF_INCLUDEDIR='"${INCLUDEDIR}"'
CFLAGS+=	-I${ATF}

.for _T in	application_test \
		env_test \
		exceptions_test \
		fs_test \
		process_test \
		text_test
ATF_TESTS_CXX+=	${_T}
SRCS.${_T}=	${_T}.cpp test_helpers.cpp
.endfor

.for p in version_helper
PROGS_CXX+=	${p}
SRCS.${p}=	${p}.cpp
MAN.${p}=	# defined
BINDIR.${p}=	${TESTSDIR}
.endfor
version_helper.o: atf-version

.include "../../../common.mk"
.include <bsd.test.mk>
