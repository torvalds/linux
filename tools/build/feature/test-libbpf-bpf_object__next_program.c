// SPDX-License-Identifier: GPL-2.0
#include <bpf/libbpf.h>

int main(void)
{
	bpf_object__next_program(NULL /* obj */, NULL /* prev */);
	return 0;
}
