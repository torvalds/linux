/*	$OpenBSD: t_exhaust.c,v 1.4 2024/07/15 10:11:56 anton Exp $	*/
/*	$NetBSD: t_exhaust.c,v 1.2 2011/10/21 00:41:34 christos Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <regex.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>
//#include <atf-c.h>


static char *
mkstr(const char *str, size_t len)
{
	size_t i, slen = strlen(str);
	char *p = malloc(slen * len + 1);
	if (p == NULL)
		err(1, "malloc");
	for (i = 0; i < len; i++)
		strlcpy(&p[i * slen], str, slen * len + 1 - (i * slen));
	return p;
}

static char *
concat(const char *d, const char *s)
{
	size_t dlen = strlen(d);
	size_t slen = strlen(s);
	char *p = malloc(dlen + slen + 1);
	strlcpy(p, d, dlen + slen + 1);
	strlcat(p, s, dlen + slen + 1);
	return p;
}

static char *
p0(size_t len)
{
	char *d, *s1, *s2;
	s1 = mkstr("\\(", len);
	s2 = concat(s1, ")");
	free(s1);
	d = concat("(", s2);
	free(s2);
	return d;
}

static char *
p1(size_t len)
{
	char *d, *s1, *s2, *s3;
	s1 = mkstr("\\(", 60);
	s2 = mkstr("(.*)", len);
	s3 = concat(s1, s2);
	free(s2);
	free(s1);
	s1 = concat(s3, ")");
	free(s3);
	d = concat("(", s1);
	free(s1);
	return d;
}

static char *
ps(const char *m, const char *s, size_t len)
{
	char *d, *s1, *s2, *s3;
	s1 = mkstr(m, len);
	s2 = mkstr(s, len);
	s3 = concat(s1, s2);
	free(s2);
	free(s1);
	d = concat("(.?)", s3);
	free(s3);
	return d;
}

static char *
p2(size_t len)
{
	return ps("((.*){0,255}", ")", len);
}

static char *
p3(size_t len)
{
	return ps("(.\\{0,}", ")", len);
}

static char *
p4(size_t len)
{
	return ps("((.*){1,255}", ")", len);
}

static char *
p5(size_t len)
{
	return ps("(", "){1,100}", len);
}

static char *
p6(size_t len)
{
	char *d, *s1, *s2;
	s1 = mkstr("(?:(.*)|", len);
	s2 = concat(s1, "(.*)");
	free(s1);
	s1 = mkstr(")", len);
	d = concat(s2, s1);
	free(s1);
	free(s2);
	return d;
}

static char *(*patterns[])(size_t) = {
	p0,
	p1,
	p2,
	p3,
	p4,
	p5,
	p6,
};

int
main(void)
{
	regex_t re;
	int e, ret = 0;
	size_t i;

	for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
		char *d = (*patterns[i])(9999);
		e = regcomp(&re, d, i == 6 ? REG_BASIC : REG_EXTENDED);
		free(d);
		if (e) {
			if (e != REG_ESPACE) {
				printf("regcomp returned %d for pattern %zu", e, i);
				ret = 1;
			}
			continue;
		}
		(void)regexec(&re, "aaaaaaaa", 0, NULL, 0);
		regfree(&re);
	}
	return ret;
}
