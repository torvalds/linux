/*	$Id: netproc.c,v 1.45 2025/09/16 15:06:02 sthen Exp $ */
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

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tls.h>
#include <vis.h>

#include "http.h"
#include "extern.h"
#include "parse.h"

#define	RETRY_DELAY 5
#define RETRY_MAX 10

/*
 * Buffer used when collecting the results of an http transfer.
 */
struct	buf {
	char	*buf; /* binary buffer */
	size_t	 sz; /* length of buffer */
};

/*
 * Used for communication with other processes.
 */
struct	conn {
	const char	  *newnonce; /* nonce authority */
	char		  *kid; /* kid when account exists */
	int		   fd; /* acctproc handle */
	int		   dfd; /* dnsproc handle */
	struct buf	   buf; /* http body buffer */
};

/*
 * If something goes wrong (or we're tracing output), we dump the
 * current transfer's data as a debug message.
 */
static void
buf_dump(const struct buf *buf)
{
	char	*nbuf;

	if (buf->sz == 0)
		return;
	/* must be at least 4 * srclen + 1 long */
	if ((nbuf = calloc(buf->sz + 1, 4)) == NULL)
		err(EXIT_FAILURE, "calloc");
	strvisx(nbuf, buf->buf, buf->sz, VIS_SAFE);
	dodbg("transfer buffer: [%s] (%zu bytes)", nbuf, buf->sz);
	free(nbuf);
}

/*
 * Extract the domain and port from a URL.
 * The url must be formatted as schema://address[/stuff].
 * This returns NULL on failure.
 */
static char *
url2host(const char *host, short *port, char **path)
{
	char	*url, *ep;

	/* We only understand HTTP and HTTPS. */
	if (strncmp(host, "https://", 8) == 0) {
		*port = 443;
		if ((url = strdup(host + 8)) == NULL) {
			warn("strdup");
			return NULL;
		}
	} else {
		warnx("%s: RFC 8555 requires https for the API server", host);
		return NULL;
	}

	/* Terminate path part. */
	if ((ep = strchr(url, '/')) != NULL) {
		*path = strdup(ep);
		*ep = '\0';
	} else
		*path = strdup("");

	if (*path == NULL) {
		warn("strdup");
		free(url);
		return NULL;
	}

	/* extract port */
	if ((ep = strchr(url, ':')) != NULL) {
		const char *errstr;

		*ep = '\0';
		*port = strtonum(ep + 1, 1, USHRT_MAX, &errstr);
		if (errstr != NULL) {
			warn("port is %s: %s", errstr, ep + 1);
			free(*path);
			*path = NULL;
			free(url);
			return NULL;
		}
	}

	return url;
}

/*
 * Contact dnsproc and resolve a host.
 * Place the answers in "v" and return the number of answers, which can
 * be at most MAX_SERVERS_DNS.
 * Return <0 on failure.
 */
static ssize_t
urlresolve(int fd, const char *host, struct source *v)
{
	char		*addr;
	size_t		 i, sz;
	long		 lval;

	if (writeop(fd, COMM_DNS, DNS_LOOKUP) <= 0)
		return -1;
	else if (writestr(fd, COMM_DNSQ, host) <= 0)
		return -1;
	else if ((lval = readop(fd, COMM_DNSLEN)) < 0)
		return -1;

	sz = lval;
	assert(sz <= MAX_SERVERS_DNS);

	for (i = 0; i < sz; i++) {
		memset(&v[i], 0, sizeof(struct source));
		if ((lval = readop(fd, COMM_DNSF)) < 0)
			goto err;
		else if (lval != 4 && lval != 6)
			goto err;
		else if ((addr = readstr(fd, COMM_DNSA)) == NULL)
			goto err;
		v[i].family = lval;
		v[i].ip = addr;
	}

	return sz;
err:
	for (i = 0; i < sz; i++)
		free(v[i].ip);
	return -1;
}

/*
 * Send a "regular" HTTP GET message to "addr" and stuff the response
 * into the connection buffer.
 * Return the HTTP error code or <0 on failure.
 */
static long
nreq(struct conn *c, const char *addr)
{
	struct httpget	*g;
	struct source	 src[MAX_SERVERS_DNS];
	struct httphead *st;
	char		*host, *path;
	short		 port;
	size_t		 srcsz;
	ssize_t		 ssz;
	long		 code;
	int		 redirects = 0;

	if ((host = url2host(addr, &port, &path)) == NULL)
		return -1;

again:
	if ((ssz = urlresolve(c->dfd, host, src)) < 0) {
		free(host);
		free(path);
		return -1;
	}
	srcsz = ssz;

	g = http_get(src, srcsz, host, port, path, 0, NULL, 0);
	free(host);
	free(path);
	if (g == NULL)
		return -1;

	switch (g->code) {
	case 301:
	case 302:
	case 303:
	case 307:
	case 308:
		redirects++;
		if (redirects > 3) {
			warnx("too many redirects");
			http_get_free(g);
			return -1;
		}

		if ((st = http_head_get("Location", g->head, g->headsz)) ==
		    NULL) {
			warnx("redirect without location header");
			http_get_free(g);
			return -1;
		}

		host = url2host(st->val, &port, &path);
		http_get_free(g);
		if (host == NULL)
			return -1;
		goto again;
		break;
	default:
		code = g->code;
		break;
	}

	/* Copy the body part into our buffer. */
	free(c->buf.buf);
	c->buf.sz = g->bodypartsz;
	c->buf.buf = malloc(c->buf.sz);
	if (c->buf.buf == NULL) {
		warn("malloc");
		code = -1;
	} else
		memcpy(c->buf.buf, g->bodypart, c->buf.sz);
	http_get_free(g);
	return code;
}

/*
 * Create and send a signed communication to the ACME server.
 * Stuff the response into the communication buffer.
 * Return <0 on failure on the HTTP error code otherwise.
 */
static long
sreq(struct conn *c, const char *addr, int kid, const char *req, char **loc)
{
	struct httpget	*g;
	struct source	 src[MAX_SERVERS_DNS];
	char		*host, *path, *nonce, *reqsn;
	short		 port;
	struct httphead	*h;
	ssize_t		 ssz;
	long		 code;
	int		 retry = 0;

	if ((host = url2host(c->newnonce, &port, &path)) == NULL)
		return -1;

	if ((ssz = urlresolve(c->dfd, host, src)) < 0) {
		free(host);
		free(path);
		return -1;
	}

	g = http_get(src, (size_t)ssz, host, port, path, 1, NULL, 0);
	free(host);
	free(path);
	if (g == NULL)
		return -1;

	h = http_head_get("Replay-Nonce", g->head, g->headsz);
	if (h == NULL) {
		warnx("%s: no replay nonce", c->newnonce);
		http_get_free(g);
		return -1;
	} else if ((nonce = strdup(h->val)) == NULL) {
		warn("strdup");
		http_get_free(g);
		return -1;
	}
	http_get_free(g);

 again:
	/*
	 * Send the url, nonce and request payload to the acctproc.
	 * This will create the proper JSON object we need.
	 */
	if (writeop(c->fd, COMM_ACCT, kid ? ACCT_KID_SIGN : ACCT_SIGN) <= 0) {
		free(nonce);
		return -1;
	} else if (writestr(c->fd, COMM_PAY, req) <= 0) {
		free(nonce);
		return -1;
	} else if (writestr(c->fd, COMM_NONCE, nonce) <= 0) {
		free(nonce);
		return -1;
	} else if (writestr(c->fd, COMM_URL, addr) <= 0) {
		free(nonce);
		return -1;
	}
	free(nonce);

	if (kid && writestr(c->fd, COMM_KID, c->kid) <= 0)
		return -1;

	/* Now read back the signed payload. */
	if ((reqsn = readstr(c->fd, COMM_REQ)) == NULL)
		return -1;

	/* Now send the signed payload to the CA. */
	if ((host = url2host(addr, &port, &path)) == NULL) {
		free(reqsn);
		return -1;
	} else if ((ssz = urlresolve(c->dfd, host, src)) < 0) {
		free(host);
		free(path);
		free(reqsn);
		return -1;
	}

	g = http_get(src, (size_t)ssz, host, port, path, 0, reqsn,
	    strlen(reqsn));

	free(host);
	free(path);
	free(reqsn);
	if (g == NULL)
		return -1;

	/* Stuff response into parse buffer. */
	code = g->code;

	free(c->buf.buf);
	c->buf.sz = g->bodypartsz;
	c->buf.buf = malloc(c->buf.sz);
	if (c->buf.buf == NULL) {
		warn("malloc");
		code = -1;
	} else
		memcpy(c->buf.buf, g->bodypart, c->buf.sz);

	if (code == 400) {
		struct jsmnn	*j;
		char		*type;

		j = json_parse(c->buf.buf, c->buf.sz);
		if (j == NULL) {
			code = -1;
			goto out;
		}

		type = json_getstr(j, "type");
		json_free(j);

		if (type == NULL) {
			code = -1;
			goto out;
		}

		if (strcmp(type, "urn:ietf:params:acme:error:badNonce") != 0) {
			free(type);
			goto out;
		}
		free(type);

		if (retry++ < RETRY_MAX) {
			h = http_head_get("Replay-Nonce", g->head, g->headsz);
			if (h == NULL) {
				warnx("no replay nonce");
				code = -1;
				goto out;
			} else if ((nonce = strdup(h->val)) == NULL) {
				warn("strdup");
				code = -1;
				goto out;
			}
			http_get_free(g);
			goto again;
		}
	}
 out:
	if (loc != NULL) {
		free(*loc);
		*loc = NULL;
		h = http_head_get("Location", g->head, g->headsz);
		/* error checking done by caller */
		if (h != NULL)
			*loc = strdup(h->val);
	}

	http_get_free(g);
	return code;
}

/*
 * Send to the CA that we want to authorise a new account.
 * This only happens once for a new account key.
 * Returns non-zero on success.
 */
static int
donewacc(struct conn *c, const struct capaths *p, const char *contact)
{
	struct jsmnn	*j = NULL;
	int		 rc = 0;
	char		*req, *detail, *error = NULL, *accturi = NULL;
	long		 lc;

	if ((req = json_fmt_newacc(contact)) == NULL)
		warnx("json_fmt_newacc");
	else if ((lc = sreq(c, p->newaccount, 0, req, &c->kid)) < 0)
		warnx("%s: bad comm", p->newaccount);
	else if (lc == 400) {
		if ((j = json_parse(c->buf.buf, c->buf.sz)) == NULL)
			warnx("%s: bad JSON object", p->newaccount);
		else {
			detail = json_getstr(j, "detail");
			if (detail != NULL && stravis(&error, detail, VIS_SAFE)
			    != -1) {
				warnx("%s", error);
				free(error);
			}
		}
	} else if (lc != 200 && lc != 201)
		warnx("%s: bad HTTP: %ld", p->newaccount, lc);
	else if (c->buf.buf == NULL || c->buf.sz == 0)
		warnx("%s: empty response", p->newaccount);
	else
		rc = 1;

	if (c->kid != NULL) {
		if (stravis(&accturi, c->kid, VIS_SAFE) != -1)
			printf("account key: %s\n", accturi);
		free(accturi);
	}

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	free(req);
	return rc;
}

/*
 * Check if our account already exists, if not create it.
 * Populates conn->kid.
 * Returns non-zero on success.
 */
static int
dochkacc(struct conn *c, const struct capaths *p, const char *contact)
{
	int		 rc = 0;
	char		*req, *accturi = NULL;
	long		 lc;

	if ((req = json_fmt_chkacc()) == NULL)
		warnx("json_fmt_chkacc");
	else if ((lc = sreq(c, p->newaccount, 0, req, &c->kid)) < 0)
		warnx("%s: bad comm", p->newaccount);
	else if (lc != 200 && lc != 400)
		warnx("%s: bad HTTP: %ld", p->newaccount, lc);
	else if (c->buf.buf == NULL || c->buf.sz == 0)
		warnx("%s: empty response", p->newaccount);
	else if (lc == 400)
		rc = donewacc(c, p, contact);
	else
		rc = 1;

	if (c->kid == NULL)
		rc = 0;
	else {
		if (stravis(&accturi, c->kid, VIS_SAFE) != -1)
			dodbg("account key: %s", accturi);
		free(accturi);
	}

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	free(req);
	return rc;
}

/*
 * Submit a new order for a certificate.
 */
static int
doneworder(struct conn *c, const char *const *alts, size_t altsz,
    struct order *order, const struct capaths *p, const char *profile)
{
	struct jsmnn	*j = NULL;
	int		 rc = 0;
	char		*req;
	long		 lc;

	if ((req = json_fmt_neworder(alts, altsz, profile)) == NULL)
		warnx("json_fmt_neworder");
	else if ((lc = sreq(c, p->neworder, 1, req, &order->uri)) < 0)
		warnx("%s: bad comm", p->neworder);
	else if (lc != 201)
		warnx("%s: bad HTTP: %ld", p->neworder, lc);
	else if ((j = json_parse(c->buf.buf, c->buf.sz)) == NULL)
		warnx("%s: bad JSON object", p->neworder);
	else if (!json_parse_order(j, order))
		warnx("%s: bad order", p->neworder);
	else if (order->status == ORDER_INVALID)
		warnx("%s: order invalid", p->neworder);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);

	free(req);
	json_free(j);
	return rc;
}

/*
 * Update order status
 */
static int
doupdorder(struct conn *c, struct order *order)
{
	struct jsmnn	*j = NULL;
	int		 rc = 0;
	long		 lc;

	if ((lc = sreq(c, order->uri, 1, "", NULL)) < 0)
		warnx("%s: bad comm", order->uri);
	else if (lc != 200)
		warnx("%s: bad HTTP: %ld", order->uri, lc);
	else if ((j = json_parse(c->buf.buf, c->buf.sz)) == NULL)
		warnx("%s: bad JSON object", order->uri);
	else if (!json_parse_upd_order(j, order))
		warnx("%s: bad order", order->uri);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);

	json_free(j);
	return rc;
}

/*
 * Request a challenge for the given domain name.
 * This must be called for each name "alt".
 * On non-zero exit, fills in "chng" with the challenge.
 */
static int
dochngreq(struct conn *c, const char *auth, struct chng *chng)
{
	int		 rc = 0;
	long		 lc;
	struct jsmnn	*j = NULL;

	dodbg("%s: %s", __func__, auth);

	if ((lc = sreq(c, auth, 1, "", NULL)) < 0)
		warnx("%s: bad comm", auth);
	else if (lc != 200)
		warnx("%s: bad HTTP: %ld", auth, lc);
	else if ((j = json_parse(c->buf.buf, c->buf.sz)) == NULL)
		warnx("%s: bad JSON object", auth);
	else if (!json_parse_challenge(j, chng))
		warnx("%s: bad challenge", auth);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	json_free(j);
	return rc;
}

/*
 * Tell the CA that a challenge response is in place.
 */
static int
dochngresp(struct conn *c, const struct chng *chng)
{
	int	 rc = 0;
	long	 lc;

	dodbg("%s: challenge", chng->uri);

	if ((lc = sreq(c, chng->uri, 1, "{}", NULL)) < 0)
		warnx("%s: bad comm", chng->uri);
	else if (lc != 200 && lc != 201 && lc != 202)
		warnx("%s: bad HTTP: %ld", chng->uri, lc);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	return rc;
}

/*
 * Submit our csr to the CA.
 */
static int
docert(struct conn *c, const char *uri, const char *csr)
{
	char	*req;
	int	 rc = 0;
	long	 lc;

	dodbg("%s: certificate", uri);

	if ((req = json_fmt_newcert(csr)) == NULL)
		warnx("json_fmt_newcert");
	else if ((lc = sreq(c, uri, 1, req, NULL)) < 0)
		warnx("%s: bad comm", uri);
	else if (lc != 200)
		warnx("%s: bad HTTP: %ld", uri, lc);
	else if (c->buf.sz == 0 || c->buf.buf == NULL)
		warnx("%s: empty response", uri);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	free(req);
	return rc;
}

/*
 * Get certificate from CA
 */
static int
dogetcert(struct conn *c, const char *uri)
{
	int	 rc = 0;
	long	 lc;

	dodbg("%s: certificate", uri);

	if ((lc = sreq(c, uri, 1, "", NULL)) < 0)
		warnx("%s: bad comm", uri);
	else if (lc != 200)
		warnx("%s: bad HTTP: %ld", uri, lc);
	else if (c->buf.sz == 0 || c->buf.buf == NULL)
		warnx("%s: empty response", uri);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);

	return rc;
}

static int
dorevoke(struct conn *c, const char *addr, const char *cert)
{
	char		*req;
	int		 rc = 0;
	long		 lc = 0;

	dodbg("%s: revocation", addr);

	if ((req = json_fmt_revokecert(cert)) == NULL)
		warnx("json_fmt_revokecert");
	else if ((lc = sreq(c, addr, 1, req, NULL)) < 0)
		warnx("%s: bad comm", addr);
	else if (lc != 200 && lc != 201 && lc != 409)
		warnx("%s: bad HTTP: %ld", addr, lc);
	else
		rc = 1;

	if (lc == 409)
		warnx("%s: already revoked", addr);

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	free(req);
	return rc;
}

/*
 * Look up directories from the certificate authority.
 */
static int
dodirs(struct conn *c, const char *addr, struct capaths *paths)
{
	struct jsmnn	*j = NULL;
	long		 lc;
	int		 rc = 0;

	dodbg("%s: directories", addr);

	if ((lc = nreq(c, addr)) < 0)
		warnx("%s: bad comm", addr);
	else if (lc != 200 && lc != 201)
		warnx("%s: bad HTTP: %ld", addr, lc);
	else if ((j = json_parse(c->buf.buf, c->buf.sz)) == NULL)
		warnx("json_parse");
	else if (!json_parse_capaths(j, paths))
		warnx("%s: bad CA paths", addr);
	else
		rc = 1;

	if (rc == 0 || verbose > 1)
		buf_dump(&c->buf);
	json_free(j);
	return rc;
}

/*
 * Communicate with the ACME server.
 * We need the certificate we want to upload and our account key information.
 */
int
netproc(int kfd, int afd, int Cfd, int cfd, int dfd, int rfd,
    int revocate, struct authority_c *authority,
    const char *const *alts, size_t altsz, const char *profile)
{
	int		 rc = 0, retries = 0;
	size_t		 i;
	char		*cert = NULL, *thumb = NULL, *error = NULL;
	struct conn	 c;
	struct capaths	 paths;
	struct order	 order;
	struct chng	*chngs = NULL;
	long		 lval;

	memset(&paths, 0, sizeof(struct capaths));
	memset(&c, 0, sizeof(struct conn));

	if (unveil(tls_default_ca_cert_file(), "r") == -1) {
		warn("unveil %s", tls_default_ca_cert_file());
		goto out;
	}

	if (pledge("stdio inet rpath", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	if (http_init(authority->insecure) == -1) {
		warn("http_init");
		goto out;
	}

	if (pledge("stdio inet", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	/*
	 * Wait until the acctproc, keyproc, and revokeproc have started up and
	 * are ready to serve us data.
	 * Then check whether revokeproc indicates that the certificate on file
	 * (if any) can be updated.
	 */
	if ((lval = readop(afd, COMM_ACCT_STAT)) == 0) {
		rc = 1;
		goto out;
	} else if (lval != ACCT_READY) {
		warnx("unknown operation from acctproc");
		goto out;
	}

	if ((lval = readop(kfd, COMM_KEY_STAT)) == 0) {
		rc = 1;
		goto out;
	} else if (lval != KEY_READY) {
		warnx("unknown operation from keyproc");
		goto out;
	}

	if ((lval = readop(rfd, COMM_REVOKE_RESP)) == 0) {
		rc = 1;
		goto out;
	} else if (lval != REVOKE_EXP && lval != REVOKE_OK) {
		warnx("unknown operation from revokeproc");
		goto out;
	}

	/* If our certificate is up-to-date, return now. */
	if (lval == REVOKE_OK) {
		rc = 1;
		goto out;
	}

	c.dfd = dfd;
	c.fd = afd;

	/*
	 * Look up the API urls of the ACME server.
	 */
	if (!dodirs(&c, authority->api, &paths))
		goto out;

	c.newnonce = paths.newnonce;

	/* Check if our account already exists or create it. */
	if (!dochkacc(&c, &paths, authority->contact))
		goto out;

	/*
	 * If we're meant to revoke, then wait for revokeproc to send us
	 * the certificate (if it's found at all).
	 * Following that, submit the request to the CA then notify the
	 * certproc, which will in turn notify the fileproc.
	 * XXX currently we can only sign with the account key, the RFC
	 * also mentions signing with the private key of the cert itself.
	 */
	if (revocate) {
		if ((cert = readstr(rfd, COMM_CSR)) == NULL)
			goto out;
		if (!dorevoke(&c, paths.revokecert, cert))
			goto out;
		else if (writeop(cfd, COMM_CSR_OP, CERT_REVOKE) > 0)
			rc = 1;
		goto out;
	}

	memset(&order, 0, sizeof(order));

	if (!doneworder(&c, alts, altsz, &order, &paths, profile))
		goto out;

	chngs = calloc(order.authsz, sizeof(struct chng));
	if (chngs == NULL) {
		warn("calloc");
		goto out;
	}

	/*
	 * Get thumbprint from acctproc. We will need it to construct
	 * a response to the challenge
	 */
	if (writeop(afd, COMM_ACCT, ACCT_THUMBPRINT) <= 0)
		goto out;
	else if ((thumb = readstr(afd, COMM_THUMB)) == NULL)
		goto out;

	while(order.status != ORDER_VALID && order.status != ORDER_INVALID) {
		switch (order.status) {
		case ORDER_INVALID:
			warnx("order invalid");
			goto out;
		case ORDER_VALID:
			rc = 1;
			continue;
		case ORDER_PENDING:
			if (order.authsz < 1) {
				warnx("order is in state pending but no "
				    "authorizations know");
				goto out;
			}
			for (i = 0; i < order.authsz; i++) {
				if (!dochngreq(&c, order.auths[i], &chngs[i]))
					goto out;

				dodbg("challenge, token: %s, uri: %s, status: "
				    "%d", chngs[i].token, chngs[i].uri,
				    chngs[i].status);

				if (chngs[i].status == CHNG_VALID ||
				    chngs[i].status == CHNG_INVALID)
					continue;

				if (chngs[i].retry++ >= RETRY_MAX) {
					warnx("%s: too many tries",
					    chngs[i].uri);
					goto out;
				}

				if (writeop(Cfd, COMM_CHNG_OP, CHNG_SYN) <= 0)
					goto out;
				else if (writestr(Cfd, COMM_THUMB, thumb) <= 0)
					goto out;
				else if (writestr(Cfd, COMM_TOK,
				    chngs[i].token) <= 0)
					goto out;

				/* Read that the challenge has been made. */
				if (readop(Cfd, COMM_CHNG_ACK) != CHNG_ACK)
					goto out;

			}
			/* Write to the CA that it's ready. */
			for (i = 0; i < order.authsz; i++) {
				if (chngs[i].status == CHNG_VALID ||
				    chngs[i].status == CHNG_INVALID)
					continue;
				if (!dochngresp(&c, &chngs[i]))
					goto out;
			}
			break;
		case ORDER_READY:
			/*
			 * Write our acknowledgement that the challenges are
			 * over.
			 * The challenge process will remove all of the files.
			 */
			if (writeop(Cfd, COMM_CHNG_OP, CHNG_STOP) <= 0)
				goto out;

			/* Wait to receive the certificate itself. */
			if ((cert = readstr(kfd, COMM_CERT)) == NULL)
				goto out;
			if (!docert(&c, order.finalize, cert))
				goto out;
			break;
		case ORDER_PROCESSING:
			/* we'll just retry */
			break;
		default:
			warnx("unhandled status: %d", order.status);
			goto out;
		}
		if (!doupdorder(&c, &order))
			goto out;

		dodbg("order.status %d", order.status);
		switch (order.status) {
		case ORDER_PENDING:
		case ORDER_PROCESSING:
			if (retries++ > RETRY_MAX) {
				warnx("too many retries");
				goto out;
			}
			sleep(RETRY_DELAY);
			break;
		default:
			retries = 0; /* state changed, we made progress */
			break;
		}
	}

	if (order.status != ORDER_VALID) {
		for (i = 0; i < order.authsz; i++) {
			dochngreq(&c, order.auths[i], &chngs[i]);
			if (chngs[i].error != NULL) {
				if (stravis(&error, chngs[i].error, VIS_SAFE)
				    != -1) {
					warnx("%s", error);
					free(error);
					error = NULL;
				}
			}
		}
		goto out;
	}

	if (order.certificate == NULL) {
		warnx("no certificate url received");
		goto out;
	}

	if (!dogetcert(&c, order.certificate))
		goto out;
	else if (writeop(cfd, COMM_CSR_OP, CERT_UPDATE) <= 0)
		goto out;
	else if (writebuf(cfd, COMM_CSR, c.buf.buf, c.buf.sz) <= 0)
		goto out;
	rc = 1;
out:
	close(cfd);
	close(kfd);
	close(afd);
	close(Cfd);
	close(dfd);
	close(rfd);
	free(cert);
	free(thumb);
	free(c.kid);
	free(c.buf.buf);
	if (chngs != NULL)
		for (i = 0; i < order.authsz; i++)
			json_free_challenge(&chngs[i]);
	free(chngs);
	json_free_capaths(&paths);
	return rc;
}
