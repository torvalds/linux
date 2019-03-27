# $FreeBSD$

.include <bsd.own.mk>

ATF_TESTS_C+=		btowc_test
ATF_TESTS_C+=		c16rtomb_test
ATF_TESTS_C+=		iswctype_test
ATF_TESTS_C+=		mblen_test
ATF_TESTS_C+=		mbrlen_test
ATF_TESTS_C+=		mbrtoc16_test
ATF_TESTS_C+=		mbrtowc_2_test
ATF_TESTS_C+=		mbsnrtowcs_2_test
ATF_TESTS_C+=		mbsrtowcs_test
ATF_TESTS_C+=		mbstowcs_2_test
ATF_TESTS_C+=		mbtowc_2_test
ATF_TESTS_C+=		towctrans_test
ATF_TESTS_C+=		wcrtomb_test
ATF_TESTS_C+=		wcsnrtombs_test
ATF_TESTS_C+=		wcsrtombs_test
ATF_TESTS_C+=		wcstombs_test
ATF_TESTS_C+=		wctomb_2_test

# Note: io_test requires zh_TW.Big5 locale (see ^/head@r315568)
#NETBSD_ATF_TESTS_C=	io_test
NETBSD_ATF_TESTS_C+=	mbrtowc_test
NETBSD_ATF_TESTS_C+=	mbstowcs_test
NETBSD_ATF_TESTS_C+=	mbsnrtowcs_test
NETBSD_ATF_TESTS_C+=	mbtowc_test
NETBSD_ATF_TESTS_C+=	wcscspn_test
NETBSD_ATF_TESTS_C+=	wcspbrk_test
NETBSD_ATF_TESTS_C+=	wcsspn_test
NETBSD_ATF_TESTS_C+=	wcstod_test
NETBSD_ATF_TESTS_C+=	wctomb_test

SRCS.mbrtowc_2_test=	mbrtowc_test.c
SRCS.mbsnrtowcs_2_test=	mbsnrtowcs_test.c
SRCS.mbstowcs_2_test=	mbstowcs_test.c
SRCS.mbtowc_2_test=	mbtowc_test.c
SRCS.wctomb_2_test=	wctomb_test.c

CFLAGS.t_wctomb.c+=	-Wno-stack-protector

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
