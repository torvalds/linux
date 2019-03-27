/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008 Edwin Groothuis. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/tftp.h>
#include <arpa/inet.h>

#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "tftp-file.h"
#include "tftp-io.h"
#include "tftp-utils.h"
#include "tftp-options.h"

struct sockaddr_storage peer_sock;
struct sockaddr_storage me_sock;

static int send_packet(int peer, uint16_t block, char *pkt, int size);

static struct errmsg {
	int	e_code;
	const char	*e_msg;
} errmsgs[] = {
	{ EUNDEF,	"Undefined error code" },
	{ ENOTFOUND,	"File not found" },
	{ EACCESS,	"Access violation" },
	{ ENOSPACE,	"Disk full or allocation exceeded" },
	{ EBADOP,	"Illegal TFTP operation" },
	{ EBADID,	"Unknown transfer ID" },
	{ EEXISTS,	"File already exists" },
	{ ENOUSER,	"No such user" },
	{ EOPTNEG,	"Option negotiation" },
	{ -1,		NULL }
};

#define DROPPACKET(s)							\
	if (packetdroppercentage != 0 &&				\
	    random()%100 < packetdroppercentage) {			\
		tftp_log(LOG_DEBUG, "Artificial packet drop in %s", s);	\
		return;							\
	}
#define DROPPACKETn(s,n)						\
	if (packetdroppercentage != 0 &&				\
	    random()%100 < packetdroppercentage) {			\
		tftp_log(LOG_DEBUG, "Artificial packet drop in %s", s);	\
		return (n);						\
	}

const char *
errtomsg(int error)
{
	static char ebuf[40];
	struct errmsg *pe;

	if (error == 0)
		return ("success");
	for (pe = errmsgs; pe->e_code >= 0; pe++)
		if (pe->e_code == error)
			return (pe->e_msg);
	snprintf(ebuf, sizeof(ebuf), "error %d", error);
	return (ebuf);
}

static int
send_packet(int peer, uint16_t block, char *pkt, int size)
{
	int i;
	int t = 1;

	for (i = 0; i < 12 ; i++) {
		DROPPACKETn("send_packet", 0);

		if (sendto(peer, pkt, size, 0, (struct sockaddr *)&peer_sock,
		    peer_sock.ss_len) == size) {
			if (i)
				tftp_log(LOG_ERR,
				    "%s block %d, attempt %d successful",
		    		    packettype(ntohs(((struct tftphdr *)
				    (pkt))->th_opcode)), block, i);
			return (0);
		}
		tftp_log(LOG_ERR,
		    "%s block %d, attempt %d failed (Error %d: %s)", 
		    packettype(ntohs(((struct tftphdr *)(pkt))->th_opcode)),
		    block, i, errno, strerror(errno));
		sleep(t);
		if (t < 32)
			t <<= 1;
	}
	tftp_log(LOG_ERR, "send_packet: %s", strerror(errno));
	return (1);
}

/*
 * Send an ERROR packet (error message).
 * Error code passed in is one of the
 * standard TFTP codes, or a UNIX errno
 * offset by 100.
 */
void
send_error(int peer, int error)
{
	struct tftphdr *tp;
	int length;
	struct errmsg *pe;
	char buf[MAXPKTSIZE];

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending ERROR %d", error);

	DROPPACKET("send_error");

	tp = (struct tftphdr *)buf;
	tp->th_opcode = htons((u_short)ERROR);
	tp->th_code = htons((u_short)error);
	for (pe = errmsgs; pe->e_code >= 0; pe++)
		if (pe->e_code == error)
			break;
	if (pe->e_code < 0) {
		pe->e_msg = strerror(error - 100);
		tp->th_code = EUNDEF;   /* set 'undef' errorcode */
	}
	strcpy(tp->th_msg, pe->e_msg);
	length = strlen(pe->e_msg);
	tp->th_msg[length] = '\0';
	length += 5;

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending ERROR %d: %s", error, tp->th_msg);

	if (sendto(peer, buf, length, 0,
		(struct sockaddr *)&peer_sock, peer_sock.ss_len) != length)
		tftp_log(LOG_ERR, "send_error: %s", strerror(errno));
}

/*
 * Send an WRQ packet (write request).
 */
int
send_wrq(int peer, char *filename, char *mode)
{
	int n;
	struct tftphdr *tp;
	char *bp;
	char buf[MAXPKTSIZE];
	int size;

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending WRQ: filename: '%s', mode '%s'",
			filename, mode
		);

	DROPPACKETn("send_wrq", 1);

	tp = (struct tftphdr *)buf;
	tp->th_opcode = htons((u_short)WRQ);
	size = offsetof(struct tftphdr, th_stuff);

	bp = tp->th_stuff;
	strlcpy(bp, filename, sizeof(buf) - size);
	bp += strlen(filename);
	*bp = 0;
	bp++;
	size += strlen(filename) + 1;

	strlcpy(bp, mode, sizeof(buf) - size);
	bp += strlen(mode);
	*bp = 0;
	bp++;
	size += strlen(mode) + 1;

	if (options_rfc_enabled)
		size += make_options(peer, bp, sizeof(buf) - size);

	n = sendto(peer, buf, size, 0,
	    (struct sockaddr *)&peer_sock, peer_sock.ss_len);
	if (n != size) {
		tftp_log(LOG_ERR, "send_wrq: %s", strerror(errno));
		return (1);
	}
	return (0);
}

/*
 * Send an RRQ packet (write request).
 */
int
send_rrq(int peer, char *filename, char *mode)
{
	int n;
	struct tftphdr *tp;
	char *bp;
	char buf[MAXPKTSIZE];
	int size;

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending RRQ: filename: '%s', mode '%s'",
			filename, mode
		);

	DROPPACKETn("send_rrq", 1);

	tp = (struct tftphdr *)buf;
	tp->th_opcode = htons((u_short)RRQ);
	size = offsetof(struct tftphdr, th_stuff);

	bp = tp->th_stuff;
	strlcpy(bp, filename, sizeof(buf) - size);
	bp += strlen(filename);
	*bp = 0;
	bp++;
	size += strlen(filename) + 1;

	strlcpy(bp, mode, sizeof(buf) - size);
	bp += strlen(mode);
	*bp = 0;
	bp++;
	size += strlen(mode) + 1;

	if (options_rfc_enabled) {
		options[OPT_TSIZE].o_request = strdup("0");
		size += make_options(peer, bp, sizeof(buf) - size);
	}

	n = sendto(peer, buf, size, 0,
	    (struct sockaddr *)&peer_sock, peer_sock.ss_len);
	if (n != size) {
		tftp_log(LOG_ERR, "send_rrq: %d %s", n, strerror(errno));
		return (1);
	}
	return (0);
}

/*
 * Send an OACK packet (option acknowledgement).
 */
int
send_oack(int peer)
{
	struct tftphdr *tp;
	int size, i, n;
	char *bp;
	char buf[MAXPKTSIZE];

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending OACK");

	DROPPACKETn("send_oack", 0);

	/*
	 * Send back an options acknowledgement (only the ones with
	 * a reply for)
	 */
	tp = (struct tftphdr *)buf;
	bp = buf + 2;
	size = sizeof(buf) - 2;
	tp->th_opcode = htons((u_short)OACK);
	for (i = 0; options[i].o_type != NULL; i++) {
		if (options[i].o_reply != NULL) {
			n = snprintf(bp, size, "%s%c%s", options[i].o_type,
				     0, options[i].o_reply);
			bp += n+1;
			size -= n+1;
			if (size < 0) {
				tftp_log(LOG_ERR, "oack: buffer overflow");
				exit(1);
			}
		}
	}
	size = bp - buf;

	if (sendto(peer, buf, size, 0,
		(struct sockaddr *)&peer_sock, peer_sock.ss_len) != size) {
		tftp_log(LOG_INFO, "send_oack: %s", strerror(errno));
		return (1);
	}

	return (0);
}

/*
 * Send an ACK packet (acknowledgement).
 */
int
send_ack(int fp, uint16_t block)
{
	struct tftphdr *tp;
	int size;
	char buf[MAXPKTSIZE];

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending ACK for block %d", block);

	DROPPACKETn("send_ack", 0);

	tp = (struct tftphdr *)buf;
	size = sizeof(buf) - 2;
	tp->th_opcode = htons((u_short)ACK);
	tp->th_block = htons((u_short)block);
	size = 4;

	if (sendto(fp, buf, size, 0,
	    (struct sockaddr *)&peer_sock, peer_sock.ss_len) != size) {
		tftp_log(LOG_INFO, "send_ack: %s", strerror(errno));
		return (1);
	}

	return (0);
}

/*
 * Send a DATA packet
 */
int
send_data(int peer, uint16_t block, char *data, int size)
{
	char buf[MAXPKTSIZE];
	struct tftphdr *pkt;
	int n;

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Sending DATA packet %d of %d bytes",
			block, size);

	DROPPACKETn("send_data", 0);

	pkt = (struct tftphdr *)buf;

	pkt->th_opcode = htons((u_short)DATA);
	pkt->th_block = htons((u_short)block);
	memcpy(pkt->th_data, data, size);

	n = send_packet(peer, block, (char *)pkt, size + 4);
	return (n);
}


/*
 * Receive a packet
 */
static jmp_buf timeoutbuf;

static void
timeout(int sig __unused)
{

	/* tftp_log(LOG_DEBUG, "Timeout\n");	Inside a signal handler... */
	longjmp(timeoutbuf, 1);
}

int
receive_packet(int peer, char *data, int size, struct sockaddr_storage *from,
    int thistimeout)
{
	struct tftphdr *pkt;
	struct sockaddr_storage from_local;
	struct sockaddr_storage *pfrom;
	socklen_t fromlen;
	int n;
	static int timed_out;

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG,
		    "Waiting %d seconds for packet", timeoutpacket);

	pkt = (struct tftphdr *)data;

	signal(SIGALRM, timeout);
	timed_out = setjmp(timeoutbuf);
	alarm(thistimeout);

	if (timed_out != 0) {
		tftp_log(LOG_ERR, "receive_packet: timeout");
		alarm(0);
		return (RP_TIMEOUT);
	}

	pfrom = (from == NULL) ? &from_local : from;
	fromlen = sizeof(*pfrom);
	n = recvfrom(peer, data, size, 0, (struct sockaddr *)pfrom, &fromlen);

	alarm(0);

	DROPPACKETn("receive_packet", RP_TIMEOUT);

	if (n < 0) {
		tftp_log(LOG_ERR, "receive_packet: timeout");
		return (RP_TIMEOUT);
	}

	if (n < 0) {
		/* No idea what could have happened if it isn't a timeout */
		tftp_log(LOG_ERR, "receive_packet: %s", strerror(errno));
		return (RP_RECVFROM);
	}
	if (n < 4) {
		tftp_log(LOG_ERR,
		    "receive_packet: packet too small (%d bytes)", n);
		return (RP_TOOSMALL);
	}

	pkt->th_opcode = ntohs((u_short)pkt->th_opcode);
	if (pkt->th_opcode == DATA ||
	    pkt->th_opcode == ACK)
		pkt->th_block = ntohs((u_short)pkt->th_block);

	if (pkt->th_opcode == DATA && n > pktsize) {
		tftp_log(LOG_ERR, "receive_packet: packet too big");
		return (RP_TOOBIG);
	}

	if (((struct sockaddr_in *)(pfrom))->sin_addr.s_addr !=
	    ((struct sockaddr_in *)(&peer_sock))->sin_addr.s_addr) {
		tftp_log(LOG_ERR,
			"receive_packet: received packet from wrong source");
		return (RP_WRONGSOURCE);
	}

	if (pkt->th_opcode == ERROR) {
		tftp_log(pkt->th_code == EUNDEF ? LOG_DEBUG : LOG_ERR,
		    "Got ERROR packet: %s", pkt->th_msg);
		return (RP_ERROR);
	}

	if (debug&DEBUG_PACKETS)
		tftp_log(LOG_DEBUG, "Received %d bytes in a %s packet",
			n, packettype(pkt->th_opcode));

	return n - 4;
}
