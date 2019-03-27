# $FreeBSD$

.include <bsd.init.mk>

ATF=		${SRCTOP}/contrib/atf
.PATH:		${ATF}/atf-sh

ATF_TESTS_SH+=	atf_check_test
ATF_TESTS_SH+=	config_test
ATF_TESTS_SH+=	integration_test
ATF_TESTS_SH+=	normalize_test
ATF_TESTS_SH+=	tc_test
ATF_TESTS_SH+=	tp_test

integration_test: Makefile
ATF_TESTS_SH_SED_integration_test= \
    -e 's,__ATF_SH__,/usr/libexec/atf-sh,g'

SCRIPTS+=		misc_helpers
SCRIPTSDIR_misc_helpers=${TESTSDIR}
CLEANFILES+=		misc_helpers misc_helpers.tmp
misc_helpers: misc_helpers.sh
	echo '#! /usr/libexec/atf-sh' >${.TARGET}.tmp
	cat ${.ALLSRC} >>${.TARGET}.tmp
	chmod +x ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}

.include <bsd.test.mk>
