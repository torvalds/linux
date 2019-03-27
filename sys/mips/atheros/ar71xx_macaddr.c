/*-
 * Copyright (c) 2015, Adrian Chadd <adrian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/libkern.h>

#include <net/ethernet.h>

#include <mips/atheros/ar71xx_macaddr.h>

/*
 * Some boards don't have a separate MAC address for each individual
 * device on-board, but instead need to derive them from a single MAC
 * address stored somewhere.
 */
uint8_t ar71xx_board_mac_addr[ETHER_ADDR_LEN];

/*
 * Initialise a MAC address 'dst' from a MAC address 'src'.
 *
 * 'offset' is added to the low three bytes to allow for sequential
 * MAC addresses to be derived from a single one.
 *
 * 'is_local' is whether this 'dst' should be made a local MAC address.
 *
 * Returns 0 if it was successfully initialised, -1 on error.
 */
int
ar71xx_mac_addr_init(unsigned char *dst, const unsigned char *src,
    int offset, int is_local)
{
	int t;

	if (dst == NULL || src == NULL)
		return (-1);

	/* XXX TODO: validate 'src' is a valid MAC address */

	t = (((uint32_t) src[3]) << 16)
	    + (((uint32_t) src[4]) << 8)
	    + ((uint32_t) src[5]);

	/* Note: this is handles both positive and negative offsets */
	t += offset;

	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = (t >> 16) & 0xff;
	dst[4] = (t >> 8) & 0xff;
	dst[5] = t & 0xff;

	if (is_local)
		dst[0] |= 0x02;

	/* Everything's okay */
	return (0);
}

/*
 * Initialise a random MAC address for use by if_arge.c and whatever
 * else requires it.
 *
 * Returns 0 on success, -1 on error.
 */
int
ar71xx_mac_addr_random_init(unsigned char *dst)
{
	uint32_t rnd;

	rnd = arc4random();

	dst[0] = 'b';
	dst[1] = 's';
	dst[2] = 'd';
	dst[3] = (rnd >> 24) & 0xff;
	dst[4] = (rnd >> 16) & 0xff;
	dst[5] = (rnd >> 8) & 0xff;

	return (0);
}
