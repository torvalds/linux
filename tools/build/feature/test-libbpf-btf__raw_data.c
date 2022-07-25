// SPDX-License-Identifier: GPL-2.0
#include <bpf/btf.h>

int main(void)
{
	btf__raw_data(NULL /* btf_ro */, NULL /* size */);
	return 0;
}
