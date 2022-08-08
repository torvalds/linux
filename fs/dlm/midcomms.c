// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

/*
 * midcomms.c
 *
 * This is the appallingly named "mid-level" comms layer.
 *
 * Its purpose is to take packets from the "real" comms layer,
 * split them up into packets and pass them to the interested
 * part of the locking mechanism.
 *
 * It also takes messages from the locking layer, formats them
 * into packets and sends them to the comms layer.
 */

#include "dlm_internal.h"
#include "lowcomms.h"
#include "config.h"
#include "lock.h"
#include "midcomms.h"

/*
 * Called from the low-level comms layer to process a buffer of
 * commands.
 */

int dlm_process_incoming_buffer(int nodeid, unsigned char *buf, int len)
{
	const unsigned char *ptr = buf;
	const struct dlm_header *hd;
	uint16_t msglen;
	int ret = 0;

	while (len >= sizeof(struct dlm_header)) {
		hd = (struct dlm_header *)ptr;

		/* no message should be more than DEFAULT_BUFFER_SIZE or
		 * less than dlm_header size.
		 *
		 * Some messages does not have a 8 byte length boundary yet
		 * which can occur in a unaligned memory access of some dlm
		 * messages. However this problem need to be fixed at the
		 * sending side, for now it seems nobody run into architecture
		 * related issues yet but it slows down some processing.
		 * Fixing this issue should be scheduled in future by doing
		 * the next major version bump.
		 */
		msglen = le16_to_cpu(hd->h_length);
		if (msglen > DEFAULT_BUFFER_SIZE ||
		    msglen < sizeof(struct dlm_header)) {
			log_print("received invalid length header: %u from node %d, will abort message parsing",
				  msglen, nodeid);
			return -EBADMSG;
		}

		/* caller will take care that leftover
		 * will be parsed next call with more data
		 */
		if (msglen > len)
			break;

		switch (hd->h_cmd) {
		case DLM_MSG:
			if (msglen < sizeof(struct dlm_message)) {
				log_print("dlm msg too small: %u, will skip this message",
					  msglen);
				goto skip;
			}

			break;
		case DLM_RCOM:
			if (msglen < sizeof(struct dlm_rcom)) {
				log_print("dlm rcom msg too small: %u, will skip this message",
					  msglen);
				goto skip;
			}

			break;
		default:
			log_print("unsupported h_cmd received: %u, will skip this message",
				  hd->h_cmd);
			goto skip;
		}

		dlm_receive_buffer((union dlm_packet *)ptr, nodeid);

skip:
		ret += msglen;
		len -= msglen;
		ptr += msglen;
	}

	return ret;
}

