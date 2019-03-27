/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998-2004 Dag-Erling Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <netinet/in.h>

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fetch.h"
#include "common.h"

auth_t	 fetchAuthMethod;
int	 fetchLastErrCode;
char	 fetchLastErrString[MAXERRSTRING];
int	 fetchTimeout;
int	 fetchRestartCalls = 1;
int	 fetchDebug;


/*** Local data **************************************************************/

/*
 * Error messages for parser errors
 */
#define URL_MALFORMED		1
#define URL_BAD_SCHEME		2
#define URL_BAD_PORT		3
static struct fetcherr url_errlist[] = {
	{ URL_MALFORMED,	FETCH_URL,	"Malformed URL" },
	{ URL_BAD_SCHEME,	FETCH_URL,	"Invalid URL scheme" },
	{ URL_BAD_PORT,		FETCH_URL,	"Invalid server port" },
	{ -1,			FETCH_UNKNOWN,	"Unknown parser error" }
};


/*** Public API **************************************************************/

/*
 * Select the appropriate protocol for the URL scheme, and return a
 * read-only stream connected to the document referenced by the URL.
 * Also fill out the struct url_stat.
 */
FILE *
fetchXGet(struct url *URL, struct url_stat *us, const char *flags)
{

	if (us != NULL) {
		us->size = -1;
		us->atime = us->mtime = 0;
	}
	if (strcmp(URL->scheme, SCHEME_FILE) == 0)
		return (fetchXGetFile(URL, us, flags));
	else if (strcmp(URL->scheme, SCHEME_FTP) == 0)
		return (fetchXGetFTP(URL, us, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTP) == 0)
		return (fetchXGetHTTP(URL, us, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTPS) == 0)
		return (fetchXGetHTTP(URL, us, flags));
	url_seterr(URL_BAD_SCHEME);
	return (NULL);
}

/*
 * Select the appropriate protocol for the URL scheme, and return a
 * read-only stream connected to the document referenced by the URL.
 */
FILE *
fetchGet(struct url *URL, const char *flags)
{
	return (fetchXGet(URL, NULL, flags));
}

/*
 * Select the appropriate protocol for the URL scheme, and return a
 * write-only stream connected to the document referenced by the URL.
 */
FILE *
fetchPut(struct url *URL, const char *flags)
{

	if (strcmp(URL->scheme, SCHEME_FILE) == 0)
		return (fetchPutFile(URL, flags));
	else if (strcmp(URL->scheme, SCHEME_FTP) == 0)
		return (fetchPutFTP(URL, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTP) == 0)
		return (fetchPutHTTP(URL, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTPS) == 0)
		return (fetchPutHTTP(URL, flags));
	url_seterr(URL_BAD_SCHEME);
	return (NULL);
}

/*
 * Select the appropriate protocol for the URL scheme, and return the
 * size of the document referenced by the URL if it exists.
 */
int
fetchStat(struct url *URL, struct url_stat *us, const char *flags)
{

	if (us != NULL) {
		us->size = -1;
		us->atime = us->mtime = 0;
	}
	if (strcmp(URL->scheme, SCHEME_FILE) == 0)
		return (fetchStatFile(URL, us, flags));
	else if (strcmp(URL->scheme, SCHEME_FTP) == 0)
		return (fetchStatFTP(URL, us, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTP) == 0)
		return (fetchStatHTTP(URL, us, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTPS) == 0)
		return (fetchStatHTTP(URL, us, flags));
	url_seterr(URL_BAD_SCHEME);
	return (-1);
}

/*
 * Select the appropriate protocol for the URL scheme, and return a
 * list of files in the directory pointed to by the URL.
 */
struct url_ent *
fetchList(struct url *URL, const char *flags)
{

	if (strcmp(URL->scheme, SCHEME_FILE) == 0)
		return (fetchListFile(URL, flags));
	else if (strcmp(URL->scheme, SCHEME_FTP) == 0)
		return (fetchListFTP(URL, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTP) == 0)
		return (fetchListHTTP(URL, flags));
	else if (strcmp(URL->scheme, SCHEME_HTTPS) == 0)
		return (fetchListHTTP(URL, flags));
	url_seterr(URL_BAD_SCHEME);
	return (NULL);
}

/*
 * Attempt to parse the given URL; if successful, call fetchXGet().
 */
FILE *
fetchXGetURL(const char *URL, struct url_stat *us, const char *flags)
{
	struct url *u;
	FILE *f;

	if ((u = fetchParseURL(URL)) == NULL)
		return (NULL);

	f = fetchXGet(u, us, flags);

	fetchFreeURL(u);
	return (f);
}

/*
 * Attempt to parse the given URL; if successful, call fetchGet().
 */
FILE *
fetchGetURL(const char *URL, const char *flags)
{
	return (fetchXGetURL(URL, NULL, flags));
}

/*
 * Attempt to parse the given URL; if successful, call fetchPut().
 */
FILE *
fetchPutURL(const char *URL, const char *flags)
{
	struct url *u;
	FILE *f;

	if ((u = fetchParseURL(URL)) == NULL)
		return (NULL);

	f = fetchPut(u, flags);

	fetchFreeURL(u);
	return (f);
}

/*
 * Attempt to parse the given URL; if successful, call fetchStat().
 */
int
fetchStatURL(const char *URL, struct url_stat *us, const char *flags)
{
	struct url *u;
	int s;

	if ((u = fetchParseURL(URL)) == NULL)
		return (-1);

	s = fetchStat(u, us, flags);

	fetchFreeURL(u);
	return (s);
}

/*
 * Attempt to parse the given URL; if successful, call fetchList().
 */
struct url_ent *
fetchListURL(const char *URL, const char *flags)
{
	struct url *u;
	struct url_ent *ue;

	if ((u = fetchParseURL(URL)) == NULL)
		return (NULL);

	ue = fetchList(u, flags);

	fetchFreeURL(u);
	return (ue);
}

/*
 * Make a URL
 */
struct url *
fetchMakeURL(const char *scheme, const char *host, int port, const char *doc,
    const char *user, const char *pwd)
{
	struct url *u;

	if (!scheme || (!host && !doc)) {
		url_seterr(URL_MALFORMED);
		return (NULL);
	}

	if (port < 0 || port > 65535) {
		url_seterr(URL_BAD_PORT);
		return (NULL);
	}

	/* allocate struct url */
	if ((u = calloc(1, sizeof(*u))) == NULL) {
		fetch_syserr();
		return (NULL);
	}
	u->netrcfd = -1;

	if ((u->doc = strdup(doc ? doc : "/")) == NULL) {
		fetch_syserr();
		free(u);
		return (NULL);
	}

#define seturl(x) snprintf(u->x, sizeof(u->x), "%s", x)
	seturl(scheme);
	seturl(host);
	seturl(user);
	seturl(pwd);
#undef seturl
	u->port = port;

	return (u);
}

/*
 * Return value of the given hex digit.
 */
static int
fetch_hexval(char ch)
{

	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	else if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);
	else if (ch >= 'A' && ch <= 'F')
		return (ch - 'A' + 10);
	return (-1);
}

/*
 * Decode percent-encoded URL component from src into dst, stopping at end
 * of string, or at @ or : separators.  Returns a pointer to the unhandled
 * part of the input string (null terminator, @, or :).  No terminator is
 * written to dst (it is the caller's responsibility).
 */
static const char *
fetch_pctdecode(char *dst, const char *src, size_t dlen)
{
	int d1, d2;
	char c;
	const char *s;

	for (s = src; *s != '\0' && *s != '@' && *s != ':'; s++) {
		if (s[0] == '%' && (d1 = fetch_hexval(s[1])) >= 0 &&
		    (d2 = fetch_hexval(s[2])) >= 0 && (d1 > 0 || d2 > 0)) {
			c = d1 << 4 | d2;
			s += 2;
		} else {
			c = *s;
		}
		if (dlen-- > 0)
			*dst++ = c;
	}
	return (s);
}

/*
 * Split an URL into components. URL syntax is:
 * [method:/][/[user[:pwd]@]host[:port]/][document]
 * This almost, but not quite, RFC1738 URL syntax.
 */
struct url *
fetchParseURL(const char *URL)
{
	char *doc;
	const char *p, *q;
	struct url *u;
	int i, n;

	/* allocate struct url */
	if ((u = calloc(1, sizeof(*u))) == NULL) {
		fetch_syserr();
		return (NULL);
	}
	u->netrcfd = -1;

	/* scheme name */
	if ((p = strstr(URL, ":/"))) {
                if (p - URL > URL_SCHEMELEN)
                        goto ouch;
                for (i = 0; URL + i < p; i++)
                        u->scheme[i] = tolower((unsigned char)URL[i]);
		URL = ++p;
		/*
		 * Only one slash: no host, leave slash as part of document
		 * Two slashes: host follows, strip slashes
		 */
		if (URL[1] == '/')
			URL = (p += 2);
	} else {
		p = URL;
	}
	if (!*URL || *URL == '/' || *URL == '.' ||
	    (u->scheme[0] == '\0' &&
		strchr(URL, '/') == NULL && strchr(URL, ':') == NULL))
		goto nohost;

	p = strpbrk(URL, "/@");
	if (p && *p == '@') {
		/* username */
		q = fetch_pctdecode(u->user, URL, URL_USERLEN);

		/* password */
		if (*q == ':')
			q = fetch_pctdecode(u->pwd, q + 1, URL_PWDLEN);

		p++;
	} else {
		p = URL;
	}

	/* hostname */
	if (*p == '[') {
		q = p + 1 + strspn(p + 1, ":0123456789ABCDEFabcdef");
		if (*q++ != ']')
			goto ouch;
	} else {
		/* valid characters in a DNS name */
		q = p + strspn(p, "-." "0123456789"
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "_"
		    "abcdefghijklmnopqrstuvwxyz");
	}
	if ((*q != '\0' && *q != '/' && *q != ':') || q - p > MAXHOSTNAMELEN)
		goto ouch;
	for (i = 0; p + i < q; i++)
		u->host[i] = tolower((unsigned char)p[i]);
	u->host[i] = '\0';
	p = q;

	/* port */
	if (*p == ':') {
		for (n = 0, q = ++p; *q && (*q != '/'); q++) {
			if (*q >= '0' && *q <= '9' && n < INT_MAX / 10) {
				n = n * 10 + (*q - '0');
			} else {
				/* invalid port */
				url_seterr(URL_BAD_PORT);
				goto ouch;
			}
		}
		if (n < 1 || n > IPPORT_MAX)
			goto ouch;
		u->port = n;
		p = q;
	}

nohost:
	/* document */
	if (!*p)
		p = "/";

	if (strcmp(u->scheme, SCHEME_HTTP) == 0 ||
	    strcmp(u->scheme, SCHEME_HTTPS) == 0) {
		const char hexnums[] = "0123456789abcdef";

		/* percent-escape whitespace. */
		if ((doc = malloc(strlen(p) * 3 + 1)) == NULL) {
			fetch_syserr();
			goto ouch;
		}
		u->doc = doc;
		while (*p != '\0') {
			if (!isspace((unsigned char)*p)) {
				*doc++ = *p++;
			} else {
				*doc++ = '%';
				*doc++ = hexnums[((unsigned int)*p) >> 4];
				*doc++ = hexnums[((unsigned int)*p) & 0xf];
				p++;
			}
		}
		*doc = '\0';
	} else if ((u->doc = strdup(p)) == NULL) {
		fetch_syserr();
		goto ouch;
	}

	DEBUGF("scheme:   \"%s\"\n"
	    "user:     \"%s\"\n"
	    "password: \"%s\"\n"
	    "host:     \"%s\"\n"
	    "port:     \"%d\"\n"
	    "document: \"%s\"\n",
	    u->scheme, u->user, u->pwd,
	    u->host, u->port, u->doc);

	return (u);

ouch:
	free(u);
	return (NULL);
}

/*
 * Free a URL
 */
void
fetchFreeURL(struct url *u)
{
	free(u->doc);
	free(u);
}
