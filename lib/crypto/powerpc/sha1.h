/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-1 optimized for PowerPC
 *
 * Copyright (c) 2015 Markus Stockhausen <stockhausen@collogia.de>
 */

#include <asm/switch_to.h>
#include <linux/preempt.h>

#ifdef CONFIG_SPE
/*
 * MAX_BYTES defines the number of bytes that are allowed to be processed
 * between preempt_disable() and preempt_enable(). SHA1 takes ~1000
 * operations per 64 bytes. e500 cores can issue two arithmetic instructions
 * per clock cycle using one 32/64 bit unit (SU1) and one 32 bit unit (SU2).
 * Thus 2KB of input data will need an estimated maximum of 18,000 cycles.
 * Headroom for cache misses included. Even with the low end model clocked
 * at 667 MHz this equals to a critical time window of less than 27us.
 *
 */
#define MAX_BYTES 2048

asmlinkage void ppc_spe_sha1_transform(struct sha1_block_state *state,
				       const u8 *data, u32 nblocks);

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

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	do {
		u32 unit = min_t(size_t, nblocks, MAX_BYTES / SHA1_BLOCK_SIZE);

		spe_begin();
		ppc_spe_sha1_transform(state, data, unit);
		spe_end();

		data += unit * SHA1_BLOCK_SIZE;
		nblocks -= unit;
	} while (nblocks);
}
#else /* CONFIG_SPE */
asmlinkage void powerpc_sha_transform(struct sha1_block_state *state,
				      const u8 data[SHA1_BLOCK_SIZE]);

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	do {
		powerpc_sha_transform(state, data);
		data += SHA1_BLOCK_SIZE;
	} while (--nblocks);
}
#endif /* !CONFIG_SPE */
