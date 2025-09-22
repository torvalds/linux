/*	$OpenBSD: sasyncd.c,v 1.28 2018/04/10 15:58:21 cheloha Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
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

/*
 * This code was written under funding by Multicom Security AB.
 */


#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "sasyncd.h"

volatile sig_atomic_t	daemon_shutdown = 0;

/* Called by signal handler for controlled daemon shutdown. */
static void
sasyncd_stop(int s)
{
	daemon_shutdown++;
}

static int
sasyncd_run(pid_t ppid)
{
	struct timespec	*timeout, ts;
	fd_set		*rfds, *wfds;
	size_t		 fdsetsize;
	int		 maxfd, n;

	n = getdtablesize();
	fdsetsize = howmany(n, NFDBITS) * sizeof(fd_mask);

	rfds = malloc(fdsetsize);
	if (!rfds) {
		log_err("malloc(%lu) failed", (unsigned long)fdsetsize);
		return -1;
	}

	wfds = malloc(fdsetsize);
	if (!wfds) {
		log_err("malloc(%lu) failed", (unsigned long)fdsetsize);
		free(rfds);
		return -1;
	}

	control_setrun();

	signal(SIGINT, sasyncd_stop);
	signal(SIGTERM, sasyncd_stop);

	timer_add("carp_undemote", CARP_DEMOTE_MAXTIME,
	    monitor_carpundemote, NULL);

	while (!daemon_shutdown) {
		memset(rfds, 0, fdsetsize);
		memset(wfds, 0, fdsetsize);
		maxfd = net_set_rfds(rfds);
		n = net_set_pending_wfds(wfds);
		if (n > maxfd)
			maxfd = n;

		pfkey_set_rfd(rfds);
		pfkey_set_pending_wfd(wfds);
		if (cfgstate.pfkey_socket + 1 > maxfd)
			maxfd = cfgstate.pfkey_socket + 1;

		carp_set_rfd(rfds);
		if (cfgstate.route_socket + 1 > maxfd)
			maxfd = cfgstate.route_socket + 1;

		timeout = &ts;
		timer_next_event(&ts);

		n = pselect(maxfd, rfds, wfds, NULL, timeout, NULL);
		if (n == -1) {
			if (errno != EINTR) {
				log_err("select()");
				sleep(1);
			}
		} else if (n) {
			net_handle_messages(rfds);
			net_send_messages(wfds);
			pfkey_read_message(rfds);
			pfkey_send_message(wfds);
			carp_read_message(rfds);
		}
		timer_run();

		/* Mostly for debugging. */
		if (getppid() != ppid) {
			log_msg(0, "sasyncd: parent died");
			daemon_shutdown++;
		}
	}
	free(rfds);
	free(wfds);
	return 0;
}

__dead static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dnv] [-c config-file]\n", __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	extern char	*__progname;
	char		*cfgfile = 0;
	int		 ch, noaction = 0;

	if (geteuid() != 0) {
		/* No point in continuing. */
		fprintf(stderr, "%s: This daemon needs to be run as root.\n",
		    __progname);
		return 1;
	}

	while ((ch = getopt(argc, argv, "c:dnv")) != -1) {
		switch (ch) {
		case 'c':
			if (cfgfile)
				usage();
			cfgfile = optarg;
			break;
		case 'd':
			cfgstate.debug++;
			break;
		case 'n':
			noaction = 1;
			break;
		case 'v':
			cfgstate.verboselevel++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	log_init(__progname);
	timer_init();

	cfgstate.runstate = INIT;
	LIST_INIT(&cfgstate.peerlist);

	cfgstate.listen_port = SASYNCD_DEFAULT_PORT;
	cfgstate.flags |= CTL_DEFAULT;

	if (!cfgfile)
		cfgfile = SASYNCD_CFGFILE;

	if (conf_parse_file(cfgfile) == 0 ) {
		if (!cfgstate.sharedkey) {
			fprintf(stderr, "config: "
			    "no shared key specified, cannot continue\n");
			exit(1);
		}
		if (!cfgstate.carp_ifname || !*cfgstate.carp_ifname) {
			fprintf(stderr, "config: "
			    "no carp interface specified, cannot continue\n");
			exit(1);
		}
	} else {
		exit(1);
	}

	if (noaction) {
		fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	carp_demote(CARP_INC, 0);

	if (carp_init())
		return 1;
	if (pfkey_init(0))
		return 1;
	if (net_init())
		return 1;

	if (!cfgstate.debug)
		if (daemon(1, 0)) {
			perror("daemon()");
			exit(1);
		}

	if (monitor_init()) {
		/* Parent, with privileges. */
		monitor_loop();
		exit(0);
	}

	/* Child, no privileges left. Run main loop. */
	sasyncd_run(getppid());

	/* Shutdown. */
	log_msg(0, "shutting down...");

	net_shutdown();
	pfkey_shutdown();
	return 0;
}
