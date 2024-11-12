// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#include <linux/compiler.h>
#include <tools/le_byteshift.h>
#include <sys/random.h>
#include <sys/auxv.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../kselftest.h"

#if defined(__aarch64__)
static bool cpu_has_capabilities(void)
{
	return getauxval(AT_HWCAP) & HWCAP_ASIMD;
}
#elif defined(__s390x__)
static bool cpu_has_capabilities(void)
{
	return getauxval(AT_HWCAP) & HWCAP_S390_VXRS;
}
#else
static bool cpu_has_capabilities(void)
{
	return true;
}
#endif

static uint32_t rol32(uint32_t word, unsigned int shift)
{
	return (word << (shift & 31)) | (word >> ((-shift) & 31));
}

static void reference_chacha20_blocks(uint8_t *dst_bytes, const uint32_t *key, uint32_t *counter, size_t nblocks)
{
	uint32_t s[16] = {
		0x61707865U, 0x3320646eU, 0x79622d32U, 0x6b206574U,
		key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
		counter[0], counter[1], 0, 0
	};

	while (nblocks--) {
		uint32_t x[16];
		memcpy(x, s, sizeof(x));
		for (unsigned int r = 0; r < 20; r += 2) {
		#define QR(a, b, c, d) ( \
			x[a] += x[b], \
			x[d] = rol32(x[d] ^ x[a], 16), \
			x[c] += x[d], \
			x[b] = rol32(x[b] ^ x[c], 12), \
			x[a] += x[b], \
			x[d] = rol32(x[d] ^ x[a], 8), \
			x[c] += x[d], \
			x[b] = rol32(x[b] ^ x[c], 7))

			QR(0, 4, 8, 12);
			QR(1, 5, 9, 13);
			QR(2, 6, 10, 14);
			QR(3, 7, 11, 15);
			QR(0, 5, 10, 15);
			QR(1, 6, 11, 12);
			QR(2, 7, 8, 13);
			QR(3, 4, 9, 14);
		}
		for (unsigned int i = 0; i < 16; ++i, dst_bytes += sizeof(uint32_t))
			put_unaligned_le32(x[i] + s[i], dst_bytes);
		if (!++s[12])
			++s[13];
	}
	counter[0] = s[12];
	counter[1] = s[13];
}

void __weak __arch_chacha20_blocks_nostack(uint8_t *dst_bytes, const uint32_t *key, uint32_t *counter, size_t nblocks)
{
	ksft_exit_skip("Not implemented on architecture\n");
}

int main(int argc, char *argv[])
{
	enum { TRIALS = 1000, BLOCKS = 128, BLOCK_SIZE = 64 };
	uint32_t key[8], counter1[2], counter2[2];
	uint8_t output1[BLOCK_SIZE * BLOCKS], output2[BLOCK_SIZE * BLOCKS];

	ksft_print_header();
	if (!cpu_has_capabilities())
		ksft_exit_skip("Required CPU capabilities missing\n");
	ksft_set_plan(1);

	for (unsigned int trial = 0; trial < TRIALS; ++trial) {
		if (getrandom(key, sizeof(key), 0) != sizeof(key))
			ksft_exit_skip("getrandom() failed unexpectedly\n");
		memset(counter1, 0, sizeof(counter1));
		reference_chacha20_blocks(output1, key, counter1, BLOCKS);
		for (unsigned int split = 0; split < BLOCKS; ++split) {
			memset(output2, 'X', sizeof(output2));
			memset(counter2, 0, sizeof(counter2));
			if (split)
				__arch_chacha20_blocks_nostack(output2, key, counter2, split);
			__arch_chacha20_blocks_nostack(output2 + split * BLOCK_SIZE, key, counter2, BLOCKS - split);
			if (memcmp(output1, output2, sizeof(output1)))
				ksft_exit_fail_msg("Main loop outputs do not match on trial %u, split %u\n", trial, split);
			if (memcmp(counter1, counter2, sizeof(counter1)))
				ksft_exit_fail_msg("Main loop counters do not match on trial %u, split %u\n", trial, split);
		}
	}
	memset(counter1, 0, sizeof(counter1));
	counter1[0] = (uint32_t)-BLOCKS + 2;
	memset(counter2, 0, sizeof(counter2));
	counter2[0] = (uint32_t)-BLOCKS + 2;

	reference_chacha20_blocks(output1, key, counter1, BLOCKS);
	__arch_chacha20_blocks_nostack(output2, key, counter2, BLOCKS);
	if (memcmp(output1, output2, sizeof(output1)))
		ksft_exit_fail_msg("Block limit outputs do not match after first round\n");
	if (memcmp(counter1, counter2, sizeof(counter1)))
		ksft_exit_fail_msg("Block limit counters do not match after first round\n");

	reference_chacha20_blocks(output1, key, counter1, BLOCKS);
	__arch_chacha20_blocks_nostack(output2, key, counter2, BLOCKS);
	if (memcmp(output1, output2, sizeof(output1)))
		ksft_exit_fail_msg("Block limit outputs do not match after second round\n");
	if (memcmp(counter1, counter2, sizeof(counter1)))
		ksft_exit_fail_msg("Block limit counters do not match after second round\n");

	ksft_test_result_pass("chacha: PASS\n");
	ksft_exit_pass();
	return 0;
}
