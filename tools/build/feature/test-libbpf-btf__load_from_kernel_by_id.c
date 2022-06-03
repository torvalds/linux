// SPDX-License-Identifier: GPL-2.0
#include <bpf/libbpf.h>

int main(void)
{
	return btf__load_from_kernel_by_id(20151128, NULL);
}
