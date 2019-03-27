/*-
 * Copyright (C) 2009, 2010 Gabor Kovesdan <gabor@FreeBSD.org>
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
#include <getopt.h>
#include <iconv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define	UC_TO_MB_FLAG	1
#define MB_TO_WC_FLAG	2
#define MB_TO_UC_FLAG	4
#define WC_TO_MB_FLAG	8

#define MAX(a,b)	((a) < (b) ? (b) : (a))

extern char	*__progname;

static const char	*optstr = "cdilrt";
static const char	*citrus_common = "SRC_ZONE\t0x0000-0xFFFF\n"
					"OOB_MODE\tILSEQ\n"
					"DST_ILSEQ\t0xFFFE\n"
					"DST_UNIT_BITS\t32\n\n"
					"BEGIN_MAP\n"
					"#\n# Generated with Citrus iconv (FreeBSD)\n#\n";
bool			 cflag;
bool			 dflag;
bool			 iflag;
bool			 lflag;
bool			 tflag;
bool			 rflag;
int			 fb_flags;

static void		 do_conv(iconv_t, bool);
void			 mb_to_uc_fb(const char*, size_t,
			     void (*write_replacement)(const unsigned int *,
			     size_t, void *), void *, void *);
void			 mb_to_wc_fb(const char*, size_t,
			     void (*write_replacement) (const wchar_t *, size_t, void *),
			     void *, void *);
void			 uc_to_mb_fb(unsigned int,
			     void (*write_replacement) (const char *, size_t, void *), void *,
			     void *);
void			 wc_to_mb_fb(wchar_t,
			     void (*write_replacement)(const char *,
			     size_t, void *), void *, void *);

struct option long_options[] =
{
	{"citrus",	no_argument,	NULL,	'c'},
	{"diagnostic",	no_argument,	NULL,	'd'},
	{"ignore",	no_argument,	NULL,	'i'},
	{"long",	no_argument,	NULL,	'l'},
	{"reverse",	no_argument,	NULL,	'r'},
	{"translit",	no_argument,	NULL,	't'},
	{NULL,		no_argument,	NULL,	0}
};
 
static void
usage(void) {

	fprintf(stderr, "Usage: %s [-cdilrt] ENCODING\n", __progname);
	exit(EXIT_FAILURE);
}

static void
format_diag(int errcode)
{
	const char *errstr;
	const char *u2m, *m2u, *m2w, *w2m;

	switch (errcode) {
	case EINVAL:
		errstr = "EINVAL ";
		break;
	case EILSEQ:
		errstr = "EILSEQ ";
		break;
	case E2BIG:
		errstr = "E2BIG ";
		break;
	default:
		errstr = "UNKNOWN ";
		break;
	}
	
	u2m = (fb_flags & UC_TO_MB_FLAG) ? "U2M " : "";
	m2w = (fb_flags & MB_TO_WC_FLAG) ? "M2W " : "";
	m2u = (fb_flags & MB_TO_UC_FLAG) ? "M2U " : "";
	w2m = (fb_flags & WC_TO_MB_FLAG) ? "W2M " : "";

	printf("%s%s%s%s%s", errstr, u2m, m2w, m2u, w2m);
}

static int
magnitude(const uint32_t p)
{

	if (p >> 8 == 0)
		return (1);
	else if (p >> 16 == 0)
		return (2);
	else
		return (p >> 24 == 0 ? 3 : 4);
}

static void
format(const uint32_t data)
{

  /* XXX: could be simpler, something like this but with leading 0s?

	printf("0x%.*X", magnitude(data), data);
  */

	switch (magnitude(data)) {
	default:
	case 2:
		printf("0x%04X", data);
		break;
	case 3:
		printf("0x%06X", data);
		break;
	case 4:
		printf("0x%08X", data);
		break;
        }
}

void
uc_to_mb_fb(unsigned int code,
    void (*write_replacement)(const char *buf, size_t buflen,
       void* callback_arg), void* callback_arg, void* data)
{

	fb_flags |= UC_TO_MB_FLAG;
}

void
mb_to_wc_fb(const char* inbuf, size_t inbufsize,
    void (*write_replacement)(const wchar_t *buf, size_t buflen,
       void* callback_arg), void* callback_arg, void* data)
{

	fb_flags |= MB_TO_WC_FLAG;
}

void            
mb_to_uc_fb(const char* inbuf, size_t inbufsize,
    void (*write_replacement)(const unsigned int *buf, size_t buflen,
       void* callback_arg), void* callback_arg, void* data)
{

	fb_flags |= MB_TO_UC_FLAG;
}

void
wc_to_mb_fb(wchar_t wc,
    void (*write_replacement)(const char *buf, size_t buflen,
       void* callback_arg), void* callback_arg, void* data)
{

	fb_flags |= WC_TO_MB_FLAG;
}

int
main (int argc, char *argv[])
{
	struct iconv_fallbacks fbs;
	iconv_t cd;
	char *tocode;
	char c;

	while (((c = getopt_long(argc, argv, optstr, long_options, NULL)) != -1)) {
		switch (c) {
		case 'c':
			cflag = true;
			break;
		case 'd':
			dflag = true;
			break;
		case 'i':
			iflag = true;
			break;
		case 'l':
			lflag = true;
			break;
		case 'r':
			rflag = true;
			break;
		case 't':
			tflag = true;
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();

	fbs.uc_to_mb_fallback = uc_to_mb_fb;
	fbs.mb_to_wc_fallback = mb_to_wc_fb;
	fbs.mb_to_uc_fallback = mb_to_uc_fb;
	fbs.wc_to_mb_fallback = wc_to_mb_fb;
	fbs.data = NULL;

	if (argc == 2) {
		asprintf(&tocode, "%s%s%s", argv[1], tflag ? "//TRASNLIT" : "",
		    iflag ? "//IGNORE" : "");

		if ((cd = iconv_open(tocode, argv[0])) == (iconv_t)-1)
			err(1, NULL);
		if (dflag) {
			if (iconvctl(cd, ICONV_SET_FALLBACKS, &fbs) != 0)
				err(1, NULL);
		}
		do_conv(cd, false);
	} else if (rflag) {
		asprintf(&tocode, "%s%s%s", argv[0], tflag ? "//TRANSLIT" : "",
		    iflag ? "//IGNORE" : "");

		if ((cd = iconv_open(tocode, "UTF-32LE")) == (iconv_t)-1)
			err(1, NULL);
		if (dflag && iconvctl(cd, ICONV_SET_FALLBACKS, &fbs) != 0)
			err(1, NULL);
		if (cflag) {
			printf("# $FreeBSD$\n\n");
			printf("TYPE\t\tROWCOL\n");
			printf("NAME\t\tUCS/%s\n", argv[0]);
			printf("%s", citrus_common);
		}
		do_conv(cd, true);
	} else {
		if ((cd = iconv_open("UTF-32LE//TRANSLIT", argv[0])) == (iconv_t)-1)
			err(1, NULL);
		if (dflag && (iconvctl(cd, ICONV_SET_FALLBACKS, &fbs) != 0))
			err(1, NULL);
		if (cflag) {
			printf("# $FreeBSD$\n\n");
			printf("TYPE\t\tROWCOL\n");
			printf("NAME\t\t%s/UCS\n", argv[0]);
			printf("%s", citrus_common);
                }
		do_conv(cd, false);
	}

	if (iconv_close(cd) != 0)
		err(1, NULL);

	return (EXIT_SUCCESS);
}

static void
do_conv(iconv_t cd, bool uniinput) {
	size_t inbytesleft, outbytesleft, ret;
	uint32_t outbuf;
	uint32_t inbuf;
	const char *inbuf_;
	char *outbuf_;

	for (inbuf = 0; inbuf < (lflag ? 0x100000 : 0x10000); inbuf += 1) {
		if (uniinput && (inbuf >= 0xD800) && (inbuf <= 0xDF00))
			continue;
		inbytesleft = uniinput ? 4 : magnitude(inbuf);
		outbytesleft = 4;
		outbuf = 0x00000000;
		outbuf_ = (char *)&outbuf;
		inbuf_ = (const char *)&inbuf;
		iconv(cd, NULL, NULL, NULL, NULL);
		fb_flags = 0;
		errno = 0;
		ret = iconv(cd, &inbuf_, &inbytesleft, &outbuf_, &outbytesleft);
		if (ret == (size_t)-1) {
			if (dflag) {
				format(inbuf);
				printf(" = ");
				format_diag(errno);
				printf("\n");
			}
			continue;
		}
		format(inbuf);
		printf(" = ");
		format(outbuf);
		printf("\n");
	}
	if (cflag)
		printf("END_MAP\n");
}
