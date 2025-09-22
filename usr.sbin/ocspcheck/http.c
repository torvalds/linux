/*	$Id: http.c,v 1.17 2023/04/19 12:58:16 jsg Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
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

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tls.h>
#include <unistd.h>

#include "http.h"

/*
 * A buffer for transferring HTTP/S data.
 */
struct	httpxfer {
	char		*hbuf;    /* header transfer buffer */
	size_t		 hbufsz;  /* header buffer size */
	int		 headok;  /* header has been parsed */
	char		*bbuf;    /* body transfer buffer */
	size_t		 bbufsz;  /* body buffer size */
	int		 bodyok;  /* body has been parsed */
	char		*headbuf; /* lookaside buffer for headers */
	struct httphead	*head;    /* parsed headers */
	size_t		 headsz;  /* number of headers */
};

/*
 * An HTTP/S connection object.
 */
struct	http {
	int		   fd;     /* connected socket */
	short		   port;   /* port number */
	struct source	   src;    /* endpoint (raw) host */
	char		  *path;   /* path to request */
	char		  *host;   /* name of endpoint host */
	struct tls	  *ctx;    /* if TLS */
	writefp		   writer; /* write function */
	readfp		   reader; /* read function */
};

struct tls_config *tlscfg;

static ssize_t
dosysread(char *buf, size_t sz, const struct http *http)
{
	ssize_t	 rc;

	rc = read(http->fd, buf, sz);
	if (rc == -1)
		warn("%s: read", http->src.ip);
	return rc;
}

static ssize_t
dosyswrite(const void *buf, size_t sz, const struct http *http)
{
	ssize_t	 rc;

	rc = write(http->fd, buf, sz);
	if (rc == -1)
		warn("%s: write", http->src.ip);
	return rc;
}

static ssize_t
dotlsread(char *buf, size_t sz, const struct http *http)
{
	ssize_t	 rc;

	do {
		rc = tls_read(http->ctx, buf, sz);
	} while (rc == TLS_WANT_POLLIN || rc == TLS_WANT_POLLOUT);

	if (rc == -1)
		warnx("%s: tls_read: %s", http->src.ip,
		    tls_error(http->ctx));
	return rc;
}

static ssize_t
dotlswrite(const void *buf, size_t sz, const struct http *http)
{
	ssize_t	 rc;

	do {
		rc = tls_write(http->ctx, buf, sz);
	} while (rc == TLS_WANT_POLLIN || rc == TLS_WANT_POLLOUT);

	if (rc == -1)
		warnx("%s: tls_write: %s", http->src.ip,
		    tls_error(http->ctx));
	return rc;
}

int
http_init(void)
{
	if (tlscfg != NULL)
		return 0;

	tlscfg = tls_config_new();
	if (tlscfg == NULL) {
		warn("tls_config_new");
		goto err;
	}

	if (tls_config_set_ca_file(tlscfg, tls_default_ca_cert_file()) == -1) {
		warn("tls_config_set_ca_file: %s", tls_config_error(tlscfg));
		goto err;
	}

	return 0;

 err:
	tls_config_free(tlscfg);
	tlscfg = NULL;

	return -1;
}

static ssize_t
http_read(char *buf, size_t sz, const struct http *http)
{
	ssize_t	 ssz, xfer;

	xfer = 0;
	do {
		if ((ssz = http->reader(buf, sz, http)) < 0)
			return -1;
		if (ssz == 0)
			break;
		xfer += ssz;
		sz -= ssz;
		buf += ssz;
	} while (ssz > 0 && sz > 0);

	return xfer;
}

static int
http_write(const char *buf, size_t sz, const struct http *http)
{
	ssize_t	 ssz, xfer;

	xfer = sz;
	while (sz > 0) {
		if ((ssz = http->writer(buf, sz, http)) < 0)
			return -1;
		sz -= ssz;
		buf += (size_t)ssz;
	}
	return xfer;
}

void
http_disconnect(struct http *http)
{
	int rc;

	if (http->ctx != NULL) {
		/* TLS connection. */
		do {
			rc = tls_close(http->ctx);
		} while (rc == TLS_WANT_POLLIN || rc == TLS_WANT_POLLOUT);

		if (rc < 0)
			warnx("%s: tls_close: %s", http->src.ip,
			    tls_error(http->ctx));

		tls_free(http->ctx);
	}
	if (http->fd != -1) {
		if (close(http->fd) == -1)
			warn("%s: close", http->src.ip);
	}

	http->fd = -1;
	http->ctx = NULL;
}

void
http_free(struct http *http)
{

	if (http == NULL)
		return;
	http_disconnect(http);
	free(http->host);
	free(http->path);
	free(http->src.ip);
	free(http);
}

struct http *
http_alloc(const struct source *addrs, size_t addrsz,
    const char *host, short port, const char *path)
{
	struct sockaddr_storage ss;
	int		 family, fd, c;
	socklen_t	 len;
	size_t		 cur, i = 0;
	struct http	*http;

	/* Do this while we still have addresses to connect. */
again:
	if (i == addrsz)
		return NULL;
	cur = i++;

	/* Convert to PF_INET or PF_INET6 address from string. */

	memset(&ss, 0, sizeof(struct sockaddr_storage));

	if (addrs[cur].family == 4) {
		family = PF_INET;
		((struct sockaddr_in *)&ss)->sin_family = AF_INET;
		((struct sockaddr_in *)&ss)->sin_port = htons(port);
		c = inet_pton(AF_INET, addrs[cur].ip,
		    &((struct sockaddr_in *)&ss)->sin_addr);
		len = sizeof(struct sockaddr_in);
	} else if (addrs[cur].family == 6) {
		family = PF_INET6;
		((struct sockaddr_in6 *)&ss)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)&ss)->sin6_port = htons(port);
		c = inet_pton(AF_INET6, addrs[cur].ip,
		    &((struct sockaddr_in6 *)&ss)->sin6_addr);
		len = sizeof(struct sockaddr_in6);
	} else {
		warnx("%s: unknown family", addrs[cur].ip);
		goto again;
	}

	if (c < 0) {
		warn("%s: inet_ntop", addrs[cur].ip);
		goto again;
	} else if (c == 0) {
		warnx("%s: inet_ntop", addrs[cur].ip);
		goto again;
	}

	/* Create socket and connect. */

	fd = socket(family, SOCK_STREAM, 0);
	if (fd == -1) {
		warn("%s: socket", addrs[cur].ip);
		goto again;
	} else if (connect(fd, (struct sockaddr *)&ss, len) == -1) {
		warn("%s: connect", addrs[cur].ip);
		close(fd);
		goto again;
	}

	/* Allocate the communicator. */

	http = calloc(1, sizeof(struct http));
	if (http == NULL) {
		warn("calloc");
		close(fd);
		return NULL;
	}
	http->fd = fd;
	http->port = port;
	http->src.family = addrs[cur].family;
	http->src.ip = strdup(addrs[cur].ip);
	http->host = strdup(host);
	http->path = strdup(path);
	if (http->src.ip == NULL || http->host == NULL || http->path == NULL) {
		warn("strdup");
		goto err;
	}

	/* If necessary, do our TLS setup. */

	if (port != 443) {
		http->writer = dosyswrite;
		http->reader = dosysread;
		return http;
	}

	http->writer = dotlswrite;
	http->reader = dotlsread;

	if ((http->ctx = tls_client()) == NULL) {
		warn("tls_client");
		goto err;
	} else if (tls_configure(http->ctx, tlscfg) == -1) {
		warnx("%s: tls_configure: %s",
			http->src.ip, tls_error(http->ctx));
		goto err;
	}

	if (tls_connect_socket(http->ctx, http->fd, http->host) != 0) {
		warnx("%s: tls_connect_socket: %s, %s", http->src.ip,
		    http->host, tls_error(http->ctx));
		goto err;
	}

	return http;
err:
	http_free(http);
	return NULL;
}

struct httpxfer *
http_open(const struct http *http, const void *p, size_t psz)
{
	char		*req;
	int		 c;
	struct httpxfer	*trans;

	if (p == NULL) {
		c = asprintf(&req,
		    "GET %s HTTP/1.0\r\n"
		    "Host: %s\r\n"
		    "\r\n",
		    http->path, http->host);
	} else {
		c = asprintf(&req,
		    "POST %s HTTP/1.0\r\n"
		    "Host: %s\r\n"
		    "Content-Type: application/ocsp-request\r\n"
		    "Content-Length: %zu\r\n"
		    "\r\n",
		    http->path, http->host, psz);
	}
	if (c == -1) {
		warn("asprintf");
		return NULL;
	} else if (!http_write(req, c, http)) {
		free(req);
		return NULL;
	} else if (p != NULL && !http_write(p, psz, http)) {
		free(req);
		return NULL;
	}

	free(req);

	trans = calloc(1, sizeof(struct httpxfer));
	if (trans == NULL)
		warn("calloc");
	return trans;
}

void
http_close(struct httpxfer *x)
{

	if (x == NULL)
		return;
	free(x->hbuf);
	free(x->bbuf);
	free(x->headbuf);
	free(x->head);
	free(x);
}

/*
 * Read the HTTP body from the wire.
 * If invoked multiple times, this will return the same pointer with the
 * same data (or NULL, if the original invocation returned NULL).
 * Returns NULL if read or allocation errors occur.
 * You must not free the returned pointer.
 */
char *
http_body_read(const struct http *http, struct httpxfer *trans, size_t *sz)
{
	char		 buf[BUFSIZ];
	ssize_t		 ssz;
	void		*pp;
	size_t		 szp;

	if (sz == NULL)
		sz = &szp;

	/* Have we already parsed this? */

	if (trans->bodyok > 0) {
		*sz = trans->bbufsz;
		return trans->bbuf;
	} else if (trans->bodyok < 0)
		return NULL;

	*sz = 0;
	trans->bodyok = -1;

	do {
		/* If less than sizeof(buf), at EOF. */
		if ((ssz = http_read(buf, sizeof(buf), http)) < 0)
			return NULL;
		else if (ssz == 0)
			break;

		pp = recallocarray(trans->bbuf,
		    trans->bbufsz, trans->bbufsz + ssz, 1);
		if (pp == NULL) {
			warn("recallocarray");
			return NULL;
		}
		trans->bbuf = pp;
		memcpy(trans->bbuf + trans->bbufsz, buf, ssz);
		trans->bbufsz += ssz;
	} while (ssz == sizeof(buf));

	trans->bodyok = 1;
	*sz = trans->bbufsz;
	return trans->bbuf;
}

struct httphead *
http_head_get(const char *v, struct httphead *h, size_t hsz)
{
	size_t	 i;

	for (i = 0; i < hsz; i++) {
		if (strcmp(h[i].key, v))
			continue;
		return &h[i];
	}
	return NULL;
}

/*
 * Look through the headers and determine our HTTP code.
 * This will return -1 on failure, otherwise the code.
 */
int
http_head_status(const struct http *http, struct httphead *h, size_t sz)
{
	int		 rc;
	unsigned int	 code;
	struct httphead *st;

	if ((st = http_head_get("Status", h, sz)) == NULL) {
		warnx("%s: no status header", http->src.ip);
		return -1;
	}

	rc = sscanf(st->val, "%*s %u %*s", &code);
	if (rc < 0) {
		warn("sscanf");
		return -1;
	} else if (rc != 1) {
		warnx("%s: cannot convert status header", http->src.ip);
		return -1;
	}
	return code;
}

/*
 * Parse headers from the transfer.
 * Malformed headers are skipped.
 * A special "Status" header is added for the HTTP status line.
 * This can only happen once http_head_read has been called with
 * success.
 * This can be invoked multiple times: it will only parse the headers
 * once and after that it will just return the cache.
 * You must not free the returned pointer.
 * If the original header parse failed, or if memory allocation fails
 * internally, this returns NULL.
 */
struct httphead *
http_head_parse(const struct http *http, struct httpxfer *trans, size_t *sz)
{
	size_t		 hsz, szp;
	struct httphead	*h;
	char		*cp, *ep, *ccp, *buf;

	if (sz == NULL)
		sz = &szp;

	/*
	 * If we've already parsed the headers, return the
	 * previously-parsed buffer now.
	 * If we have errors on the stream, return NULL now.
	 */

	if (trans->head != NULL) {
		*sz = trans->headsz;
		return trans->head;
	} else if (trans->headok <= 0)
		return NULL;

	if ((buf = strdup(trans->hbuf)) == NULL) {
		warn("strdup");
		return NULL;
	}
	hsz = 0;
	cp = buf;

	do {
		if ((cp = strstr(cp, "\r\n")) != NULL)
			cp += 2;
		hsz++;
	} while (cp != NULL);

	/*
	 * Allocate headers, then step through the data buffer, parsing
	 * out headers as we have them.
	 * We know at this point that the buffer is NUL-terminated in
	 * the usual way.
	 */

	h = calloc(hsz, sizeof(struct httphead));
	if (h == NULL) {
		warn("calloc");
		free(buf);
		return NULL;
	}

	*sz = hsz;
	hsz = 0;
	cp = buf;

	do {
		if ((ep = strstr(cp, "\r\n")) != NULL) {
			*ep = '\0';
			ep += 2;
		}
		if (hsz == 0) {
			h[hsz].key = "Status";
			h[hsz++].val = cp;
			continue;
		}

		/* Skip bad headers. */
		if ((ccp = strchr(cp, ':')) == NULL) {
			warnx("%s: header without separator", http->src.ip);
			continue;
		}

		*ccp++ = '\0';
		while (isspace((unsigned char)*ccp))
			ccp++;
		h[hsz].key = cp;
		h[hsz++].val = ccp;
	} while ((cp = ep) != NULL);

	trans->headbuf = buf;
	trans->head = h;
	trans->headsz = hsz;
	return h;
}

/*
 * Read the HTTP headers from the wire.
 * If invoked multiple times, this will return the same pointer with the
 * same data (or NULL, if the original invocation returned NULL).
 * Returns NULL if read or allocation errors occur.
 * You must not free the returned pointer.
 */
char *
http_head_read(const struct http *http, struct httpxfer *trans, size_t *sz)
{
	char		 buf[BUFSIZ];
	ssize_t		 ssz;
	char		*ep;
	void		*pp;
	size_t		 szp;

	if (sz == NULL)
		sz = &szp;

	/* Have we already parsed this? */

	if (trans->headok > 0) {
		*sz = trans->hbufsz;
		return trans->hbuf;
	} else if (trans->headok < 0)
		return NULL;

	*sz = 0;
	ep = NULL;
	trans->headok = -1;

	/*
	 * Begin by reading by BUFSIZ blocks until we reach the header
	 * termination marker (two CRLFs).
	 * We might read into our body, but that's ok: we'll copy out
	 * the body parts into our body buffer afterward.
	 */

	do {
		/* If less than sizeof(buf), at EOF. */
		if ((ssz = http_read(buf, sizeof(buf), http)) < 0)
			return NULL;
		else if (ssz == 0)
			break;
		pp = realloc(trans->hbuf, trans->hbufsz + ssz);
		if (pp == NULL) {
			warn("realloc");
			return NULL;
		}
		trans->hbuf = pp;
		memcpy(trans->hbuf + trans->hbufsz, buf, ssz);
		trans->hbufsz += ssz;
		/* Search for end of headers marker. */
		ep = memmem(trans->hbuf, trans->hbufsz, "\r\n\r\n", 4);
	} while (ep == NULL && ssz == sizeof(buf));

	if (ep == NULL) {
		warnx("%s: partial transfer", http->src.ip);
		return NULL;
	}
	*ep = '\0';

	/*
	 * The header data is invalid if it has any binary characters in
	 * it: check that now.
	 * This is important because we want to guarantee that all
	 * header keys and pairs are properly NUL-terminated.
	 */

	if (strlen(trans->hbuf) != (uintptr_t)(ep - trans->hbuf)) {
		warnx("%s: binary data in header", http->src.ip);
		return NULL;
	}

	/*
	 * Copy remaining buffer into body buffer.
	 */

	ep += 4;
	trans->bbufsz = (trans->hbuf + trans->hbufsz) - ep;
	trans->bbuf = malloc(trans->bbufsz);
	if (trans->bbuf == NULL) {
		warn("malloc");
		return NULL;
	}
	memcpy(trans->bbuf, ep, trans->bbufsz);

	trans->headok = 1;
	*sz = trans->hbufsz;
	return trans->hbuf;
}

void
http_get_free(struct httpget *g)
{

	if (g == NULL)
		return;
	http_close(g->xfer);
	http_free(g->http);
	free(g);
}

struct httpget *
http_get(const struct source *addrs, size_t addrsz, const char *domain,
    short port, const char *path, const void *post, size_t postsz)
{
	struct http	*h;
	struct httpxfer	*x;
	struct httpget	*g;
	struct httphead	*head;
	size_t		 headsz, bodsz, headrsz;
	int		 code;
	char		*bod, *headr;

	h = http_alloc(addrs, addrsz, domain, port, path);
	if (h == NULL)
		return NULL;

	if ((x = http_open(h, post, postsz)) == NULL) {
		http_free(h);
		return NULL;
	} else if ((headr = http_head_read(h, x, &headrsz)) == NULL) {
		http_close(x);
		http_free(h);
		return NULL;
	} else if ((bod = http_body_read(h, x, &bodsz)) == NULL) {
		http_close(x);
		http_free(h);
		return NULL;
	}

	http_disconnect(h);

	if ((head = http_head_parse(h, x, &headsz)) == NULL) {
		http_close(x);
		http_free(h);
		return NULL;
	} else if ((code = http_head_status(h, head, headsz)) < 0) {
		http_close(x);
		http_free(h);
		return NULL;
	}

	if ((g = calloc(1, sizeof(struct httpget))) == NULL) {
		warn("calloc");
		http_close(x);
		http_free(h);
		return NULL;
	}

	g->headpart = headr;
	g->headpartsz = headrsz;
	g->bodypart = bod;
	g->bodypartsz = bodsz;
	g->head = head;
	g->headsz = headsz;
	g->code = code;
	g->xfer = x;
	g->http = h;
	return g;
}

#if 0
int
main(void)
{
	struct httpget	*g;
	struct httphead	*httph;
	size_t		 i, httphsz;
	struct source	 addrs[2];
	size_t		 addrsz;

#if 0
	addrs[0].ip = "127.0.0.1";
	addrs[0].family = 4;
	addrsz = 1;
#else
	addrs[0].ip = "2a00:1450:400a:806::2004";
	addrs[0].family = 6;
	addrs[1].ip = "193.135.3.123";
	addrs[1].family = 4;
	addrsz = 2;
#endif

	if (http_init() == -1)
		errx(EXIT_FAILURE, "http_init");

#if 0
	g = http_get(addrs, addrsz, "localhost", 80, "/index.html");
#else
	g = http_get(addrs, addrsz, "www.google.ch", 80, "/index.html",
	    NULL, 0);
#endif

	if (g == NULL)
		errx(EXIT_FAILURE, "http_get");

	httph = http_head_parse(g->http, g->xfer, &httphsz);
	warnx("code: %d", g->code);

	for (i = 0; i < httphsz; i++)
		warnx("head: [%s]=[%s]", httph[i].key, httph[i].val);

	http_get_free(g);
	return (EXIT_SUCCESS);
}
#endif
