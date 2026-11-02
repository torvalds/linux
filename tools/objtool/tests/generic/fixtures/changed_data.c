// SPDX-License-Identifier: GPL-2.0
/* Data whose value differs between the two builds. */

static const char __modinfo[]
	__attribute__((section(".modinfo"), used, aligned(1))) = "\0name=vmlinux";

#ifdef PATCHED
int klp_test_data = 2;
#else
int klp_test_data = 1;
#endif

int target(int x)
{
	return x + klp_test_data;
}
