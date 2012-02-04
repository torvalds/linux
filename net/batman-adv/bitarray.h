/*
 * Copyright (C) 2006-2012 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_BITARRAY_H_
#define _NET_BATMAN_ADV_BITARRAY_H_

/* returns true if the corresponding bit in the given seq_bits indicates true
 * and curr_seqno is within range of last_seqno */
static inline int bat_test_bit(const unsigned long *seq_bits,
			       uint32_t last_seqno, uint32_t curr_seqno)
{
	int32_t diff;

	diff = last_seqno - curr_seqno;
	if (diff < 0 || diff >= TQ_LOCAL_WINDOW_SIZE)
		return 0;
	else
		return  test_bit(diff, seq_bits);
}

/* turn corresponding bit on, so we can remember that we got the packet */
static inline void bat_set_bit(unsigned long *seq_bits, int32_t n)
{
	/* if too old, just drop it */
	if (n < 0 || n >= TQ_LOCAL_WINDOW_SIZE)
		return;

	set_bit(n, seq_bits); /* turn the position on */
}

/* receive and process one packet, returns 1 if received seq_num is considered
 * new, 0 if old  */
int bit_get_packet(void *priv, unsigned long *seq_bits,
		   int32_t seq_num_diff, int set_mark);

#endif /* _NET_BATMAN_ADV_BITARRAY_H_ */
