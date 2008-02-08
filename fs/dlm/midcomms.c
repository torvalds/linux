/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
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


static void copy_from_cb(void *dst, const void *base, unsigned offset,
			 unsigned len, unsigned limit)
{
	unsigned copy = len;

	if ((copy + offset) > limit)
		copy = limit - offset;
	memcpy(dst, base + offset, copy);
	len -= copy;
	if (len)
		memcpy(dst + copy, base, len);
}

/*
 * Called from the low-level comms layer to process a buffer of
 * commands.
 *
 * Only complete messages are processed here, any "spare" bytes from
 * the end of a buffer are saved and tacked onto the front of the next
 * message that comes in. I doubt this will happen very often but we
 * need to be able to cope with it and I don't want the task to be waiting
 * for packets to come in when there is useful work to be done.
 */

int dlm_process_incoming_buffer(int nodeid, const void *base,
				unsigned offset, unsigned len, unsigned limit)
{
	union {
		unsigned char __buf[DLM_INBUF_LEN];
		/* this is to force proper alignment on some arches */
		union dlm_packet p;
	} __tmp;
	union dlm_packet *p = &__tmp.p;
	int ret = 0;
	int err = 0;
	uint16_t msglen;
	uint32_t lockspace;

	while (len > sizeof(struct dlm_header)) {

		/* Copy just the header to check the total length.  The
		   message may wrap around the end of the buffer back to the
		   start, so we need to use a temp buffer and copy_from_cb. */

		copy_from_cb(p, base, offset, sizeof(struct dlm_header),
			     limit);

		msglen = le16_to_cpu(p->header.h_length);
		lockspace = p->header.h_lockspace;

		err = -EINVAL;
		if (msglen < sizeof(struct dlm_header))
			break;
		if (p->header.h_cmd == DLM_MSG) {
			if (msglen < sizeof(struct dlm_message))
				break;
		} else {
			if (msglen < sizeof(struct dlm_rcom))
				break;
		}
		err = -E2BIG;
		if (msglen > dlm_config.ci_buffer_size) {
			log_print("message size %d from %d too big, buf len %d",
				  msglen, nodeid, len);
			break;
		}
		err = 0;

		/* If only part of the full message is contained in this
		   buffer, then do nothing and wait for lowcomms to call
		   us again later with more data.  We return 0 meaning
		   we've consumed none of the input buffer. */

		if (msglen > len)
			break;

		/* Allocate a larger temp buffer if the full message won't fit
		   in the buffer on the stack (which should work for most
		   ordinary messages). */

		if (msglen > sizeof(__tmp) && p == &__tmp.p) {
			p = kmalloc(dlm_config.ci_buffer_size, GFP_KERNEL);
			if (p == NULL)
				return ret;
		}

		copy_from_cb(p, base, offset, msglen, limit);

		BUG_ON(lockspace != p->header.h_lockspace);

		ret += msglen;
		offset += msglen;
		offset &= (limit - 1);
		len -= msglen;

		dlm_receive_buffer(p, nodeid);
	}

	if (p != &__tmp.p)
		kfree(p);

	return err ? err : ret;
}

