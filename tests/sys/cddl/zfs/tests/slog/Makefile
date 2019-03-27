# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/slog
FILESDIR=${TESTSDIR}

ATF_TESTS_KSH93+=	slog_test
TEST_METADATA+=		required_user="root"
TEST_METADATA+=		is_exclusive=true

${PACKAGE}FILES+=	slog_012_neg.ksh
${PACKAGE}FILES+=	slog_013_pos.ksh
${PACKAGE}FILES+=	slog_002_pos.ksh
${PACKAGE}FILES+=	slog_006_pos.ksh
${PACKAGE}FILES+=	slog.kshlib
${PACKAGE}FILES+=	slog_003_pos.ksh
${PACKAGE}FILES+=	slog_007_pos.ksh
${PACKAGE}FILES+=	cleanup.ksh
${PACKAGE}FILES+=	slog.cfg
${PACKAGE}FILES+=	slog_001_pos.ksh
${PACKAGE}FILES+=	slog_005_pos.ksh
${PACKAGE}FILES+=	slog_008_neg.ksh
${PACKAGE}FILES+=	slog_011_neg.ksh
${PACKAGE}FILES+=	setup.ksh
${PACKAGE}FILES+=	slog_014_pos.ksh
${PACKAGE}FILES+=	slog_010_neg.ksh
${PACKAGE}FILES+=	slog_004_pos.ksh
${PACKAGE}FILES+=	slog_009_neg.ksh

.include <bsd.test.mk>
