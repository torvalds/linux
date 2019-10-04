/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_MEMSWAP_H_
#define PERF_MEMSWAP_H_

#include <linux/types.h>

union u64_swap {
	u64 val64;
	u32 val32[2];
};

void mem_bswap_64(void *src, int byte_size);
void mem_bswap_32(void *src, int byte_size);

#endif /* PERF_MEMSWAP_H_ */
