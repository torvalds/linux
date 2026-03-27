/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Optimited xor routines
 *
 * Copyright IBM Corp. 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
extern struct xor_block_template xor_block_xc;

static __always_inline void __init arch_xor_init(void)
{
	xor_force(&xor_block_xc);
}
