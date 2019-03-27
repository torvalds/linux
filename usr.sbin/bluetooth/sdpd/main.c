/*-
 * main.c
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: main.c,v 1.8 2004/01/13 19:31:54 max Exp $
 * $FreeBSD$
 */

#include <sys/select.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "server.h"

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include "profile.h"
#include "provider.h"

#define	SDPD			"sdpd"

static int32_t 	drop_root	(char const *user, char const *group);
static void	sighandler	(int32_t s);
static void	usage		(void);

static int32_t	done;

/*
 * Bluetooth Service Discovery Procotol (SDP) daemon
 */

int
main(int argc, char *argv[])
{
	server_t		 server;
	char const		*control = SDP_LOCAL_PATH;
	char const		*user = "nobody", *group = "nobody";
	int32_t			 detach = 1, opt;
	struct sigaction	 sa;

	while ((opt = getopt(argc, argv, "c:dg:hu:")) != -1) {
		switch (opt) {
		case 'c': /* control */
			control = optarg;
			break;

		case 'd': /* do not detach */
			detach = 0;
			break;

		case 'g': /* group */
			group = optarg;
			break;

		case 'u': /* user */
			user = optarg;
			break;

		case 'h':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	log_open(SDPD, !detach);

	/* Become daemon if required */
	if (detach && daemon(0, 0) < 0) {
		log_crit("Could not become daemon. %s (%d)",
			strerror(errno), errno);
		exit(1);
	}

	/* Set signal handlers */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sighandler;

	if (sigaction(SIGTERM, &sa, NULL) < 0 ||
	    sigaction(SIGHUP,  &sa, NULL) < 0 ||
	    sigaction(SIGINT,  &sa, NULL) < 0) {
		log_crit("Could not install signal handlers. %s (%d)",
			strerror(errno), errno); 
		exit(1);
	}

	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) < 0) {
		log_crit("Could not install signal handlers. %s (%d)",
			strerror(errno), errno); 
		exit(1);
	}

	/* Initialize server */
	if (server_init(&server, control) < 0)
		exit(1);

	if ((user != NULL || group != NULL) && drop_root(user, group) < 0)
		exit(1);

	for (done = 0; !done; ) {
		if (server_do(&server) != 0)
			done ++;
	}

	server_shutdown(&server);
	log_close();

	return (0);
}

/*
 * Drop root
 */

static int32_t
drop_root(char const *user, char const *group)
{
	int	 uid, gid;
	char	*ep;

	if ((uid = getuid()) != 0) {
		log_notice("Cannot set uid/gid. Not a superuser");
		return (0); /* dont do anything unless root */
	}

	gid = getgid();

	if (user != NULL) {
		uid = strtol(user, &ep, 10);
		if (*ep != '\0') {
			struct passwd	*pwd = getpwnam(user);

			if (pwd == NULL) {
				log_err("Could not find passwd entry for " \
					"user %s", user);
				return (-1);
			}

			uid = pwd->pw_uid;
		}
	}

	if (group != NULL) {
		gid = strtol(group, &ep, 10);
		if (*ep != '\0') {
			struct group	*grp = getgrnam(group);

			if (grp == NULL) {
				log_err("Could not find group entry for " \
					"group %s", group);
				return (-1);
			}

			gid = grp->gr_gid;
		}
	}

	if (setgid(gid) < 0) {
		log_err("Could not setgid(%s). %s (%d)",
			group, strerror(errno), errno);
		return (-1);
	}

	if (setuid(uid) < 0) {
		log_err("Could not setuid(%s). %s (%d)",
			user, strerror(errno), errno);
		return (-1);
	}

	return (0);
}

/*
 * Signal handler
 */

static void
sighandler(int32_t s)
{
	log_notice("Got signal %d. Total number of signals received %d",
		s, ++ done);
}

/*
 * Display usage information and quit
 */

static void
usage(void)
{
	fprintf(stderr,
"Usage: %s [options]\n" \
"Where options are:\n" \
"	-c	specify control socket name (default %s)\n" \
"	-d	do not detach (run in foreground)\n" \
"	-g grp	specify group\n" \
"	-h	display usage and exit\n" \
"	-u usr	specify user\n",
		SDPD, SDP_LOCAL_PATH);
	exit(255);
}

