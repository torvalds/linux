// SPDX-License-Identifier: GPL-2.0-only
#include "hwprobe.h"
#include "../../kselftest.h"

int main(int argc, char **argv)
{
	struct riscv_hwprobe pairs[8];
	unsigned long cpus;
	long out;

	ksft_print_header();
	ksft_set_plan(5);

	/* Fake the CPU_SET ops. */
	cpus = -1;

	/*
	 * Just run a basic test: pass enough pairs to get up to the base
	 * behavior, and then check to make sure it's sane.
	 */
	for (long i = 0; i < 8; i++)
		pairs[i].key = i;

	out = riscv_hwprobe(pairs, 8, 1, &cpus, 0);
	if (out != 0)
		ksft_exit_fail_msg("hwprobe() failed with %ld\n", out);

	for (long i = 0; i < 4; ++i) {
		/* Fail if the kernel claims not to recognize a base key. */
		if ((i < 4) && (pairs[i].key != i))
			ksft_exit_fail_msg("Failed to recognize base key: key != i, "
					   "key=%lld, i=%ld\n", pairs[i].key, i);

		if (pairs[i].key != RISCV_HWPROBE_KEY_BASE_BEHAVIOR)
			continue;

		if (pairs[i].value & RISCV_HWPROBE_BASE_BEHAVIOR_IMA)
			continue;

		ksft_exit_fail_msg("Unexpected pair: (%lld, %llu)\n", pairs[i].key, pairs[i].value);
	}

	out = riscv_hwprobe(pairs, 8, 0, 0, 0);
	ksft_test_result(out == 0, "NULL CPU set\n");

	out = riscv_hwprobe(pairs, 8, 0, &cpus, 0);
	ksft_test_result(out != 0, "Bad CPU set\n");

	out = riscv_hwprobe(pairs, 8, 1, 0, 0);
	ksft_test_result(out != 0, "NULL CPU set with non-zero size\n");

	pairs[0].key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR;
	out = riscv_hwprobe(pairs, 1, 1, &cpus, 0);
	ksft_test_result(out == 0 && pairs[0].key == RISCV_HWPROBE_KEY_BASE_BEHAVIOR,
			 "Existing key is maintained\n");

	pairs[0].key = 0x5555;
	pairs[1].key = 1;
	pairs[1].value = 0xAAAA;
	out = riscv_hwprobe(pairs, 2, 0, 0, 0);
	ksft_test_result(out == 0 && pairs[0].key == -1 &&
			 pairs[1].key == 1 && pairs[1].value != 0xAAAA,
			 "Unknown key overwritten with -1 and doesn't block other elements\n");

	ksft_finished();
}
