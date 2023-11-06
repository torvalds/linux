// SPDX-License-Identifier: GPL-2.0-only

#include "../../kselftest.h"
#define MAX_VSIZE	(8192 * 32)

void dump(char *ptr, int size)
{
	int i = 0;

	for (i = 0; i < size; i++) {
		if (i != 0) {
			if (i % 16 == 0)
				printf("\n");
			else if (i % 8 == 0)
				printf("  ");
		}
		printf("%02x ", ptr[i]);
	}
	printf("\n");
}

int main(void)
{
	int i;
	unsigned long vl;
	char *datap, *tmp;

	datap = malloc(MAX_VSIZE);
	if (!datap) {
		ksft_test_result_fail("fail to allocate memory for size = %lu\n", MAX_VSIZE);
		exit(-1);
	}

	tmp = datap;
	asm volatile (
		".option push\n\t"
		".option arch, +v\n\t"
		"vsetvli	%0, x0, e8, m8, ta, ma\n\t"
		"vse8.v		v0, (%2)\n\t"
		"add		%1, %2, %0\n\t"
		"vse8.v		v8, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vse8.v		v16, (%1)\n\t"
		"add		%1, %1, %0\n\t"
		"vse8.v		v24, (%1)\n\t"
		".option pop\n\t"
		: "=&r" (vl), "=r" (tmp) : "r" (datap) : "memory");

	ksft_print_msg("vl = %lu\n", vl);

	if (datap[0] != 0x00 && datap[0] != 0xff) {
		ksft_test_result_fail("v-regesters are not properly initialized\n");
		dump(datap, vl * 4);
		exit(-1);
	}

	for (i = 1; i < vl * 4; i++) {
		if (datap[i] != datap[0]) {
			ksft_test_result_fail("detect stale values on v-regesters\n");
			dump(datap, vl * 4);
			exit(-2);
		}
	}

	free(datap);
	ksft_exit_pass();
	return 0;
}
