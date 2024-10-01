// SPDX-License-Identifier: GPL-2.0
#include <bpf/libbpf.h>

int main(void)
{
	bpf_program__set_insns(NULL /* prog */, NULL /* new_insns */, 0 /* new_insn_cnt */);
	return 0;
}
