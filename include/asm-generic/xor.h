/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/asm-generic/xor.h
 *
 * Generic optimized RAID-5 checksumming functions.
 */

extern struct xor_block_template xor_block_8regs;
extern struct xor_block_template xor_block_32regs;
extern struct xor_block_template xor_block_8regs_p;
extern struct xor_block_template xor_block_32regs_p;
