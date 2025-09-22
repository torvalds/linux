/*	$OpenBSD: dhcpd.c,v 1.63 2025/06/14 12:45:39 kn Exp $ */

/*
 * Copyright (c) 2004 Henning Brauer <henning@cvs.openbsd.org>
 * Copyright (c) 1995, 1996, 1997, 1998, 1999
 * The Internet Software Consortium.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>

#include <arpa/inet.h>

#include <err.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "dhcp.h"
#include "tree.h"
#include "dhcpd.h"
#include "log.h"
#include "sync.h"


__dead void usage(void);

time_t cur_time, last_scan;
struct group root_group;

u_int16_t server_port;
u_int16_t client_port;

int rdomain;
struct passwd *pw;
int log_priority;
int pfpipe[2];
int gotpipe = 0;
int syncrecv;
int syncsend;
pid_t pfproc_pid = -1;
char *path_dhcpd_conf = _PATH_DHCPD_CONF;
char *path_dhcpd_db = _PATH_DHCPD_DB;
char *abandoned_tab = NULL;
char *changedmac_tab = NULL;
char *leased_tab = NULL;

int
main(int argc, char *argv[])
{
	int ch, cftest = 0, udpsockmode = 0;
	int debug = 0, verbose = 0;
	char *sync_iface = NULL;
	char *sync_baddr = NULL;
	u_short sync_port = 0;
	struct servent *ent;
	struct in_addr udpaddr;

	log_init(1, LOG_DAEMON);	/* log to stderr until daemonized */
	log_setverbose(1);

	rdomain = getrtable();

	opterr = 0;
	while ((ch = getopt(argc, argv, "A:C:L:c:dfl:nu::vY:y:")) != -1)
		switch (ch) {
		case 'Y':
			syncsend = 1;
			break;
		case 'y':
			syncrecv = 1;
			break;
		}
	if (syncsend || syncrecv) {
		if ((ent = getservbyname("dhcpd-sync", "udp")) == NULL)
			errx(1, "Can't find service \"dhcpd-sync\" in "
			    "/etc/services");
		sync_port = ntohs(ent->s_port);
	}

	udpaddr.s_addr = htonl(INADDR_BROADCAST);

	optreset = optind = opterr = 1;
	while ((ch = getopt(argc, argv, "A:C:L:c:dfl:nu::vY:y:")) != -1)
		switch (ch) {
		case 'A':
			abandoned_tab = optarg;
			break;
		case 'C':
			changedmac_tab = optarg;
			break;
		case 'L':
			leased_tab = optarg;
			break;
		case 'c':
			path_dhcpd_conf = optarg;
			break;
		case 'd':
			/* FALLTHROUGH */
		case 'f':
			debug = 1;
			break;
		case 'l':
			path_dhcpd_db = optarg;
			break;
		case 'n':
			debug = 1;
			cftest = 1;
			break;
		case 'u':
			udpsockmode = 1;
			if (optarg != NULL) {
				if (inet_pton(AF_INET, optarg, &udpaddr) != 1)
					errx(1, "Cannot parse binding IP "
					    "address: %s", optarg);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case 'Y':
			if (sync_addhost(optarg, sync_port) != 0)
				sync_iface = optarg;
			syncsend = 1;
			break;
		case 'y':
			sync_baddr = optarg;
			syncrecv = 1;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	while (argc > 0) {
		struct interface_info *tmp = calloc(1, sizeof(*tmp));
		if (!tmp)
			fatalx("calloc");
		strlcpy(tmp->name, argv[0], sizeof(tmp->name));
		tmp->next = interfaces;
		interfaces = tmp;
		argc--;
		argv++;
	}

	/* Default DHCP/BOOTP ports. */
	server_port = htons(SERVER_PORT);
	client_port = htons(CLIENT_PORT);

	tzset();

	time(&cur_time);
	if (!readconf())
		fatalx("Configuration file errors encountered");

	if (cftest)
		exit(0);

	db_startup();
	if (!udpsockmode || argc > 0)
		discover_interfaces();

	if (syncsend || syncrecv) {
		if (sync_init(sync_iface, sync_baddr, sync_port) == -1)
			err(1, "sync init");
	}

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if (!debug)
		daemon(0, 0);

	if ((pw = getpwnam("_dhcp")) == NULL)
		fatalx("user \"_dhcp\" not found");

	/* don't go near /dev/pf unless we actually intend to use it */
	if ((abandoned_tab != NULL) ||
	    (changedmac_tab != NULL) ||
	    (leased_tab != NULL)){
		if (pipe(pfpipe) == -1)
			fatal("pipe");
		switch (pfproc_pid = fork()){
		case -1:
			fatal("fork");
			/* NOTREACHED */
			exit(1);
		case 0:
			/* child process. start up table engine */
			close(pfpipe[1]);
			pftable_handler();
			/* NOTREACHED */
			exit(1);
		default:
			close(pfpipe[0]);
			gotpipe = 1;
			break;
		}
	}

	if (udpsockmode)
		udpsock_startup(udpaddr);

	icmp_startup(1, lease_pinged);

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot %s", pw->pw_dir);
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (udpsockmode) {
		if (pledge("stdio inet route sendfd", NULL) == -1)
			err(1, "pledge");
	} else {
		if (pledge("stdio inet sendfd", NULL) == -1)
			err(1, "pledge");
	}

	add_timeout(cur_time + 5, periodic_scan, NULL);
	dispatch();

	/* not reached */
	exit(0);
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dfnv] [-A abandoned_ip_table]",
	    __progname);
	fprintf(stderr, " [-C changed_ip_table]\n");
	fprintf(stderr, "\t[-c config-file] [-L leased_ip_table]");
	fprintf(stderr, " [-l lease-file] [-u[bind_address]]\n");
	fprintf(stderr, "\t[-Y synctarget] [-y synclisten] [interface ...]\n");
	exit(1);
}

void
lease_pinged(struct iaddr from, u_int8_t *packet, int length)
{
	struct lease *lp;

	/*
	 * Don't try to look up a pinged lease if we aren't trying to
	 * ping one - otherwise somebody could easily make us churn by
	 * just forging repeated ICMP EchoReply packets for us to look
	 * up.
	 */
	if (!outstanding_pings)
		return;

	lp = find_lease_by_ip_addr(from);

	if (!lp) {
		log_info("unexpected ICMP Echo Reply from %s", piaddr(from));
		return;
	}

	if (!lp->state && !lp->releasing) {
		log_warnx("ICMP Echo Reply for %s arrived late or is "
		    "spurious.", piaddr(from));
		return;
	}

	/* At this point it looks like we pinged a lease and got a
	 * response, which shouldn't have happened.
	 * if it did it's either one of two two cases:
	 * 1 - we pinged this lease before offering it and
	 *     something answered, so we abandon it.
	 * 2 - we pinged this lease before releasing it
	 *     and something answered, so we don't release it.
	 */
	if (lp->releasing) {
		log_warnx("IP address %s answers a ping after sending a "
		    "release", piaddr(lp->ip_addr));
		log_warnx("Possible release spoof - Not releasing address %s",
		    piaddr(lp->ip_addr));
		lp->releasing = 0;
	} else {
		free_lease_state(lp->state, "lease_pinged");
		lp->state = NULL;
		abandon_lease(lp, "pinged before offer");
	}
	cancel_timeout(lease_ping_timeout, lp);
	--outstanding_pings;
}

void
lease_ping_timeout(void *vlp)
{
	struct lease	*lp = vlp;

	--outstanding_pings;
	if (lp->releasing) {
		lp->releasing = 0;
		release_lease(lp);
	} else
		dhcp_reply(lp);
}

/* from memory.c - needed to be able to walk the lease table */
extern struct subnet *subnets;

#define MINIMUM(a,b) (((a)<(b))?(a):(b))

void
periodic_scan(void *p)
{
	time_t x, y;
	struct subnet		*n;
	struct group		*g;
	struct shared_network	*s;
	struct lease		*l;

	/* find the shortest lease this server gives out */
	x = MINIMUM(root_group.default_lease_time, root_group.max_lease_time);
	for (n = subnets; n; n = n->next_subnet)
		for (g = n->group; g; g = g->next)
			x = MINIMUM(x, g->default_lease_time);

	/* use half of the shortest lease as the scan interval */
	y = x / 2;
	if (y < 1)
		y = 1;

	/* walk across all leases to find the exired ones */
	for (n = subnets; n; n = n->next_subnet)
		for (g = n->group; g; g = g->next)
			for (s = g->shared_network; s; s = s->next)
				for (l = s->leases; l && l->ends; l = l->next)
					if (cur_time >= l->ends)
						if (l->ends > last_scan)
							pfmsg('R', l);

	last_scan = cur_time;
	add_timeout(cur_time + y, periodic_scan, NULL);
}
