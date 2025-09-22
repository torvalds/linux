/*	$OpenBSD: onewire_bitbang.c,v 1.2 2010/07/02 03:13:42 tedu Exp $	*/

/*
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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
 * 1-Wire bus bit-banging routines.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/onewire/onewirevar.h>

int
onewire_bb_reset(const struct onewire_bbops *ops, void *arg)
{
	int s, rv, i;

	s = splhigh();
	ops->bb_tx(arg);
	ops->bb_set(arg, 0);
	DELAY(480);
	ops->bb_set(arg, 1);
	ops->bb_rx(arg);
	DELAY(30);
	for (i = 0; i < 6; i++) {
		if ((rv = ops->bb_get(arg)) == 0)
			break;
		DELAY(20);
	}
	DELAY(450);
	splx(s);

	return (rv);
}

int
onewire_bb_bit(const struct onewire_bbops *ops, void *arg, int value)
{
	int s, rv, i;

	s = splhigh();
	ops->bb_tx(arg);
	ops->bb_set(arg, 0);
	DELAY(2);
	rv = 0;
	if (value) {
		ops->bb_set(arg, 1);
		ops->bb_rx(arg);
		for (i = 0; i < 15; i++) {
			if ((rv = ops->bb_get(arg)) == 0)
				break;
			DELAY(2);
		}
		ops->bb_tx(arg);
	}
	DELAY(60);
	ops->bb_set(arg, 1);
	DELAY(5);
	splx(s);

	return (rv);
}
