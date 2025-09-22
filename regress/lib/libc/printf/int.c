/* $OpenBSD: int.c,v 1.2 2020/07/14 16:40:04 kettenis Exp $ */
/*
 * Copyright (c) 2020 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Test the %c, %lc, %s, and %ls conversion specifiers with all their
 * modifiers, in particular with the minus flag, width, and maxbytes.
 * Also verify that other flags do nothing useful.
 */
#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum	int_size {
	S_CHAR,
	S_SHORT,
	S_INT,
	S_LONG,
	S_LL,
	S_MAX,
	S_PTR,
	S_SIZE
};

void	 ti(const char *, enum int_size, long long, const char *);
void	 tu(const char *, enum int_size, unsigned long long, const char *);

static int	 badret, badlen, badout;	/* Error counters. */
static int	 verbose;			/* For debugging. */


/*
 * Print the signed integer i with the format fmt,
 * check that the result matches what we want,
 * and report and count the error on failure.
 */
void
ti(const char *fmt, enum int_size sz, long long i, const char *want)
{
	char		 buf[32];
	size_t		 len;
	int		 irc, happy;

	happy = 1;
	switch (sz) {
	case S_CHAR:
		irc = snprintf(buf, sizeof(buf), fmt, (signed char)i);
		break;
	case S_SHORT:
		irc = snprintf(buf, sizeof(buf), fmt, (short)i);
		break;
	case S_INT:
		irc = snprintf(buf, sizeof(buf), fmt, (int)i);
		break;
	case S_LONG:
		irc = snprintf(buf, sizeof(buf), fmt, (long)i);
		break;
	case S_LL:
		irc = snprintf(buf, sizeof(buf), fmt, (long long)i);
		break;
	case S_MAX:
		irc = snprintf(buf, sizeof(buf), fmt, (intmax_t)i);
		break;
	case S_PTR:
		irc = snprintf(buf, sizeof(buf), fmt, (ptrdiff_t)i);
		break;
	case S_SIZE:
		irc = snprintf(buf, sizeof(buf), fmt, (ssize_t)i);
		break;
	default:
		warnx("printf(\"%s\", %lld) unknown size code %d",
		    fmt, i, sz);
		badret++;
		return;
	}
	len = strlen(want);
	if (irc < 0) {
		warn("printf(\"%s\", %lld) returned %d", fmt, i, irc);
		badret++;
		return;
	}
	if ((unsigned long long)irc != len) {
		warnx("printf(\"%s\", %lld) returned %d (expected %zu)",
		    fmt, i, irc, len);
		badlen++;
		happy = 0;
	}
	if (strcmp(buf, want) != 0) {
		warnx("printf(\"%s\", %lld) wrote \"%s\" (expected \"%s\")",
		    fmt, i, buf, want);
		badout++;
		happy = 0;
	}
	if (verbose && happy)
		warnx("printf(\"%s\", %lld) wrote \"%s\" length %d (OK)",
		    fmt, i, buf, irc);
}

/*
 * Print the unsigned integer i with the format fmt,
 * check that the result matches what we want,
 * and report and count the error on failure.
 */
void
tu(const char *fmt, enum int_size sz, unsigned long long i, const char *want)
{
	char		 buf[32];
	size_t		 len;
	int		 irc, happy;

	happy = 1;
	switch (sz) {
	case S_CHAR:
		irc = snprintf(buf, sizeof(buf), fmt, (unsigned char)i);
		break;
	case S_SHORT:
		irc = snprintf(buf, sizeof(buf), fmt, (unsigned short)i);
		break;
	case S_INT:
		irc = snprintf(buf, sizeof(buf), fmt, (unsigned int)i);
		break;
	case S_LONG:
		irc = snprintf(buf, sizeof(buf), fmt, (unsigned long)i);
		break;
	case S_LL:
		irc = snprintf(buf, sizeof(buf), fmt, (unsigned long long)i);
		break;
	case S_MAX:
		irc = snprintf(buf, sizeof(buf), fmt, (uintmax_t)i);
		break;
	case S_SIZE:
		irc = snprintf(buf, sizeof(buf), fmt, (size_t)i);
		break;
	default:
		warnx("printf(\"%s\", %llu) unknown size code %d",
		    fmt, i, sz);
		badret++;
		return;
	}
	len = strlen(want);
	if (irc < 0) {
		warn("printf(\"%s\", %llu) returned %d", fmt, i, irc);
		badret++;
		return;
	}
	if ((unsigned long long)irc != len) {
		warnx("printf(\"%s\", %llu) returned %d (expected %zu)",
		    fmt, i, irc, len);
		badlen++;
		happy = 0;
	}
	if (strcmp(buf, want) != 0) {
		warnx("printf(\"%s\", %llu) wrote \"%s\" (expected \"%s\")",
		    fmt, i, buf, want);
		badout++;
		happy = 0;
	}
	if (verbose && happy)
		warnx("printf(\"%s\", %llu) wrote \"%s\" length %d (OK)",
		    fmt, i, buf, irc);
}

int
main(int argc, char *argv[])
{
	int		 badarg, picky;
	int		 ch;

	badarg = picky = 0;
	while ((ch = getopt(argc, argv, "pv")) != -1) {
		switch (ch) {
		case 'p':
			picky = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			badarg = 1;
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		warnx("unexpected argument \"%s\"", *argv);
		badarg = 1;
	}
	if (badarg) {
		fputs("usage: int [-pv]\n", stderr);
		return 1;
	}

	/*
	 * Valid use cases of %d.
	 */

	ti("<%d>", S_INT, 0, "<0>");
	ti("<%d>", S_INT, 1, "<1>");
	ti("<%d>", S_INT, -1, "<-1>");
	ti("<%d>", S_INT, 42, "<42>");
	ti("<%d>", S_INT, INT32_MAX, "<2147483647>");
	ti("<%d>", S_INT, INT32_MIN, "<-2147483648>");
	ti("<% d>", S_INT, 42, "< 42>");
	ti("<% d>", S_INT, -42, "<-42>");
	ti("<%+d>", S_INT, 42, "<+42>");
	ti("<%+d>", S_INT, -42, "<-42>");
	ti("<% +d>", S_INT, 42, "<+42>");
	ti("<% +d>", S_INT, -42, "<-42>");
	ti("<%-4d>", S_INT, 42, "<42  >");
	ti("<% -4d>", S_INT, 42, "< 42 >");
	ti("<%+-4d>", S_INT, 42, "<+42 >");
	ti("<%04d>", S_INT, 42, "<0042>");
	ti("<%-04d>", S_INT, 42, "<42  >");
	ti("<% 04d>", S_INT, 42, "< 042>");
	ti("<%+04d>", S_INT, 42, "<+042>");
	ti("<%4.3d>", S_INT, 42, "< 042>");
	ti("<% 5.3d>", S_INT, 42, "<  042>");
	ti("<%+5.3d>", S_INT, 42, "< +042>");
	ti("<%-4.3d>", S_INT, 42, "<042 >");
	ti("<%04.3d>", S_INT, 42, "< 042>");

	ti("<%hhd>", S_CHAR, INT8_MIN, "<-128>");
	ti("<%hhd>", S_CHAR, -1, "<-1>");
	ti("<%hhd>", S_CHAR, 0, "<0>");
	ti("<%hhd>", S_CHAR, 1, "<1>");
	ti("<%hhd>", S_CHAR, INT8_MAX, "<127>");
	ti("<%+.4hhd>", S_CHAR, 42, "<+0042>");
	ti("<% 04hhd>", S_CHAR, 42, "< 042>");

	ti("<%hd>", S_SHORT, INT16_MIN, "<-32768>");
	ti("<%hd>", S_SHORT, -1, "<-1>");
	ti("<%hd>", S_SHORT, 0, "<0>");
	ti("<%hd>", S_SHORT, 1, "<1>");
	ti("<%hd>", S_SHORT, INT16_MAX, "<32767>");

	ti("<%hld>", S_LONG, INT32_MIN, "<-2147483648>");
	ti("<%hld>", S_LONG, -1, "<-1>");
	ti("<%hld>", S_LONG, 0, "<0>");
	ti("<%hld>", S_LONG, 1, "<1>");
	ti("<%hld>", S_LONG, INT32_MAX, "<2147483647>");

	ti("<%hlld>", S_LL, INT64_MIN, "<-9223372036854775808>");
	ti("<%hlld>", S_LL, -1, "<-1>");
	ti("<%hlld>", S_LL, 0, "<0>");
	ti("<%hlld>", S_LL, 1, "<1>");
	ti("<%hlld>", S_LL, INT64_MAX, "<9223372036854775807>");
	ti("<%h-19lld>", S_LL, 123456789123456789LL, "<123456789123456789 >");

	ti("<%hjd>", S_MAX, INT64_MIN, "<-9223372036854775808>");
	ti("<%hjd>", S_MAX, -1, "<-1>");
	ti("<%hjd>", S_MAX, 0, "<0>");
	ti("<%hjd>", S_MAX, 1, "<1>");
	ti("<%hjd>", S_MAX, INT64_MAX, "<9223372036854775807>");

	ti("<%htd>", S_PTR, INT32_MIN, "<-2147483648>");
	ti("<%htd>", S_PTR, -1, "<-1>");
	ti("<%htd>", S_PTR, 0, "<0>");
	ti("<%htd>", S_PTR, 1, "<1>");
	ti("<%htd>", S_PTR, INT32_MAX, "<2147483647>");

	ti("<%hzd>", S_SIZE, INT32_MIN, "<-2147483648>");
	ti("<%hzd>", S_SIZE, -1, "<-1>");
	ti("<%hzd>", S_SIZE, 0, "<0>");
	ti("<%hzd>", S_SIZE, 1, "<1>");
	ti("<%hzd>", S_SIZE, INT32_MAX, "<2147483647>");

	/*
	 * Undefined behaviour of %d.
	 * Do not test by default to avoid noise.
	 * But provide the tests anyway to help track down
	 * unintended changes of behaviour when needed.
	 */

	if (picky) {
		ti("<%#d>", S_INT, 42, "<42>");
		ti("<%Ld>", S_INT, 42, "<42>");
	}

	/*
	 * Valid use cases of %u.
	 */

	tu("<%u>", S_INT, 0, "<0>");
	tu("<%u>", S_INT, 1, "<1>");
	tu("<%u>", S_INT, 42, "<42>");
	tu("<%u>", S_INT, UINT32_MAX, "<4294967295>");
	tu("<%-4u>", S_INT, 42, "<42  >");
	tu("<%04u>", S_INT, 42, "<0042>");
	tu("<%-04u>", S_INT, 42, "<42  >");
	tu("<%4.3u>", S_INT, 42, "< 042>");
	tu("<%-4.3u>", S_INT, 42, "<042 >");
	tu("<%04.3u>", S_INT, 42, "< 042>");

	tu("<%hhu>", S_CHAR, 0, "<0>");
	tu("<%hhu>", S_CHAR, UINT8_MAX, "<255>");
	tu("<%hhu>", S_CHAR, -1, "<255>");
	tu("<%-4hhu>", S_CHAR, 42, "<42  >");
	tu("<%04hhu>", S_CHAR, 42, "<0042>");

	tu("<%hu>", S_SHORT, 0, "<0>");
	tu("<%hu>", S_SHORT, UINT16_MAX, "<65535>");
	tu("<%hlu>", S_LONG, 0, "<0>");
	tu("<%hlu>", S_LONG, UINT32_MAX, "<4294967295>");
	tu("<%hllu>", S_LL, 0, "<0>");
	tu("<%hllu>", S_LL, UINT64_MAX, "<18446744073709551615>");
	tu("<%h-19llu>", S_LL, 123456789123456789ULL, "<123456789123456789 >");
	tu("<%hju>", S_MAX, 0, "<0>");
	tu("<%hju>", S_MAX, UINT64_MAX, "<18446744073709551615>");
	tu("<%hzu>", S_SIZE, 0, "<0>");
	tu("<%hzu>", S_SIZE, UINT32_MAX, "<4294967295>");

	tu("<%hho>", S_CHAR, 0, "<0>");
	tu("<%#hho>", S_CHAR, 0, "<0>");
	tu("<%hho>", S_CHAR, UINT8_MAX, "<377>");
	tu("<%#hho>", S_CHAR, UINT8_MAX, "<0377>");
	tu("<%hho>", S_CHAR, -1, "<377>");
	tu("<%#hho>", S_CHAR, -1, "<0377>");
	tu("<%-4hho>", S_CHAR, 42, "<52  >");
	tu("<%#-4hho>", S_CHAR, 42, "<052 >");
	tu("<%04hho>", S_CHAR, 42, "<0052>");
	tu("<%#04hho>", S_CHAR, 42, "<0052>");

	tu("<%hx>", S_SHORT, 0, "<0>");
	tu("<%#hx>", S_SHORT, 0, "<0>");
	tu("<%hX>", S_SHORT, 0, "<0>");
	tu("<%#hX>", S_SHORT, 0, "<0>");
	tu("<%hx>", S_SHORT, 1, "<1>");
	tu("<%#hx>", S_SHORT, 1, "<0x1>");
	tu("<%hX>", S_SHORT, 1, "<1>");
	tu("<%#hX>", S_SHORT, 1, "<0X1>");
	tu("<%hx>", S_SHORT, 10, "<a>");
	tu("<%#hx>", S_SHORT, 10, "<0xa>");
	tu("<%hX>", S_SHORT, 10, "<A>");
	tu("<%#hX>", S_SHORT, 10, "<0XA>");
	tu("<%hx>", S_SHORT, UINT16_MAX, "<ffff>");
	tu("<%#hx>", S_SHORT, UINT16_MAX, "<0xffff>");
	tu("<%hX>", S_SHORT, UINT16_MAX, "<FFFF>");
	tu("<%#hX>", S_SHORT, UINT16_MAX, "<0XFFFF>");

	/*
	 * Undefined behaviour of %u.
	 */

	if (picky) {
		tu("<%#u>", S_INT, 42, "<42>");
		tu("<% u>", S_INT, 42, "<42>");
		tu("<%+u>", S_INT, 42, "<42>");
		tu("<%Lu>", S_INT, 42, "<42>");
	}

	/*
	 * Summarize the results.
	 */

	if (badret + badlen + badout)
		errx(1, "ERRORS: %d fail + %d mismatch (incl. %d bad length)",
		    badret, badout, badlen);
	else if (verbose)
		warnx("SUCCESS");
	return 0;
}
