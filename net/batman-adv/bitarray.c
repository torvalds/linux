/* Copyright (C) 2006-2016  B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "bitarray.h"
#include "main.h"

#include <linux/bitmap.h>

#include "log.h"

/* shift the packet array by n places. */
static void batadv_bitmap_shift_left(unsigned long *seq_bits, s32 n)
{
	if (n <= 0 || n >= BATADV_TQ_LOCAL_WINDOW_SIZE)
		return;

	bitmap_shift_left(seq_bits, seq_bits, n, BATADV_TQ_LOCAL_WINDOW_SIZE);
}

/**
 * batadv_bit_get_packet - receive and process one packet within the sequence
 *  number window
 * @priv: the bat priv with all the soft interface information
 * @seq_bits: pointer to the sequence number receive packet
 * @seq_num_diff: difference between the current/received sequence number and
 *  the last sequence number
 * @set_mark: whether this packet should be marked in seq_bits
 *
 * Return: true if the window was moved (either new or very old),
 *  false if the window was not moved/shifted.
 */
bool batadv_bit_get_packet(void *priv, unsigned long *seq_bits,
			   s32 seq_num_diff, int set_mark)
{
	struct batadv_priv *bat_priv = priv;

	/* sequence number is slightly older. We already got a sequence number
	 * higher than this one, so we just mark it.
	 */
	if (seq_num_diff <= 0 && seq_num_diff > -BATADV_TQ_LOCAL_WINDOW_SIZE) {
		if (set_mark)
			batadv_set_bit(seq_bits, -seq_num_diff);
		return false;
	}

	/* sequence number is slightly newer, so we shift the window and
	 * set the mark if required
	 */
	if (seq_num_diff > 0 && seq_num_diff < BATADV_TQ_LOCAL_WINDOW_SIZE) {
		batadv_bitmap_shift_left(seq_bits, seq_num_diff);

		if (set_mark)
			batadv_set_bit(seq_bits, 0);
		return true;
	}

	/* sequence number is much newer, probably missed a lot of packets */
	if (seq_num_diff >= BATADV_TQ_LOCAL_WINDOW_SIZE &&
	    seq_num_diff < BATADV_EXPECTED_SEQNO_RANGE) {
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "We missed a lot of packets (%i) !\n",
			   seq_num_diff - 1);
		bitmap_zero(seq_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);
		if (set_mark)
			batadv_set_bit(seq_bits, 0);
		return true;
	}

	/* received a much older packet. The other host either restarted
	 * or the old packet got delayed somewhere in the network. The
	 * packet should be dropped without calling this function if the
	 * seqno window is protected.
	 *
	 * seq_num_diff <= -BATADV_TQ_LOCAL_WINDOW_SIZE
	 * or
	 * seq_num_diff >= BATADV_EXPECTED_SEQNO_RANGE
	 */
	batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
		   "Other host probably restarted!\n");

	bitmap_zero(seq_bits, BATADV_TQ_LOCAL_WINDOW_SIZE);
	if (set_mark)
		batadv_set_bit(seq_bits, 0);

	return true;
}
