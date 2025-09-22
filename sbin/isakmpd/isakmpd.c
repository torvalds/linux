/* $OpenBSD: isakmpd.c,v 1.109 2023/03/08 04:43:06 guenther Exp $	 */
/* $EOM: isakmpd.c,v 1.54 2000/10/05 09:28:22 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001 Håkan Olsson.  All rights reserved.
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
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <paths.h>

#include "app.h"
#include "conf.h"
#include "connection.h"
#include "init.h"
#include "libcrypto.h"
#include "log.h"
#include "message.h"
#include "monitor.h"
#include "nat_traversal.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "udp.h"
#include "udp_encap.h"
#include "ui.h"
#include "util.h"
#include "cert.h"

#include "policy.h"

static void     usage(void);

/*
 * Set if -d is given, currently just for running in the foreground and log
 * to stderr instead of syslog.
 */
int             debug = 0;

/* Set when no policy file shall be used. */
int		acquire_only = 0;

/* Set when SAs shall be deleted on shutdown. */
int		delete_sas = 1;

/*
 * If we receive a SIGHUP signal, this flag gets set to show we need to
 * reconfigure ASAP.
 */
volatile sig_atomic_t sighupped = 0;

/*
 * If we receive a USR1 signal, this flag gets set to show we need to dump
 * a report over our internal state ASAP.  The file to report to is settable
 * via the -R parameter.
 */
volatile sig_atomic_t sigusr1ed = 0;
static char    *report_file = "/var/run/isakmpd.report";

/*
 * If we receive a TERM signal, perform a "clean shutdown" of the daemon.
 * This includes to send DELETE notifications for all our active SAs.
 * Also on recv of an INT signal (Ctrl-C out of an '-d' session, typically).
 */
volatile sig_atomic_t sigtermed = 0;
void            daemon_shutdown_now(int);
void		set_slave_signals(void);
void		sanitise_stdfd(void);

/* The default path of the PID file.  */
char	       *pid_file = "/var/run/isakmpd.pid";

/* The path of the IKE packet capture log file.  */
static char    *pcap_file = 0;

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,
	    "usage: %s [-46adKLnSTv] [-c config-file] [-D class=level] [-f fifo]\n"
	    "          [-i pid-file] [-l packetlog-file] [-N udpencap-port]\n"
	    "          [-p listen-port] [-R report-file]\n",
	    __progname);
	exit(1);
}

static void
parse_args(int argc, char *argv[])
{
	int             ch;
	int             cls, level;
	int             do_packetlog = 0;

	while ((ch = getopt(argc, argv, "46ac:dD:f:i:KnN:p:Ll:R:STv")) != -1) {
		switch (ch) {
		case '4':
			bind_family |= BIND_FAMILY_INET4;
			break;

		case '6':
			bind_family |= BIND_FAMILY_INET6;
			break;

		case 'a':
			acquire_only = 1;
			break;

		case 'c':
			conf_path = optarg;
			break;

		case 'd':
			debug++;
			break;

		case 'D':
			if (sscanf(optarg, "%d=%d", &cls, &level) != 2) {
				if (sscanf(optarg, "A=%d", &level) == 1) {
					for (cls = 0; cls < LOG_ENDCLASS;
					    cls++)
						log_debug_cmd(cls, level);
				} else
					log_print("parse_args: -D argument "
					    "unparseable: %s", optarg);
			} else
				log_debug_cmd(cls, level);
			break;

		case 'f':
			ui_fifo = optarg;
			break;

		case 'i':
			pid_file = optarg;
			break;

		case 'K':
			ignore_policy++;
			break;

		case 'n':
			app_none++;
			break;

		case 'N':
			udp_encap_default_port = optarg;
			break;

		case 'p':
			udp_default_port = optarg;
			break;

		case 'l':
			pcap_file = optarg;
			/* FALLTHROUGH */

		case 'L':
			do_packetlog++;
			break;

		case 'R':
			report_file = optarg;
			break;

		case 'S':
			delete_sas = 0;
			ui_daemon_passive = 1;
			break;

		case 'T':
			disable_nat_t = 1;
			break;

		case 'v':
			verbose_logging = 1;
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0)	
		usage();

	if (do_packetlog && !pcap_file)
		pcap_file = PCAP_FILE_DEFAULT;
}

static void
sighup(int sig)
{
	sighupped = 1;
}

/* Report internal state on SIGUSR1.  */
static void
report(void)
{
	FILE	*rfp, *old;
	mode_t	old_umask;

	old_umask = umask(S_IRWXG | S_IRWXO);
	rfp = monitor_fopen(report_file, "w");
	umask(old_umask);

	if (!rfp) {
		log_error("report: fopen (\"%s\", \"w\") failed", report_file);
		return;
	}
	/* Divert the log channel to the report file during the report.  */
	old = log_current();
	log_to(rfp);
	ui_report("r");
	log_to(old);
	fclose(rfp);
}

static void
sigusr1(int sig)
{
	sigusr1ed = 1;
}

static int
phase2_sa_check(struct sa *sa, void *arg)
{
	return sa->phase == 2;
}

static int
phase1_sa_check(struct sa *sa, void *arg)
{
	return sa->phase == 1;
}

void
set_slave_signals(void)
{
	int n;

	for (n = 1; n < _NSIG; n++)
		signal(n, SIG_DFL);

	/*
	 * Do a clean daemon shutdown on TERM/INT. These signals must be
	 * initialized before monitor_init(). INT is only used with '-d'.
	 */
	signal(SIGTERM, daemon_shutdown_now);
	if (debug == 1)		/* i.e '-dd' will skip this.  */
		signal(SIGINT, daemon_shutdown_now);

	/* Reinitialize on HUP reception.  */
	signal(SIGHUP, sighup);

	/* Report state on USR1 reception.  */
	signal(SIGUSR1, sigusr1);
}

static void
daemon_shutdown(void)
{
	/* Perform a (protocol-wise) clean shutdown of the daemon.  */
	struct sa	*sa;

	if (sigtermed == 1) {
		log_print("isakmpd: shutting down...");

		if (delete_sas &&
		    strncmp("no", conf_get_str("General", "Delete-SAs"), 2)) {
			/*
			 * Delete all active SAs.  First IPsec SAs, then
			 * ISAKMPD.  Each DELETE is another (outgoing) message.
			 */
			while ((sa = sa_find(phase2_sa_check, NULL)))
				sa_delete(sa, 1);

			while ((sa = sa_find(phase1_sa_check, NULL)))
				sa_delete(sa, 1);
		}

		/* We only want to do this once. */
		sigtermed++;
	}
	if (transport_prio_sendqs_empty()) {
		/*
		 * When the prioritized transport sendq:s are empty, i.e all
		 * the DELETE notifications have been sent, we can shutdown.
		 */

		log_packet_stop();
		log_print("isakmpd: exit");
		exit(0);
	}
}

/* Called on SIGTERM, SIGINT or by ui_shutdown_daemon().  */
void
daemon_shutdown_now(int sig)
{
	sigtermed = 1;
}

/* Write pid file.  */
static void
write_pid_file(void)
{
	FILE	*fp;

	unlink(pid_file);

	fp = fopen(pid_file, "w");
	if (fp != NULL) {
		if (fprintf(fp, "%ld\n", (long) getpid()) < 0)
			log_error("write_pid_file: failed to write PID to "
			    "\"%.100s\"", pid_file);
		fclose(fp);
	} else
		log_fatal("write_pid_file: fopen (\"%.100s\", \"w\") failed",
		    pid_file);
}

void
sanitise_stdfd(void)
{
	int nullfd, dupfd;

	if ((nullfd = dupfd = open(_PATH_DEVNULL, O_RDWR)) == -1) {
		fprintf(stderr, "Couldn't open /dev/null: %s\n",
		    strerror(errno));
		exit(1);
	}
	while (++dupfd <= STDERR_FILENO) {
		/* Only populate closed fds */
		if (fcntl(dupfd, F_GETFL) == -1 && errno == EBADF) {
			if (dup2(nullfd, dupfd) == -1) {
				fprintf(stderr, "dup2: %s\n", strerror(errno));
				exit(1);
			}
		}
	}
	if (nullfd > STDERR_FILENO)
		close(nullfd);
}

int
main(int argc, char *argv[])
{
	fd_set         *rfds, *wfds;
	int             n, m;
	size_t          mask_size;
	struct timespec ts, *timeout;

	closefrom(STDERR_FILENO + 1);

	/*
	 * Make sure init() won't alloc fd 0, 1 or 2, as daemon() will close
	 * them.
	 */
	sanitise_stdfd();

	/* Log cmd line parsing and initialization errors to stderr.  */
	log_to(stderr);
	parse_args(argc, argv);
	log_init(debug);
	log_print("isakmpd: starting");

	/* Open protocols and services databases.  */
	setprotoent(1);
	setservent(1);

	/* Open command fifo */
	ui_init();

	set_slave_signals();
	/* Daemonize before forking unpriv'ed child */
	if (!debug)
		if (daemon(0, 0))
			log_fatal("main: daemon (0, 0) failed");

	/* Set timezone before priv'separation */
	tzset();

	write_pid_file();

	if (monitor_init(debug)) {
		/* The parent, with privileges enters infinite monitor loop. */
		monitor_loop(debug);
		exit(0);	/* Never reached.  */
	}
	/* Child process only from this point on, no privileges left.  */

	init();

	/* If we wanted IKE packet capture to file, initialize it now.  */
	if (pcap_file != 0)
		log_packet_init(pcap_file);

	/* Allocate the file descriptor sets just big enough.  */
	n = getdtablesize();
	mask_size = howmany(n, NFDBITS) * sizeof(fd_mask);
	rfds = malloc(mask_size);
	if (!rfds)
		log_fatal("main: malloc (%lu) failed",
		    (unsigned long)mask_size);
	wfds = malloc(mask_size);
	if (!wfds)
		log_fatal("main: malloc (%lu) failed",
		    (unsigned long)mask_size);

	monitor_init_done();

	while (1) {
		/* If someone has sent SIGHUP to us, reconfigure.  */
		if (sighupped) {
			sighupped = 0;
			log_print("SIGHUP received");
			reinit();
		}
		/* and if someone sent SIGUSR1, do a state report.  */
		if (sigusr1ed) {
			sigusr1ed = 0;
			log_print("SIGUSR1 received");
			report();
		}
		/*
		 * and if someone set 'sigtermed' (SIGTERM, SIGINT or via the
		 * UI), this indicates we should start a controlled shutdown
		 * of the daemon.
		 *
		 * Note: Since _one_ message is sent per iteration of this
		 * enclosing while-loop, and we want to send a number of
		 * DELETE notifications, we must loop atleast this number of
		 * times. The daemon_shutdown() function starts by queueing
		 * the DELETEs, all other calls just increments the
		 * 'sigtermed' variable until it reaches a "safe" value, and
		 * the daemon exits.
		 */
		if (sigtermed)
			daemon_shutdown();

		/* Setup the descriptors to look for incoming messages at.  */
		bzero(rfds, mask_size);
		n = transport_fd_set(rfds);
		FD_SET(ui_socket, rfds);
		if (ui_socket + 1 > n)
			n = ui_socket + 1;

		/*
		 * XXX Some day we might want to deal with an abstract
		 * application class instead, with many instantiations
		 * possible.
		 */
		if (!app_none && app_socket >= 0) {
			FD_SET(app_socket, rfds);
			if (app_socket + 1 > n)
				n = app_socket + 1;
		}
		/* Setup the descriptors that have pending messages to send. */
		bzero(wfds, mask_size);
		m = transport_pending_wfd_set(wfds);
		if (m > n)
			n = m;

		/* Find out when the next timed event is.  */
		timeout = &ts;
		timer_next_event(&timeout);

		n = pselect(n, rfds, wfds, NULL, timeout, NULL);
		if (n == -1) {
			if (errno != EINTR) {
				log_error("main: select");

				/*
				 * In order to give the unexpected error
				 * condition time to resolve without letting
				 * this process eat up all available CPU
				 * we sleep for a short while.
				 */
				sleep(1);
			}
		} else if (n) {
			transport_handle_messages(rfds);
			transport_send_messages(wfds);
			if (FD_ISSET(ui_socket, rfds))
				ui_handler();
			if (!app_none && app_socket >= 0 &&
			    FD_ISSET(app_socket, rfds))
				app_handler();
		}
		timer_handle_expirations();
	}
}
