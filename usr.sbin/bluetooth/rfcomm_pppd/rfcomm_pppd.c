/*
 * rfcomm_pppd.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2008 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: rfcomm_pppd.c,v 1.5 2003/09/07 18:32:11 max Exp $
 * $FreeBSD$
 */
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sdp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define RFCOMM_PPPD	"rfcomm_pppd"

int		rfcomm_channel_lookup	(bdaddr_t const *local,
					 bdaddr_t const *remote,
					 int service, int *channel, int *error);

static void	exec_ppp	(int s, char *unit, char *label);
static void	sighandler	(int s);
static void	usage		(void);

static int	done;

/* Main */
int
main(int argc, char *argv[])
{
	struct sockaddr_rfcomm   sock_addr;
	char			*label = NULL, *unit = NULL, *ep = NULL;
	bdaddr_t		 addr;
	int			 s, channel, detach, server, service,
				 regdun, regsp;
	pid_t			 pid;

	memcpy(&addr, NG_HCI_BDADDR_ANY, sizeof(addr));
	channel = 0;
	detach = 1;
	server = 0;
	service = 0;
	regdun = 0;
	regsp = 0;

	/* Parse command line arguments */
	while ((s = getopt(argc, argv, "a:cC:dDhl:sSu:")) != -1) {
		switch (s) {
		case 'a': /* BDADDR */
			if (!bt_aton(optarg, &addr)) {
				struct hostent	*he = NULL;

				if ((he = bt_gethostbyname(optarg)) == NULL)
					errx(1, "%s: %s", optarg, hstrerror(h_errno));

				memcpy(&addr, he->h_addr, sizeof(addr));
			}
			break;

		case 'c': /* client */
			server = 0;
			break;

		case 'C': /* RFCOMM channel */
			channel = strtoul(optarg, &ep, 10);
			if (*ep != '\0') {
				channel = 0;
				switch (tolower(optarg[0])) {
				case 'd': /* DialUp Networking */
					service = SDP_SERVICE_CLASS_DIALUP_NETWORKING;
					break;

				case 'l': /* LAN Access Using PPP */
					service = SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP;
					break;
				}
			}
			break;

		case 'd': /* do not detach */
			detach = 0;
			break;

		case 'D': /* Register DUN service as well as LAN service */
			regdun = 1;
			break;

		case 'l': /* PPP label */
			label = optarg;
			break;

		case 's': /* server */
			server = 1;
			break;

		case 'S': /* Register SP service as well as LAN service */
			regsp = 1;
			break;

		case 'u': /* PPP -unit option */
			strtoul(optarg, &ep, 10);
			if (*ep != '\0')
				usage();
				/* NOT REACHED */

			unit = optarg;
			break;

		case 'h':
		default:
			usage();
			/* NOT REACHED */
		}
	}

	/* Check if we got everything we wanted */
	if (label == NULL)
                errx(1, "Must specify PPP label");

	if (!server) {
		if (memcmp(&addr, NG_HCI_BDADDR_ANY, sizeof(addr)) == 0)
                	errx(1, "Must specify server BD_ADDR");

		/* Check channel, if was not set then obtain it via SDP */
		if (channel == 0 && service != 0)
			if (rfcomm_channel_lookup(NULL, &addr, service,
							&channel, &s) != 0)
				errc(1, s, "Could not obtain RFCOMM channel");
	}

        if (channel <= 0 || channel > 30)
                errx(1, "Invalid RFCOMM channel number %d", channel);

	openlog(RFCOMM_PPPD, LOG_PID | LOG_PERROR | LOG_NDELAY, LOG_USER);

	if (detach && daemon(0, 0) < 0) {
		syslog(LOG_ERR, "Could not daemon(0, 0). %s (%d)",
			strerror(errno), errno);
		exit(1);
	}

	s = socket(PF_BLUETOOTH, SOCK_STREAM, BLUETOOTH_PROTO_RFCOMM);
	if (s < 0) {
		syslog(LOG_ERR, "Could not create socket. %s (%d)",
			strerror(errno), errno);
		exit(1);
	}

	if (server) {
		struct sigaction	 sa;
		void			*ss = NULL;
		sdp_lan_profile_t	 lan;

		/* Install signal handler */
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sighandler;

		if (sigaction(SIGTERM, &sa, NULL) < 0) {
			syslog(LOG_ERR, "Could not sigaction(SIGTERM). %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		if (sigaction(SIGHUP, &sa, NULL) < 0) {
			syslog(LOG_ERR, "Could not sigaction(SIGHUP). %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		if (sigaction(SIGINT, &sa, NULL) < 0) {
			syslog(LOG_ERR, "Could not sigaction(SIGINT). %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		sa.sa_handler = SIG_IGN;
		sa.sa_flags = SA_NOCLDWAIT;

		if (sigaction(SIGCHLD, &sa, NULL) < 0) {
			syslog(LOG_ERR, "Could not sigaction(SIGCHLD). %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		/* bind socket and listen for incoming connections */
		sock_addr.rfcomm_len = sizeof(sock_addr);
		sock_addr.rfcomm_family = AF_BLUETOOTH;
		memcpy(&sock_addr.rfcomm_bdaddr, &addr,
			sizeof(sock_addr.rfcomm_bdaddr));
		sock_addr.rfcomm_channel = channel;

		if (bind(s, (struct sockaddr *) &sock_addr,
				sizeof(sock_addr)) < 0) {
			syslog(LOG_ERR, "Could not bind socket. %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		if (listen(s, 10) < 0) {
			syslog(LOG_ERR, "Could not listen on socket. %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		ss = sdp_open_local(NULL);
		if (ss == NULL) {
			syslog(LOG_ERR, "Unable to create local SDP session");
			exit(1);
		}

		if (sdp_error(ss) != 0) {
			syslog(LOG_ERR, "Unable to open local SDP session. " \
				"%s (%d)", strerror(sdp_error(ss)),
				sdp_error(ss));
			exit(1);
		}

		memset(&lan, 0, sizeof(lan));
		lan.server_channel = channel;

		if (sdp_register_service(ss,
				SDP_SERVICE_CLASS_LAN_ACCESS_USING_PPP,
				&addr, (void *) &lan, sizeof(lan), NULL) != 0) {
			syslog(LOG_ERR, "Unable to register LAN service with " \
				"local SDP daemon. %s (%d)",
				strerror(sdp_error(ss)), sdp_error(ss));
			exit(1);
		}

		/*
		 * Register DUN (Dial-Up Networking) service on the same
		 * RFCOMM channel if requested. There is really no good reason
		 * to not to support this. AT-command exchange can be faked
		 * with chat script in ppp.conf
		 */

		if (regdun) {
			sdp_dun_profile_t	dun;

			memset(&dun, 0, sizeof(dun));
			dun.server_channel = channel;

			if (sdp_register_service(ss,
					SDP_SERVICE_CLASS_DIALUP_NETWORKING,
					&addr, (void *) &dun, sizeof(dun),
					NULL) != 0) {
				syslog(LOG_ERR, "Unable to register DUN " \
					"service with local SDP daemon. " \
					"%s (%d)", strerror(sdp_error(ss)),
					sdp_error(ss));
				exit(1);
			}
		}

		/*
		 * Register SP (Serial Port) service on the same RFCOMM channel
		 * if requested. It appears that some cell phones are using so
		 * called "callback mechanism". In this scenario user is trying
		 * to connect his cell phone to the Internet, and, user's host
		 * computer is acting as the gateway server. It seems that it
		 * is not possible to tell the phone to just connect and start
		 * using the LAN service. Instead the user's host computer must
		 * "jump start" the phone by connecting to the phone's SP
		 * service. What happens next is the phone kills the existing
		 * connection and opens another connection back to the user's
		 * host computer. The phone really wants to use LAN service,
		 * but for whatever reason it looks for SP service on the
		 * user's host computer. This brain damaged behavior was
		 * reported for Nokia 6600 and Sony/Ericsson P900. Both phones
		 * are Symbian-based phones. Perhaps this is a Symbian problem?
		 */

		if (regsp) {
			sdp_sp_profile_t	sp;

			memset(&sp, 0, sizeof(sp));
			sp.server_channel = channel;

			if (sdp_register_service(ss,
					SDP_SERVICE_CLASS_SERIAL_PORT,
					&addr, (void *) &sp, sizeof(sp),
					NULL) != 0) {
				syslog(LOG_ERR, "Unable to register SP " \
					"service with local SDP daemon. " \
					"%s (%d)", strerror(sdp_error(ss)),
					sdp_error(ss));
				exit(1);
			}
		}
		
		for (done = 0; !done; ) {
			socklen_t	len = sizeof(sock_addr);
			int		s1 = accept(s, (struct sockaddr *) &sock_addr, &len);

			if (s1 < 0) {
				syslog(LOG_ERR, "Could not accept connection " \
					"on socket. %s (%d)", strerror(errno),
					errno);
				exit(1);
			}
				
			pid = fork();
			if (pid == (pid_t) -1) {
				syslog(LOG_ERR, "Could not fork(). %s (%d)",
					strerror(errno), errno);
				exit(1);
			}

			if (pid == 0) {
				sdp_close(ss);
				close(s);

				/* Reset signal handler */
				memset(&sa, 0, sizeof(sa));
				sa.sa_handler = SIG_DFL;

				sigaction(SIGTERM, &sa, NULL);
				sigaction(SIGHUP, &sa, NULL);
				sigaction(SIGINT, &sa, NULL);
				sigaction(SIGCHLD, &sa, NULL);

				/* Become daemon */
				daemon(0, 0);

				/*
				 * XXX Make sure user does not shoot himself
				 * in the foot. Do not pass unit option to the
				 * PPP when operating in the server mode.
				 */

				exec_ppp(s1, NULL, label);
			} else
				close(s1);
		}
	} else {
		sock_addr.rfcomm_len = sizeof(sock_addr);
		sock_addr.rfcomm_family = AF_BLUETOOTH;
		memcpy(&sock_addr.rfcomm_bdaddr, NG_HCI_BDADDR_ANY,
			sizeof(sock_addr.rfcomm_bdaddr));
		sock_addr.rfcomm_channel = 0;

		if (bind(s, (struct sockaddr *) &sock_addr,
				sizeof(sock_addr)) < 0) {
			syslog(LOG_ERR, "Could not bind socket. %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		memcpy(&sock_addr.rfcomm_bdaddr, &addr,
			sizeof(sock_addr.rfcomm_bdaddr));
		sock_addr.rfcomm_channel = channel;

		if (connect(s, (struct sockaddr *) &sock_addr,
				sizeof(sock_addr)) < 0) {
			syslog(LOG_ERR, "Could not connect socket. %s (%d)",
				strerror(errno), errno);
			exit(1);
		}

		exec_ppp(s, unit, label);
	}

	exit(0);
} /* main */

/* 
 * Redirects stdin/stdout to s, stderr to /dev/null and exec
 * 'ppp -direct -quiet [-unit N] label'. Never returns.
 */

static void
exec_ppp(int s, char *unit, char *label)
{
	char	 ppp[] = "/usr/sbin/ppp";
	char	*ppp_args[] = { ppp,  "-direct", "-quiet",
				NULL, NULL,      NULL,     NULL };

	close(0);
	if (dup(s) < 0) {
		syslog(LOG_ERR, "Could not dup(0). %s (%d)",
			strerror(errno), errno);
		exit(1);
	}

	close(1);
	if (dup(s) < 0) {
		syslog(LOG_ERR, "Could not dup(1). %s (%d)",
			strerror(errno), errno);
		exit(1);
	}

	close(2);
	open("/dev/null", O_RDWR);

	if (unit != NULL) {
		ppp_args[3] = "-unit";
		ppp_args[4] = unit;
		ppp_args[5] = label;
	} else
		ppp_args[3] = label;

	if (execv(ppp, ppp_args) < 0) {
		syslog(LOG_ERR, "Could not exec(%s -direct -quiet%s%s %s). " \
			"%s (%d)", ppp, (unit != NULL)? " -unit " : "",
			(unit != NULL)? unit : "", label,
			strerror(errno), errno);
		exit(1);
	}
} /* run_ppp */

/* Signal handler */
static void
sighandler(int s)
{
	done = 1;
} /* sighandler */

/* Display usage and exit */
static void
usage(void)
{
	fprintf(stdout,
"Usage: %s options\n" \
"Where options are:\n" \
"\t-a address   Address to listen on or connect to (required for client)\n" \
"\t-c           Act as a clinet (default)\n" \
"\t-C channel   RFCOMM channel to listen on or connect to (required)\n" \
"\t-d           Run in foreground\n" \
"\t-D           Register Dial-Up Networking service (server mode only)\n" \
"\t-l label     Use PPP label (required)\n" \
"\t-s           Act as a server\n" \
"\t-S           Register Serial Port service (server mode only)\n" \
"\t-u N         Tell PPP to operate on /dev/tunN (client mode only)\n" \
"\t-h           Display this message\n", RFCOMM_PPPD);

	exit(255);
} /* usage */

