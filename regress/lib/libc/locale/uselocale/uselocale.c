/* $OpenBSD: uselocale.c,v 1.9 2024/02/05 06:48:04 anton Exp $ */
/*
 * Copyright (c) 2017, 2022 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* Keep in sync with /usr/src/lib/libc/locale/rune.h. */
#define	_LOCALE_NONE	 (locale_t)0
#define	_LOCALE_C	 (locale_t)1
#define	_LOCALE_UTF8	 (locale_t)2
#define	_LOCALE_BAD	 (locale_t)3

/* Options for switch_thread() below. */
#define	SWITCH_SIGNAL	 1	/* Call pthread_cond_signal(3). */
#define	SWITCH_WAIT	 2	/* Call pthread_cond_timedwait(3). */

/* Options for TESTFUNC(). */
#define	TOPT_ERR	 (1 << 0)

/*
 * Generate one test function for a specific interface.
 * Fn =		function name
 * Ft =		function return type
 * FUNCPARA =	function parameter list with types and names
 * FUNCARGS =	function argument list, names only, no types
 * Af =		format string to print the arguments
 * Rf =		format string to print the return value
 * Op =		options for the test function, see above
 * line =	source code line number in this test file
 * ee =		expected error number
 * er =		expected return value
 * ar =		actual return value
 * errno =	actual error number (global)
 */
#define	TESTFUNC(Fn, Ft, Af, Rf, Op)					\
static void								\
_test_##Fn(int line, int ee, Ft er, FUNCPARA)				\
{									\
	Ft ar;								\
	errno = 0;							\
	ar = Fn(FUNCARGS);						\
	if (ar != er)							\
		errx(1, "[%d] %s(" Af ")=" Rf " [exp: " Rf "]",		\
		    line, #Fn, FUNCARGS, ar, er);			\
	if (Op & TOPT_ERR && errno != ee)				\
		errx(1, "[%d] %s(" Af ") errno=%d [exp: %d]",		\
		    line, #Fn, FUNCARGS, errno, ee);			\
}

#define	STRTESTFUNC(Fn, Af)						\
static void								\
_test_##Fn(int line, int ee, const char *er, FUNCPARA)			\
{									\
	const char *ar;							\
	errno = 0;							\
	ar = Fn(FUNCARGS);						\
	if (er == NULL)							\
		er = "NULL";						\
	if (ar == NULL)							\
		ar = "NULL";						\
	if (strcmp((const char *)er, (const char *)ar) != 0)		\
		errx(1, "[%d] %s(" Af ")=%s [exp: %s]",			\
		    line, #Fn, FUNCARGS, ar, er);			\
}

/*
 * Test functions for all tested interfaces.
 */
#define	FUNCPARA	int mask, const char *locname
#define	FUNCARGS	mask, locname, _LOCALE_NONE
TESTFUNC(newlocale, locale_t, "%d, %s, %p", "%p", TOPT_ERR)

#define	FUNCPARA	locale_t locale
#define	FUNCARGS	locale
TESTFUNC(duplocale, locale_t, "%p", "%p", TOPT_ERR)
TESTFUNC(uselocale, locale_t, "%p", "%p", TOPT_ERR)

#define	FUNCPARA	int category, char *locname
#define	FUNCARGS	category, locname
STRTESTFUNC(setlocale, "%d, %s")

#define	FUNCPARA	nl_item item
#define	FUNCARGS	item
STRTESTFUNC(nl_langinfo, "%ld")

#define	FUNCPARA	nl_item item, locale_t locale
#define	FUNCARGS	item, locale
STRTESTFUNC(nl_langinfo_l, "%ld, %p")

#define	FUNCPARA	int c
#define	FUNCARGS	c
TESTFUNC(isalpha, int, "0x%.2x", "%d", 0)
TESTFUNC(tolower, int, "0x%.2x", "0x%.2x", 0)

#define	FUNCPARA	int c, locale_t locale
#define	FUNCARGS	c, locale
TESTFUNC(isalpha_l, int, "0x%.2x, %p", "%d", 0)
TESTFUNC(tolower_l, int, "0x%.2x, %p", "0x%.2x", 0)

#define	FUNCPARA	wint_t wc
#define	FUNCARGS	wc
TESTFUNC(iswalpha, int, "U+%.4X", "%d", 0)
TESTFUNC(towupper, wint_t, "U+%.4X", "U+%.4X", 0)

#define	FUNCPARA	wint_t wc, locale_t locale
#define	FUNCARGS	wc, locale
TESTFUNC(iswalpha_l, int, "U+%.4X, %p", "%d", 0)
TESTFUNC(towupper_l, wint_t, "U+%.4X, %p", "U+%.4X", 0)

#define	FUNCPARA	wint_t wc, wctype_t charclass
#define	FUNCARGS	wc, charclass
TESTFUNC(iswctype, int, "U+%.4X, %p", "%d", 0)

#define	FUNCPARA	wint_t wc, wctype_t charclass, locale_t locale
#define	FUNCARGS	wc, charclass, locale
TESTFUNC(iswctype_l, int, "U+%.4X, %p, %p", "%d", 0)

#define	FUNCPARA	wint_t wc, wctrans_t charmap
#define	FUNCARGS	wc, charmap
TESTFUNC(towctrans, wint_t, "U+%.4X, %p", "U+%.4X", 0)

#define	FUNCPARA	wint_t wc, wctrans_t charmap, locale_t locale
#define	FUNCARGS	wc, charmap, locale
TESTFUNC(towctrans_l, wint_t, "U+%.4X, %p, %p", "U+%.4X", 0)

#define	FUNCPARA	const wchar_t *s1, const wchar_t *s2
#define	FUNCARGS	s1, s2
TESTFUNC(wcscasecmp, int, "%ls, %ls", "%d", 0)

#define	FUNCPARA	const wchar_t *s1, const wchar_t *s2, locale_t locale
#define	FUNCARGS	s1, s2, locale
TESTFUNC(wcscasecmp_l, int, "%ls, %ls, %p", "%d", 0)

#define	FUNCPARA	const wchar_t *s1, const wchar_t *s2, size_t len
#define	FUNCARGS	s1, s2, len
TESTFUNC(wcsncasecmp, int, "%ls, %ls, %zu", "%d", 0)

#define	FUNCPARA	const wchar_t *s1, const wchar_t *s2, size_t len, \
			locale_t locale
#define	FUNCARGS	s1, s2, len, locale
TESTFUNC(wcsncasecmp_l, int, "%ls, %ls, %zu, %p", "%d", 0)

static void
_test_MB_CUR_MAX(int line, int ee, size_t ar)
{
	if (MB_CUR_MAX != ar)
		errx(1, "[%d] MB_CUR_MAX=%zd [exp: %zd]",
		    line, MB_CUR_MAX, ar);
}

/*
 * Test macros:
 * TEST_R(funcname, er, arguments) if you expect errno == 0.
 * TEST_ER(funcname, ee, er, arguments) otherwise.
 */
#define	TEST_R(Fn, ...)		_test_##Fn(__LINE__, 0, __VA_ARGS__)
#define	TEST_ER(Fn, ...)	_test_##Fn(__LINE__, __VA_ARGS__)

static pthread_mutex_t		 mtx;
static pthread_mutexattr_t	 mtxattr;
static pthread_cond_t		 cond;

/*
 * SWITCH_SIGNAL wakes the other thread.
 * SWITCH_WAIT goes to sleep.
 * Both can be combined.
 * The step argument is used for error reporting only.
 */
static void
switch_thread(int step, int flags)
{
	struct timespec	 t;
	int		 irc;

	if (flags & SWITCH_SIGNAL) {
		if ((irc = pthread_cond_signal(&cond)) != 0)
			errc(1, irc, "pthread_cond_signal(%d)", step);
	}
	if (flags & SWITCH_WAIT) {
		if ((irc = pthread_mutex_trylock(&mtx)) != 0)
			errc(1, irc, "pthread_mutex_trylock(%d)", step);
		t.tv_sec = time(NULL) + 2;
		t.tv_nsec = 0;
		if ((irc = pthread_cond_timedwait(&cond, &mtx, &t)) != 0)
			errc(1, irc, "pthread_cond_timedwait(%d)", step);
		if ((irc = pthread_mutex_unlock(&mtx)) != 0)
			errc(1, irc, "pthread_mutex_unlock(%d)", step);
	}
}

static void *
child_func(void *arg)
{
	const wchar_t	 s1[] = { 0x00C7, 0x00E0, 0x0000 }; 
	const wchar_t	 s2[] = { 0x00E7, 0x00C0, 0x0000 };
	const wchar_t	 s3[] = { 0x00C9, 0x0074, 0x00C9, 0x0000 }; 
	const wchar_t	 s4[] = { 0x00E9, 0x0054, 0x00CC, 0x0000 }; 
	wctype_t	 wctyg, wctyu, wctyc;
	wctrans_t	 wctrg, wctru, wctrc;
	char		*sego, *segc, *selo, *selc;

	/* Test invalid newlocale(3) arguments. */
	TEST_ER(newlocale, EINVAL, _LOCALE_NONE, LC_CTYPE_MASK, NULL);
	TEST_R(MB_CUR_MAX, 1);
	TEST_ER(newlocale, EINVAL, _LOCALE_NONE, LC_ALL_MASK + 1, "C.UTF-8");
	TEST_R(MB_CUR_MAX, 1);
	TEST_ER(newlocale, ENOENT, _LOCALE_NONE, LC_COLLATE_MASK, "C.INV");
	TEST_R(MB_CUR_MAX, 1);
	setenv("LC_TIME", "C.INV", 1);
	TEST_ER(newlocale, ENOENT, _LOCALE_NONE, LC_TIME_MASK, "");
	unsetenv("LC_TIME");
	TEST_R(MB_CUR_MAX, 1);
	setenv("LC_CTYPE", "C.INV", 1);
	TEST_ER(newlocale, ENOENT, _LOCALE_NONE, LC_CTYPE_MASK, "");
	TEST_R(MB_CUR_MAX, 1);

	/* Test duplocale(3). */
	TEST_ER(duplocale, EINVAL, _LOCALE_NONE, _LOCALE_UTF8);
	TEST_R(duplocale, _LOCALE_C, _LOCALE_C);
	TEST_R(duplocale, _LOCALE_C, LC_GLOBAL_LOCALE);

	/* Test premature UTF-8 uselocale(3). */
	TEST_ER(uselocale, EINVAL, _LOCALE_NONE, _LOCALE_UTF8);
	TEST_R(MB_CUR_MAX, 1);
	TEST_R(uselocale, LC_GLOBAL_LOCALE, _LOCALE_NONE);

	/* Test UTF-8 initialization. */
	setenv("LC_CTYPE", "C.UTF-8", 1);
	TEST_R(newlocale, _LOCALE_UTF8, LC_CTYPE_MASK, "");
	unsetenv("LC_CTYPE");
	TEST_R(MB_CUR_MAX, 1);
	TEST_R(duplocale, _LOCALE_UTF8, _LOCALE_UTF8);

	/* Test invalid uselocale(3) argument. */
	TEST_ER(uselocale, EINVAL, _LOCALE_NONE, _LOCALE_BAD);
	TEST_R(MB_CUR_MAX, 1);
	TEST_R(uselocale, LC_GLOBAL_LOCALE, _LOCALE_NONE);
	TEST_R(nl_langinfo, "US-ASCII", CODESET);
	TEST_R(nl_langinfo_l, "UTF-8", CODESET, _LOCALE_UTF8);
	TEST_R(iswalpha, 0, 0x00E9);
	TEST_R(iswalpha_l, 1, 0x00E9, _LOCALE_UTF8);
	TEST_R(towupper, 0x00E9, 0x00E9);
	TEST_R(towupper_l, 0x00C9, 0x00E9, _LOCALE_UTF8);
	TEST_R(wcscasecmp, *s1 - *s2, s1, s2);
	TEST_R(wcscasecmp_l, 0, s1, s2, _LOCALE_UTF8);

	/* Test switching the thread locale. */
	TEST_R(uselocale, LC_GLOBAL_LOCALE, _LOCALE_UTF8);
	TEST_R(MB_CUR_MAX, 4);
	TEST_R(uselocale, _LOCALE_UTF8, _LOCALE_NONE);
	TEST_R(nl_langinfo, "UTF-8", CODESET);
	TEST_R(nl_langinfo_l, "UTF-8", CODESET, _LOCALE_UTF8);
	TEST_R(nl_langinfo_l, "US-ASCII", CODESET, _LOCALE_C);
	TEST_R(isalpha, _CTYPE_L, 0x65);  /* e */
	TEST_R(isalpha_l, _CTYPE_L, 0x65, _LOCALE_UTF8);
	TEST_R(isalpha_l, _CTYPE_L, 0x65, _LOCALE_C);
	TEST_R(isalpha_l, _CTYPE_L, 0x65, _LOCALE_C);
	TEST_R(isalpha, 0, 0x30);  /* 0 */
	TEST_R(isalpha_l, 0, 0x30, _LOCALE_UTF8);
	TEST_R(isalpha_l, 0, 0x30, _LOCALE_C);
	TEST_R(tolower, 0x61, 0x41);  /* A */
	TEST_R(tolower_l, 0x61, 0x41, _LOCALE_UTF8);
	TEST_R(tolower_l, 0x61, 0x41, _LOCALE_C);
	TEST_R(tolower, 0x40, 0x40);  /* @ */
	TEST_R(tolower_l, 0x40, 0x40, _LOCALE_UTF8);
	TEST_R(tolower_l, 0x40, 0x40, _LOCALE_C);
	TEST_R(iswalpha, 1, 0x00E9);  /* e accent aigu */
	TEST_R(iswalpha_l, 1, 0x00E9, _LOCALE_UTF8);
	TEST_R(iswalpha_l, 0, 0x00E9, _LOCALE_C);
	TEST_R(iswalpha, 1, 0x0153);  /* ligature oe */
	TEST_R(iswalpha_l, 1, 0x0153, _LOCALE_UTF8);
	TEST_R(iswalpha_l, 0, 0x0153, _LOCALE_C);
	TEST_R(iswalpha, 0, 0x2200);  /* for all */
	TEST_R(iswalpha_l, 0, 0x2200, _LOCALE_UTF8);
	TEST_R(iswalpha_l, 0, 0x2200, _LOCALE_C);
	TEST_R(towupper, 0x00C9, 0x00E9);
	TEST_R(towupper_l, 0x00C9, 0x00E9, _LOCALE_UTF8);
	TEST_R(towupper_l, 0x00E9, 0x00E9, _LOCALE_C);
	TEST_R(towupper, 0x0152, 0x0153);
	TEST_R(towupper_l, 0x0152, 0x0153, _LOCALE_UTF8);
	TEST_R(towupper_l, 0x0153, 0x0153, _LOCALE_C);
	TEST_R(towupper, 0x2205, 0x2205);
	TEST_R(towupper_l, 0x2205, 0x2205, _LOCALE_UTF8);
	TEST_R(towupper_l, 0x2205, 0x2205, _LOCALE_C);
	wctyg = wctype("upper");
	if (wctyg == NULL)
		errx(1, "wctype(upper) == NULL");
	wctyu = wctype_l("upper", _LOCALE_UTF8);
	if (wctyu == NULL)
		errx(1, "wctype_l(upper, UTF-8) == NULL");
	if (wctyg != wctyu)
		errx(1, "wctype global != UTF-8");
	wctyc = wctype_l("upper", _LOCALE_C);
	if (wctyc == NULL)
		errx(1, "wctype_l(upper, C) == NULL");
	TEST_R(iswctype, 1, 0x00D0, wctyg);  /* Eth */
	TEST_R(iswctype_l, 1, 0x00D0, wctyu, _LOCALE_UTF8);
	TEST_R(iswctype_l, 0, 0x00D0, wctyc, _LOCALE_C);
	TEST_R(iswctype, 1, 0x0393, wctyg);  /* Gamma */
	TEST_R(iswctype_l, 1, 0x0393, wctyu, _LOCALE_UTF8);
	TEST_R(iswctype_l, 0, 0x0393, wctyc, _LOCALE_C);
	TEST_R(iswctype, 0, 0x2205, wctyg);  /* empty set */
	TEST_R(iswctype_l, 0, 0x2205, wctyu, _LOCALE_UTF8);
	TEST_R(iswctype_l, 0, 0x2205, wctyc, _LOCALE_C);
	wctrg = wctrans("tolower");
	if (wctrg == NULL)
		errx(1, "wctrans(tolower) == NULL");
	wctru = wctrans_l("tolower", _LOCALE_UTF8);
	if (wctru == NULL)
		errx(1, "wctrans(tolower, UTF-8) == NULL");
	if (wctrg != wctru)
		errx(1, "wctrans global != UTF-8");
	wctrc = wctrans_l("tolower", _LOCALE_C);
	if (wctrc == NULL)
		errx(1, "wctrans(tolower, C) == NULL");
	TEST_R(towctrans, 0x00FE, 0x00DE, wctrg);  /* Thorn */
	TEST_R(towctrans_l, 0x00FE, 0x00DE, wctru, _LOCALE_UTF8);
	TEST_R(towctrans_l, 0x00DE, 0x00DE, wctrc, _LOCALE_C);
	TEST_R(towctrans, 0x03C6, 0x03A6, wctrg);  /* Phi */
	TEST_R(towctrans_l, 0x03C6, 0x03A6, wctru, _LOCALE_UTF8);
	TEST_R(towctrans_l, 0x03A6, 0x03A6, wctrc, _LOCALE_C);
	TEST_R(towctrans, 0x2207, 0x2207, wctrg);  /* Nabla */
	TEST_R(towctrans_l, 0x2207, 0x2207, wctru, _LOCALE_UTF8);
	TEST_R(towctrans_l, 0x2207, 0x2207, wctrc, _LOCALE_C);
	TEST_R(wcscasecmp, 0, s1, s2);
	TEST_R(wcscasecmp_l, 0, s1, s2, _LOCALE_UTF8);
	TEST_R(wcscasecmp_l, *s1 - *s2, s1, s2, _LOCALE_C);
	TEST_R(wcsncasecmp, 0, s3, s4, 2);
	TEST_R(wcsncasecmp_l, 0, s3, s4, 2, _LOCALE_UTF8);
	TEST_R(wcsncasecmp_l, *s3 - *s4, s3, s4, 2, _LOCALE_C);

	/* Test non-ctype newlocale(3). */
	TEST_R(newlocale, _LOCALE_C, LC_MESSAGES_MASK, "en_US.UTF-8");

	/* Test strerror(3). */
	sego = strerror(EPERM);
	segc = strdup(sego);
	selo = strerror_l(ENOENT, _LOCALE_C);
	selc = strdup(selo);
	if (strcmp(sego, segc) != 0)
		errx(1, "child: strerror_l clobbered strerror");
	free(segc);
	sego = strerror(ESRCH);
	if (strcmp(selo, selc) != 0)
		errx(1, "child: strerror clobbered strerror_l");
	
	/* Temporarily switch to the main thread. */
	switch_thread(2, SWITCH_SIGNAL | SWITCH_WAIT);
	if (strcmp(selo, selc) != 0)
		errx(1, "child: main clobbered strerror_l");
	free(selc);

	/* Check that the C locale works even while all is set to UTF-8. */
	TEST_R(nl_langinfo_l, "US-ASCII", CODESET, _LOCALE_C);
	TEST_R(iswalpha_l, 0, 0x00E9, _LOCALE_C);
	TEST_R(towupper_l, 0x00E9, 0x00E9, _LOCALE_C);
	TEST_R(wcscasecmp_l, *s1 - *s2, s1, s2, _LOCALE_C);

	/* Test displaying the global locale while a local one is set. */
	TEST_R(setlocale, "C/C.UTF-8/C/C/C/C", LC_ALL, NULL);

	/* Test switching the thread locale back. */
	TEST_R(MB_CUR_MAX, 4);
	TEST_R(duplocale, _LOCALE_UTF8, LC_GLOBAL_LOCALE);
	TEST_R(uselocale, _LOCALE_UTF8, _LOCALE_C);
	TEST_R(MB_CUR_MAX, 1);
	TEST_R(uselocale, _LOCALE_C, _LOCALE_NONE);

	/* Check that UTF-8 works even with a C thread locale. */
	TEST_R(nl_langinfo, "US-ASCII", CODESET);
	TEST_R(nl_langinfo_l, "UTF-8", CODESET, _LOCALE_UTF8);
	TEST_R(iswalpha, 0, 0x0153);
	TEST_R(iswalpha_l, 1, 0x0153, _LOCALE_UTF8);
	TEST_R(towupper, 0x0153, 0x0153);
	TEST_R(towupper_l, 0x0152, 0x0153, _LOCALE_UTF8);
	TEST_R(wcsncasecmp, *s3 - *s4, s3, s4, 2);
	TEST_R(wcsncasecmp_l, 0, s3, s4, 2, _LOCALE_UTF8);

	/* Test switching back to the global locale. */
	TEST_R(uselocale, _LOCALE_C, LC_GLOBAL_LOCALE);
	TEST_R(MB_CUR_MAX, 4);
	TEST_R(uselocale, LC_GLOBAL_LOCALE, _LOCALE_NONE);

	/* Check that the global locale takes effect even in a thread. */
	TEST_R(nl_langinfo, "UTF-8", CODESET);
	TEST_R(iswalpha, 1, 0x0153);
	TEST_R(towupper, 0x0152, 0x0153);
	TEST_R(wcscasecmp, 0, s1, s2);

	/* Hand control back to the main thread. */
	switch_thread(4, SWITCH_SIGNAL);
	return NULL;
}

int
main(void)
{
	pthread_t	 child_thread;
	char		*sego, *segc, *selo, *selc;
	int		 irc;

	/* Initialize environment. */
	unsetenv("LC_ALL");
	unsetenv("LC_COLLATE");
	unsetenv("LC_CTYPE");
	unsetenv("LC_MONETARY");
	unsetenv("LC_NUMERIC");
	unsetenv("LC_TIME");
	unsetenv("LC_MESSAGES");
	unsetenv("LANG");

	if ((irc = pthread_mutexattr_init(&mtxattr)) != 0)
		errc(1, irc, "pthread_mutexattr_init");
	if ((irc = pthread_mutexattr_settype(&mtxattr,
	    PTHREAD_MUTEX_STRICT_NP)) != 0)
		errc(1, irc, "pthread_mutexattr_settype");
	if ((irc = pthread_mutex_init(&mtx, &mtxattr)) != 0)
		errc(1, irc, "pthread_mutex_init");
	if ((irc = pthread_cond_init(&cond, NULL)) != 0)
		errc(1, irc, "pthread_cond_init");

	/* First let the child do some tests. */
	if ((irc = pthread_create(&child_thread, NULL, child_func, NULL)) != 0)
		errc(1, irc, "pthread_create");
	switch_thread(1, SWITCH_WAIT);

	/* Check that the global locale is undisturbed. */
	TEST_R(setlocale, "C", LC_ALL, NULL);
	TEST_R(MB_CUR_MAX, 1);

	/* Check that *_l(3) works without any locale installed. */
	TEST_R(nl_langinfo_l, "UTF-8", CODESET, _LOCALE_UTF8);
	TEST_R(iswalpha_l, 1, 0x00E9, _LOCALE_UTF8);
	TEST_R(towupper_l, 0x00C9, 0x00E9, _LOCALE_UTF8);

	/* Test setting the globale locale. */
	TEST_R(setlocale, "C.UTF-8", LC_CTYPE, "C.UTF-8");
	TEST_R(MB_CUR_MAX, 4);
	TEST_R(uselocale, LC_GLOBAL_LOCALE, _LOCALE_NONE);

	/* Test strerror(3). */
	sego = strerror(EINTR);
	segc = strdup(sego);
	selo = strerror_l(EIO, _LOCALE_C);
	selc = strdup(selo);
	if (strcmp(sego, segc) != 0)
		errx(1, "main: strerror_l clobbered strerror");
	free(segc);
	sego = strerror(ENXIO);
	if (strcmp(selo, selc) != 0)
		errx(1, "main: strerror clobbered strerror_l");
	free(selc);

	/* Let the child do some more tests, then clean up. */
	switch_thread(3, SWITCH_SIGNAL);
	if ((irc = pthread_join(child_thread, NULL)) != 0)
		errc(1, irc, "pthread_join");
	return 0;
}
