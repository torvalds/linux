/* Copyright (C) 2006-2014 B.A.T.M.A.N. contributors:
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

#ifndef _NET_BATMAN_ADV_BITARRAY_H_
#define _NET_BATMAN_ADV_BITARRAY_H_

/* Returns 1 if the corresponding bit in the given seq_bits indicates true
 * and curr_seqno is within range of last_seqno. Otherwise returns 0.
 */
static inline int batadv_test_bit(const unsigned long *seq_bits,
				  uint32_t last_seqno, uint32_t curr_seqno)
{
	int32_t diff;

	diff = last_seqno - curr_seqno;
	if (diff < 0 || diff >= BATADV_TQ_LOCAL_WINDOW_SIZE)
		return 0;
	return test_bit(diff, seq_bits) != 0;
}

/* turn corresponding bit on, so we can remember that we got the packet */
static inline void batadv_set_bit(unsigned long *seq_bits, int32_t n)
{
	/* if too old, just drop it */
	if (n < 0 || n >= BATADV_TQ_LOCAL_WINDOW_SIZE)
		return;

	set_bit(n, seq_bits); /* turn the position on */
}

/* receive and process one packet, returns 1 if received seq_num is considered
 * new, 0 if old
 */
int batadv_bit_get_packet(void *priv, unsigned long *seq_bits,
			  int32_t seq_num_diff, int set_mark);

#endif /* _NET_BATMAN_ADV_BITARRAY_H_ */
