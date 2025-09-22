/*	$OpenBSD: mips64r2.S,v 1.4 2017/06/11 03:35:30 visa Exp $	*/

/*
 * Copyright (c) 2011 Miodrag Vallat.
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
 * Routines to access extra coprocessor 0 registers
 */

#include <machine/asm.h>
#include <mips64/mips_cpu.h>

	.set	noreorder
	.set	mips64r2

LEAF(cp0_get_config_1, 0)
	j	ra
	mfc0	v0, COP_0_CONFIG, 1
END(cp0_get_config_1)

LEAF(cp0_get_config_2, 0)
	j	ra
	mfc0	v0, COP_0_CONFIG, 2
END(cp0_get_config_2)

LEAF(cp0_get_config_3, 0)
	j	ra
	mfc0	v0, COP_0_CONFIG, 3
END(cp0_get_config_3)

LEAF(cp0_get_config_4, 0)
	j	ra
	mfc0	v0, COP_0_CONFIG, 4
END(cp0_get_config_4)

LEAF(cp0_get_pagegrain, 0)
	j	ra
	 mfc0	v0, COP_0_TLB_PG_GRAIN
END(cp0_get_pagegrain)

LEAF(cp0_set_pagegrain, 0)
	mtc0	a0, COP_0_TLB_PG_GRAIN
	jr.hb	ra
	 nop
END(cp0_set_pagegrain)
