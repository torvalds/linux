# $FreeBSD$

PACKAGE=	tests

BINDIR=		${TESTSDIR}

PROGS+=		crypt
WARNS?=		6
LIBADD+=	crypt

ATF_TESTS_SH=	pw_etcdir_test \
		pw_lock_test \
		pw_config_test \
		pw_groupadd_test \
		pw_groupdel_test \
		pw_groupmod_test \
		pw_groupshow_test \
		pw_useradd_test \
		pw_userdel_test \
		pw_usermod_test \
		pw_usernext_test \
		pw_usershow_test

.for tp in ${ATF_TESTS_SH}
TEST_METADATA.${tp}+=	required_user="root"
.endfor

${PACKAGE}FILES+=	group
${PACKAGE}FILES+=	helper_functions.shin
${PACKAGE}FILES+=	master.passwd
${PACKAGE}FILES+=	pw.conf
${PACKAGE}FILES+=	pw-modified.conf

.include <bsd.test.mk>
