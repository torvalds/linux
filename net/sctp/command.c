/* SCTP kernel implementation Copyright (C) 1999-2001
 * Cisco, Motorola, and IBM
 * Copyright 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions manipulate sctp command sequences.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Initialize a block of memory as a command sequence. */
int sctp_init_cmd_seq(sctp_cmd_seq_t *seq)
{
	memset(seq, 0, sizeof(sctp_cmd_seq_t));
	return 1;		/* We always succeed.  */
}

/* Add a command to a sctp_cmd_seq_t.
 * Return 0 if the command sequence is full.
 */
int sctp_add_cmd(sctp_cmd_seq_t *seq, sctp_verb_t verb, sctp_arg_t obj)
{
	if (seq->next_free_slot >= SCTP_MAX_NUM_COMMANDS)
		goto fail;

	seq->cmds[seq->next_free_slot].verb = verb;
	seq->cmds[seq->next_free_slot++].obj = obj;

	return 1;

fail:
	return 0;
}

/* Return the next command structure in a sctp_cmd_seq.
 * Returns NULL at the end of the sequence.
 */
sctp_cmd_t *sctp_next_cmd(sctp_cmd_seq_t *seq)
{
	sctp_cmd_t *retval = NULL;

	if (seq->next_cmd < seq->next_free_slot)
		retval = &seq->cmds[seq->next_cmd++];

	return retval;
}

