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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/tftp.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "tftp-file.h"
#include "tftp-io.h"
#include "tftp-utils.h"
#include "tftp-options.h"
#include "tftp-transfer.h"

/*
 * Send a file via the TFTP data session.
 */
void
tftp_send(int peer, uint16_t *block, struct tftp_stats *ts)
{
	struct tftphdr *rp;
	int size, n_data, n_ack, try;
	uint16_t oldblock;
	char sendbuffer[MAXPKTSIZE];
	char recvbuffer[MAXPKTSIZE];

	rp = (struct tftphdr *)recvbuffer;
	*block = 1;
	ts->amount = 0;
	do {
		if (debug&DEBUG_SIMPLE)
			tftp_log(LOG_DEBUG, "Sending block %d", *block);

		size = read_file(sendbuffer, segsize);
		if (size < 0) {
			tftp_log(LOG_ERR, "read_file returned %d", size);
			send_error(peer, errno + 100);
			goto abort;
		}

		for (try = 0; ; try++) {
			n_data = send_data(peer, *block, sendbuffer, size);
			if (n_data > 0) {
				if (try == maxtimeouts) {
					tftp_log(LOG_ERR,
					    "Cannot send DATA packet #%d, "
					    "giving up", *block);
					return;
				}
				tftp_log(LOG_ERR,
				    "Cannot send DATA packet #%d, trying again",
				    *block);
				continue;
			}

			n_ack = receive_packet(peer, recvbuffer,
			    MAXPKTSIZE, NULL, timeoutpacket);
			if (n_ack < 0) {
				if (n_ack == RP_TIMEOUT) {
					if (try == maxtimeouts) {
						tftp_log(LOG_ERR,
						    "Timeout #%d send ACK %d "
						    "giving up", try, *block);
						return;
					}
					tftp_log(LOG_WARNING,
					    "Timeout #%d on ACK %d",
					    try, *block);
					continue;
				}

				/* Either read failure or ERROR packet */
				if (debug&DEBUG_SIMPLE)
					tftp_log(LOG_ERR, "Aborting: %s",
					    rp_strerror(n_ack));
				goto abort;
			}
			if (rp->th_opcode == ACK) {
				ts->blocks++;
				if (rp->th_block == *block) {
					ts->amount += size;
					break;
				}

				/* Re-synchronize with the other side */
				(void) synchnet(peer);
				if (rp->th_block == (*block - 1)) {
					ts->retries++;
					continue;
				}
			}

		}
		oldblock = *block;
		(*block)++;
		if (oldblock > *block) {
			if (options[OPT_ROLLOVER].o_request == NULL) {
				/*
				 * "rollover" option not specified in
				 * tftp client.  Default to rolling block
				 * counter to 0.
				 */
				*block = 0;
			} else {
				*block = atoi(options[OPT_ROLLOVER].o_request);
			}

			ts->rollovers++;
		}
		gettimeofday(&(ts->tstop), NULL);
	} while (size == segsize);
abort:
	return;
}

/*
 * Receive a file via the TFTP data session.
 *
 * - It could be that the first block has already arrived while
 *   trying to figure out if we were receiving options or not. In
 *   that case it is passed to this function.
 */
void
tftp_receive(int peer, uint16_t *block, struct tftp_stats *ts,
    struct tftphdr *firstblock, size_t fb_size)
{
	struct tftphdr *rp;
	uint16_t oldblock;
	int n_data, n_ack, writesize, i, retry;
	char recvbuffer[MAXPKTSIZE];

	ts->amount = 0;

	if (firstblock != NULL) {
		writesize = write_file(firstblock->th_data, fb_size);
		ts->amount += writesize;
		for (i = 0; ; i++) {
			n_ack = send_ack(peer, *block);
			if (n_ack > 0) {
				if (i == maxtimeouts) {
					tftp_log(LOG_ERR,
					    "Cannot send ACK packet #%d, "
					    "giving up", *block);
					return;
				}
				tftp_log(LOG_ERR,
				    "Cannot send ACK packet #%d, trying again",
				    *block);
				continue;
			}

			break;
		}

		if (fb_size != segsize) {
			gettimeofday(&(ts->tstop), NULL);
			return;
		}
	}

	rp = (struct tftphdr *)recvbuffer;
	do {
		oldblock = *block;
		(*block)++;
		if (oldblock > *block) {
			if (options[OPT_ROLLOVER].o_request == NULL) {
				/*
				 * "rollover" option not specified in
				 * tftp client.  Default to rolling block
				 * counter to 0.
				 */
				*block = 0;
			} else {
				*block = atoi(options[OPT_ROLLOVER].o_request);
			}

			ts->rollovers++;
		}

		for (retry = 0; ; retry++) {
			if (debug&DEBUG_SIMPLE)
				tftp_log(LOG_DEBUG,
				    "Receiving DATA block %d", *block);

			n_data = receive_packet(peer, recvbuffer,
			    MAXPKTSIZE, NULL, timeoutpacket);
			if (n_data < 0) {
				if (retry == maxtimeouts) {
					tftp_log(LOG_ERR,
					    "Timeout #%d on DATA block %d, "
					    "giving up", retry, *block);
					return;
				}
				if (n_data == RP_TIMEOUT) {
					tftp_log(LOG_WARNING,
					    "Timeout #%d on DATA block %d",
					    retry, *block);
					send_ack(peer, oldblock);
					continue;
				}

				/* Either read failure or ERROR packet */
				if (debug&DEBUG_SIMPLE)
					tftp_log(LOG_DEBUG, "Aborting: %s",
					    rp_strerror(n_data));
				goto abort;
			}
			if (rp->th_opcode == DATA) {
				ts->blocks++;

				if (rp->th_block == *block)
					break;

				tftp_log(LOG_WARNING,
				    "Expected DATA block %d, got block %d",
				    *block, rp->th_block);

				/* Re-synchronize with the other side */
				(void) synchnet(peer);
				if (rp->th_block == (*block-1)) {
					tftp_log(LOG_INFO, "Trying to sync");
					*block = oldblock;
					ts->retries++;
					goto send_ack;	/* rexmit */
				}

			} else {
				tftp_log(LOG_WARNING,
				    "Expected DATA block, got %s block",
				    packettype(rp->th_opcode));
			}
		}

		if (n_data > 0) {
			writesize = write_file(rp->th_data, n_data);
			ts->amount += writesize;
			if (writesize <= 0) {
				tftp_log(LOG_ERR,
				    "write_file returned %d", writesize);
				if (writesize < 0)
					send_error(peer, errno + 100);
				else
					send_error(peer, ENOSPACE);
				goto abort;
			}
			if (n_data != segsize)
				write_close();
		}

send_ack:
		for (i = 0; ; i++) {
			n_ack = send_ack(peer, *block);
			if (n_ack > 0) {

				if (i == maxtimeouts) {
					tftp_log(LOG_ERR,
					    "Cannot send ACK packet #%d, "
					    "giving up", *block);
					return;
				}

				tftp_log(LOG_ERR,
				    "Cannot send ACK packet #%d, trying again",
				    *block);
				continue;
			}

			break;
		}
		gettimeofday(&(ts->tstop), NULL);
	} while (n_data == segsize);

	/* Don't do late packet management for the client implementation */
	if (acting_as_client)
		return;

	for (i = 0; ; i++) {
		n_data = receive_packet(peer, (char *)rp, pktsize,
		    NULL, timeoutpacket);
		if (n_data <= 0)
			break;
		if (n_data > 0 &&
		    rp->th_opcode == DATA &&	/* and got a data block */
		    *block == rp->th_block)	/* then my last ack was lost */
			send_ack(peer, *block);	/* resend final ack */
	}

abort:
	return;
}
