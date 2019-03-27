/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Paycounter, Inc.
 * Author: Alfred Perlstein <alfred@paycounter.com>, <alfred@FreeBSD.org>
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

#define ACCEPT_FILTER_MOD

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/socketvar.h>

/* check for GET/HEAD */
static int sohashttpget(struct socket *so, void *arg, int waitflag);
/* check for HTTP/1.0 or HTTP/1.1 */
static int soparsehttpvers(struct socket *so, void *arg, int waitflag);
/* check for end of HTTP/1.x request */
static int soishttpconnected(struct socket *so, void *arg, int waitflag);
/* strcmp on an mbuf chain */
static int mbufstrcmp(struct mbuf *m, struct mbuf *npkt, int offset, char *cmp);
/* strncmp on an mbuf chain */
static int mbufstrncmp(struct mbuf *m, struct mbuf *npkt, int offset,
	int max, char *cmp);
/* socketbuffer is full */
static int sbfull(struct sockbuf *sb);

static struct accept_filter accf_http_filter = {
	"httpready",
	sohashttpget,
	NULL,
	NULL
};

static moduledata_t accf_http_mod = {
	"accf_http",
	accept_filt_generic_mod_event,
	&accf_http_filter
};

DECLARE_MODULE(accf_http, accf_http_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

static int parse_http_version = 1;

static SYSCTL_NODE(_net_inet_accf, OID_AUTO, http, CTLFLAG_RW, 0,
"HTTP accept filter");
SYSCTL_INT(_net_inet_accf_http, OID_AUTO, parsehttpversion, CTLFLAG_RW,
&parse_http_version, 1,
"Parse http version so that non 1.x requests work");

#ifdef ACCF_HTTP_DEBUG
#define DPRINT(fmt, args...)						\
	do {								\
		printf("%s:%d: " fmt "\n", __func__, __LINE__, ##args);	\
	} while (0)
#else
#define DPRINT(fmt, args...)
#endif

static int
sbfull(struct sockbuf *sb)
{

	DPRINT("sbfull, cc(%ld) >= hiwat(%ld): %d, "
	    "mbcnt(%ld) >= mbmax(%ld): %d",
	    sb->sb_cc, sb->sb_hiwat, sb->sb_cc >= sb->sb_hiwat,
	    sb->sb_mbcnt, sb->sb_mbmax, sb->sb_mbcnt >= sb->sb_mbmax);
	return (sbused(sb) >= sb->sb_hiwat || sb->sb_mbcnt >= sb->sb_mbmax);
}

/*
 * start at mbuf m, (must provide npkt if exists)
 * starting at offset in m compare characters in mbuf chain for 'cmp'
 */
static int
mbufstrcmp(struct mbuf *m, struct mbuf *npkt, int offset, char *cmp)
{
	struct mbuf *n;

	for (; m != NULL; m = n) {
		n = npkt;
		if (npkt)
			npkt = npkt->m_nextpkt;
		for (; m; m = m->m_next) {
			for (; offset < m->m_len; offset++, cmp++) {
				if (*cmp == '\0')
					return (1);
				else if (*cmp != *(mtod(m, char *) + offset))
					return (0);
			}
			if (*cmp == '\0')
				return (1);
			offset = 0;
		}
	}
	return (0);
}

/*
 * start at mbuf m, (must provide npkt if exists)
 * starting at offset in m compare characters in mbuf chain for 'cmp'
 * stop at 'max' characters
 */
static int
mbufstrncmp(struct mbuf *m, struct mbuf *npkt, int offset, int max, char *cmp)
{
	struct mbuf *n;

	for (; m != NULL; m = n) {
		n = npkt;
		if (npkt)
			npkt = npkt->m_nextpkt;
		for (; m; m = m->m_next) {
			for (; offset < m->m_len; offset++, cmp++, max--) {
				if (max == 0 || *cmp == '\0')
					return (1);
				else if (*cmp != *(mtod(m, char *) + offset))
					return (0);
			}
			if (max == 0 || *cmp == '\0')
				return (1);
			offset = 0;
		}
	}
	return (0);
}

#define STRSETUP(sptr, slen, str)					\
	do {								\
		sptr = str;						\
		slen = sizeof(str) - 1;					\
	} while(0)

static int
sohashttpget(struct socket *so, void *arg, int waitflag)
{

	if ((so->so_rcv.sb_state & SBS_CANTRCVMORE) == 0 &&
	    !sbfull(&so->so_rcv)) {
		struct mbuf *m;
		char *cmp;
		int	cmplen, cc;

		m = so->so_rcv.sb_mb;
		cc = sbavail(&so->so_rcv) - 1;
		if (cc < 1)
			return (SU_OK);
		switch (*mtod(m, char *)) {
		case 'G':
			STRSETUP(cmp, cmplen, "ET ");
			break;
		case 'H':
			STRSETUP(cmp, cmplen, "EAD ");
			break;
		default:
			goto fallout;
		}
		if (cc < cmplen) {
			if (mbufstrncmp(m, m->m_nextpkt, 1, cc, cmp) == 1) {
				DPRINT("short cc (%d) but mbufstrncmp ok", cc);
				return (SU_OK);
			} else {
				DPRINT("short cc (%d) mbufstrncmp failed", cc);
				goto fallout;
			}
		}
		if (mbufstrcmp(m, m->m_nextpkt, 1, cmp) == 1) {
			DPRINT("mbufstrcmp ok");
			if (parse_http_version == 0)
				return (soishttpconnected(so, arg, waitflag));
			else
				return (soparsehttpvers(so, arg, waitflag));
		}
		DPRINT("mbufstrcmp bad");
	}

fallout:
	DPRINT("fallout");
	return (SU_ISCONNECTED);
}

static int
soparsehttpvers(struct socket *so, void *arg, int waitflag)
{
	struct mbuf *m, *n;
	int	i, cc, spaces, inspaces;

	if ((so->so_rcv.sb_state & SBS_CANTRCVMORE) != 0 || sbfull(&so->so_rcv))
		goto fallout;

	m = so->so_rcv.sb_mb;
	cc = sbavail(&so->so_rcv);
	inspaces = spaces = 0;
	for (m = so->so_rcv.sb_mb; m; m = n) {
		n = m->m_nextpkt;
		for (; m; m = m->m_next) {
			for (i = 0; i < m->m_len; i++, cc--) {
				switch (*(mtod(m, char *) + i)) {
				case ' ':
					/* tabs? '\t' */
					if (!inspaces) {
						spaces++;
						inspaces = 1;
					}
					break;
				case '\r':
				case '\n':
					DPRINT("newline");
					goto fallout;
				default:
					if (spaces != 2) {
						inspaces = 0;
						break;
					}

					/*
					 * if we don't have enough characters
					 * left (cc < sizeof("HTTP/1.0") - 1)
					 * then see if the remaining ones
					 * are a request we can parse.
					 */
					if (cc < sizeof("HTTP/1.0") - 1) {
						if (mbufstrncmp(m, n, i, cc,
							"HTTP/1.") == 1) {
							DPRINT("ok");
							goto readmore;
						} else {
							DPRINT("bad");
							goto fallout;
						}
					} else if (
					    mbufstrcmp(m, n, i, "HTTP/1.0") ||
					    mbufstrcmp(m, n, i, "HTTP/1.1")) {
						DPRINT("ok");
						return (soishttpconnected(so,
						    arg, waitflag));
					} else {
						DPRINT("bad");
						goto fallout;
					}
				}
			}
		}
	}
readmore:
	DPRINT("readmore");
	/*
	 * if we hit here we haven't hit something
	 * we don't understand or a newline, so try again
	 */
	soupcall_set(so, SO_RCV, soparsehttpvers, arg);
	return (SU_OK);

fallout:
	DPRINT("fallout");
	return (SU_ISCONNECTED);
}


#define NCHRS 3

static int
soishttpconnected(struct socket *so, void *arg, int waitflag)
{
	char a, b, c;
	struct mbuf *m, *n;
	int ccleft, copied;

	DPRINT("start");
	if ((so->so_rcv.sb_state & SBS_CANTRCVMORE) != 0 || sbfull(&so->so_rcv))
		goto gotit;

	/*
	 * Walk the socketbuffer and copy the last NCHRS (3) into a, b, and c
	 * copied - how much we've copied so far
	 * ccleft - how many bytes remaining in the socketbuffer
	 * just loop over the mbufs subtracting from 'ccleft' until we only
	 * have NCHRS left
	 */
	copied = 0;
	ccleft = sbavail(&so->so_rcv);
	if (ccleft < NCHRS)
		goto readmore;
	a = b = c = '\0';
	for (m = so->so_rcv.sb_mb; m; m = n) {
		n = m->m_nextpkt;
		for (; m; m = m->m_next) {
			ccleft -= m->m_len;
			if (ccleft <= NCHRS) {
				char *src;
				int tocopy;

				tocopy = (NCHRS - ccleft) - copied;
				src = mtod(m, char *) + (m->m_len - tocopy);

				while (tocopy--) {
					switch (copied++) {
					case 0:
						a = *src++;
						break;
					case 1:
						b = *src++;
						break;
					case 2:
						c = *src++;
						break;
					}
				}
			}
		}
	}
	if (c == '\n' && (b == '\n' || (b == '\r' && a == '\n'))) {
		/* we have all request headers */
		goto gotit;
	}

readmore:
	soupcall_set(so, SO_RCV, soishttpconnected, arg);
	return (SU_OK);

gotit:
	return (SU_ISCONNECTED);
}
