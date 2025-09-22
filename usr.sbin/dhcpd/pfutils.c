/*	$OpenBSD: pfutils.c,v 1.24 2023/02/08 08:20:53 tb Exp $ */
/*
 * Copyright (c) 2006 Chris Kuethe <ckuethe@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"

extern struct passwd *pw;
extern int pfpipe[2];
extern int gotpipe;
extern char *abandoned_tab;
extern char *changedmac_tab;
extern char *leased_tab;

__dead void
pftable_handler(void)
{
	struct pf_cmd cmd;
	struct pollfd pfd[1];
	int l, r, fd, nfds;

	if ((fd = open(_PATH_DEV_PF, O_RDWR|O_NOFOLLOW)) == -1)
		fatal("can't open pf device");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	/* no filesystem visibility */
	if (unveil("/", "") == -1)
		fatal("unveil /");
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	setproctitle("pf table handler");
	l = sizeof(struct pf_cmd);

	for (;;) {
		pfd[0].fd = pfpipe[0];
		pfd[0].events = POLLIN;
		if ((nfds = poll(pfd, 1, -1)) == -1)
			if (errno != EINTR)
				log_warn("poll");

		if (nfds > 0 && (pfd[0].revents & POLLHUP))
			fatalx("pf pipe closed");

		if (nfds > 0 && (pfd[0].revents & POLLIN)) {
			memset(&cmd, 0, l);
			r = atomicio(read, pfpipe[0], &cmd, l);

			if (r != l)
				fatalx("pf pipe error");

			switch (cmd.type) {
			case 'A':
				/*
				 * When we abandon an address, we add it to
				 * the table of abandoned addresses, and remove
				 * it from the table of active leases.
				 */
				pf_change_table(fd, 1, cmd.ip, abandoned_tab);
				pf_change_table(fd, 0, cmd.ip, leased_tab);
				pf_kill_state(fd, cmd.ip);
				break;
			case 'C':
				/*
				 * When the hardware address for an IP changes,
				 * remove it from the table of abandoned
				 * addresses, and from the table of overloaded
				 * addresses.
				 */
				pf_change_table(fd, 0, cmd.ip, abandoned_tab);
				pf_change_table(fd, 0, cmd.ip, changedmac_tab);
				break;
			case 'L':
				/*
				 * When a lease is granted or renewed, remove
				 * it from the table of abandoned addresses,
				 * and ensure it is in the table of active
				 * leases.
				 */
				pf_change_table(fd, 0, cmd.ip, abandoned_tab);
				pf_change_table(fd, 1, cmd.ip, leased_tab);
				break;
			case 'R':
				/*
				 * When we release or expire a lease, remove
				 * it from the table of active leases. As long
				 * as dhcpd doesn't abandon the address, no
				 * further action is required.
				 */
				pf_change_table(fd, 0, cmd.ip, leased_tab);
				break;
			default:
				break;
			}
		}
	}
	/* not reached */
	exit(1);
}

/* inspired by ("stolen") from usr.sbin/authpf/authpf.c */
void
pf_change_table(int fd, int op, struct in_addr ip, char *table)
{
	struct pfioc_table	io;
	struct pfr_addr		addr;

	if (table == NULL)
		return;

	memset(&io, 0, sizeof(io));
	strlcpy(io.pfrio_table.pfrt_name, table,
	    sizeof(io.pfrio_table.pfrt_name));
	io.pfrio_buffer = &addr;
	io.pfrio_esize = sizeof(addr);
	io.pfrio_size = 1;

	memset(&addr, 0, sizeof(addr));
	memcpy(&addr.pfra_ip4addr, &ip, 4);
	addr.pfra_af = AF_INET;
	addr.pfra_net = 32;

	if (ioctl(fd, op ? DIOCRADDADDRS : DIOCRDELADDRS, &io) == -1 &&
	    errno != ESRCH) {
		log_warn( "DIOCR%sADDRS on table %s", op ? "ADD" : "DEL",
		    table);
	}
}

void
pf_kill_state(int fd, struct in_addr ip)
{
	struct pfioc_state_kill	psk;
	struct pf_addr target;

	memset(&psk, 0, sizeof(psk));
	memset(&target, 0, sizeof(target));

	memcpy(&target.v4, &ip.s_addr, 4);
	psk.psk_af = AF_INET;

	/* Kill all states from target */
	memcpy(&psk.psk_src.addr.v.a.addr, &target,
	    sizeof(psk.psk_src.addr.v.a.addr));
	memset(&psk.psk_src.addr.v.a.mask, 0xff,
	    sizeof(psk.psk_src.addr.v.a.mask));
	if (ioctl(fd, DIOCKILLSTATES, &psk) == -1) {
		log_warn("DIOCKILLSTATES failed");
	}

	/* Kill all states to target */
	memset(&psk.psk_src, 0, sizeof(psk.psk_src));
	memcpy(&psk.psk_dst.addr.v.a.addr, &target,
	    sizeof(psk.psk_dst.addr.v.a.addr));
	memset(&psk.psk_dst.addr.v.a.mask, 0xff,
	    sizeof(psk.psk_dst.addr.v.a.mask));
	if (ioctl(fd, DIOCKILLSTATES, &psk) == -1) {
		log_warn("DIOCKILLSTATES failed");
	}
}

/* inspired by ("stolen") from usr.bin/ssh/atomicio.c */
size_t
atomicio(ssize_t (*f) (int, void *, size_t), int fd, void *_s, size_t n)
{
	char *s = _s;
	size_t pos = 0;
	ssize_t res;

	while (n > pos) {
		res = (f) (fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return 0;
		case 0:
			errno = EPIPE;
			return pos;
		default:
			pos += (size_t)res;
		}
	}
	return (pos);
}

/*
 * This function sends commands to the pf table handler. It will safely and
 * silently return if the handler is unconfigured, therefore it can be called
 * on all interesting lease events, whether or not the user actually wants to
 * use the pf table feature.
 */
void
pfmsg(char c, struct lease *lp)
{
	struct pf_cmd cmd;

	if (gotpipe == 0)
		return;

	cmd.type = c;
	memcpy(&cmd.ip.s_addr, lp->ip_addr.iabuf, 4);

	switch (c) {
	case 'A': /* address is being abandoned */
		/* FALLTHROUGH */
	case 'C': /* IP moved to different ethernet address */
		/* FALLTHROUGH */
	case 'L': /* Address is being leased (unabandoned) */
		/* FALLTHROUGH */
	case 'R': /* Address is being released or lease has expired */
		(void)atomicio(vwrite, pfpipe[1], &cmd,
		    sizeof(struct pf_cmd));
		break;
	default: /* silently ignore unknown commands */
		break;
	}
}
