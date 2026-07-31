// SPDX-License-Identifier: GPL-2.0
/*
 * Test interpreter for the bound-interpreter case of the binfmt_misc_bpf
 * selftest. Two copies are installed at different paths and bound to one
 * entry under different names; printing argv[0] - the path the kernel ran
 * this copy under - tells the harness which of them the load program picked.
 */
#include <stdio.h>

int main(int argc, char **argv)
{
	printf("BIND_RAN %s\n", argc > 0 ? argv[0] : "");
	return 0;
}
