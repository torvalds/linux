// SPDX-License-Identifier: GPL-2.0
/*
 * Test interpreter for the binfmt_misc_bpf selftest. A bpf-backed 'B' handler
 * routes a matched binary here; printing this marker proves the program's
 * chosen interpreter actually ran.
 */
#include <unistd.h>

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	write(1, "BPF_INTERP_RAN\n", 15);
	return 0;
}
