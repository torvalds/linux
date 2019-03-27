# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/xattr
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	xattr_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	xattr_002_neg.ksh
${PACKAGE}FILES+=	xattr_012_pos.ksh
${PACKAGE}FILES+=	xattr_013_pos.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	xattr_003_neg.ksh
${PACKAGE}FILES+=	xattr_007_neg.ksh
${PACKAGE}FILES+=	xattr_common.kshlib
${PACKAGE}FILES+=	xattr.cfg
${PACKAGE}FILES+=	xattr_006_pos.ksh
${PACKAGE}FILES+=	xattr_011_pos.ksh
${PACKAGE}FILES+=	xattr_010_neg.ksh
${PACKAGE}FILES+=	xattr_009_neg.ksh
${PACKAGE}FILES+=	xattr_008_pos.ksh
${PACKAGE}FILES+=	xattr_004_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	xattr_005_pos.ksh
${PACKAGE}FILES+=	xattr_001_pos.ksh

.include <bsd.test.mk>
