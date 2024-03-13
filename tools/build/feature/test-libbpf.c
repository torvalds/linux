// SPDX-License-Identifier: GPL-2.0
#include <bpf/libbpf.h>

int main(void)
{
	return bpf_object__open("test") ? 0 : -1;
}
