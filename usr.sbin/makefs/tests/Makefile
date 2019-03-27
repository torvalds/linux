# $FreeBSD$

ATF_TESTS_SH+=	makefs_cd9660_tests
ATF_TESTS_SH+=	makefs_ffs_tests

BINDIR=		${TESTSDIR}

# XXX: PACKAGE support for SCRIPTS
SCRIPTS+=	makefs_tests_common.sh
SCRIPTSNAME_makefs_tests_common.sh=	makefs_tests_common.sh

TEST_METADATA.makefs_cd9660_tests+=	required_files="/sbin/mount_cd9660"

.for t in ${ATF_TESTS_SH}
TEST_METADATA.$t+=	required_user="root"
.endfor

.include <bsd.test.mk>
