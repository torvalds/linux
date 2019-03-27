/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if 0
#ifndef lint
static char sccsid[] = "@(#)unix.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Display protocol blocks in the unix domain.
 */
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#define	_WANT_SOCKET
#include <sys/socketvar.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/un.h>
#define	_WANT_UNPCB
#include <sys/unpcb.h>

#include <netinet/in.h>

#include <errno.h>
#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <strings.h>
#include <kvm.h>
#include <libxo/xo.h>
#include "netstat.h"

static	void unixdomainpr(struct xunpcb *, struct xsocket *);

static	const char *const socktype[] =
    { "#0", "stream", "dgram", "raw", "rdm", "seqpacket" };

static int
pcblist_sysctl(int type, char **bufp)
{
	char 	*buf;
	size_t	len;
	char mibvar[sizeof "net.local.seqpacket.pcblist"];

	snprintf(mibvar, sizeof(mibvar), "net.local.%s.pcblist", socktype[type]);

	len = 0;
	if (sysctlbyname(mibvar, 0, &len, 0, 0) < 0) {
		if (errno != ENOENT)
			xo_warn("sysctl: %s", mibvar);
		return (-1);
	}
	if ((buf = malloc(len)) == NULL) {
		xo_warnx("malloc %lu bytes", (u_long)len);
		return (-2);
	}
	if (sysctlbyname(mibvar, buf, &len, 0, 0) < 0) {
		xo_warn("sysctl: %s", mibvar);
		free(buf);
		return (-2);
	}
	*bufp = buf;
	return (0);
}

static int
pcblist_kvm(u_long count_off, u_long gencnt_off, u_long head_off, char **bufp)
{
	struct unp_head head;
	struct unpcb *unp, unp0, unp_conn;
	u_char sun_len;
	struct socket so;
	struct xunpgen xug;
	struct xunpcb xu;
	unp_gen_t unp_gencnt;
	u_int	unp_count;
	char 	*buf, *p;
	size_t	len;

	if (count_off == 0 || gencnt_off == 0)
		return (-2);
	if (head_off == 0)
		return (-1);
	kread(count_off, &unp_count, sizeof(unp_count));
	len = 2 * sizeof(xug) + (unp_count + unp_count / 8) * sizeof(xu);
	if ((buf = malloc(len)) == NULL) {
		xo_warnx("malloc %lu bytes", (u_long)len);
		return (-2);
	}
	p = buf;

#define	COPYOUT(obj, size) do {						\
	if (len < (size)) {						\
		xo_warnx("buffer size exceeded");			\
		goto fail;						\
	}								\
	bcopy((obj), p, (size));					\
	len -= (size);							\
	p += (size);							\
} while (0)

#define	KREAD(off, buf, len) do {					\
	if (kread((uintptr_t)(off), (buf), (len)) != 0)			\
		goto fail;						\
} while (0)

	/* Write out header. */
	kread(gencnt_off, &unp_gencnt, sizeof(unp_gencnt));
	xug.xug_len = sizeof xug;
	xug.xug_count = unp_count;
	xug.xug_gen = unp_gencnt;
	xug.xug_sogen = 0;
	COPYOUT(&xug, sizeof xug);

	/* Walk the PCB list. */
	xu.xu_len = sizeof xu;
	KREAD(head_off, &head, sizeof(head));
	LIST_FOREACH(unp, &head, unp_link) {
		xu.xu_unpp = (uintptr_t)unp;
		KREAD(unp, &unp0, sizeof (*unp));
		unp = &unp0;

		if (unp->unp_gencnt > unp_gencnt)
			continue;
		if (unp->unp_addr != NULL) {
			KREAD(unp->unp_addr, &sun_len, sizeof(sun_len));
			KREAD(unp->unp_addr, &xu.xu_addr, sun_len);
		}
		if (unp->unp_conn != NULL) {
			KREAD(unp->unp_conn, &unp_conn, sizeof(unp_conn));
			if (unp_conn.unp_addr != NULL) {
				KREAD(unp_conn.unp_addr, &sun_len,
				    sizeof(sun_len));
				KREAD(unp_conn.unp_addr, &xu.xu_caddr, sun_len);
			}
		}
		KREAD(unp->unp_socket, &so, sizeof(so));
		if (sotoxsocket(&so, &xu.xu_socket) != 0)
			goto fail;
		COPYOUT(&xu, sizeof(xu));
	}

	/* Reread the counts and write the footer. */
	kread(count_off, &unp_count, sizeof(unp_count));
	kread(gencnt_off, &unp_gencnt, sizeof(unp_gencnt));
	xug.xug_count = unp_count;
	xug.xug_gen = unp_gencnt;
	COPYOUT(&xug, sizeof xug);

	*bufp = buf;
	return (0);

fail:
	free(buf);
	return (-1);
#undef COPYOUT
#undef KREAD
}

void
unixpr(u_long count_off, u_long gencnt_off, u_long dhead_off, u_long shead_off,
    u_long sphead_off, bool *first)
{
	char 	*buf;
	int	ret, type;
	struct	xsocket *so;
	struct	xunpgen *xug, *oxug;
	struct	xunpcb *xunp;
	u_long	head_off;

	buf = NULL;
	for (type = SOCK_STREAM; type <= SOCK_SEQPACKET; type++) {
		if (live)
			ret = pcblist_sysctl(type, &buf);
		else {
			head_off = 0;
			switch (type) {
			case SOCK_STREAM:
				head_off = shead_off;
				break;

			case SOCK_DGRAM:
				head_off = dhead_off;
				break;

			case SOCK_SEQPACKET:
				head_off = sphead_off;
				break;
			}
			ret = pcblist_kvm(count_off, gencnt_off, head_off,
			    &buf);
		}
		if (ret == -1)
			continue;
		if (ret < 0)
			return;

		oxug = xug = (struct xunpgen *)buf;
		for (xug = (struct xunpgen *)((char *)xug + xug->xug_len);
		    xug->xug_len > sizeof(struct xunpgen);
		    xug = (struct xunpgen *)((char *)xug + xug->xug_len)) {
			xunp = (struct xunpcb *)xug;
			so = &xunp->xu_socket;

			/* Ignore PCBs which were freed during copyout. */
			if (xunp->unp_gencnt > oxug->xug_gen)
				continue;
			if (*first) {
				xo_open_list("socket");
				*first = false;
			}
			xo_open_instance("socket");
			unixdomainpr(xunp, so);
			xo_close_instance("socket");
		}
		if (xug != oxug && xug->xug_gen != oxug->xug_gen) {
			if (oxug->xug_count > xug->xug_count) {
				xo_emit("Some {:type/%s} sockets may have "
				    "been {:action/deleted}.\n",
				    socktype[type]);
			} else if (oxug->xug_count < xug->xug_count) {
				xo_emit("Some {:type/%s} sockets may have "
				    "been {:action/created}.\n",
				    socktype[type]);
			} else {
				xo_emit("Some {:type/%s} sockets may have "
				    "been {:action/created or deleted}",
				    socktype[type]);
			}
		}
		free(buf);
	}
}

static void
unixdomainpr(struct xunpcb *xunp, struct xsocket *so)
{
	struct sockaddr_un *sa;
	static int first = 1;
	char buf1[33];
	static const char *titles[2] = {
	    "{T:/%-8.8s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} {T:/%8.8s} "
	    "{T:/%8.8s} {T:/%8.8s} {T:/%8.8s} {T:Addr}\n",
	    "{T:/%-16.16s} {T:/%-6.6s} {T:/%-6.6s} {T:/%-6.6s} {T:/%16.16s} "
	    "{T:/%16.16s} {T:/%16.16s} {T:/%16.16s} {T:Addr}\n"
	};
	static const char *format[2] = {
	    "{q:address/%8lx} {t:type/%-6.6s} "
	    "{:receive-bytes-waiting/%6u} "
	    "{:send-bytes-waiting/%6u} "
	    "{q:vnode/%8lx} {q:connection/%8lx} "
	    "{q:first-reference/%8lx} {q:next-reference/%8lx}",
	    "{q:address/%16lx} {t:type/%-6.6s} "
	    "{:receive-bytes-waiting/%6u} "
	    "{:send-bytes-waiting/%6u} "
	    "{q:vnode/%16lx} {q:connection/%16lx} "
	    "{q:first-reference/%16lx} {q:next-reference/%16lx}"
	};
	int fmt = (sizeof(void *) == 8) ? 1 : 0;

	sa = (xunp->xu_addr.sun_family == AF_UNIX) ? &xunp->xu_addr : NULL;

	if (first && !Lflag) {
		xo_emit("{T:Active UNIX domain sockets}\n");
		xo_emit(titles[fmt],
		    "Address", "Type", "Recv-Q", "Send-Q",
		    "Inode", "Conn", "Refs", "Nextref");
		first = 0;
	}

	if (Lflag && so->so_qlimit == 0)
		return;

	if (Lflag) {
		snprintf(buf1, sizeof buf1, "%u/%u/%u", so->so_qlen,
		    so->so_incqlen, so->so_qlimit);
		xo_emit("unix  {d:socket/%-32.32s}{e:queue-length/%u}"
		    "{e:incomplete-queue-length/%u}{e:queue-limit/%u}",
		    buf1, so->so_qlen, so->so_incqlen, so->so_qlimit);
	} else {
		xo_emit(format[fmt],
		    (long)so->so_pcb, socktype[so->so_type], so->so_rcv.sb_cc,
		    so->so_snd.sb_cc, (long)xunp->unp_vnode,
		    (long)xunp->unp_conn, (long)xunp->xu_firstref,
		    (long)xunp->xu_nextref);
	}
	if (sa)
		xo_emit(" {:path/%.*s}",
		    (int)(sa->sun_len - offsetof(struct sockaddr_un, sun_path)),
		    sa->sun_path);
	xo_emit("\n");
}
