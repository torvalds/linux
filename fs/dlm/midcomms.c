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

#include <asm/unaligned.h>

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

		/* no message should be more than this otherwise we
		 * cannot deliver this message to upper layers
		 */
		msglen = get_unaligned_le16(&hd->h_length);
		if (msglen > DEFAULT_BUFFER_SIZE) {
			log_print("received invalid length header: %u, will abort message parsing",
				  msglen);
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

		/* for aligned memory access, we just copy current message
		 * to begin of the buffer which contains already parsed buffer
		 * data and should provide align access for upper layers
		 * because the start address of the buffer has a aligned
		 * address. This memmove can be removed when the upperlayer
		 * is capable of unaligned memory access.
		 */
		memmove(buf, ptr, msglen);
		dlm_receive_buffer((union dlm_packet *)buf, nodeid);

skip:
		ret += msglen;
		len -= msglen;
		ptr += msglen;
	}

	return ret;
}

