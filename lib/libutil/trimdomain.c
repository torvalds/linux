/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Brian Somers <brian@Awfulhak.org>
 *   Based on original work by Atsushi Murai <amurai@FreeBSD.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <libutil.h>
#include <string.h>
#include <unistd.h>

static int	isDISP(const char *);

/*-
 * Trim the current domain name from fullhost, but only if the result
 * is less than or equal to hostsize in length.
 *
 * This function understands $DISPLAY type fullhosts.
 *
 * For example:
 *
 *     trimdomain("abcde.my.domain", 5)       ->   "abcde"
 *     trimdomain("abcde.my.domain", 4)       ->   "abcde.my.domain"
 *     trimdomain("abcde.my.domain:0.0", 9)   ->   "abcde:0.0"
 *     trimdomain("abcde.my.domain:0.0", 8)   ->   "abcde.my.domain:0.0"
 */
void
trimdomain(char *fullhost, int hostsize)
{
	static size_t dlen;
	static int first = 1;
	static char domain[MAXHOSTNAMELEN];
	char *end, *s;
	size_t len;

	if (first) {
		/* XXX: Should we assume that our domain is this persistent ? */
		first = 0;
		if (gethostname(domain, sizeof(domain) - 1) == 0 &&
		    (s = strchr(domain, '.')) != NULL)
			memmove(domain, s + 1, strlen(s + 1) + 1);
		else
			domain[0] = '\0';
		dlen = strlen(domain);
	}

	if (domain[0] == '\0')
		return;

	s = fullhost;
	end = s + hostsize + 1;
	if ((s = memchr(s, '.', (size_t)(end - s))) != NULL) {
		if (strncasecmp(s + 1, domain, dlen) == 0) {
			if (s[dlen + 1] == '\0') {
				/* Found -- lose the domain. */
				*s = '\0';
			} else if (s[dlen + 1] == ':' &&
			    isDISP(s + dlen + 2) &&
			    (len = strlen(s + dlen + 1)) < (size_t)(end - s)) {
				/* Found -- shuffle the DISPLAY back. */
				memmove(s, s + dlen + 1, len + 1);
			}
		}
	}
}

/*
 * Is the given string NN or NN.NN where ``NN'' is an all-numeric string ?
 */
static int
isDISP(const char *disp)
{
	size_t w;
	int res;

	w = strspn(disp, "0123456789");
	res = 0;
	if (w > 0) {
		if (disp[w] == '\0')
			res = 1;	/* NN */
		else if (disp[w] == '.') {
			disp += w + 1;
			w = strspn(disp, "0123456789");
			if (w > 0 && disp[w] == '\0')
				res = 1;	/* NN.NN */
		}
	}
	return (res);
}
