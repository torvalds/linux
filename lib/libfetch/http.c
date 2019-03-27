/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000-2014 Dag-Erling Smørgrav
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
 *    derived from this software without specific prior written permission.
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

/*
 * The following copyright applies to the base64 code:
 *
 *-
 * Copyright 1997 Massachusetts Institute of Technology
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby
 * granted, provided that both the above copyright notice and this
 * permission notice appear in all copies, that both the above
 * copyright notice and this permission notice appear in all
 * supporting documentation, and that the name of M.I.T. not be used
 * in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  M.I.T. makes
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THIS SOFTWARE IS PROVIDED BY M.I.T. ``AS IS''.  M.I.T. DISCLAIMS
 * ALL EXPRESS OR IMPLIED WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT
 * SHALL M.I.T. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef WITH_SSL
#include <openssl/md5.h>
#define MD5Init(c) MD5_Init(c)
#define MD5Update(c, data, len) MD5_Update(c, data, len)
#define MD5Final(md, c) MD5_Final(md, c)
#else
#include <md5.h>
#endif

#include <netinet/in.h>
#include <netinet/tcp.h>

#include "fetch.h"
#include "common.h"
#include "httperr.h"

/* Maximum number of redirects to follow */
#define MAX_REDIRECT 20

/* Symbolic names for reply codes we care about */
#define HTTP_OK			200
#define HTTP_PARTIAL		206
#define HTTP_MOVED_PERM		301
#define HTTP_MOVED_TEMP		302
#define HTTP_SEE_OTHER		303
#define HTTP_NOT_MODIFIED	304
#define HTTP_USE_PROXY		305
#define HTTP_TEMP_REDIRECT	307
#define HTTP_PERM_REDIRECT	308
#define HTTP_NEED_AUTH		401
#define HTTP_NEED_PROXY_AUTH	407
#define HTTP_BAD_RANGE		416
#define HTTP_PROTOCOL_ERROR	999

#define HTTP_REDIRECT(xyz) ((xyz) == HTTP_MOVED_PERM \
			    || (xyz) == HTTP_MOVED_TEMP \
			    || (xyz) == HTTP_TEMP_REDIRECT \
			    || (xyz) == HTTP_PERM_REDIRECT \
			    || (xyz) == HTTP_USE_PROXY \
			    || (xyz) == HTTP_SEE_OTHER)

#define HTTP_ERROR(xyz) ((xyz) >= 400 && (xyz) <= 599)


/*****************************************************************************
 * I/O functions for decoding chunked streams
 */

struct httpio
{
	conn_t		*conn;		/* connection */
	int		 chunked;	/* chunked mode */
	char		*buf;		/* chunk buffer */
	size_t		 bufsize;	/* size of chunk buffer */
	size_t		 buflen;	/* amount of data currently in buffer */
	size_t		 bufpos;	/* current read offset in buffer */
	int		 eof;		/* end-of-file flag */
	int		 error;		/* error flag */
	size_t		 chunksize;	/* remaining size of current chunk */
#ifndef NDEBUG
	size_t		 total;
#endif
};

/*
 * Get next chunk header
 */
static int
http_new_chunk(struct httpio *io)
{
	char *p;

	if (fetch_getln(io->conn) == -1)
		return (-1);

	if (io->conn->buflen < 2 || !isxdigit((unsigned char)*io->conn->buf))
		return (-1);

	for (p = io->conn->buf; *p && !isspace((unsigned char)*p); ++p) {
		if (*p == ';')
			break;
		if (!isxdigit((unsigned char)*p))
			return (-1);
		if (isdigit((unsigned char)*p)) {
			io->chunksize = io->chunksize * 16 +
			    *p - '0';
		} else {
			io->chunksize = io->chunksize * 16 +
			    10 + tolower((unsigned char)*p) - 'a';
		}
	}

#ifndef NDEBUG
	if (fetchDebug) {
		io->total += io->chunksize;
		if (io->chunksize == 0)
			fprintf(stderr, "%s(): end of last chunk\n", __func__);
		else
			fprintf(stderr, "%s(): new chunk: %lu (%lu)\n",
			    __func__, (unsigned long)io->chunksize,
			    (unsigned long)io->total);
	}
#endif

	return (io->chunksize);
}

/*
 * Grow the input buffer to at least len bytes
 */
static inline int
http_growbuf(struct httpio *io, size_t len)
{
	char *tmp;

	if (io->bufsize >= len)
		return (0);

	if ((tmp = realloc(io->buf, len)) == NULL)
		return (-1);
	io->buf = tmp;
	io->bufsize = len;
	return (0);
}

/*
 * Fill the input buffer, do chunk decoding on the fly
 */
static ssize_t
http_fillbuf(struct httpio *io, size_t len)
{
	ssize_t nbytes;
	char ch;

	if (io->error)
		return (-1);
	if (io->eof)
		return (0);

	/* not chunked: just fetch the requested amount */
	if (io->chunked == 0) {
		if (http_growbuf(io, len) == -1)
			return (-1);
		if ((nbytes = fetch_read(io->conn, io->buf, len)) == -1) {
			io->error = errno;
			return (-1);
		}
		io->buflen = nbytes;
		io->bufpos = 0;
		return (io->buflen);
	}

	/* chunked, but we ran out: get the next chunk header */
	if (io->chunksize == 0) {
		switch (http_new_chunk(io)) {
		case -1:
			io->error = EPROTO;
			return (-1);
		case 0:
			io->eof = 1;
			return (0);
		}
	}

	/* fetch the requested amount, but no more than the current chunk */
	if (len > io->chunksize)
		len = io->chunksize;
	if (http_growbuf(io, len) == -1)
		return (-1);
	if ((nbytes = fetch_read(io->conn, io->buf, len)) == -1) {
		io->error = errno;
		return (-1);
	}
	io->bufpos = 0;
	io->buflen = nbytes;
	io->chunksize -= nbytes;

	if (io->chunksize == 0) {
		if (fetch_read(io->conn, &ch, 1) != 1 || ch != '\r' ||
		    fetch_read(io->conn, &ch, 1) != 1 || ch != '\n')
			return (-1);
	}

	return (io->buflen);
}

/*
 * Read function
 */
static int
http_readfn(void *v, char *buf, int len)
{
	struct httpio *io = (struct httpio *)v;
	int rlen;

	if (io->error)
		return (-1);
	if (io->eof)
		return (0);

	/* empty buffer */
	if (!io->buf || io->bufpos == io->buflen) {
		if ((rlen = http_fillbuf(io, len)) < 0) {
			if ((errno = io->error) == EINTR)
				io->error = 0;
			return (-1);
		} else if (rlen == 0) {
			return (0);
		}
	}

	rlen = io->buflen - io->bufpos;
	if (len < rlen)
		rlen = len;
	memcpy(buf, io->buf + io->bufpos, rlen);
	io->bufpos += rlen;
	return (rlen);
}

/*
 * Write function
 */
static int
http_writefn(void *v, const char *buf, int len)
{
	struct httpio *io = (struct httpio *)v;

	return (fetch_write(io->conn, buf, len));
}

/*
 * Close function
 */
static int
http_closefn(void *v)
{
	struct httpio *io = (struct httpio *)v;
	int r;

	r = fetch_close(io->conn);
	if (io->buf)
		free(io->buf);
	free(io);
	return (r);
}

/*
 * Wrap a file descriptor up
 */
static FILE *
http_funopen(conn_t *conn, int chunked)
{
	struct httpio *io;
	FILE *f;

	if ((io = calloc(1, sizeof(*io))) == NULL) {
		fetch_syserr();
		return (NULL);
	}
	io->conn = conn;
	io->chunked = chunked;
	f = funopen(io, http_readfn, http_writefn, NULL, http_closefn);
	if (f == NULL) {
		fetch_syserr();
		free(io);
		return (NULL);
	}
	return (f);
}


/*****************************************************************************
 * Helper functions for talking to the server and parsing its replies
 */

/* Header types */
typedef enum {
	hdr_syserror = -2,
	hdr_error = -1,
	hdr_end = 0,
	hdr_unknown = 1,
	hdr_content_length,
	hdr_content_range,
	hdr_last_modified,
	hdr_location,
	hdr_transfer_encoding,
	hdr_www_authenticate,
	hdr_proxy_authenticate,
} hdr_t;

/* Names of interesting headers */
static struct {
	hdr_t		 num;
	const char	*name;
} hdr_names[] = {
	{ hdr_content_length,		"Content-Length" },
	{ hdr_content_range,		"Content-Range" },
	{ hdr_last_modified,		"Last-Modified" },
	{ hdr_location,			"Location" },
	{ hdr_transfer_encoding,	"Transfer-Encoding" },
	{ hdr_www_authenticate,		"WWW-Authenticate" },
	{ hdr_proxy_authenticate,	"Proxy-Authenticate" },
	{ hdr_unknown,			NULL },
};

/*
 * Send a formatted line; optionally echo to terminal
 */
static int
http_cmd(conn_t *conn, const char *fmt, ...)
{
	va_list ap;
	size_t len;
	char *msg;
	int r;

	va_start(ap, fmt);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (msg == NULL) {
		errno = ENOMEM;
		fetch_syserr();
		return (-1);
	}

	r = fetch_putln(conn, msg, len);
	free(msg);

	if (r == -1) {
		fetch_syserr();
		return (-1);
	}

	return (0);
}

/*
 * Get and parse status line
 */
static int
http_get_reply(conn_t *conn)
{
	char *p;

	if (fetch_getln(conn) == -1)
		return (-1);
	/*
	 * A valid status line looks like "HTTP/m.n xyz reason" where m
	 * and n are the major and minor protocol version numbers and xyz
	 * is the reply code.
	 * Unfortunately, there are servers out there (NCSA 1.5.1, to name
	 * just one) that do not send a version number, so we can't rely
	 * on finding one, but if we do, insist on it being 1.0 or 1.1.
	 * We don't care about the reason phrase.
	 */
	if (strncmp(conn->buf, "HTTP", 4) != 0)
		return (HTTP_PROTOCOL_ERROR);
	p = conn->buf + 4;
	if (*p == '/') {
		if (p[1] != '1' || p[2] != '.' || (p[3] != '0' && p[3] != '1'))
			return (HTTP_PROTOCOL_ERROR);
		p += 4;
	}
	if (*p != ' ' ||
	    !isdigit((unsigned char)p[1]) ||
	    !isdigit((unsigned char)p[2]) ||
	    !isdigit((unsigned char)p[3]))
		return (HTTP_PROTOCOL_ERROR);

	conn->err = (p[1] - '0') * 100 + (p[2] - '0') * 10 + (p[3] - '0');
	return (conn->err);
}

/*
 * Check a header; if the type matches the given string, return a pointer
 * to the beginning of the value.
 */
static const char *
http_match(const char *str, const char *hdr)
{
	while (*str && *hdr &&
	    tolower((unsigned char)*str++) == tolower((unsigned char)*hdr++))
		/* nothing */;
	if (*str || *hdr != ':')
		return (NULL);
	while (*hdr && isspace((unsigned char)*++hdr))
		/* nothing */;
	return (hdr);
}


/*
 * Get the next header and return the appropriate symbolic code.  We
 * need to read one line ahead for checking for a continuation line
 * belonging to the current header (continuation lines start with
 * white space).
 *
 * We get called with a fresh line already in the conn buffer, either
 * from the previous http_next_header() invocation, or, the first
 * time, from a fetch_getln() performed by our caller.
 *
 * This stops when we encounter an empty line (we dont read beyond the header
 * area).
 *
 * Note that the "headerbuf" is just a place to return the result. Its
 * contents are not used for the next call. This means that no cleanup
 * is needed when ie doing another connection, just call the cleanup when
 * fully done to deallocate memory.
 */

/* Limit the max number of continuation lines to some reasonable value */
#define HTTP_MAX_CONT_LINES 10

/* Place into which to build a header from one or several lines */
typedef struct {
	char	*buf;		/* buffer */
	size_t	 bufsize;	/* buffer size */
	size_t	 buflen;	/* length of buffer contents */
} http_headerbuf_t;

static void
init_http_headerbuf(http_headerbuf_t *buf)
{
	buf->buf = NULL;
	buf->bufsize = 0;
	buf->buflen = 0;
}

static void
clean_http_headerbuf(http_headerbuf_t *buf)
{
	if (buf->buf)
		free(buf->buf);
	init_http_headerbuf(buf);
}

/* Remove whitespace at the end of the buffer */
static void
http_conn_trimright(conn_t *conn)
{
	while (conn->buflen &&
	       isspace((unsigned char)conn->buf[conn->buflen - 1]))
		conn->buflen--;
	conn->buf[conn->buflen] = '\0';
}

static hdr_t
http_next_header(conn_t *conn, http_headerbuf_t *hbuf, const char **p)
{
	unsigned int i, len;

	/*
	 * Have to do the stripping here because of the first line. So
	 * it's done twice for the subsequent lines. No big deal
	 */
	http_conn_trimright(conn);
	if (conn->buflen == 0)
		return (hdr_end);

	/* Copy the line to the headerbuf */
	if (hbuf->bufsize < conn->buflen + 1) {
		if ((hbuf->buf = realloc(hbuf->buf, conn->buflen + 1)) == NULL)
			return (hdr_syserror);
		hbuf->bufsize = conn->buflen + 1;
	}
	strcpy(hbuf->buf, conn->buf);
	hbuf->buflen = conn->buflen;

	/*
	 * Fetch possible continuation lines. Stop at 1st non-continuation
	 * and leave it in the conn buffer
	 */
	for (i = 0; i < HTTP_MAX_CONT_LINES; i++) {
		if (fetch_getln(conn) == -1)
			return (hdr_syserror);

		/*
		 * Note: we carry on the idea from the previous version
		 * that a pure whitespace line is equivalent to an empty
		 * one (so it's not continuation and will be handled when
		 * we are called next)
		 */
		http_conn_trimright(conn);
		if (conn->buf[0] != ' ' && conn->buf[0] != "\t"[0])
			break;

		/* Got a continuation line. Concatenate to previous */
		len = hbuf->buflen + conn->buflen;
		if (hbuf->bufsize < len + 1) {
			len *= 2;
			if ((hbuf->buf = realloc(hbuf->buf, len + 1)) == NULL)
				return (hdr_syserror);
			hbuf->bufsize = len + 1;
		}
		strcpy(hbuf->buf + hbuf->buflen, conn->buf);
		hbuf->buflen += conn->buflen;
	}

	/*
	 * We could check for malformed headers but we don't really care.
	 * A valid header starts with a token immediately followed by a
	 * colon; a token is any sequence of non-control, non-whitespace
	 * characters except "()<>@,;:\\\"{}".
	 */
	for (i = 0; hdr_names[i].num != hdr_unknown; i++)
		if ((*p = http_match(hdr_names[i].name, hbuf->buf)) != NULL)
			return (hdr_names[i].num);

	return (hdr_unknown);
}

/**************************
 * [Proxy-]Authenticate header parsing
 */

/*
 * Read doublequote-delimited string into output buffer obuf (allocated
 * by caller, whose responsibility it is to ensure that it's big enough)
 * cp points to the first char after the initial '"'
 * Handles \ quoting
 * Returns pointer to the first char after the terminating double quote, or
 * NULL for error.
 */
static const char *
http_parse_headerstring(const char *cp, char *obuf)
{
	for (;;) {
		switch (*cp) {
		case 0: /* Unterminated string */
			*obuf = 0;
			return (NULL);
		case '"': /* Ending quote */
			*obuf = 0;
			return (++cp);
		case '\\':
			if (*++cp == 0) {
				*obuf = 0;
				return (NULL);
			}
			/* FALLTHROUGH */
		default:
			*obuf++ = *cp++;
		}
	}
}

/* Http auth challenge schemes */
typedef enum {HTTPAS_UNKNOWN, HTTPAS_BASIC,HTTPAS_DIGEST} http_auth_schemes_t;

/* Data holder for a Basic or Digest challenge. */
typedef struct {
	http_auth_schemes_t scheme;
	char	*realm;
	char	*qop;
	char	*nonce;
	char	*opaque;
	char	*algo;
	int	 stale;
	int	 nc; /* Nonce count */
} http_auth_challenge_t;

static void
init_http_auth_challenge(http_auth_challenge_t *b)
{
	b->scheme = HTTPAS_UNKNOWN;
	b->realm = b->qop = b->nonce = b->opaque = b->algo = NULL;
	b->stale = b->nc = 0;
}

static void
clean_http_auth_challenge(http_auth_challenge_t *b)
{
	if (b->realm)
		free(b->realm);
	if (b->qop)
		free(b->qop);
	if (b->nonce)
		free(b->nonce);
	if (b->opaque)
		free(b->opaque);
	if (b->algo)
		free(b->algo);
	init_http_auth_challenge(b);
}

/* Data holder for an array of challenges offered in an http response. */
#define MAX_CHALLENGES 10
typedef struct {
	http_auth_challenge_t *challenges[MAX_CHALLENGES];
	int	count; /* Number of parsed challenges in the array */
	int	valid; /* We did parse an authenticate header */
} http_auth_challenges_t;

static void
init_http_auth_challenges(http_auth_challenges_t *cs)
{
	int i;
	for (i = 0; i < MAX_CHALLENGES; i++)
		cs->challenges[i] = NULL;
	cs->count = cs->valid = 0;
}

static void
clean_http_auth_challenges(http_auth_challenges_t *cs)
{
	int i;
	/* We rely on non-zero pointers being allocated, not on the count */
	for (i = 0; i < MAX_CHALLENGES; i++) {
		if (cs->challenges[i] != NULL) {
			clean_http_auth_challenge(cs->challenges[i]);
			free(cs->challenges[i]);
		}
	}
	init_http_auth_challenges(cs);
}

/*
 * Enumeration for lexical elements. Separators will be returned as their own
 * ascii value
 */
typedef enum {HTTPHL_WORD=256, HTTPHL_STRING=257, HTTPHL_END=258,
	      HTTPHL_ERROR = 259} http_header_lex_t;

/*
 * Determine what kind of token comes next and return possible value
 * in buf, which is supposed to have been allocated big enough by
 * caller. Advance input pointer and return element type.
 */
static int
http_header_lex(const char **cpp, char *buf)
{
	size_t l;
	/* Eat initial whitespace */
	*cpp += strspn(*cpp, " \t");
	if (**cpp == 0)
		return (HTTPHL_END);

	/* Separator ? */
	if (**cpp == ',' || **cpp == '=')
		return (*((*cpp)++));

	/* String ? */
	if (**cpp == '"') {
		*cpp = http_parse_headerstring(++*cpp, buf);
		if (*cpp == NULL)
			return (HTTPHL_ERROR);
		return (HTTPHL_STRING);
	}

	/* Read other token, until separator or whitespace */
	l = strcspn(*cpp, " \t,=");
	memcpy(buf, *cpp, l);
	buf[l] = 0;
	*cpp += l;
	return (HTTPHL_WORD);
}

/*
 * Read challenges from http xxx-authenticate header and accumulate them
 * in the challenges list structure.
 *
 * Headers with multiple challenges are specified by rfc2617, but
 * servers (ie: squid) often send them in separate headers instead,
 * which in turn is forbidden by the http spec (multiple headers with
 * the same name are only allowed for pure comma-separated lists, see
 * rfc2616 sec 4.2).
 *
 * We support both approaches anyway
 */
static int
http_parse_authenticate(const char *cp, http_auth_challenges_t *cs)
{
	int ret = -1;
	http_header_lex_t lex;
	char *key = malloc(strlen(cp) + 1);
	char *value = malloc(strlen(cp) + 1);
	char *buf = malloc(strlen(cp) + 1);

	if (key == NULL || value == NULL || buf == NULL) {
		fetch_syserr();
		goto out;
	}

	/* In any case we've seen the header and we set the valid bit */
	cs->valid = 1;

	/* Need word first */
	lex = http_header_lex(&cp, key);
	if (lex != HTTPHL_WORD)
		goto out;

	/* Loop on challenges */
	for (; cs->count < MAX_CHALLENGES; cs->count++) {
		cs->challenges[cs->count] =
			malloc(sizeof(http_auth_challenge_t));
		if (cs->challenges[cs->count] == NULL) {
			fetch_syserr();
			goto out;
		}
		init_http_auth_challenge(cs->challenges[cs->count]);
		if (strcasecmp(key, "basic") == 0) {
			cs->challenges[cs->count]->scheme = HTTPAS_BASIC;
		} else if (strcasecmp(key, "digest") == 0) {
			cs->challenges[cs->count]->scheme = HTTPAS_DIGEST;
		} else {
			cs->challenges[cs->count]->scheme = HTTPAS_UNKNOWN;
			/*
			 * Continue parsing as basic or digest may
			 * follow, and the syntax is the same for
			 * all. We'll just ignore this one when
			 * looking at the list
			 */
		}

		/* Loop on attributes */
		for (;;) {
			/* Key */
			lex = http_header_lex(&cp, key);
			if (lex != HTTPHL_WORD)
				goto out;

			/* Equal sign */
			lex = http_header_lex(&cp, buf);
			if (lex != '=')
				goto out;

			/* Value */
			lex = http_header_lex(&cp, value);
			if (lex != HTTPHL_WORD && lex != HTTPHL_STRING)
				goto out;

			if (strcasecmp(key, "realm") == 0) {
				cs->challenges[cs->count]->realm =
				    strdup(value);
			} else if (strcasecmp(key, "qop") == 0) {
				cs->challenges[cs->count]->qop =
				    strdup(value);
			} else if (strcasecmp(key, "nonce") == 0) {
				cs->challenges[cs->count]->nonce =
				    strdup(value);
			} else if (strcasecmp(key, "opaque") == 0) {
				cs->challenges[cs->count]->opaque =
				    strdup(value);
			} else if (strcasecmp(key, "algorithm") == 0) {
				cs->challenges[cs->count]->algo =
				    strdup(value);
			} else if (strcasecmp(key, "stale") == 0) {
				cs->challenges[cs->count]->stale =
				    strcasecmp(value, "no");
			} else {
				/* ignore unknown attributes */
			}

			/* Comma or Next challenge or End */
			lex = http_header_lex(&cp, key);
			/*
			 * If we get a word here, this is the beginning of the
			 * next challenge. Break the attributes loop
			 */
			if (lex == HTTPHL_WORD)
				break;

			if (lex == HTTPHL_END) {
				/* End while looking for ',' is normal exit */
				cs->count++;
				ret = 0;
				goto out;
			}
			/* Anything else is an error */
			if (lex != ',')
				goto out;

		} /* End attributes loop */
	} /* End challenge loop */

	/*
	 * Challenges max count exceeded. This really can't happen
	 * with normal data, something's fishy -> error
	 */

out:
	if (key)
		free(key);
	if (value)
		free(value);
	if (buf)
		free(buf);
	return (ret);
}


/*
 * Parse a last-modified header
 */
static int
http_parse_mtime(const char *p, time_t *mtime)
{
	char locale[64], *r;
	struct tm tm;

	strlcpy(locale, setlocale(LC_TIME, NULL), sizeof(locale));
	setlocale(LC_TIME, "C");
	r = strptime(p, "%a, %d %b %Y %H:%M:%S GMT", &tm);
	/*
	 * Some proxies use UTC in response, but it should still be
	 * parsed. RFC2616 states GMT and UTC are exactly equal for HTTP.
	 */
	if (r == NULL)
		r = strptime(p, "%a, %d %b %Y %H:%M:%S UTC", &tm);
	/* XXX should add support for date-2 and date-3 */
	setlocale(LC_TIME, locale);
	if (r == NULL)
		return (-1);
	DEBUGF("last modified: [%04d-%02d-%02d %02d:%02d:%02d]\n",
	    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);
	*mtime = timegm(&tm);
	return (0);
}

/*
 * Parse a content-length header
 */
static int
http_parse_length(const char *p, off_t *length)
{
	off_t len;

	for (len = 0; *p && isdigit((unsigned char)*p); ++p)
		len = len * 10 + (*p - '0');
	if (*p)
		return (-1);
	DEBUGF("content length: [%lld]\n", (long long)len);
	*length = len;
	return (0);
}

/*
 * Parse a content-range header
 */
static int
http_parse_range(const char *p, off_t *offset, off_t *length, off_t *size)
{
	off_t first, last, len;

	if (strncasecmp(p, "bytes ", 6) != 0)
		return (-1);
	p += 6;
	if (*p == '*') {
		first = last = -1;
		++p;
	} else {
		for (first = 0; *p && isdigit((unsigned char)*p); ++p)
			first = first * 10 + *p - '0';
		if (*p != '-')
			return (-1);
		for (last = 0, ++p; *p && isdigit((unsigned char)*p); ++p)
			last = last * 10 + *p - '0';
	}
	if (first > last || *p != '/')
		return (-1);
	for (len = 0, ++p; *p && isdigit((unsigned char)*p); ++p)
		len = len * 10 + *p - '0';
	if (*p || len < last - first + 1)
		return (-1);
	if (first == -1) {
		DEBUGF("content range: [*/%lld]\n", (long long)len);
		*length = 0;
	} else {
		DEBUGF("content range: [%lld-%lld/%lld]\n",
		    (long long)first, (long long)last, (long long)len);
		*length = last - first + 1;
	}
	*offset = first;
	*size = len;
	return (0);
}


/*****************************************************************************
 * Helper functions for authorization
 */

/*
 * Base64 encoding
 */
static char *
http_base64(const char *src)
{
	static const char base64[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz"
	    "0123456789+/";
	char *str, *dst;
	size_t l;
	int t, r;

	l = strlen(src);
	if ((str = malloc(((l + 2) / 3) * 4 + 1)) == NULL)
		return (NULL);
	dst = str;
	r = 0;

	while (l >= 3) {
		t = (src[0] << 16) | (src[1] << 8) | src[2];
		dst[0] = base64[(t >> 18) & 0x3f];
		dst[1] = base64[(t >> 12) & 0x3f];
		dst[2] = base64[(t >> 6) & 0x3f];
		dst[3] = base64[(t >> 0) & 0x3f];
		src += 3; l -= 3;
		dst += 4; r += 4;
	}

	switch (l) {
	case 2:
		t = (src[0] << 16) | (src[1] << 8);
		dst[0] = base64[(t >> 18) & 0x3f];
		dst[1] = base64[(t >> 12) & 0x3f];
		dst[2] = base64[(t >> 6) & 0x3f];
		dst[3] = '=';
		dst += 4;
		r += 4;
		break;
	case 1:
		t = src[0] << 16;
		dst[0] = base64[(t >> 18) & 0x3f];
		dst[1] = base64[(t >> 12) & 0x3f];
		dst[2] = dst[3] = '=';
		dst += 4;
		r += 4;
		break;
	case 0:
		break;
	}

	*dst = 0;
	return (str);
}


/*
 * Extract authorization parameters from environment value.
 * The value is like scheme:realm:user:pass
 */
typedef struct {
	char	*scheme;
	char	*realm;
	char	*user;
	char	*password;
} http_auth_params_t;

static void
init_http_auth_params(http_auth_params_t *s)
{
	s->scheme = s->realm = s->user = s->password = NULL;
}

static void
clean_http_auth_params(http_auth_params_t *s)
{
	if (s->scheme)
		free(s->scheme);
	if (s->realm)
		free(s->realm);
	if (s->user)
		free(s->user);
	if (s->password)
		free(s->password);
	init_http_auth_params(s);
}

static int
http_authfromenv(const char *p, http_auth_params_t *parms)
{
	int ret = -1;
	char *v, *ve;
	char *str = strdup(p);

	if (str == NULL) {
		fetch_syserr();
		return (-1);
	}
	v = str;

	if ((ve = strchr(v, ':')) == NULL)
		goto out;

	*ve = 0;
	if ((parms->scheme = strdup(v)) == NULL) {
		fetch_syserr();
		goto out;
	}
	v = ve + 1;

	if ((ve = strchr(v, ':')) == NULL)
		goto out;

	*ve = 0;
	if ((parms->realm = strdup(v)) == NULL) {
		fetch_syserr();
		goto out;
	}
	v = ve + 1;

	if ((ve = strchr(v, ':')) == NULL)
		goto out;

	*ve = 0;
	if ((parms->user = strdup(v)) == NULL) {
		fetch_syserr();
		goto out;
	}
	v = ve + 1;


	if ((parms->password = strdup(v)) == NULL) {
		fetch_syserr();
		goto out;
	}
	ret = 0;
out:
	if (ret == -1)
		clean_http_auth_params(parms);
	if (str)
		free(str);
	return (ret);
}


/*
 * Digest response: the code to compute the digest is taken from the
 * sample implementation in RFC2616
 */
#define IN const
#define OUT

#define HASHLEN 16
typedef char HASH[HASHLEN];
#define HASHHEXLEN 32
typedef char HASHHEX[HASHHEXLEN+1];

static const char *hexchars = "0123456789abcdef";
static void
CvtHex(IN HASH Bin, OUT HASHHEX Hex)
{
	unsigned short i;
	unsigned char j;

	for (i = 0; i < HASHLEN; i++) {
		j = (Bin[i] >> 4) & 0xf;
		Hex[i*2] = hexchars[j];
		j = Bin[i] & 0xf;
		Hex[i*2+1] = hexchars[j];
	}
	Hex[HASHHEXLEN] = '\0';
};

/* calculate H(A1) as per spec */
static void
DigestCalcHA1(
	IN char * pszAlg,
	IN char * pszUserName,
	IN char * pszRealm,
	IN char * pszPassword,
	IN char * pszNonce,
	IN char * pszCNonce,
	OUT HASHHEX SessionKey
	)
{
	MD5_CTX Md5Ctx;
	HASH HA1;

	MD5Init(&Md5Ctx);
	MD5Update(&Md5Ctx, pszUserName, strlen(pszUserName));
	MD5Update(&Md5Ctx, ":", 1);
	MD5Update(&Md5Ctx, pszRealm, strlen(pszRealm));
	MD5Update(&Md5Ctx, ":", 1);
	MD5Update(&Md5Ctx, pszPassword, strlen(pszPassword));
	MD5Final(HA1, &Md5Ctx);
	if (strcasecmp(pszAlg, "md5-sess") == 0) {

		MD5Init(&Md5Ctx);
		MD5Update(&Md5Ctx, HA1, HASHLEN);
		MD5Update(&Md5Ctx, ":", 1);
		MD5Update(&Md5Ctx, pszNonce, strlen(pszNonce));
		MD5Update(&Md5Ctx, ":", 1);
		MD5Update(&Md5Ctx, pszCNonce, strlen(pszCNonce));
		MD5Final(HA1, &Md5Ctx);
	}
	CvtHex(HA1, SessionKey);
}

/* calculate request-digest/response-digest as per HTTP Digest spec */
static void
DigestCalcResponse(
	IN HASHHEX HA1,           /* H(A1) */
	IN char * pszNonce,       /* nonce from server */
	IN char * pszNonceCount,  /* 8 hex digits */
	IN char * pszCNonce,      /* client nonce */
	IN char * pszQop,         /* qop-value: "", "auth", "auth-int" */
	IN char * pszMethod,      /* method from the request */
	IN char * pszDigestUri,   /* requested URL */
	IN HASHHEX HEntity,       /* H(entity body) if qop="auth-int" */
	OUT HASHHEX Response      /* request-digest or response-digest */
	)
{
#if 0
	DEBUGF("Calc: HA1[%s] Nonce[%s] qop[%s] method[%s] URI[%s]\n",
	    HA1, pszNonce, pszQop, pszMethod, pszDigestUri);
#endif
	MD5_CTX Md5Ctx;
	HASH HA2;
	HASH RespHash;
	HASHHEX HA2Hex;

	// calculate H(A2)
	MD5Init(&Md5Ctx);
	MD5Update(&Md5Ctx, pszMethod, strlen(pszMethod));
	MD5Update(&Md5Ctx, ":", 1);
	MD5Update(&Md5Ctx, pszDigestUri, strlen(pszDigestUri));
	if (strcasecmp(pszQop, "auth-int") == 0) {
		MD5Update(&Md5Ctx, ":", 1);
		MD5Update(&Md5Ctx, HEntity, HASHHEXLEN);
	}
	MD5Final(HA2, &Md5Ctx);
	CvtHex(HA2, HA2Hex);

	// calculate response
	MD5Init(&Md5Ctx);
	MD5Update(&Md5Ctx, HA1, HASHHEXLEN);
	MD5Update(&Md5Ctx, ":", 1);
	MD5Update(&Md5Ctx, pszNonce, strlen(pszNonce));
	MD5Update(&Md5Ctx, ":", 1);
	if (*pszQop) {
		MD5Update(&Md5Ctx, pszNonceCount, strlen(pszNonceCount));
		MD5Update(&Md5Ctx, ":", 1);
		MD5Update(&Md5Ctx, pszCNonce, strlen(pszCNonce));
		MD5Update(&Md5Ctx, ":", 1);
		MD5Update(&Md5Ctx, pszQop, strlen(pszQop));
		MD5Update(&Md5Ctx, ":", 1);
	}
	MD5Update(&Md5Ctx, HA2Hex, HASHHEXLEN);
	MD5Final(RespHash, &Md5Ctx);
	CvtHex(RespHash, Response);
}

/*
 * Generate/Send a Digest authorization header
 * This looks like: [Proxy-]Authorization: credentials
 *
 *  credentials      = "Digest" digest-response
 *  digest-response  = 1#( username | realm | nonce | digest-uri
 *                      | response | [ algorithm ] | [cnonce] |
 *                      [opaque] | [message-qop] |
 *                          [nonce-count]  | [auth-param] )
 *  username         = "username" "=" username-value
 *  username-value   = quoted-string
 *  digest-uri       = "uri" "=" digest-uri-value
 *  digest-uri-value = request-uri   ; As specified by HTTP/1.1
 *  message-qop      = "qop" "=" qop-value
 *  cnonce           = "cnonce" "=" cnonce-value
 *  cnonce-value     = nonce-value
 *  nonce-count      = "nc" "=" nc-value
 *  nc-value         = 8LHEX
 *  response         = "response" "=" request-digest
 *  request-digest = <"> 32LHEX <">
 */
static int
http_digest_auth(conn_t *conn, const char *hdr, http_auth_challenge_t *c,
		 http_auth_params_t *parms, struct url *url)
{
	int r;
	char noncecount[10];
	char cnonce[40];
	char *options = NULL;

	if (!c->realm || !c->nonce) {
		DEBUGF("realm/nonce not set in challenge\n");
		return(-1);
	}
	if (!c->algo)
		c->algo = strdup("");

	if (asprintf(&options, "%s%s%s%s",
	    *c->algo? ",algorithm=" : "", c->algo,
	    c->opaque? ",opaque=" : "", c->opaque?c->opaque:"") < 0)
		return (-1);

	if (!c->qop) {
		c->qop = strdup("");
		*noncecount = 0;
		*cnonce = 0;
	} else {
		c->nc++;
		sprintf(noncecount, "%08x", c->nc);
		/* We don't try very hard with the cnonce ... */
		sprintf(cnonce, "%x%lx", getpid(), (unsigned long)time(0));
	}

	HASHHEX HA1;
	DigestCalcHA1(c->algo, parms->user, c->realm,
		      parms->password, c->nonce, cnonce, HA1);
	DEBUGF("HA1: [%s]\n", HA1);
	HASHHEX digest;
	DigestCalcResponse(HA1, c->nonce, noncecount, cnonce, c->qop,
			   "GET", url->doc, "", digest);

	if (c->qop[0]) {
		r = http_cmd(conn, "%s: Digest username=\"%s\",realm=\"%s\","
			     "nonce=\"%s\",uri=\"%s\",response=\"%s\","
			     "qop=\"auth\", cnonce=\"%s\", nc=%s%s",
			     hdr, parms->user, c->realm,
			     c->nonce, url->doc, digest,
			     cnonce, noncecount, options);
	} else {
		r = http_cmd(conn, "%s: Digest username=\"%s\",realm=\"%s\","
			     "nonce=\"%s\",uri=\"%s\",response=\"%s\"%s",
			     hdr, parms->user, c->realm,
			     c->nonce, url->doc, digest, options);
	}
	if (options)
		free(options);
	return (r);
}

/*
 * Encode username and password
 */
static int
http_basic_auth(conn_t *conn, const char *hdr, const char *usr, const char *pwd)
{
	char *upw, *auth;
	int r;

	DEBUGF("basic: usr: [%s]\n", usr);
	DEBUGF("basic: pwd: [%s]\n", pwd);
	if (asprintf(&upw, "%s:%s", usr, pwd) == -1)
		return (-1);
	auth = http_base64(upw);
	free(upw);
	if (auth == NULL)
		return (-1);
	r = http_cmd(conn, "%s: Basic %s", hdr, auth);
	free(auth);
	return (r);
}

/*
 * Chose the challenge to answer and call the appropriate routine to
 * produce the header.
 */
static int
http_authorize(conn_t *conn, const char *hdr, http_auth_challenges_t *cs,
	       http_auth_params_t *parms, struct url *url)
{
	http_auth_challenge_t *digest = NULL;
	int i;

	/* If user or pass are null we're not happy */
	if (!parms->user || !parms->password) {
		DEBUGF("NULL usr or pass\n");
		return (-1);
	}

	/* Look for a Digest */
	for (i = 0; i < cs->count; i++) {
		if (cs->challenges[i]->scheme == HTTPAS_DIGEST)
			digest = cs->challenges[i];
	}

	/* Error if "Digest" was specified and there is no Digest challenge */
	if (!digest &&
	    (parms->scheme && strcasecmp(parms->scheme, "digest") == 0)) {
		DEBUGF("Digest auth in env, not supported by peer\n");
		return (-1);
	}
	/*
	 * If "basic" was specified in the environment, or there is no Digest
	 * challenge, do the basic thing. Don't need a challenge for this,
	 * so no need to check basic!=NULL
	 */
	if (!digest ||
	    (parms->scheme && strcasecmp(parms->scheme, "basic") == 0))
		return (http_basic_auth(conn,hdr,parms->user,parms->password));

	/* Else, prefer digest. We just checked that it's not NULL */
	return (http_digest_auth(conn, hdr, digest, parms, url));
}

/*****************************************************************************
 * Helper functions for connecting to a server or proxy
 */

/*
 * Connect to the correct HTTP server or proxy.
 */
static conn_t *
http_connect(struct url *URL, struct url *purl, const char *flags)
{
	struct url *curl;
	conn_t *conn;
	hdr_t h;
	http_headerbuf_t headerbuf;
	const char *p;
	int verbose;
	int af, val;
	int serrno;

#ifdef INET6
	af = AF_UNSPEC;
#else
	af = AF_INET;
#endif

	verbose = CHECK_FLAG('v');
	if (CHECK_FLAG('4'))
		af = AF_INET;
#ifdef INET6
	else if (CHECK_FLAG('6'))
		af = AF_INET6;
#endif

	curl = (purl != NULL) ? purl : URL;

	if ((conn = fetch_connect(curl->host, curl->port, af, verbose)) == NULL)
		/* fetch_connect() has already set an error code */
		return (NULL);
	init_http_headerbuf(&headerbuf);
	if (strcmp(URL->scheme, SCHEME_HTTPS) == 0 && purl) {
		http_cmd(conn, "CONNECT %s:%d HTTP/1.1",
		    URL->host, URL->port);
		http_cmd(conn, "Host: %s:%d",
		    URL->host, URL->port);
		http_cmd(conn, "");
		if (http_get_reply(conn) != HTTP_OK) {
			http_seterr(conn->err);
			goto ouch;
		}
		/* Read and discard the rest of the proxy response */
		if (fetch_getln(conn) < 0) {
			fetch_syserr();
			goto ouch;
		}
		do {
			switch ((h = http_next_header(conn, &headerbuf, &p))) {
			case hdr_syserror:
				fetch_syserr();
				goto ouch;
			case hdr_error:
				http_seterr(HTTP_PROTOCOL_ERROR);
				goto ouch;
			default:
				/* ignore */ ;
			}
		} while (h > hdr_end);
	}
	if (strcmp(URL->scheme, SCHEME_HTTPS) == 0 &&
	    fetch_ssl(conn, URL, verbose) == -1) {
		/* grrr */
		errno = EAUTH;
		fetch_syserr();
		goto ouch;
	}

	val = 1;
	setsockopt(conn->sd, IPPROTO_TCP, TCP_NOPUSH, &val, sizeof(val));

	clean_http_headerbuf(&headerbuf);
	return (conn);
ouch:
	serrno = errno;
	clean_http_headerbuf(&headerbuf);
	fetch_close(conn);
	errno = serrno;
	return (NULL);
}

static struct url *
http_get_proxy(struct url * url, const char *flags)
{
	struct url *purl;
	char *p;

	if (flags != NULL && strchr(flags, 'd') != NULL)
		return (NULL);
	if (fetch_no_proxy_match(url->host))
		return (NULL);
	if (((p = getenv("HTTP_PROXY")) || (p = getenv("http_proxy"))) &&
	    *p && (purl = fetchParseURL(p))) {
		if (!*purl->scheme)
			strcpy(purl->scheme, SCHEME_HTTP);
		if (!purl->port)
			purl->port = fetch_default_proxy_port(purl->scheme);
		if (strcmp(purl->scheme, SCHEME_HTTP) == 0)
			return (purl);
		fetchFreeURL(purl);
	}
	return (NULL);
}

static void
http_print_html(FILE *out, FILE *in)
{
	size_t len;
	char *line, *p, *q;
	int comment, tag;

	comment = tag = 0;
	while ((line = fgetln(in, &len)) != NULL) {
		while (len && isspace((unsigned char)line[len - 1]))
			--len;
		for (p = q = line; q < line + len; ++q) {
			if (comment && *q == '-') {
				if (q + 2 < line + len &&
				    strcmp(q, "-->") == 0) {
					tag = comment = 0;
					q += 2;
				}
			} else if (tag && !comment && *q == '>') {
				p = q + 1;
				tag = 0;
			} else if (!tag && *q == '<') {
				if (q > p)
					fwrite(p, q - p, 1, out);
				tag = 1;
				if (q + 3 < line + len &&
				    strcmp(q, "<!--") == 0) {
					comment = 1;
					q += 3;
				}
			}
		}
		if (!tag && q > p)
			fwrite(p, q - p, 1, out);
		fputc('\n', out);
	}
}


/*****************************************************************************
 * Core
 */

FILE *
http_request(struct url *URL, const char *op, struct url_stat *us,
	struct url *purl, const char *flags)
{

	return (http_request_body(URL, op, us, purl, flags, NULL, NULL));
}

/*
 * Send a request and process the reply
 *
 * XXX This function is way too long, the do..while loop should be split
 * XXX off into a separate function.
 */
FILE *
http_request_body(struct url *URL, const char *op, struct url_stat *us,
	struct url *purl, const char *flags, const char *content_type,
	const char *body)
{
	char timebuf[80];
	char hbuf[MAXHOSTNAMELEN + 7], *host;
	conn_t *conn;
	struct url *url, *new;
	int chunked, direct, ims, noredirect, verbose;
	int e, i, n, val;
	off_t offset, clength, length, size;
	time_t mtime;
	const char *p;
	FILE *f;
	hdr_t h;
	struct tm *timestruct;
	http_headerbuf_t headerbuf;
	http_auth_challenges_t server_challenges;
	http_auth_challenges_t proxy_challenges;
	size_t body_len;

	/* The following calls don't allocate anything */
	init_http_headerbuf(&headerbuf);
	init_http_auth_challenges(&server_challenges);
	init_http_auth_challenges(&proxy_challenges);

	direct = CHECK_FLAG('d');
	noredirect = CHECK_FLAG('A');
	verbose = CHECK_FLAG('v');
	ims = CHECK_FLAG('i');

	if (direct && purl) {
		fetchFreeURL(purl);
		purl = NULL;
	}

	/* try the provided URL first */
	url = URL;

	n = MAX_REDIRECT;
	i = 0;

	e = HTTP_PROTOCOL_ERROR;
	do {
		new = NULL;
		chunked = 0;
		offset = 0;
		clength = -1;
		length = -1;
		size = -1;
		mtime = 0;

		/* check port */
		if (!url->port)
			url->port = fetch_default_port(url->scheme);

		/* were we redirected to an FTP URL? */
		if (purl == NULL && strcmp(url->scheme, SCHEME_FTP) == 0) {
			if (strcmp(op, "GET") == 0)
				return (ftp_request(url, "RETR", us, purl, flags));
			else if (strcmp(op, "HEAD") == 0)
				return (ftp_request(url, "STAT", us, purl, flags));
		}

		/* connect to server or proxy */
		if ((conn = http_connect(url, purl, flags)) == NULL)
			goto ouch;

		/* append port number only if necessary */
		host = url->host;
		if (url->port != fetch_default_port(url->scheme)) {
			snprintf(hbuf, sizeof(hbuf), "%s:%d", host, url->port);
			host = hbuf;
		}

		/* send request */
		if (verbose)
			fetch_info("requesting %s://%s%s",
			    url->scheme, host, url->doc);
		if (purl && strcmp(url->scheme, SCHEME_HTTPS) != 0) {
			http_cmd(conn, "%s %s://%s%s HTTP/1.1",
			    op, url->scheme, host, url->doc);
		} else {
			http_cmd(conn, "%s %s HTTP/1.1",
			    op, url->doc);
		}

		if (ims && url->ims_time) {
			timestruct = gmtime((time_t *)&url->ims_time);
			(void)strftime(timebuf, 80, "%a, %d %b %Y %T GMT",
			    timestruct);
			if (verbose)
				fetch_info("If-Modified-Since: %s", timebuf);
			http_cmd(conn, "If-Modified-Since: %s", timebuf);
		}
		/* virtual host */
		http_cmd(conn, "Host: %s", host);

		/*
		 * Proxy authorization: we only send auth after we received
		 * a 407 error. We do not first try basic anyway (changed
		 * when support was added for digest-auth)
		 */
		if (purl && proxy_challenges.valid) {
			http_auth_params_t aparams;
			init_http_auth_params(&aparams);
			if (*purl->user || *purl->pwd) {
				aparams.user = strdup(purl->user);
				aparams.password = strdup(purl->pwd);
			} else if ((p = getenv("HTTP_PROXY_AUTH")) != NULL &&
				   *p != '\0') {
				if (http_authfromenv(p, &aparams) < 0) {
					http_seterr(HTTP_NEED_PROXY_AUTH);
					goto ouch;
				}
			} else if (fetch_netrc_auth(purl) == 0) {
				aparams.user = strdup(purl->user);
				aparams.password = strdup(purl->pwd);
			}
			http_authorize(conn, "Proxy-Authorization",
				       &proxy_challenges, &aparams, url);
			clean_http_auth_params(&aparams);
		}

		/*
		 * Server authorization: we never send "a priori"
		 * Basic auth, which used to be done if user/pass were
		 * set in the url. This would be weird because we'd send the
		 * password in the clear even if Digest is finally to be
		 * used (it would have made more sense for the
		 * pre-digest version to do this when Basic was specified
		 * in the environment)
		 */
		if (server_challenges.valid) {
			http_auth_params_t aparams;
			init_http_auth_params(&aparams);
			if (*url->user || *url->pwd) {
				aparams.user = strdup(url->user);
				aparams.password = strdup(url->pwd);
			} else if ((p = getenv("HTTP_AUTH")) != NULL &&
				   *p != '\0') {
				if (http_authfromenv(p, &aparams) < 0) {
					http_seterr(HTTP_NEED_AUTH);
					goto ouch;
				}
			} else if (fetch_netrc_auth(url) == 0) {
				aparams.user = strdup(url->user);
				aparams.password = strdup(url->pwd);
			} else if (fetchAuthMethod &&
				   fetchAuthMethod(url) == 0) {
				aparams.user = strdup(url->user);
				aparams.password = strdup(url->pwd);
			} else {
				http_seterr(HTTP_NEED_AUTH);
				goto ouch;
			}
			http_authorize(conn, "Authorization",
				       &server_challenges, &aparams, url);
			clean_http_auth_params(&aparams);
		}

		/* other headers */
		if ((p = getenv("HTTP_ACCEPT")) != NULL) {
			if (*p != '\0')
				http_cmd(conn, "Accept: %s", p);
		} else {
			http_cmd(conn, "Accept: */*");
		}
		if ((p = getenv("HTTP_REFERER")) != NULL && *p != '\0') {
			if (strcasecmp(p, "auto") == 0)
				http_cmd(conn, "Referer: %s://%s%s",
				    url->scheme, host, url->doc);
			else
				http_cmd(conn, "Referer: %s", p);
		}
		if ((p = getenv("HTTP_USER_AGENT")) != NULL) {
			/* no User-Agent if defined but empty */
			if  (*p != '\0')
				http_cmd(conn, "User-Agent: %s", p);
		} else {
			/* default User-Agent */
			http_cmd(conn, "User-Agent: %s " _LIBFETCH_VER,
			    getprogname());
		}
		if (url->offset > 0)
			http_cmd(conn, "Range: bytes=%lld-", (long long)url->offset);
		http_cmd(conn, "Connection: close");

		if (body) {
			body_len = strlen(body);
			http_cmd(conn, "Content-Length: %zu", body_len);
			if (content_type != NULL)
				http_cmd(conn, "Content-Type: %s", content_type);
		}

		http_cmd(conn, "");

		if (body)
			fetch_write(conn, body, body_len);

		/*
		 * Force the queued request to be dispatched.  Normally, one
		 * would do this with shutdown(2) but squid proxies can be
		 * configured to disallow such half-closed connections.  To
		 * be compatible with such configurations, fiddle with socket
		 * options to force the pending data to be written.
		 */
		val = 0;
		setsockopt(conn->sd, IPPROTO_TCP, TCP_NOPUSH, &val,
			   sizeof(val));
		val = 1;
		setsockopt(conn->sd, IPPROTO_TCP, TCP_NODELAY, &val,
			   sizeof(val));

		/* get reply */
		switch (http_get_reply(conn)) {
		case HTTP_OK:
		case HTTP_PARTIAL:
		case HTTP_NOT_MODIFIED:
			/* fine */
			break;
		case HTTP_MOVED_PERM:
		case HTTP_MOVED_TEMP:
		case HTTP_TEMP_REDIRECT:
		case HTTP_PERM_REDIRECT:
		case HTTP_SEE_OTHER:
		case HTTP_USE_PROXY:
			/*
			 * Not so fine, but we still have to read the
			 * headers to get the new location.
			 */
			break;
		case HTTP_NEED_AUTH:
			if (server_challenges.valid) {
				/*
				 * We already sent out authorization code,
				 * so there's nothing more we can do.
				 */
				http_seterr(conn->err);
				goto ouch;
			}
			/* try again, but send the password this time */
			if (verbose)
				fetch_info("server requires authorization");
			break;
		case HTTP_NEED_PROXY_AUTH:
			if (proxy_challenges.valid) {
				/*
				 * We already sent our proxy
				 * authorization code, so there's
				 * nothing more we can do. */
				http_seterr(conn->err);
				goto ouch;
			}
			/* try again, but send the password this time */
			if (verbose)
				fetch_info("proxy requires authorization");
			break;
		case HTTP_BAD_RANGE:
			/*
			 * This can happen if we ask for 0 bytes because
			 * we already have the whole file.  Consider this
			 * a success for now, and check sizes later.
			 */
			break;
		case HTTP_PROTOCOL_ERROR:
			/* fall through */
		case -1:
			fetch_syserr();
			goto ouch;
		default:
			http_seterr(conn->err);
			if (!verbose)
				goto ouch;
			/* fall through so we can get the full error message */
		}

		/* get headers. http_next_header expects one line readahead */
		if (fetch_getln(conn) == -1) {
			fetch_syserr();
			goto ouch;
		}
		do {
			switch ((h = http_next_header(conn, &headerbuf, &p))) {
			case hdr_syserror:
				fetch_syserr();
				goto ouch;
			case hdr_error:
				http_seterr(HTTP_PROTOCOL_ERROR);
				goto ouch;
			case hdr_content_length:
				http_parse_length(p, &clength);
				break;
			case hdr_content_range:
				http_parse_range(p, &offset, &length, &size);
				break;
			case hdr_last_modified:
				http_parse_mtime(p, &mtime);
				break;
			case hdr_location:
				if (!HTTP_REDIRECT(conn->err))
					break;
				/*
				 * if the A flag is set, we don't follow
				 * temporary redirects.
				 */
				if (noredirect &&
				    conn->err != HTTP_MOVED_PERM &&
				    conn->err != HTTP_PERM_REDIRECT &&
				    conn->err != HTTP_USE_PROXY) {
					n = 1;
					break;
				}
				if (new)
					free(new);
				if (verbose)
					fetch_info("%d redirect to %s",
					    conn->err, p);
				if (*p == '/')
					/* absolute path */
					new = fetchMakeURL(url->scheme, url->host,
					    url->port, p, url->user, url->pwd);
				else
					new = fetchParseURL(p);
				if (new == NULL) {
					/* XXX should set an error code */
					DEBUGF("failed to parse new URL\n");
					goto ouch;
				}

				/* Only copy credentials if the host matches */
				if (strcmp(new->host, url->host) == 0 &&
				    !*new->user && !*new->pwd) {
					strcpy(new->user, url->user);
					strcpy(new->pwd, url->pwd);
				}
				new->offset = url->offset;
				new->length = url->length;
				new->ims_time = url->ims_time;
				break;
			case hdr_transfer_encoding:
				/* XXX weak test*/
				chunked = (strcasecmp(p, "chunked") == 0);
				break;
			case hdr_www_authenticate:
				if (conn->err != HTTP_NEED_AUTH)
					break;
				if (http_parse_authenticate(p, &server_challenges) == 0)
					++n;
				break;
			case hdr_proxy_authenticate:
				if (conn->err != HTTP_NEED_PROXY_AUTH)
					break;
				if (http_parse_authenticate(p, &proxy_challenges) == 0)
					++n;
				break;
			case hdr_end:
				/* fall through */
			case hdr_unknown:
				/* ignore */
				break;
			}
		} while (h > hdr_end);

		/* we need to provide authentication */
		if (conn->err == HTTP_NEED_AUTH ||
		    conn->err == HTTP_NEED_PROXY_AUTH) {
			e = conn->err;
			if ((conn->err == HTTP_NEED_AUTH &&
			     !server_challenges.valid) ||
			    (conn->err == HTTP_NEED_PROXY_AUTH &&
			     !proxy_challenges.valid)) {
				/* 401/7 but no www/proxy-authenticate ?? */
				DEBUGF("%03d without auth header\n", conn->err);
				goto ouch;
			}
			fetch_close(conn);
			conn = NULL;
			continue;
		}

		/* requested range not satisfiable */
		if (conn->err == HTTP_BAD_RANGE) {
			if (url->offset > 0 && url->length == 0) {
				/* asked for 0 bytes; fake it */
				offset = url->offset;
				clength = -1;
				conn->err = HTTP_OK;
				break;
			} else {
				http_seterr(conn->err);
				goto ouch;
			}
		}

		/* we have a hit or an error */
		if (conn->err == HTTP_OK
		    || conn->err == HTTP_NOT_MODIFIED
		    || conn->err == HTTP_PARTIAL
		    || HTTP_ERROR(conn->err))
			break;

		/* all other cases: we got a redirect */
		e = conn->err;
		clean_http_auth_challenges(&server_challenges);
		fetch_close(conn);
		conn = NULL;
		if (!new) {
			DEBUGF("redirect with no new location\n");
			break;
		}
		if (url != URL)
			fetchFreeURL(url);
		url = new;
	} while (++i < n);

	/* we failed, or ran out of retries */
	if (conn == NULL) {
		http_seterr(e);
		goto ouch;
	}

	DEBUGF("offset %lld, length %lld, size %lld, clength %lld\n",
	    (long long)offset, (long long)length,
	    (long long)size, (long long)clength);

	if (conn->err == HTTP_NOT_MODIFIED) {
		http_seterr(HTTP_NOT_MODIFIED);
		return (NULL);
	}

	/* check for inconsistencies */
	if (clength != -1 && length != -1 && clength != length) {
		http_seterr(HTTP_PROTOCOL_ERROR);
		goto ouch;
	}
	if (clength == -1)
		clength = length;
	if (clength != -1)
		length = offset + clength;
	if (length != -1 && size != -1 && length != size) {
		http_seterr(HTTP_PROTOCOL_ERROR);
		goto ouch;
	}
	if (size == -1)
		size = length;

	/* fill in stats */
	if (us) {
		us->size = size;
		us->atime = us->mtime = mtime;
	}

	/* too far? */
	if (URL->offset > 0 && offset > URL->offset) {
		http_seterr(HTTP_PROTOCOL_ERROR);
		goto ouch;
	}

	/* report back real offset and size */
	URL->offset = offset;
	URL->length = clength;

	/* wrap it up in a FILE */
	if ((f = http_funopen(conn, chunked)) == NULL) {
		fetch_syserr();
		goto ouch;
	}

	if (url != URL)
		fetchFreeURL(url);
	if (purl)
		fetchFreeURL(purl);

	if (HTTP_ERROR(conn->err)) {
		http_print_html(stderr, f);
		fclose(f);
		f = NULL;
	}
	clean_http_headerbuf(&headerbuf);
	clean_http_auth_challenges(&server_challenges);
	clean_http_auth_challenges(&proxy_challenges);
	return (f);

ouch:
	if (url != URL)
		fetchFreeURL(url);
	if (purl)
		fetchFreeURL(purl);
	if (conn != NULL)
		fetch_close(conn);
	clean_http_headerbuf(&headerbuf);
	clean_http_auth_challenges(&server_challenges);
	clean_http_auth_challenges(&proxy_challenges);
	return (NULL);
}


/*****************************************************************************
 * Entry points
 */

/*
 * Retrieve and stat a file by HTTP
 */
FILE *
fetchXGetHTTP(struct url *URL, struct url_stat *us, const char *flags)
{
	return (http_request(URL, "GET", us, http_get_proxy(URL, flags), flags));
}

/*
 * Retrieve a file by HTTP
 */
FILE *
fetchGetHTTP(struct url *URL, const char *flags)
{
	return (fetchXGetHTTP(URL, NULL, flags));
}

/*
 * Store a file by HTTP
 */
FILE *
fetchPutHTTP(struct url *URL __unused, const char *flags __unused)
{
	warnx("fetchPutHTTP(): not implemented");
	return (NULL);
}

/*
 * Get an HTTP document's metadata
 */
int
fetchStatHTTP(struct url *URL, struct url_stat *us, const char *flags)
{
	FILE *f;

	f = http_request(URL, "HEAD", us, http_get_proxy(URL, flags), flags);
	if (f == NULL)
		return (-1);
	fclose(f);
	return (0);
}

/*
 * List a directory
 */
struct url_ent *
fetchListHTTP(struct url *url __unused, const char *flags __unused)
{
	warnx("fetchListHTTP(): not implemented");
	return (NULL);
}

FILE *
fetchReqHTTP(struct url *URL, const char *method, const char *flags,
	const char *content_type, const char *body)
{

	return (http_request_body(URL, method, NULL, http_get_proxy(URL, flags),
	    flags, content_type, body));
}
