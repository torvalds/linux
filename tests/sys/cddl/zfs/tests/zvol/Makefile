# $FreeBSD$

.include <src.opts.mk>

PACKAGE=tests
TESTSDIR=${TESTSBASE}/sys/cddl/zfs/tests/zvol
FILESDIR=${TESTSDIR}

${PACKAGE}FILES+=	zvol.cfg
${PACKAGE}FILES+=	zvol_common.kshlib

TESTS_SUBDIRS+=	zvol_ENOSPC
TESTS_SUBDIRS+=	zvol_swap
TESTS_SUBDIRS+=	zvol_misc
TESTS_SUBDIRS+=	zvol_cli

.include <bsd.test.mk>
