# $FreeBSD$

ATF_TESTS_C+=		memcmp_test
ATF_TESTS_C+=		memset_s_test
ATF_TESTS_C+=		stpncpy_test
ATF_TESTS_C+=		strerror2_test
ATF_TESTS_C+=		wcscasecmp_test
ATF_TESTS_C+=		wcsnlen_test
ATF_TESTS_C+=		strxfrm_test
ATF_TESTS_C+=		wcscoll_test

# TODO: popcount, stresep

NETBSD_ATF_TESTS_C+=	memchr_test
NETBSD_ATF_TESTS_C+=	memcpy_test
NETBSD_ATF_TESTS_C+=	memmem_test
NETBSD_ATF_TESTS_C+=	memset_test
NETBSD_ATF_TESTS_C+=	strcat_test
NETBSD_ATF_TESTS_C+=	strchr_test
NETBSD_ATF_TESTS_C+=	strcmp_test
NETBSD_ATF_TESTS_C+=	strcpy_test
NETBSD_ATF_TESTS_C+=	strcspn_test
NETBSD_ATF_TESTS_C+=	strerror_test
NETBSD_ATF_TESTS_C+=	strlen_test
NETBSD_ATF_TESTS_C+=	strpbrk_test
NETBSD_ATF_TESTS_C+=	strrchr_test
NETBSD_ATF_TESTS_C+=	strspn_test
NETBSD_ATF_TESTS_C+=	swab_test

SRCS.strerror2_test=	strerror_test.c

.include "../Makefile.netbsd-tests"

LIBADD.memchr_test+=	md
LIBADD.memcpy_test+=	md

.include <bsd.test.mk>
