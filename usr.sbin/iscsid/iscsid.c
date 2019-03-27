/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/socket.h>
#include <sys/capsicum.h>
#include <sys/wait.h>
#include <assert.h>
#include <capsicum_helpers.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"

static volatile bool sigalrm_received = false;

static int nchildren = 0;

static void
usage(void)
{

	fprintf(stderr, "usage: iscsid [-P pidfile][-d][-m maxproc][-t timeout]\n");
	exit(1);
}

char *
checked_strdup(const char *s)
{
	char *c;

	c = strdup(s);
	if (c == NULL)
		log_err(1, "strdup");
	return (c);
}

static void
resolve_addr(const struct connection *conn, const char *address,
    struct addrinfo **ai, bool initiator_side)
{
	struct addrinfo hints;
	char *arg, *addr, *ch;
	const char *port;
	int error, colons = 0;

	arg = checked_strdup(address);

	if (arg[0] == '\0') {
		fail(conn, "empty address");
		log_errx(1, "empty address");
	}
	if (arg[0] == '[') {
		/*
		 * IPv6 address in square brackets, perhaps with port.
		 */
		arg++;
		addr = strsep(&arg, "]");
		if (arg == NULL) {
			fail(conn, "malformed address");
			log_errx(1, "malformed address %s", address);
		}
		if (arg[0] == '\0') {
			port = NULL;
		} else if (arg[0] == ':') {
			port = arg + 1;
		} else {
			fail(conn, "malformed address");
			log_errx(1, "malformed address %s", address);
		}
	} else {
		/*
		 * Either IPv6 address without brackets - and without
		 * a port - or IPv4 address.  Just count the colons.
		 */
		for (ch = arg; *ch != '\0'; ch++) {
			if (*ch == ':')
				colons++;
		}
		if (colons > 1) {
			addr = arg;
			port = NULL;
		} else {
			addr = strsep(&arg, ":");
			if (arg == NULL)
				port = NULL;
			else
				port = arg;
		}
	}

	if (port == NULL && !initiator_side)
		port = "3260";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
	if (initiator_side)
		hints.ai_flags |= AI_PASSIVE;

	error = getaddrinfo(addr, port, &hints, ai);
	if (error != 0) {
		fail(conn, gai_strerror(error));
		log_errx(1, "getaddrinfo for %s failed: %s",
		    address, gai_strerror(error));
	}
}

static struct connection *
connection_new(int iscsi_fd, const struct iscsi_daemon_request *request)
{
	struct connection *conn;
	struct iscsi_session_limits *isl;
	struct addrinfo *from_ai, *to_ai;
	const char *from_addr, *to_addr;
#ifdef ICL_KERNEL_PROXY
	struct iscsi_daemon_connect idc;
#endif
	int error, sockbuf;

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL)
		log_err(1, "calloc");

	/*
	 * Default values, from RFC 3720, section 12.
	 */
	conn->conn_header_digest = CONN_DIGEST_NONE;
	conn->conn_data_digest = CONN_DIGEST_NONE;
	conn->conn_initial_r2t = true;
	conn->conn_immediate_data = true;
	conn->conn_max_recv_data_segment_length = 8192;
	conn->conn_max_send_data_segment_length = 8192;
	conn->conn_max_burst_length = 262144;
	conn->conn_first_burst_length = 65536;
	conn->conn_iscsi_fd = iscsi_fd;

	conn->conn_session_id = request->idr_session_id;
	memcpy(&conn->conn_conf, &request->idr_conf, sizeof(conn->conn_conf));
	memcpy(&conn->conn_isid, &request->idr_isid, sizeof(conn->conn_isid));
	conn->conn_tsih = request->idr_tsih;

	/*
	 * Read the driver limits and provide reasonable defaults for the ones
	 * the driver doesn't care about.  If a max_snd_dsl is not explicitly
	 * provided by the driver then we'll make sure both conn->max_snd_dsl
	 * and isl->max_snd_dsl are set to the rcv_dsl.  This preserves historic
	 * behavior.
	 */
	isl = &conn->conn_limits;
	memcpy(isl, &request->idr_limits, sizeof(*isl));
	if (isl->isl_max_recv_data_segment_length == 0)
		isl->isl_max_recv_data_segment_length = (1 << 24) - 1;
	if (isl->isl_max_send_data_segment_length == 0)
		isl->isl_max_send_data_segment_length =
		    isl->isl_max_recv_data_segment_length;
	if (isl->isl_max_burst_length == 0)
		isl->isl_max_burst_length = (1 << 24) - 1;
	if (isl->isl_first_burst_length == 0)
		isl->isl_first_burst_length = (1 << 24) - 1;
	if (isl->isl_first_burst_length > isl->isl_max_burst_length)
		isl->isl_first_burst_length = isl->isl_max_burst_length;

	/*
	 * Limit default send length in case it won't be negotiated.
	 * We can't do it for other limits, since they may affect both
	 * sender and receiver operation, and we must obey defaults.
	 */
	if (conn->conn_max_send_data_segment_length >
	    isl->isl_max_send_data_segment_length) {
		conn->conn_max_send_data_segment_length =
		    isl->isl_max_send_data_segment_length;
	}

	from_addr = conn->conn_conf.isc_initiator_addr;
	to_addr = conn->conn_conf.isc_target_addr;

	if (from_addr[0] != '\0')
		resolve_addr(conn, from_addr, &from_ai, true);
	else
		from_ai = NULL;

	resolve_addr(conn, to_addr, &to_ai, false);

#ifdef ICL_KERNEL_PROXY
	if (conn->conn_conf.isc_iser) {
		memset(&idc, 0, sizeof(idc));
		idc.idc_session_id = conn->conn_session_id;
		if (conn->conn_conf.isc_iser)
			idc.idc_iser = 1;
		idc.idc_domain = to_ai->ai_family;
		idc.idc_socktype = to_ai->ai_socktype;
		idc.idc_protocol = to_ai->ai_protocol;
		if (from_ai != NULL) {
			idc.idc_from_addr = from_ai->ai_addr;
			idc.idc_from_addrlen = from_ai->ai_addrlen;
		}
		idc.idc_to_addr = to_ai->ai_addr;
		idc.idc_to_addrlen = to_ai->ai_addrlen;

		log_debugx("connecting to %s using ICL kernel proxy", to_addr);
		error = ioctl(iscsi_fd, ISCSIDCONNECT, &idc);
		if (error != 0) {
			fail(conn, strerror(errno));
			log_err(1, "failed to connect to %s "
			    "using ICL kernel proxy: ISCSIDCONNECT", to_addr);
		}

		return (conn);
	}
#endif /* ICL_KERNEL_PROXY */

	if (conn->conn_conf.isc_iser) {
		fail(conn, "iSER not supported");
		log_errx(1, "iscsid(8) compiled without ICL_KERNEL_PROXY "
		    "does not support iSER");
	}

	conn->conn_socket = socket(to_ai->ai_family, to_ai->ai_socktype,
	    to_ai->ai_protocol);
	if (conn->conn_socket < 0) {
		fail(conn, strerror(errno));
		log_err(1, "failed to create socket for %s", from_addr);
	}
	sockbuf = SOCKBUF_SIZE;
	if (setsockopt(conn->conn_socket, SOL_SOCKET, SO_RCVBUF,
	    &sockbuf, sizeof(sockbuf)) == -1)
		log_warn("setsockopt(SO_RCVBUF) failed");
	sockbuf = SOCKBUF_SIZE;
	if (setsockopt(conn->conn_socket, SOL_SOCKET, SO_SNDBUF,
	    &sockbuf, sizeof(sockbuf)) == -1)
		log_warn("setsockopt(SO_SNDBUF) failed");
	if (from_ai != NULL) {
		error = bind(conn->conn_socket, from_ai->ai_addr,
		    from_ai->ai_addrlen);
		if (error != 0) {
			fail(conn, strerror(errno));
			log_err(1, "failed to bind to %s", from_addr);
		}
	}
	log_debugx("connecting to %s", to_addr);
	error = connect(conn->conn_socket, to_ai->ai_addr, to_ai->ai_addrlen);
	if (error != 0) {
		fail(conn, strerror(errno));
		log_err(1, "failed to connect to %s", to_addr);
	}

	return (conn);
}

static void
handoff(struct connection *conn)
{
	struct iscsi_daemon_handoff idh;
	int error;

	log_debugx("handing off connection to the kernel");

	memset(&idh, 0, sizeof(idh));
	idh.idh_session_id = conn->conn_session_id;
	idh.idh_socket = conn->conn_socket;
	strlcpy(idh.idh_target_alias, conn->conn_target_alias,
	    sizeof(idh.idh_target_alias));
	idh.idh_tsih = conn->conn_tsih;
	idh.idh_statsn = conn->conn_statsn;
	idh.idh_header_digest = conn->conn_header_digest;
	idh.idh_data_digest = conn->conn_data_digest;
	idh.idh_initial_r2t = conn->conn_initial_r2t;
	idh.idh_immediate_data = conn->conn_immediate_data;
	idh.idh_max_recv_data_segment_length =
	    conn->conn_max_recv_data_segment_length;
	idh.idh_max_send_data_segment_length =
	    conn->conn_max_send_data_segment_length;
	idh.idh_max_burst_length = conn->conn_max_burst_length;
	idh.idh_first_burst_length = conn->conn_first_burst_length;

	error = ioctl(conn->conn_iscsi_fd, ISCSIDHANDOFF, &idh);
	if (error != 0)
		log_err(1, "ISCSIDHANDOFF");
}

void
fail(const struct connection *conn, const char *reason)
{
	struct iscsi_daemon_fail idf;
	int error, saved_errno;

	saved_errno = errno;

	memset(&idf, 0, sizeof(idf));
	idf.idf_session_id = conn->conn_session_id;
	strlcpy(idf.idf_reason, reason, sizeof(idf.idf_reason));

	error = ioctl(conn->conn_iscsi_fd, ISCSIDFAIL, &idf);
	if (error != 0)
		log_err(1, "ISCSIDFAIL");

	errno = saved_errno;
}

/*
 * XXX: I CANT INTO LATIN
 */
static void
capsicate(struct connection *conn)
{
	cap_rights_t rights;
#ifdef ICL_KERNEL_PROXY
	const unsigned long cmds[] = { ISCSIDCONNECT, ISCSIDSEND, ISCSIDRECEIVE,
	    ISCSIDHANDOFF, ISCSIDFAIL, ISCSISADD, ISCSISREMOVE, ISCSISMODIFY };
#else
	const unsigned long cmds[] = { ISCSIDHANDOFF, ISCSIDFAIL, ISCSISADD,
	    ISCSISREMOVE, ISCSISMODIFY };
#endif

	cap_rights_init(&rights, CAP_IOCTL);
	if (caph_rights_limit(conn->conn_iscsi_fd, &rights) < 0)
		log_err(1, "cap_rights_limit");

	if (caph_ioctls_limit(conn->conn_iscsi_fd, cmds, nitems(cmds)) < 0)
		log_err(1, "cap_ioctls_limit");

	if (caph_enter() != 0)
		log_err(1, "cap_enter");

	if (cap_sandboxed())
		log_debugx("Capsicum capability mode enabled");
	else
		log_warnx("Capsicum capability mode not supported");
}

bool
timed_out(void)
{

	return (sigalrm_received);
}

static void
sigalrm_handler(int dummy __unused)
{
	/*
	 * It would be easiest to just log an error and exit.  We can't
	 * do this, though, because log_errx() is not signal safe, since
	 * it calls syslog(3).  Instead, set a flag checked by pdu_send()
	 * and pdu_receive(), to call log_errx() there.  Should they fail
	 * to notice, we'll exit here one second later.
	 */
	if (sigalrm_received) {
		/*
		 * Oh well.  Just give up and quit.
		 */
		_exit(2);
	}

	sigalrm_received = true;
}

static void
set_timeout(int timeout)
{
	struct sigaction sa;
	struct itimerval itv;
	int error;

	if (timeout <= 0) {
		log_debugx("session timeout disabled");
		return;
	}

	bzero(&sa, sizeof(sa));
	sa.sa_handler = sigalrm_handler;
	sigfillset(&sa.sa_mask);
	error = sigaction(SIGALRM, &sa, NULL);
	if (error != 0)
		log_err(1, "sigaction");

	/*
	 * First SIGALRM will arive after conf_timeout seconds.
	 * If we do nothing, another one will arrive a second later.
	 */
	bzero(&itv, sizeof(itv));
	itv.it_interval.tv_sec = 1;
	itv.it_value.tv_sec = timeout;

	log_debugx("setting session timeout to %d seconds",
	    timeout);
	error = setitimer(ITIMER_REAL, &itv, NULL);
	if (error != 0)
		log_err(1, "setitimer");
}

static void
sigchld_handler(int dummy __unused)
{

	/*
	 * The only purpose of this handler is to make SIGCHLD
	 * interrupt the ISCSIDWAIT ioctl(2), so we can call
	 * wait_for_children().
	 */
}

static void
register_sigchld(void)
{
	struct sigaction sa;
	int error;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = sigchld_handler;
	sigfillset(&sa.sa_mask);
	error = sigaction(SIGCHLD, &sa, NULL);
	if (error != 0)
		log_err(1, "sigaction");

}

static void
handle_request(int iscsi_fd, const struct iscsi_daemon_request *request, int timeout)
{
	struct connection *conn;

	log_set_peer_addr(request->idr_conf.isc_target_addr);
	if (request->idr_conf.isc_target[0] != '\0') {
		log_set_peer_name(request->idr_conf.isc_target);
		setproctitle("%s (%s)", request->idr_conf.isc_target_addr, request->idr_conf.isc_target);
	} else {
		setproctitle("%s", request->idr_conf.isc_target_addr);
	}

	conn = connection_new(iscsi_fd, request);
	set_timeout(timeout);
	capsicate(conn);
	login(conn);
	if (conn->conn_conf.isc_discovery != 0)
		discovery(conn);
	else
		handoff(conn);

	log_debugx("nothing more to do; exiting");
	exit (0);
}

static int
wait_for_children(bool block)
{
	pid_t pid;
	int status;
	int num = 0;

	for (;;) {
		/*
		 * If "block" is true, wait for at least one process.
		 */
		if (block && num == 0)
			pid = wait4(-1, &status, 0, NULL);
		else
			pid = wait4(-1, &status, WNOHANG, NULL);
		if (pid <= 0)
			break;
		if (WIFSIGNALED(status)) {
			log_warnx("child process %d terminated with signal %d",
			    pid, WTERMSIG(status));
		} else if (WEXITSTATUS(status) != 0) {
			log_warnx("child process %d terminated with exit status %d",
			    pid, WEXITSTATUS(status));
		} else {
			log_debugx("child process %d terminated gracefully", pid);
		}
		num++;
	}

	return (num);
}

int
main(int argc, char **argv)
{
	int ch, debug = 0, error, iscsi_fd, maxproc = 30, retval, saved_errno,
	    timeout = 60;
	bool dont_daemonize = false;
	struct pidfh *pidfh;
	pid_t pid, otherpid;
	const char *pidfile_path = DEFAULT_PIDFILE;
	struct iscsi_daemon_request request;

	while ((ch = getopt(argc, argv, "P:dl:m:t:")) != -1) {
		switch (ch) {
		case 'P':
			pidfile_path = optarg;
			break;
		case 'd':
			dont_daemonize = true;
			debug++;
			break;
		case 'l':
			debug = atoi(optarg);
			break;
		case 'm':
			maxproc = atoi(optarg);
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	if (argc != 0)
		usage();

	log_init(debug);

	pidfh = pidfile_open(pidfile_path, 0600, &otherpid);
	if (pidfh == NULL) {
		if (errno == EEXIST)
			log_errx(1, "daemon already running, pid: %jd.",
			    (intmax_t)otherpid);
		log_err(1, "cannot open or create pidfile \"%s\"",
		    pidfile_path);
	}

	iscsi_fd = open(ISCSI_PATH, O_RDWR);
	if (iscsi_fd < 0 && errno == ENOENT) {
		saved_errno = errno;
		retval = kldload("iscsi");
		if (retval != -1)
			iscsi_fd = open(ISCSI_PATH, O_RDWR);
		else
			errno = saved_errno;
	}
	if (iscsi_fd < 0)
		log_err(1, "failed to open %s", ISCSI_PATH);

	if (dont_daemonize == false) {
		if (daemon(0, 0) == -1) {
			log_warn("cannot daemonize");
			pidfile_remove(pidfh);
			exit(1);
		}
	}

	pidfile_write(pidfh);

	register_sigchld();

	for (;;) {
		log_debugx("waiting for request from the kernel");

		memset(&request, 0, sizeof(request));
		error = ioctl(iscsi_fd, ISCSIDWAIT, &request);
		if (error != 0) {
			if (errno == EINTR) {
				nchildren -= wait_for_children(false);
				assert(nchildren >= 0);
				continue;
			}

			log_err(1, "ISCSIDWAIT");
		}

		if (dont_daemonize) {
			log_debugx("not forking due to -d flag; "
			    "will exit after servicing a single request");
		} else {
			nchildren -= wait_for_children(false);
			assert(nchildren >= 0);

			while (maxproc > 0 && nchildren >= maxproc) {
				log_debugx("maxproc limit of %d child processes hit; "
				    "waiting for child process to exit", maxproc);
				nchildren -= wait_for_children(true);
				assert(nchildren >= 0);
			}
			log_debugx("incoming connection; forking child process #%d",
			    nchildren);
			nchildren++;

			pid = fork();
			if (pid < 0)
				log_err(1, "fork");
			if (pid > 0)
				continue;
		}

		pidfile_close(pidfh);
		handle_request(iscsi_fd, &request, timeout);
	}

	return (0);
}
