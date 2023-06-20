// SPDX-License-Identifier: GPL-2.0
#include <bpf/bpf.h>

int main(void)
{
	return bpf_prog_load(0 /* prog_type */, NULL /* prog_name */,
			     NULL /* license */, NULL /* insns */,
			     0 /* insn_cnt */, NULL /* opts */);
}
