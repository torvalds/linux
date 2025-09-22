/*	$OpenBSD: pdu.c,v 1.15 2025/01/23 12:17:48 claudio Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <scsi/iscsi.h>

#include <errno.h>
#include <event.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "log.h"

size_t	pdu_readbuf_read(struct pdu_readbuf *, void *, size_t);
size_t	pdu_readbuf_len(struct pdu_readbuf *);

#define PDU_MIN(_x, _y)		((_x) < (_y) ? (_x) : (_y))

void *
pdu_gethdr(struct pdu *p)
{
	void *hdr;

	if (!(hdr = calloc(1, sizeof(struct iscsi_pdu))))
		return NULL;
	if (pdu_addbuf(p, hdr, sizeof(struct iscsi_pdu), PDU_HEADER)) {
		free(hdr);
		return NULL;
	}
	return hdr;
}

int
text_to_pdu(struct kvp *k, struct pdu *p)
{
	char *buf, *s;
	size_t	len = 0, rem;
	int n, nk;

	if (k == NULL)
		return 0;

	nk = 0;
	while(k[nk].key) {
		len += 2 + strlen(k[nk].key) + strlen(k[nk].value);
		nk++;
	}

	if (!(buf = pdu_alloc(len)))
		return -1;
	s = buf;
	rem = len;
	nk = 0;
	while(k[nk].key) {
		n = snprintf(s, rem, "%s=%s", k[nk].key, k[nk].value);
		if (n < 0 || (size_t)n >= rem)
			fatalx("text_to_pdu");
		rem -= n + 1;
		s += n + 1;
		nk++;
	}

	if (pdu_addbuf(p, buf, len, PDU_DATA))
		return -1;
	return len;
}

struct kvp *
pdu_to_text(char *buf, size_t len)
{
	struct kvp *k;
	size_t n;
	char *eq;
	unsigned int nkvp = 0, i;

	/* remove padding zeros */
	for (n = len; n > 0 && buf[n - 1] == '\0'; n--)
		;
	if (n == len) {
		log_debug("pdu_to_text: badly terminated text data");
		return NULL;
	}
	len = n + 1;

	for(n = 0; n < len; n++)
		if (buf[n] == '\0')
			nkvp++;

	if (!(k = calloc(nkvp + 1, sizeof(*k))))
		return NULL;

	for (i = 0; i < nkvp; i++) {
		eq = strchr(buf, '=');
		if (!eq) {
			log_debug("pdu_to_text: badly encoded text data");
			free(k);
			return NULL;
		}
		*eq++ = '\0';
		k[i].key = buf;
		k[i].value = eq;
		buf = eq + strlen(eq) + 1;
	}
	return k;
}

/* Modified version of strtonum() to fit iscsid's need 
 *
 * Copyright (c) 2004 Ted Unangst and Todd Miller
 * All rights reserved.
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
u_int64_t
text_to_num(const char *numstr, u_int64_t minval, u_int64_t maxval,
    const char **errstrp)
{
	unsigned long long ull = 0;
	char *ep;
	int error = 0;
	struct errval {
		const char *errstr;
		int err;
	} ev[4] = {
		{ NULL,		0 },
		{ "invalid",	EINVAL },
		{ "too small",  ERANGE },
		{ "too large",	ERANGE }
	};
#define INVALID		1
#define TOOSMALL	2
#define TOOLARGE	3

	ev[0].err = errno;
	errno = 0;
	if (minval > maxval)
		error = INVALID;
	else {
		ull = strtoull(numstr, &ep, 0);
		if (numstr == ep || *ep != '\0')
			error = INVALID;
		else if (ull < minval)
			error = TOOSMALL;
		else if ((ull == ULLONG_MAX && errno == ERANGE) || ull > maxval)
			error = TOOLARGE;
	}
	if (errstrp != NULL)
		*errstrp = ev[error].errstr;
	errno = ev[error].err;
	if (error)
		ull = 0;

	return ull;
#undef INVALID
#undef TOOSMALL
#undef TOOLARGE
}

int
text_to_bool(const char *buf, const char **errstrp)
{
	int val;

	if (strcmp(buf, "Yes") == 0)
		val = 1;
	else if (strcmp(buf, "No") == 0)
		val = 0;
	else {
		if (errstrp != NULL)
			*errstrp = "invalid";
		return 0;
	}
	if (errstrp != NULL)
		*errstrp = NULL;
	return val;
}

int
text_to_digest(const char *buf, const char **errstrp)
{
	int val = 0;
	size_t len;
	const char *p;

	while (buf != NULL) {
		p = strchr(buf, ',');
		if (p == NULL)
			len = strlen(buf);
		else
			len = p++ - buf;

		if (strncmp(buf, "None", len) == 0)
			val |= DIGEST_NONE;
		else if (strncmp(buf, "CRC32C", len) == 0)
			val |= DIGEST_CRC32C;
		else {
			if (errstrp != NULL)
				*errstrp = "invalid";
			return 0;
		}
		buf = p;
	}
	if (errstrp != NULL)
		*errstrp = NULL;
	return val;
}

/*
 * Internal functions to send/recv pdus.
 */

void
pdu_free_queue(struct pduq *channel)
{
	struct pdu *p;

	while ((p = TAILQ_FIRST(channel))) {
		TAILQ_REMOVE(channel, p, entry);
		pdu_free(p);
	}
}

ssize_t
pdu_read(struct connection *c)
{
	struct iovec iov[2];
	unsigned int niov = 1;
	ssize_t n;

	bzero(&iov, sizeof(iov));
	iov[0].iov_base = c->prbuf.buf + c->prbuf.wpos;
	if (c->prbuf.wpos < c->prbuf.rpos)
		iov[0].iov_len = c->prbuf.rpos - c->prbuf.wpos;
	else {
		iov[0].iov_len = c->prbuf.size - c->prbuf.wpos;
		if (c->prbuf.rpos > 0) {
			niov++;
			iov[1].iov_base = c->prbuf.buf;
			iov[1].iov_len = c->prbuf.rpos - 1;
		}
	}

	if ((n = readv(c->fd, iov, niov)) == -1)
		return -1;
	if (n == 0)
		/* XXX what should we do on close with remaining data? */
		return 0;

	c->prbuf.wpos += n;
	if (c->prbuf.wpos >= c->prbuf.size)
		c->prbuf.wpos -= c->prbuf.size;

	return n;
}

ssize_t
pdu_write(struct connection *c)
{
	struct iovec iov[PDU_WRIOV];
	struct pdu *b, *nb;
	unsigned int niov = 0, j;
	size_t off, resid, size;
	ssize_t n;

	TAILQ_FOREACH(b, &c->pdu_w, entry) {
		if (niov >= PDU_WRIOV)
			break;
		off = b->resid;
		for (j = 0; j < PDU_MAXIOV && niov < PDU_WRIOV; j++) {
			if (!b->iov[j].iov_len)
				continue;
			if (off >= b->iov[j].iov_len) {
				off -= b->iov[j].iov_len;
				continue;
			}
			iov[niov].iov_base = (char *)b->iov[j].iov_base + off;
			iov[niov++].iov_len = b->iov[j].iov_len - off;
			off = 0;
		}
	}

	if ((n = writev(c->fd, iov, niov)) == -1) {
		if (errno == EAGAIN || errno == ENOBUFS ||
		    errno == EINTR)	/* try later */
			return 0;
		else {
			log_warn("pdu_write");
			return -1;
		}
	}
	if (n == 0)
		return 0;

	size = n;
	for (b = TAILQ_FIRST(&c->pdu_w); b != NULL && size > 0; b = nb) {
		nb = TAILQ_NEXT(b, entry);
		resid = b->resid;
		for (j = 0; j < PDU_MAXIOV; j++) {
			if (resid >= b->iov[j].iov_len)
				resid -= b->iov[j].iov_len;
			else if (size >= b->iov[j].iov_len - resid) {
				size -= b->iov[j].iov_len - resid;
				b->resid += b->iov[j].iov_len - resid;
				resid = 0;
			} else {
				b->resid += size;
				size = 0;
				break;
			}
		}
		if (j == PDU_MAXIOV) {
			/* all written */
			TAILQ_REMOVE(&c->pdu_w, b, entry);
			pdu_free(b);
		}
	}
	return n;
}

int
pdu_pending(struct connection *c)
{
	if (TAILQ_EMPTY(&c->pdu_w))
		return 0;
	else
		return 1;
}

void
pdu_parse(struct connection *c)
{
	struct pdu *p;
	struct iscsi_pdu *ipdu;
	char *ahb, *db;
	size_t ahslen, dlen, off;
	ssize_t n;
	unsigned int j;

/* XXX XXX I DON'T LIKE YOU. CAN I REWRITE YOU? */

	do {
		if (!(p = c->prbuf.wip)) {
			/* get and parse base header */
			if (pdu_readbuf_len(&c->prbuf) < sizeof(*ipdu))
				return;
			if (!(p = pdu_new()))
				goto fail;
			if (!(ipdu = pdu_gethdr(p)))
				goto fail;

			c->prbuf.wip = p;
			/*
			 * XXX maybe a pdu_readbuf_peek() would allow a better
			 * error handling.
			 */
			pdu_readbuf_read(&c->prbuf, ipdu, sizeof(*ipdu));

			ahslen = ipdu->ahslen * sizeof(u_int32_t);
			if (ahslen != 0) {
				if (!(ahb = pdu_alloc(ahslen)) ||
				    pdu_addbuf(p, ahb, ahslen, PDU_AHS))
					goto fail;
			}

			dlen = ipdu->datalen[0] << 16 | ipdu->datalen[1] << 8 |
			    ipdu->datalen[2];
			if (dlen != 0) {
				if (!(db = pdu_alloc(dlen)) ||
				    pdu_addbuf(p, db, dlen, PDU_DATA))
					goto fail;
			}

			p->resid = sizeof(*ipdu);
		} else {
			off = p->resid;
			for (j = 0; j < PDU_MAXIOV; j++) {
				if (off >= p->iov[j].iov_len)
					off -=  p->iov[j].iov_len;
				else {
					n = pdu_readbuf_read(&c->prbuf,
					    (char *)p->iov[j].iov_base + off,
					    p->iov[j].iov_len - off);
					p->resid += n;
					if (n == 0 || off + n !=
					    p->iov[j].iov_len)
						return;
				}
			}
			p->resid = 0; /* reset resid so pdu can be reused */
			c->prbuf.wip = NULL;
			task_pdu_cb(c, p);
		}
	} while (1);
fail:
	fatalx("pdu_parse hit a space oddity");
}

size_t
pdu_readbuf_read(struct pdu_readbuf *rb, void *ptr, size_t len)
{
	size_t l;

	if (rb->rpos == rb->wpos) {
		return 0;
	} else if (rb->rpos < rb->wpos) {
		l = PDU_MIN(rb->wpos - rb->rpos, len);
		memcpy(ptr, rb->buf + rb->rpos, l);
		rb->rpos += l;
		return l;
	} else {
		l = PDU_MIN(rb->size - rb->rpos, len);
		memcpy(ptr, rb->buf + rb->rpos, l);
		rb->rpos += l;
		if (rb->rpos == rb->size)
			rb->rpos = 0;
		if (l < len)
			return l + pdu_readbuf_read(rb, (char *)ptr + l,
			    len - l);
		return l;
	}
}

size_t
pdu_readbuf_len(struct pdu_readbuf *rb)
{
	if (rb->rpos <= rb->wpos)
		return rb->wpos - rb->rpos;
	else
		return rb->size - (rb->rpos - rb->wpos);
}

int
pdu_readbuf_set(struct pdu_readbuf *rb, size_t bsize)
{
	char *nb;

	if (bsize < rb->size)
		/* can't shrink */
		return 0;
	if ((nb = realloc(rb->buf, bsize)) == NULL) {
		free(rb->buf);
		return -1;
	}
	rb->buf = nb;
	rb->size = bsize;
	return 0;
}

void
pdu_readbuf_free(struct pdu_readbuf *rb)
{
	free(rb->buf);
}
