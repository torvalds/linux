// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <sodium/crypto_stream_chacha20.h>
#include <sys/random.h>
#include <string.h>
#include <stdint.h>
#include "../kselftest.h"

extern void __arch_chacha20_blocks_nostack(uint8_t *dst_bytes, const uint8_t *key, uint32_t *counter, size_t nblocks);

int main(int argc, char *argv[])
{
	enum { TRIALS = 1000, BLOCKS = 128, BLOCK_SIZE = 64 };
	static const uint8_t nonce[8] = { 0 };
	uint32_t counter[2];
	uint8_t key[32];
	uint8_t output1[BLOCK_SIZE * BLOCKS], output2[BLOCK_SIZE * BLOCKS];

	ksft_print_header();
	ksft_set_plan(1);

	for (unsigned int trial = 0; trial < TRIALS; ++trial) {
		if (getrandom(key, sizeof(key), 0) != sizeof(key)) {
			printf("getrandom() failed!\n");
			return KSFT_SKIP;
		}
		crypto_stream_chacha20(output1, sizeof(output1), nonce, key);
		for (unsigned int split = 0; split < BLOCKS; ++split) {
			memset(output2, 'X', sizeof(output2));
			memset(counter, 0, sizeof(counter));
			if (split)
				__arch_chacha20_blocks_nostack(output2, key, counter, split);
			__arch_chacha20_blocks_nostack(output2 + split * BLOCK_SIZE, key, counter, BLOCKS - split);
			if (memcmp(output1, output2, sizeof(output1)))
				return KSFT_FAIL;
		}
	}
	ksft_test_result_pass("chacha: PASS\n");
	return KSFT_PASS;
}
