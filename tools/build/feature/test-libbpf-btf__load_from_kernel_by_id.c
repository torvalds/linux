// SPDX-License-Identifier: GPL-2.0
#include <bpf/btf.h>

int main(void)
{
	btf__load_from_kernel_by_id(20151128);
	return 0;
}
