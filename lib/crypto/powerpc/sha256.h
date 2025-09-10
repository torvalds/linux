/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 Secure Hash Algorithm, SPE optimized
 *
 * Based on generic implementation. The assembler module takes care
 * about the SPE registers so it can run from interrupt context.
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <asm/switch_to.h>
#include <linux/preempt.h>

/*
 * MAX_BYTES defines the number of bytes that are allowed to be processed
 * between preempt_disable() and preempt_enable(). SHA256 takes ~2,000
 * operations per 64 bytes. e500 cores can issue two arithmetic instructions
 * per clock cycle using one 32/64 bit unit (SU1) and one 32 bit unit (SU2).
 * Thus 1KB of input data will need an estimated maximum of 18,000 cycles.
 * Headroom for cache misses included. Even with the low end model clocked
 * at 667 MHz this equals to a critical time window of less than 27us.
 *
 */
#define MAX_BYTES 1024

extern void ppc_spe_sha256_transform(struct sha256_block_state *state,
				     const u8 *src, u32 blocks);

static void spe_begin(void)
{
	/* We just start SPE operations and will save SPE registers later. */
	preempt_disable();
	enable_kernel_spe();
}

static void spe_end(void)
{
	disable_kernel_spe();
	/* reenable preemption */
	preempt_enable();
}

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	do {
		/* cut input data into smaller blocks */
		u32 unit = min_t(size_t, nblocks,
				 MAX_BYTES / SHA256_BLOCK_SIZE);

		spe_begin();
		ppc_spe_sha256_transform(state, data, unit);
		spe_end();

		data += unit * SHA256_BLOCK_SIZE;
		nblocks -= unit;
	} while (nblocks);
}
