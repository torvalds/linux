# $FreeBSD$

#TODO: t_o_search

NETBSD_ATF_TESTS_C=	faccessat_test
NETBSD_ATF_TESTS_C+=	fchmodat_test
NETBSD_ATF_TESTS_C+=	fchownat_test
NETBSD_ATF_TESTS_C+=	fexecve_test
NETBSD_ATF_TESTS_C+=	fstatat_test
NETBSD_ATF_TESTS_C+=	linkat_test
NETBSD_ATF_TESTS_C+=	mkdirat_test
NETBSD_ATF_TESTS_C+=	mkfifoat_test
NETBSD_ATF_TESTS_C+=	mknodat_test
NETBSD_ATF_TESTS_C+=	openat_test
NETBSD_ATF_TESTS_C+=	readlinkat_test
NETBSD_ATF_TESTS_C+=	renameat_test
NETBSD_ATF_TESTS_C+=	symlinkat_test
NETBSD_ATF_TESTS_C+=	unlinkat_test
NETBSD_ATF_TESTS_C+=	utimensat_test

CFLAGS+= -D_INCOMPLETE_XOPEN_C063

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
