# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/acl/cifs
FILESDIR=${TESTSDIR}

${PACKAGE}FILES+=	cifs.kshlib
${PACKAGE}FILES+=	cifs_attr_001_pos.ksh
${PACKAGE}FILES+=	cifs_attr_002_pos.ksh
${PACKAGE}FILES+=	cifs_attr_003_pos.ksh

ATF_TESTS_KSH93+=	cifs_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

.include <bsd.test.mk>
