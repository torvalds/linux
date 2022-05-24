// SPDX-License-Identifier: GPL-2.0
#include <bpf/libbpf.h>

int main(void)
{
	bpf_object__next_map(NULL /* obj */, NULL /* prev */);
	return 0;
}
