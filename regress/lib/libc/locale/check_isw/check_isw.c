/* $OpenBSD: check_isw.c,v 1.2 2017/07/27 15:08:37 bluhm Exp $ */
/*
 * Copyright (c) 2005 Marc Espie <espie@openbsd.org>
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
 */

/* This checks consistency of the isw* functions with the default <ctype>
 * functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>

int bad = 0;

void
check_bool(int v1, int v2, char msg)
{
	if (!v1 != !v2) {
		printf("%c", msg);
		bad++;
	}
}

void 
check_value(int v1, int v2, char msg)
{
	if (v1 != v2) {
		printf("%c", msg);
		bad++;
	}
}

void
test1()
{
	int i;

	for (i = 0; i < 256; i++) {
		printf(" %02x: ", i);
		check_bool(isalnum(i), iswalnum(i), '1');
		check_bool(isalpha(i), iswalpha(i), '2');
		check_bool(isblank(i), iswblank(i), '3');
		check_bool(iscntrl(i), iswcntrl(i), '4');
		check_bool(isdigit(i), iswdigit(i), '5');
		check_bool(isgraph(i), iswgraph(i), '6');
		check_bool(islower(i), iswlower(i), '6');
		check_bool(isprint(i), iswprint(i), '7');
		check_bool(ispunct(i), iswpunct(i), '8');
		check_bool(isspace(i), iswspace(i), '9');
		check_bool(isupper(i), iswupper(i), 'a');
		check_bool(isxdigit(i), iswxdigit(i), 'b');
		check_value(tolower(i), towlower(i), 'c');
		check_value(toupper(i), towupper(i), 'd');
		if (i % 8 == 7)
			printf("\n");
	}
	printf("\n");
}

void
test2()
{
	unsigned char *s;
	unsigned char *buf;
	int i, j;
	size_t n;
	wchar_t c, d;
	mbstate_t state;

	s = malloc(256);
	if (!s) {
		bad++;
		return;
	}
	buf = malloc(MB_CUR_MAX);
	if (!buf) {
		bad++;
		free(s);
		return;
	}
	for (i = 0; i < 256; i++)
		s[i] = i+1;

	j = 0;
	mbrtowc(NULL, NULL, 1, &state);
	printf(" %02x: ", 0);

	while ((n = mbrtowc(&c, s+j, 256-j, &state)) == 1) {
		printf(" %02x: ", s[j]);
		check_bool(isalnum(s[j]), iswalnum(c), '1');
		check_bool(isalpha(s[j]), iswalpha(c), '2');
		check_bool(isblank(s[j]), iswblank(c), '3');
		check_bool(iscntrl(s[j]), iswcntrl(c), '4');
		check_bool(isdigit(s[j]), iswdigit(c), '5');
		check_bool(isgraph(s[j]), iswgraph(c), '6');
		check_bool(islower(s[j]), iswlower(c), '6');
		check_bool(isprint(s[j]), iswprint(c), '7');
		check_bool(ispunct(s[j]), iswpunct(c), '8');
		check_bool(isspace(s[j]), iswspace(c), '9');
		check_bool(isupper(s[j]), iswupper(c), 'a');
		check_bool(isxdigit(s[j]), iswxdigit(c), 'b');
		d = towlower(c);
		if (wctomb(buf, d) == 1) {	
			check_value(tolower(s[j]), buf[0], 'c');
		} else {
			bad++;
		}
		d = towupper(c);
		if (wctomb(buf, d) == 1) {	
			check_value(toupper(s[j]), buf[0], 'c');
		} else {
			bad++;
		}
		if (s[j] % 8 == 7)
			printf("\n");
		j++;
	}
	if (n != 0 || j != 255) {
		bad++;
	}
	free(s);
	free(buf);
}


int
main()
{
	test1();
	test2();
	return bad !=0;
}
