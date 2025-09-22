/*	$OpenBSD: ip_id.c,v 1.26 2025/01/01 13:44:22 bluhm Exp $ */

/*
 * Copyright (c) 2008 Theo de Raadt, Ryan McBride
 *
 * Slightly different algorithm from the one designed by
 * Matthew Dillon <dillon@backplane.com> for The DragonFly Project
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Random ip sequence number generator.  Use the system PRNG to shuffle
 * the 65536 entry ID space.  We reshuffle the ID we pick out of the array
 * into the previous 32767 cells, providing an guarantee that an ID will not
 * be reused for at least 32768 calls.
 */
#include <sys/param.h>
#include <sys/systm.h>

static u_int16_t ip_shuffle[65536];
static int isindex = 0;

u_int16_t ip_randomid(void);

/*
 * Return a random IP id.  Shuffle the new value we get into the previous half
 * of the ip_shuffle ring (-32767 or swap with ourself), to avoid duplicates
 * occurring too quickly but also still be random.
 *
 * 0 is a special IP ID -- don't return it.
 */
u_int16_t
ip_randomid(void)
{
	static int ipid_initialized;
	u_int16_t si, r;
	int i, i2;

	if (!ipid_initialized) {
		ipid_initialized = 1;

		/*
		 * Initialize with a random permutation. Do so using Knuth
		 * which avoids the exchange in the Durstenfeld shuffle.
		 * (See "The Art of Computer Programming, Vol 2" 3rd ed, pg. 145).
		 *
		 * Even if our PRNG is imperfect at boot time, we have deferred
		 * doing this until the first packet being sent and now must
		 * generate an ID.
		 */
		for (i = 0; i < nitems(ip_shuffle); ++i) {
			i2 = arc4random_uniform(i + 1);
			ip_shuffle[i] = ip_shuffle[i2];
			ip_shuffle[i2] = i;
		}
	}

	do {
		arc4random_buf(&si, sizeof(si));
		i = isindex & 0xFFFF;
		i2 = (isindex - (si & 0x7FFF)) & 0xFFFF;
		r = ip_shuffle[i];
		ip_shuffle[i] = ip_shuffle[i2];
		ip_shuffle[i2] = r;
		isindex++;
	} while (r == 0);

	return (r);
}
