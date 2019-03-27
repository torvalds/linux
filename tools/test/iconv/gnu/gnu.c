/*-
 * Copyright (C) 2009 Gabor Kovesdan <gabor@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <iconv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool uc_hook = false;
static bool wc_hook = false;
static bool mb_uc_fb = false;

void	 unicode_hook(unsigned int mbr, void *data);
void	 wchar_hook(wchar_t wc, void *data);

void    mb_to_uc_fb(const char *, size_t,
            void (*write_replacement) (const unsigned int *, size_t, void *),
            void *, void *);

static int
ctl_get_translit1(void)
{
	iconv_t cd;
	int arg, ret;

	cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_GET_TRANSLITERATE, &arg) == 0)
		ret = (arg == 1) ? 0 : -1;
	else
		ret = -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_get_translit2(void)
{
	iconv_t cd;
	int arg, ret;

	cd = iconv_open("ASCII", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_GET_TRANSLITERATE, &arg) == 0)
		ret = (arg == 0) ? 0 : -1;
	else
		ret = -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_set_translit1(void)
{
	iconv_t cd;
	int arg = 1, ret;

	cd = iconv_open("ASCII", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	ret = iconvctl(cd, ICONV_SET_TRANSLITERATE, &arg) == 0 ? 0 : -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_set_translit2(void)
{
	iconv_t cd;
	int arg = 0, ret;

	cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	ret = iconvctl(cd, ICONV_SET_TRANSLITERATE, &arg) == 0 ? 0 : -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_get_discard_ilseq1(void)
{
	iconv_t cd;
        int arg, ret;

	cd = iconv_open("ASCII", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_GET_DISCARD_ILSEQ, &arg) == 0)
		ret = arg == 0 ? 0 : -1;
	else
		ret = -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_get_discard_ilseq2(void)
{
	iconv_t cd;
	int arg, ret;

	cd = iconv_open("ASCII//IGNORE", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_GET_DISCARD_ILSEQ, &arg) == 0)
		ret = arg == 1 ? 0 : -1;
	else
		ret = -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_set_discard_ilseq1(void)
{
	iconv_t cd;
	int arg = 1, ret;

	cd = iconv_open("ASCII", "UTF-8");
	if (cd == (iconv_t)-1)
		return (-1);
	ret = iconvctl(cd, ICONV_SET_DISCARD_ILSEQ, &arg) == 0 ? 0 : -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_set_discard_ilseq2(void)
{
	iconv_t cd;
        int arg = 0, ret;

	cd = iconv_open("ASCII//IGNORE", "UTF-8");
	if (cd == (iconv_t)-1)
	return (-1); 
	ret = iconvctl(cd, ICONV_SET_DISCARD_ILSEQ, &arg) == 0 ? 0 : -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_trivialp1(void)
{
	iconv_t cd;
        int arg, ret;

	cd = iconv_open("latin2", "latin2");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_TRIVIALP, &arg) == 0) {
		ret = (arg == 1) ? 0 : -1;
        } else
                ret = -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

static int
ctl_trivialp2(void)
{
	iconv_t cd;
	int arg, ret;

	cd = iconv_open("ASCII", "KOI8-R");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_TRIVIALP, &arg) == 0) {
		ret = (arg == 0) ? 0 : -1;
	} else
		ret = -1;
	if (iconv_close(cd) == -1)
		return (-1);
	return (ret);
}

void
unicode_hook(unsigned int mbr, void *data)
{

#ifdef VERBOSE
	printf("Unicode hook: %u\n", mbr);
#endif
	uc_hook = true;
}

void
wchar_hook(wchar_t wc, void *data)
{

#ifdef VERBOSE
	printf("Wchar hook: %ull\n", wc);
#endif
	wc_hook = true;
}

static int
ctl_uc_hook(void)
{
	struct iconv_hooks hooks;
	iconv_t cd;
	size_t inbytesleft = 15, outbytesleft = 40;
	const char **inptr;
	const char *s = "Hello World!";
	char **outptr;
	char *outbuf;

	inptr = &s;
	hooks.uc_hook = unicode_hook;
	hooks.wc_hook = NULL;

	outbuf = malloc(40);
	outptr = &outbuf;

	cd = iconv_open("UTF-8", "ASCII");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_SET_HOOKS, (void *)&hooks) != 0)
		return (-1);
	if (iconv(cd, inptr, &inbytesleft, outptr, &outbytesleft) == (size_t)-1)
		return (-1);
	if (iconv_close(cd) == -1)
		return (-1);
	return (uc_hook ? 0 : 1);
}

static int
ctl_wc_hook(void)
{
	struct iconv_hooks hooks;
	iconv_t cd;
	size_t inbytesleft, outbytesleft = 40;
	const char **inptr;
	const char *s = "Hello World!";
	char **outptr;
	char *outbuf;

	inptr = &s;
	hooks.wc_hook = wchar_hook;
	hooks.uc_hook = NULL;

	outbuf = malloc(40);
	outptr = &outbuf;
	inbytesleft = sizeof(s);

	cd = iconv_open("SHIFT_JIS", "ASCII");
	if (cd == (iconv_t)-1)
		return (-1);
	if (iconvctl(cd, ICONV_SET_HOOKS, (void *)&hooks) != 0)
		return (-1);
	if (iconv(cd, inptr, &inbytesleft, outptr, &outbytesleft) == (size_t)-1)
		return (-1);
	if (iconv_close(cd) == -1)
		return (-1);
	return (wc_hook ? 0 : 1);
}



static int
gnu_canonicalize1(void)
{

	return (strcmp(iconv_canonicalize("latin2"), "ISO-8859-2"));
}

static int
gnu_canonicalize2(void)
{

	return (!strcmp(iconv_canonicalize("ASCII"), iconv_canonicalize("latin2")));
}


static int
iconvlist_cb(unsigned int count, const char * const *names, void *data)
{

	return (*(int *)data = ((names == NULL) && (count > 0)) ? -1 : 0);
}

static int
gnu_iconvlist(void)
{
	int i;

	iconvlist(iconvlist_cb, (void *)&i);
	return (i);
}

void
mb_to_uc_fb(const char* inbuf, size_t inbufsize,
    void (*write_replacement)(const unsigned int *buf, size_t buflen,
       void* callback_arg), void* callback_arg, void* data)
{
	unsigned int c = 0x3F;

	mb_uc_fb = true;
	write_replacement((const unsigned int *)&c, 1, NULL);
}

static int __unused
ctl_mb_to_uc_fb(void)
{
	struct iconv_fallbacks fb;
	iconv_t cd;
	size_t inbytesleft, outbytesleft;
	uint16_t inbuf[1] = { 0xF187 };
	uint8_t outbuf[4] = { 0x00, 0x00, 0x00, 0x00 };
	const char *inptr;
	char *outptr;
	int ret;

	if ((cd = iconv_open("UTF-32", "UTF-8")) == (iconv_t)-1)
		return (1);

	fb.uc_to_mb_fallback = NULL;
	fb.mb_to_wc_fallback = NULL;
	fb.wc_to_mb_fallback = NULL;
	fb.mb_to_uc_fallback = mb_to_uc_fb;
	fb.data = NULL;

	if (iconvctl(cd, ICONV_SET_FALLBACKS, (void *)&fb) != 0)
		return (1);

	inptr = (const char *)inbuf;
	outptr = (char *)outbuf;
	inbytesleft = 2;
	outbytesleft = 4;

	errno = 0;
	ret = iconv(cd, &inptr, &inbytesleft, &outptr, &outbytesleft);

#ifdef VERBOSE
	printf("mb_uc fallback: %c\n", outbuf[0]);
#endif

	if (mb_uc_fb && (outbuf[0] == 0x3F))
		return (0);
	else
		return (1);
}

static int
gnu_openinto(void)
{
	iconv_allocation_t *myspace;
	size_t inbytesleft, outbytesleft;
	const char *inptr;
	char *inbuf = "works!", *outptr;
	char outbuf[6];

	if ((myspace = (iconv_allocation_t *)malloc(sizeof(iconv_allocation_t))) == NULL)
		return (1);
	if (iconv_open_into("ASCII", "ASCII", myspace) == -1)
		return (1);

	inptr = (const char *)inbuf;
	outptr = (char *)outbuf;
	inbytesleft = 6;
	outbytesleft = 6;

	iconv((iconv_t)myspace, &inptr, &inbytesleft, &outptr, &outbytesleft);

	return ((memcmp(inbuf, outbuf, 6) == 0)	? 0 : 1);
}

static void
test(int (tester) (void), const char * label)
{
	int ret;

	if ((ret = tester()))
		printf("%s failed (%d)\n", label, ret);
	else
		printf("%s succeeded\n", label);
}

int
main(void)
{
	test(ctl_get_translit1, "ctl_get_translit1");
	test(ctl_get_translit2, "ctl_get_translit2");
	test(ctl_set_translit1, "ctl_set_translit1");
	test(ctl_set_translit2, "ctl_set_translit2");
	test(ctl_get_discard_ilseq1, "ctl_get_discard_ilseq1");
	test(ctl_get_discard_ilseq2, "ctl_get_discard_ilseq2");
	test(ctl_set_discard_ilseq1, "ctl_set_discard_ilseq1");
	test(ctl_set_discard_ilseq2, "ctl_set_discard_ilseq2");
	test(ctl_trivialp1, "ctl_trivialp1");
	test(ctl_trivialp2, "ctl_trivialp2");
	test(ctl_uc_hook, "ctl_uc_hook");
	test(ctl_wc_hook, "ctl_wc_hook");
//	test(ctl_mb_to_uc_fb, "ctl_mb_to_uc_fb");
	test(gnu_openinto, "gnu_openinto");
	test(gnu_canonicalize1, "gnu_canonicalize1");
	test(gnu_canonicalize2, "gnu_canonicalize2");
	test(gnu_iconvlist, "gnu_iconvlist");
}
