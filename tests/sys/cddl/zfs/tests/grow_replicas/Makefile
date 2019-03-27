# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/grow_replicas
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	grow_replicas_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	grow_replicas.cfg
${PACKAGE}FILES+=	grow_replicas_001_pos.ksh

.include <bsd.test.mk>
