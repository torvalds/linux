/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
static char sccsid[] = "@(#)tftp.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Many bug fixes are from Jim Guyton <guyton@rand-unix> */

/*
 * TFTP User Program -- Protocol Machines
 */
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <arpa/tftp.h>

#include <assert.h>
#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "tftp.h"
#include "tftp-file.h"
#include "tftp-utils.h"
#include "tftp-io.h"
#include "tftp-transfer.h"
#include "tftp-options.h"

/*
 * Send the requested file.
 */
void
xmitfile(int peer, char *port, int fd, char *name, char *mode)
{
	struct tftphdr *rp;
	int n, i;
	uint16_t block;
	struct sockaddr_storage serv;	/* valid server port number */
	char recvbuffer[MAXPKTSIZE];
	struct tftp_stats tftp_stats;

	stats_init(&tftp_stats);

	memset(&serv, 0, sizeof(serv));
	rp = (struct tftphdr *)recvbuffer;

	if (port == NULL) {
		struct servent *se;
		se = getservbyname("tftp", "udp");
		assert(se != NULL);
		((struct sockaddr_in *)&peer_sock)->sin_port = se->s_port;
	} else
		((struct sockaddr_in *)&peer_sock)->sin_port =
		    htons(atoi(port));

	for (i = 0; i < 12; i++) {
		struct sockaddr_storage from;

		/* Tell the other side what we want to do */
		if (debug&DEBUG_SIMPLE)
			printf("Sending %s\n", name);

		n = send_wrq(peer, name, mode);
		if (n > 0) {
			printf("Cannot send WRQ packet\n");
			return;
		}

		/*
		 * The first packet we receive has the new destination port
		 * we have to send the next packets to.
		 */
		n = receive_packet(peer, recvbuffer,
		    MAXPKTSIZE, &from, timeoutpacket);

		/* We got some data! */
		if (n >= 0) {
			((struct sockaddr_in *)&peer_sock)->sin_port =
			    ((struct sockaddr_in *)&from)->sin_port;
			break;
		}

		/* This should be retried */
		if (n == RP_TIMEOUT) {
			printf("Try %d, didn't receive answer from remote.\n",
			    i + 1);
			continue;
		}

		/* Everything else is fatal */
		break;
	}
	if (i == 12) {
		printf("Transfer timed out.\n");
		return;
	}
	if (rp->th_opcode == ERROR) {
		printf("Got ERROR, aborted\n");
		return;
	}

	/*
	 * If the first packet is an OACK instead of an ACK packet,
	 * handle it different.
	 */
	if (rp->th_opcode == OACK) {
		if (!options_rfc_enabled) {
			printf("Got OACK while options are not enabled!\n");
			send_error(peer, EBADOP);
			return;
		}

		parse_options(peer, rp->th_stuff, n + 2);
	}

	if (read_init(fd, NULL, mode) < 0) {
		warn("read_init()");
		return;
	}

	block = 1;
	tftp_send(peer, &block, &tftp_stats);

	read_close();
	if (tftp_stats.amount > 0)
		printstats("Sent", verbose, &tftp_stats);

	txrx_error = 1;
}

/*
 * Receive a file.
 */
void
recvfile(int peer, char *port, int fd, char *name, char *mode)
{
	struct tftphdr *rp;
	uint16_t block;
	char recvbuffer[MAXPKTSIZE];
	int n, i;
	struct tftp_stats tftp_stats;

	stats_init(&tftp_stats);

	rp = (struct tftphdr *)recvbuffer;

	if (port == NULL) {
		struct servent *se;
		se = getservbyname("tftp", "udp");
		assert(se != NULL);
		((struct sockaddr_in *)&peer_sock)->sin_port = se->s_port;
	} else
		((struct sockaddr_in *)&peer_sock)->sin_port =
		    htons(atoi(port));

	for (i = 0; i < 12; i++) {
		struct sockaddr_storage from;

		/* Tell the other side what we want to do */
		if (debug&DEBUG_SIMPLE)
			printf("Requesting %s\n", name);

		n = send_rrq(peer, name, mode);
		if (n > 0) {
			printf("Cannot send RRQ packet\n");
			return;
		}

		/*
		 * The first packet we receive has the new destination port
		 * we have to send the next packets to.
		 */
		n = receive_packet(peer, recvbuffer,
		    MAXPKTSIZE, &from, timeoutpacket);

		/* We got something useful! */
		if (n >= 0) {
			((struct sockaddr_in *)&peer_sock)->sin_port =
			    ((struct sockaddr_in *)&from)->sin_port;
			break;
		}

		/* We should retry if this happens */
		if (n == RP_TIMEOUT) {
			printf("Try %d, didn't receive answer from remote.\n",
			    i + 1);
			continue;
		}

		/* Otherwise it is a fatal error */
		break;
	}
	if (i == 12) {
		printf("Transfer timed out.\n");
		return;
	}
	if (rp->th_opcode == ERROR) {
		tftp_log(LOG_ERR, "Error code %d: %s", rp->th_code, rp->th_msg);
		return;
	}

	if (write_init(fd, NULL, mode) < 0) {
		warn("write_init");
		return;
	}

	/*
	 * If the first packet is an OACK packet instead of an DATA packet,
	 * handle it different.
	 */
	if (rp->th_opcode == OACK) {
		if (!options_rfc_enabled) {
			printf("Got OACK while options are not enabled!\n");
			send_error(peer, EBADOP);
			return;
		}

		parse_options(peer, rp->th_stuff, n + 2);

		n = send_ack(peer, 0);
		if (n > 0) {
			printf("Cannot send ACK on OACK.\n");
			return;
		}
		block = 0;
		tftp_receive(peer, &block, &tftp_stats, NULL, 0);
	} else {
		block = 1;
		tftp_receive(peer, &block, &tftp_stats, rp, n);
	}

	if (tftp_stats.amount > 0)
		printstats("Received", verbose, &tftp_stats);
	return;
}
