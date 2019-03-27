/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <assert.h>

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/bio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <geom/gate/g_gate.h>
#include "ggate.h"


static enum { UNSET, CREATE, DESTROY, LIST, RESCUE } action = UNSET;

static const char *path = NULL;
static const char *host = NULL;
static int unit = G_GATE_UNIT_AUTO;
static unsigned flags = 0;
static int force = 0;
static unsigned queue_size = G_GATE_QUEUE_SIZE;
static unsigned port = G_GATE_PORT;
static off_t mediasize;
static unsigned sectorsize = 0;
static unsigned timeout = G_GATE_TIMEOUT;
static int sendfd, recvfd;
static uint32_t token;
static pthread_t sendtd, recvtd;
static int reconnect;

static void
usage(void)
{

	fprintf(stderr, "usage: %s create [-nv] [-o <ro|wo|rw>] [-p port] "
	    "[-q queue_size] [-R rcvbuf] [-S sndbuf] [-s sectorsize] "
	    "[-t timeout] [-u unit] <host> <path>\n", getprogname());
	fprintf(stderr, "       %s rescue [-nv] [-o <ro|wo|rw>] [-p port] "
	    "[-R rcvbuf] [-S sndbuf] <-u unit> <host> <path>\n", getprogname());
	fprintf(stderr, "       %s destroy [-f] <-u unit>\n", getprogname());
	fprintf(stderr, "       %s list [-v] [-u unit]\n", getprogname());
	exit(EXIT_FAILURE);
}

static void *
send_thread(void *arg __unused)
{
	struct g_gate_ctl_io ggio;
	struct g_gate_hdr hdr;
	char buf[MAXPHYS];
	ssize_t data;
	int error;

	g_gate_log(LOG_NOTICE, "%s: started!", __func__);

	ggio.gctl_version = G_GATE_VERSION;
	ggio.gctl_unit = unit;
	ggio.gctl_data = buf;

	for (;;) {
		ggio.gctl_length = sizeof(buf);
		ggio.gctl_error = 0;
		g_gate_ioctl(G_GATE_CMD_START, &ggio);
		error = ggio.gctl_error;
		switch (error) {
		case 0:
			break;
		case ECANCELED:
			if (reconnect)
				break;
			/* Exit gracefully. */
			g_gate_close_device();
			exit(EXIT_SUCCESS);
#if 0
		case ENOMEM:
			/* Buffer too small. */
			ggio.gctl_data = realloc(ggio.gctl_data,
			    ggio.gctl_length);
			if (ggio.gctl_data != NULL) {
				bsize = ggio.gctl_length;
				goto once_again;
			}
			/* FALLTHROUGH */
#endif
		case ENXIO:
		default:
			g_gate_xlog("ioctl(/dev/%s): %s.", G_GATE_CTL_NAME,
			    strerror(error));
		}

		if (reconnect)
			break;

		switch (ggio.gctl_cmd) {
		case BIO_READ:
			hdr.gh_cmd = GGATE_CMD_READ;
			break;
		case BIO_WRITE:
			hdr.gh_cmd = GGATE_CMD_WRITE;
			break;
		}
		hdr.gh_seq = ggio.gctl_seq;
		hdr.gh_offset = ggio.gctl_offset;
		hdr.gh_length = ggio.gctl_length;
		hdr.gh_error = 0;
		g_gate_swap2n_hdr(&hdr);

		data = g_gate_send(sendfd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
		g_gate_log(LOG_DEBUG, "Sent hdr packet.");
		g_gate_swap2h_hdr(&hdr);
		if (reconnect)
			break;
		if (data != sizeof(hdr)) {
			g_gate_log(LOG_ERR, "Lost connection 1.");
			reconnect = 1;
			pthread_kill(recvtd, SIGUSR1);
			break;
		}

		if (hdr.gh_cmd == GGATE_CMD_WRITE) {
			data = g_gate_send(sendfd, ggio.gctl_data,
			    ggio.gctl_length, MSG_NOSIGNAL);
			if (reconnect)
				break;
			if (data != ggio.gctl_length) {
				g_gate_log(LOG_ERR, "Lost connection 2 (%zd != %zd).", data, (ssize_t)ggio.gctl_length);
				reconnect = 1;
				pthread_kill(recvtd, SIGUSR1);
				break;
			}
			g_gate_log(LOG_DEBUG, "Sent %zd bytes (offset=%llu, "
			    "size=%u).", data, hdr.gh_offset, hdr.gh_length);
		}
	}
	g_gate_log(LOG_DEBUG, "%s: Died.", __func__);
	return (NULL);
}

static void *
recv_thread(void *arg __unused)
{
	struct g_gate_ctl_io ggio;
	struct g_gate_hdr hdr;
	char buf[MAXPHYS];
	ssize_t data;

	g_gate_log(LOG_NOTICE, "%s: started!", __func__);

	ggio.gctl_version = G_GATE_VERSION;
	ggio.gctl_unit = unit;
	ggio.gctl_data = buf;

	for (;;) {
		data = g_gate_recv(recvfd, &hdr, sizeof(hdr), MSG_WAITALL);
		if (reconnect)
			break;
		g_gate_swap2h_hdr(&hdr);
		if (data != sizeof(hdr)) {
			if (data == -1 && errno == EAGAIN)
				continue;
			g_gate_log(LOG_ERR, "Lost connection 3.");
			reconnect = 1;
			pthread_kill(sendtd, SIGUSR1);
			break;
		}
		g_gate_log(LOG_DEBUG, "Received hdr packet.");

		ggio.gctl_seq = hdr.gh_seq;
		ggio.gctl_cmd = hdr.gh_cmd;
		ggio.gctl_offset = hdr.gh_offset;
		ggio.gctl_length = hdr.gh_length;
		ggio.gctl_error = hdr.gh_error;

		if (ggio.gctl_error == 0 && ggio.gctl_cmd == GGATE_CMD_READ) {
			data = g_gate_recv(recvfd, ggio.gctl_data,
			    ggio.gctl_length, MSG_WAITALL);
			if (reconnect)
				break;
			g_gate_log(LOG_DEBUG, "Received data packet.");
			if (data != ggio.gctl_length) {
				g_gate_log(LOG_ERR, "Lost connection 4.");
				reconnect = 1;
				pthread_kill(sendtd, SIGUSR1);
				break;
			}
			g_gate_log(LOG_DEBUG, "Received %d bytes (offset=%ju, "
			    "size=%zu).", data, (uintmax_t)hdr.gh_offset,
			    (size_t)hdr.gh_length);
		}

		g_gate_ioctl(G_GATE_CMD_DONE, &ggio);
	}
	g_gate_log(LOG_DEBUG, "%s: Died.", __func__);
	pthread_exit(NULL);
}

static int
handshake(int dir)
{
	struct g_gate_version ver;
	struct g_gate_cinit cinit;
	struct g_gate_sinit sinit;
	struct sockaddr_in serv;
	int sfd;

	/*
	 * Do the network stuff.
	 */
	bzero(&serv, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = g_gate_str2ip(host);
	if (serv.sin_addr.s_addr == INADDR_NONE) {
		g_gate_log(LOG_DEBUG, "Invalid IP/host name: %s.", host);
		return (-1);
	}
	serv.sin_port = htons(port);
	sfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sfd == -1) {
		g_gate_log(LOG_DEBUG, "Cannot open socket: %s.",
		    strerror(errno));
		return (-1);
	}

	g_gate_socket_settings(sfd);

	if (connect(sfd, (struct sockaddr *)&serv, sizeof(serv)) == -1) {
		g_gate_log(LOG_DEBUG, "Cannot connect to server: %s.",
		    strerror(errno));
		close(sfd);
		return (-1);
	}

	g_gate_log(LOG_INFO, "Connected to the server: %s:%d.", host, port);

	/*
	 * Create and send version packet.
	 */
	g_gate_log(LOG_DEBUG, "Sending version packet.");
	assert(strlen(GGATE_MAGIC) == sizeof(ver.gv_magic));
	bcopy(GGATE_MAGIC, ver.gv_magic, sizeof(ver.gv_magic));
	ver.gv_version = GGATE_VERSION;
	ver.gv_error = 0;
	g_gate_swap2n_version(&ver);
	if (g_gate_send(sfd, &ver, sizeof(ver), MSG_NOSIGNAL) == -1) {
		g_gate_log(LOG_DEBUG, "Error while sending version packet: %s.",
		    strerror(errno));
		close(sfd);
		return (-1);
	}
	bzero(&ver, sizeof(ver));
	if (g_gate_recv(sfd, &ver, sizeof(ver), MSG_WAITALL) == -1) {
		g_gate_log(LOG_DEBUG, "Error while receiving data: %s.",
		    strerror(errno));
		close(sfd);
		return (-1);
	}
	if (ver.gv_error != 0) {
		g_gate_log(LOG_DEBUG, "Version verification problem: %s.",
		    strerror(errno));
		close(sfd);
		return (-1);
	}

	/*
	 * Create and send initial packet.
	 */
	g_gate_log(LOG_DEBUG, "Sending initial packet.");
	if (strlcpy(cinit.gc_path, path, sizeof(cinit.gc_path)) >=
	    sizeof(cinit.gc_path)) {
		g_gate_log(LOG_DEBUG, "Path name too long.");
		close(sfd);
		return (-1);
	}
	cinit.gc_flags = flags | dir;
	cinit.gc_token = token;
	cinit.gc_nconn = 2;
	g_gate_swap2n_cinit(&cinit);
	if (g_gate_send(sfd, &cinit, sizeof(cinit), MSG_NOSIGNAL) == -1) {
	        g_gate_log(LOG_DEBUG, "Error while sending initial packet: %s.",
		    strerror(errno));
		close(sfd);
		return (-1);
	}
	g_gate_swap2h_cinit(&cinit);

	/*
	 * Receiving initial packet from server.
	 */
	g_gate_log(LOG_DEBUG, "Receiving initial packet.");
	if (g_gate_recv(sfd, &sinit, sizeof(sinit), MSG_WAITALL) == -1) {
		g_gate_log(LOG_DEBUG, "Error while receiving data: %s.",
		    strerror(errno));
		close(sfd);
		return (-1);
	}
	g_gate_swap2h_sinit(&sinit);
	if (sinit.gs_error != 0) {
	        g_gate_log(LOG_DEBUG, "Error from server: %s.",
		    strerror(sinit.gs_error));
		close(sfd);
		return (-1);
	}
	g_gate_log(LOG_DEBUG, "Received initial packet.");

	mediasize = sinit.gs_mediasize;
	if (sectorsize == 0)
		sectorsize = sinit.gs_sectorsize;

	return (sfd);
}

static void
mydaemon(void)
{

	if (g_gate_verbose > 0)
		return;
	if (daemon(0, 0) == 0)
		return;
	if (action == CREATE)
		g_gate_destroy(unit, 1);
	err(EXIT_FAILURE, "Cannot daemonize");
}

static int
g_gatec_connect(void)
{

	token = arc4random();
	/*
	 * Our receive descriptor is connected to the send descriptor on the
	 * server side.
	 */
	recvfd = handshake(GGATE_FLAG_SEND);
	if (recvfd == -1)
		return (0);
	/*
	 * Our send descriptor is connected to the receive descriptor on the
	 * server side.
	 */
	sendfd = handshake(GGATE_FLAG_RECV);
	if (sendfd == -1)
		return (0);
	return (1);
}

static void
g_gatec_start(void)
{
	int error;

	reconnect = 0;
	error = pthread_create(&recvtd, NULL, recv_thread, NULL);
	if (error != 0) {
		g_gate_destroy(unit, 1);
		g_gate_xlog("pthread_create(recv_thread): %s.",
		    strerror(error));
	}
	sendtd = pthread_self();
	send_thread(NULL);
	/* Disconnected. */
	close(sendfd);
	close(recvfd);
}

static void
signop(int sig __unused)
{

	/* Do nothing. */
}

static void
g_gatec_loop(void)
{
	struct g_gate_ctl_cancel ggioc;

	signal(SIGUSR1, signop);
	for (;;) {
		g_gatec_start();
		g_gate_log(LOG_NOTICE, "Disconnected [%s %s]. Connecting...",
		    host, path);
		while (!g_gatec_connect()) {
			sleep(2);
			g_gate_log(LOG_NOTICE, "Connecting [%s %s]...", host,
			    path);
		}
		ggioc.gctl_version = G_GATE_VERSION;
		ggioc.gctl_unit = unit;
		ggioc.gctl_seq = 0;
		g_gate_ioctl(G_GATE_CMD_CANCEL, &ggioc);
	}
}

static void
g_gatec_create(void)
{
	struct g_gate_ctl_create ggioc;

	if (!g_gatec_connect())
		g_gate_xlog("Cannot connect: %s.", strerror(errno));

	/*
	 * Ok, got both sockets, time to create provider.
	 */
	memset(&ggioc, 0, sizeof(ggioc));
	ggioc.gctl_version = G_GATE_VERSION;
	ggioc.gctl_mediasize = mediasize;
	ggioc.gctl_sectorsize = sectorsize;
	ggioc.gctl_flags = flags;
	ggioc.gctl_maxcount = queue_size;
	ggioc.gctl_timeout = timeout;
	ggioc.gctl_unit = unit;
	snprintf(ggioc.gctl_info, sizeof(ggioc.gctl_info), "%s:%u %s", host,
	    port, path);
	g_gate_ioctl(G_GATE_CMD_CREATE, &ggioc);
	if (unit == -1) {
		printf("%s%u\n", G_GATE_PROVIDER_NAME, ggioc.gctl_unit);
		fflush(stdout);
	}
	unit = ggioc.gctl_unit;

	mydaemon();
	g_gatec_loop();
}

static void
g_gatec_rescue(void)
{
	struct g_gate_ctl_cancel ggioc;

	if (!g_gatec_connect())
		g_gate_xlog("Cannot connect: %s.", strerror(errno));

	ggioc.gctl_version = G_GATE_VERSION;
	ggioc.gctl_unit = unit;
	ggioc.gctl_seq = 0;
	g_gate_ioctl(G_GATE_CMD_CANCEL, &ggioc);

	mydaemon();
	g_gatec_loop();
}

int
main(int argc, char *argv[])
{

	if (argc < 2)
		usage();
	if (strcasecmp(argv[1], "create") == 0)
		action = CREATE;
	else if (strcasecmp(argv[1], "destroy") == 0)
		action = DESTROY;
	else if (strcasecmp(argv[1], "list") == 0)
		action = LIST;
	else if (strcasecmp(argv[1], "rescue") == 0)
		action = RESCUE;
	else
		usage();
	argc -= 1;
	argv += 1;
	for (;;) {
		int ch;

		ch = getopt(argc, argv, "fno:p:q:R:S:s:t:u:v");
		if (ch == -1)
			break;
		switch (ch) {
		case 'f':
			if (action != DESTROY)
				usage();
			force = 1;
			break;
		case 'n':
			if (action != CREATE && action != RESCUE)
				usage();
			nagle = 0;
			break;
		case 'o':
			if (action != CREATE && action != RESCUE)
				usage();
			if (strcasecmp("ro", optarg) == 0)
				flags = G_GATE_FLAG_READONLY;
			else if (strcasecmp("wo", optarg) == 0)
				flags = G_GATE_FLAG_WRITEONLY;
			else if (strcasecmp("rw", optarg) == 0)
				flags = 0;
			else {
				errx(EXIT_FAILURE,
				    "Invalid argument for '-o' option.");
			}
			break;
		case 'p':
			if (action != CREATE && action != RESCUE)
				usage();
			errno = 0;
			port = strtoul(optarg, NULL, 10);
			if (port == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid port.");
			break;
		case 'q':
			if (action != CREATE)
				usage();
			errno = 0;
			queue_size = strtoul(optarg, NULL, 10);
			if (queue_size == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid queue_size.");
			break;
		case 'R':
			if (action != CREATE && action != RESCUE)
				usage();
			errno = 0;
			rcvbuf = strtoul(optarg, NULL, 10);
			if (rcvbuf == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid rcvbuf.");
			break;
		case 'S':
			if (action != CREATE && action != RESCUE)
				usage();
			errno = 0;
			sndbuf = strtoul(optarg, NULL, 10);
			if (sndbuf == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid sndbuf.");
			break;
		case 's':
			if (action != CREATE)
				usage();
			errno = 0;
			sectorsize = strtoul(optarg, NULL, 10);
			if (sectorsize == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid sectorsize.");
			break;
		case 't':
			if (action != CREATE)
				usage();
			errno = 0;
			timeout = strtoul(optarg, NULL, 10);
			if (timeout == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid timeout.");
			break;
		case 'u':
			errno = 0;
			unit = strtol(optarg, NULL, 10);
			if (unit == 0 && errno != 0)
				errx(EXIT_FAILURE, "Invalid unit number.");
			break;
		case 'v':
			if (action == DESTROY)
				usage();
			g_gate_verbose++;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	switch (action) {
	case CREATE:
		if (argc != 2)
			usage();
		g_gate_load_module();
		g_gate_open_device();
		host = argv[0];
		path = argv[1];
		g_gatec_create();
		break;
	case DESTROY:
		if (unit == -1) {
			fprintf(stderr, "Required unit number.\n");
			usage();
		}
		g_gate_verbose = 1;
		g_gate_open_device();
		g_gate_destroy(unit, force);
		break;
	case LIST:
		g_gate_list(unit, g_gate_verbose);
		break;
	case RESCUE:
		if (argc != 2)
			usage();
		if (unit == -1) {
			fprintf(stderr, "Required unit number.\n");
			usage();
		}
		g_gate_open_device();
		host = argv[0];
		path = argv[1];
		g_gatec_rescue();
		break;
	case UNSET:
	default:
		usage();
	}
	g_gate_close_device();
	exit(EXIT_SUCCESS);
}
