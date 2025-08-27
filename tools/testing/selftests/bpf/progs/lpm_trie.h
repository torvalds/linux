/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PROGS_LPM_TRIE_H
#define __PROGS_LPM_TRIE_H

struct trie_key {
	__u32 prefixlen;
	__u32 data;
};

/* Benchmark operations */
enum {
	LPM_OP_NOOP = 0,
	LPM_OP_BASELINE,
	LPM_OP_LOOKUP,
	LPM_OP_INSERT,
	LPM_OP_UPDATE,
	LPM_OP_DELETE,
	LPM_OP_FREE
};

/*
 * Return values from run_bench.
 *
 * Negative values are also allowed and represent kernel error codes.
 */
#define LPM_BENCH_SUCCESS	0
#define LPM_BENCH_REINIT_MAP 	1	/* Reset trie to initial state for current op */

#endif
