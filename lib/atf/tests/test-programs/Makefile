# $FreeBSD$

.include <bsd.init.mk>

TESTSDIR=	${TESTSBASE}/lib/atf/test-programs

KYUAFILE=	yes

ATF=		${SRCTOP}/contrib/atf
.PATH:		${ATF}/test-programs

CFLAGS+=	-I${ATF}

ATF_TESTS_C=		c_helpers

ATF_TESTS_CXX=		cpp_helpers
SRCS.cpp_helpers=	cpp_helpers.cpp

ATF_TESTS_SH=		sh_helpers
.for _T in config_test expect_test meta_data_test result_test srcdir_test
ATF_TESTS_SH+=		${_T}
ATF_TESTS_SH_SRC_${_T}=	common.sh ${_T}.sh
.endfor

.include <bsd.test.mk>
