// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <asm/hwprobe.h>

/*
 * Rather than relying on having a new enough libc to define this, just do it
 * ourselves.  This way we don't need to be coupled to a new-enough libc to
 * contain the call.
 */
long riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
		   size_t cpu_count, unsigned long *cpus, unsigned int flags);

int main(int argc, char **argv)
{
	struct riscv_hwprobe pairs[8];
	unsigned long cpus;
	long out;

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
		return -1;
	for (long i = 0; i < 4; ++i) {
		/* Fail if the kernel claims not to recognize a base key. */
		if ((i < 4) && (pairs[i].key != i))
			return -2;

		if (pairs[i].key != RISCV_HWPROBE_KEY_BASE_BEHAVIOR)
			continue;

		if (pairs[i].value & RISCV_HWPROBE_BASE_BEHAVIOR_IMA)
			continue;

		return -3;
	}

	/*
	 * This should also work with a NULL CPU set, but should not work
	 * with an improperly supplied CPU set.
	 */
	out = riscv_hwprobe(pairs, 8, 0, 0, 0);
	if (out != 0)
		return -4;

	out = riscv_hwprobe(pairs, 8, 0, &cpus, 0);
	if (out == 0)
		return -5;

	out = riscv_hwprobe(pairs, 8, 1, 0, 0);
	if (out == 0)
		return -6;

	/*
	 * Check that keys work by providing one that we know exists, and
	 * checking to make sure the resultig pair is what we asked for.
	 */
	pairs[0].key = RISCV_HWPROBE_KEY_BASE_BEHAVIOR;
	out = riscv_hwprobe(pairs, 1, 1, &cpus, 0);
	if (out != 0)
		return -7;
	if (pairs[0].key != RISCV_HWPROBE_KEY_BASE_BEHAVIOR)
		return -8;

	/*
	 * Check that an unknown key gets overwritten with -1,
	 * but doesn't block elements after it.
	 */
	pairs[0].key = 0x5555;
	pairs[1].key = 1;
	pairs[1].value = 0xAAAA;
	out = riscv_hwprobe(pairs, 2, 0, 0, 0);
	if (out != 0)
		return -9;

	if (pairs[0].key != -1)
		return -10;

	if ((pairs[1].key != 1) || (pairs[1].value == 0xAAAA))
		return -11;

	return 0;
}
