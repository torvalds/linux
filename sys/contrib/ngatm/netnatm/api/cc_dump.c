/*
 * Copyright (c) 2003-2004
 *	Hartmut Brandt
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Begemot: libunimsg/netnatm/api/cc_dump.c,v 1.3 2004/08/05 07:10:56 brandt Exp $
 *
 * ATM API as defined per af-saa-0108
 */

#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/api/unisap.h>
#include <netnatm/sig/unidef.h>
#include <netnatm/api/atmapi.h>
#include <netnatm/api/ccatm.h>
#include <netnatm/api/ccpriv.h>

#ifdef _KERNEL
#ifdef __FreeBSD__
#include <machine/stdarg.h>
#endif
#else	/* !_KERNEL */
#include <stdarg.h>
#endif

/*
 * local structure to reduce number of arguments to functions
 */
struct dump {
	struct ccdata	*cc;	/* what to dump */
	size_t		maxsiz;	/* size of user buffer */
	cc_dump_f	func;	/* user function */
	void		*uarg;	/* user supplied argument */
	char		*buf;	/* user buffer */
	size_t		len;	/* current string length */
	int		ret;	/* return code */
};

static void cc_dumpf(struct dump *, const char *, ...) __printflike(2, 3);

static void
cc_dumpf(struct dump *d, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (d->ret != 0)
		return;
	if (d->len >= d->maxsiz - 1) {
		d->ret = d->func(d->cc, d->uarg, d->buf);
		if (d->ret != 0)
			return;
		d->buf[0] = '\0';
		d->len = 0;
	}
	va_start(ap, fmt);
	n = vsnprintf(d->buf + d->len, d->maxsiz - d->len, fmt, ap);
	va_end(ap);

	if (n < 0) {
		d->ret = CCGETERRNO();
		return;
	}
	if ((size_t)n < d->maxsiz - d->len) {
		d->len += n;
		return;
	}

	/* undo the vsnprintf() and flush */
	d->buf[d->len] = '\0';
	d->ret = d->func(d->cc, d->uarg, d->buf);
	if (d->ret != 0)
		return;
	d->buf[0] = '\0';
	d->len = 0;

	va_start(ap, fmt);
	n = vsnprintf(d->buf, d->maxsiz, fmt, ap);
	va_end(ap);

	if (n < 0) {
		d->ret = CCGETERRNO();
		return;
	}
	if ((size_t)n >= d->maxsiz) {
		/* ok, truncate */
		d->len = d->maxsiz - 1;
		return;
	}
	d->len = n;
}

/*
 * Dump a SAP
 */
static void
cc_dump_sap(struct dump *d, const struct uni_sap *sap)
{
	static const char *const tagtab[] = {
		[UNISVE_ABSENT] =	"absent",
		[UNISVE_ANY] =		"any",
		[UNISVE_PRESENT] =	"present"
	};
	static const char *const plantab[] = {
		[UNI_ADDR_E164] =	"E164",
		[UNI_ADDR_ATME] =	"ATME",
	};
	static const char *const hlitab[] = {
		[UNI_BHLI_ISO] =	"ISO",
		[UNI_BHLI_VENDOR] =	"VENDOR",
		[UNI_BHLI_USER] =	"USER"
	};
	u_int i;

	cc_dumpf(d, "  sap(%p):\n", sap);
	cc_dumpf(d, "    addr=%s", tagtab[sap->addr.tag]);
	if (sap->addr.tag == UNISVE_PRESENT) {
		cc_dumpf(d, " %s %u ", plantab[sap->addr.plan], sap->addr.len);
		if (sap->addr.plan == UNI_ADDR_E164)
			for (i = 0; i < sap->addr.len; i++)
				cc_dumpf(d, "%c", sap->addr.addr[i]);
		else
			for (i = 0; i < sap->addr.len; i++)
				cc_dumpf(d, "%02x", sap->addr.addr[i]);
	}
	cc_dumpf(d, "\n");

	cc_dumpf(d, "    selector=%s", tagtab[sap->selector.tag]);
	if (sap->selector.tag == UNISVE_PRESENT)
		cc_dumpf(d, " %02x", sap->selector.selector);
	cc_dumpf(d, "\n");

	cc_dumpf(d, "    blli_id2=%s", tagtab[sap->blli_id2.tag]);
	if (sap->blli_id2.tag == UNISVE_PRESENT)
		cc_dumpf(d, " %02x %02x", sap->blli_id2.proto,
		    sap->blli_id2.user);
	cc_dumpf(d, "\n");

	cc_dumpf(d, "    blli_id3=%s", tagtab[sap->blli_id3.tag]);
	if (sap->blli_id3.tag == UNISVE_PRESENT)
		cc_dumpf(d, " %02x,%02x, %02x(%d),%03x,%02x",
		    sap->blli_id3.proto, sap->blli_id3.user,
		    sap->blli_id3.ipi, sap->blli_id3.noipi,
		    sap->blli_id3.oui, sap->blli_id3.pid);
	cc_dumpf(d, "\n");

	cc_dumpf(d, "    bhli=%s", tagtab[sap->bhli.tag]);
	if (sap->bhli.tag == UNISVE_PRESENT) {
		cc_dumpf(d, " %s ", hlitab[sap->bhli.type]);
		for (i = 0; i < sap->bhli.len; i++)
			cc_dumpf(d, "%02x", sap->bhli.info[i]);
	}
	cc_dumpf(d, "\n");
}

/*
 * Dump a user.
 */
static void
cc_dump_user(struct dump *d, const struct ccuser *user)
{
	struct ccconn *conn;

	cc_dumpf(d, "user(%p): %s '%s' %s\n", user,
	    cc_user_state2str(user->state), user->name,
	    (user->config == USER_P2P) ? "p2p" :
	    (user->config == USER_ROOT) ? "root" :
	    (user->config == USER_LEAF) ? "leaf" : "?");
	if (user->sap)
		cc_dump_sap(d, user->sap);

	cc_dumpf(d, "  queue=%u/%u accepted=%p aborted=%u\n", user->queue_max,
	    user->queue_act, user->accepted, user->aborted);

	cc_dumpf(d, "  connq:");
	TAILQ_FOREACH(conn, &user->connq, connq_link)
		cc_dumpf(d, "%p", conn);
	cc_dumpf(d, "\n");
}

/*
 * Dump a party
 */
static void
cc_dump_party(struct dump *d, const struct ccparty *party, const char *pfx)
{

	cc_dumpf(d, "%s  party(%p): %u.%u %s\n", pfx, party,
	    party->epref.flag, party->epref.epref,
	    cc_party_state2str(party->state));
}

/*
 * Dump a connection
 */
static void
cc_dump_conn(struct dump *d, const struct ccconn *conn, const char *pfx)
{
	const struct ccparty *party;

	cc_dumpf(d, "%sconn(%p): %s\n", pfx, conn,
	    cc_conn_state2str(conn->state));
	cc_dumpf(d, "%s  user=%p cref=%u.%u acceptor=%p\n", pfx,
	    conn->user, conn->cref.cref, conn->cref.flag,
	    conn->acceptor);

	cc_dumpf(d, "%s  blli_sel=%u\n", pfx, conn->blli_selector);

	LIST_FOREACH(party, &conn->parties, link)
		cc_dump_party(d, party, pfx);
}

/*
 * Dump a port
 */
static void
cc_dump_port(struct dump *d, const struct ccport *p)
{
	u_int i;
	const struct ccaddr *a;
	const struct ccconn *c;
	const struct ccreq *r;

	static const char *const ttab[] = {
		[UNI_ADDR_UNKNOWN] =		"unknown",
		[UNI_ADDR_INTERNATIONAL] =	"international",
		[UNI_ADDR_NATIONAL] =		"national",
		[UNI_ADDR_NETWORK] =		"network",
		[UNI_ADDR_SUBSCR] =		"subscr",
		[UNI_ADDR_ABBR] =		"abbr",
	};
	static const char *const ptab[] = {
		[UNI_ADDR_UNKNOWN] =	"unknown",
		[UNI_ADDR_E164] =	"e164",
		[UNI_ADDR_ATME] =	"atme",
		[UNI_ADDR_DATA] =	"data",
		[UNI_ADDR_PRIVATE] =	"private",
	};

	cc_dumpf(d, "port(%p) %u: %s\n", p, p->param.port,
	    (p->admin == CCPORT_STOPPED) ? "STOPPED" :
	    (p->admin == CCPORT_RUNNING) ? "RUNNING" : "????");
	cc_dumpf(d, "  pcr=%u bits=%u.%u ids=%u/%u/%u esi=%02x:%02x:"
	    "%02x:%02x:%02x:%02x naddrs=%u\n", p->param.pcr,
	    p->param.max_vpi_bits, p->param.max_vci_bits, p->param.max_svpc_vpi,
	    p->param.max_svcc_vpi, p->param.min_svcc_vci, p->param.esi[0],
	    p->param.esi[1], p->param.esi[2], p->param.esi[3], p->param.esi[4],
	    p->param.esi[5], p->param.num_addrs);

	cc_dumpf(d, "  cookies:");
	TAILQ_FOREACH(r, &p->cookies, link)
		cc_dumpf(d, " %u(%p,%u)", r->cookie, r->conn, r->req);
	cc_dumpf(d, "\n");

	TAILQ_FOREACH(a, &p->addr_list, port_link) {
		cc_dumpf(d, "  addr(%p): %s %s %u ", a,
		    (a->addr.type < sizeof(ttab) / sizeof(ttab[0]) &&
		    ttab[a->addr.type] != NULL) ? ttab[a->addr.type] : "?", 
		    (a->addr.plan < sizeof(ptab) / sizeof(ptab[0]) &&
		    ptab[a->addr.plan] != NULL) ? ptab[a->addr.plan] : "?", 
		    a->addr.len);
		for (i = 0; i < a->addr.len; i++)
			cc_dumpf(d, "%02x", a->addr.addr[i]);
		cc_dumpf(d, "\n");
	}
	LIST_FOREACH(c, &p->conn_list, port_link)
		cc_dump_conn(d, c, "  ");
}

/*
 * Produce a textual dump of the state
 */
int
cc_dump(struct ccdata *cc, size_t maxsiz, cc_dump_f func, void *uarg)
{
	struct dump d;
	struct ccuser *user;
	struct ccconn *conn;
	struct ccport *port;

	d.ret = 0;
	d.uarg = uarg;
	d.maxsiz = maxsiz;
	d.cc = cc;
	d.func = func;
	d.buf = CCMALLOC(maxsiz);
	if (d.buf == NULL)
		return (ENOMEM);
	d.len = 0;

	cc_dumpf(&d, "dump of node %p\n", cc);

	TAILQ_FOREACH(port, &cc->port_list, node_link)
		cc_dump_port(&d, port);

	LIST_FOREACH(user, &cc->user_list, node_link)
		cc_dump_user(&d, user);

	cc_dumpf(&d, "orphaned conns:\n");
	LIST_FOREACH(conn, &cc->orphaned_conns, port_link)
		cc_dump_conn(&d, conn, "");

	if (d.len > 0 && d.ret == 0)
		d.ret = d.func(d.cc, d.uarg, d.buf);

	CCFREE(d.buf);
	return (d.ret);
}
