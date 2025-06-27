// SPDX-License-Identifier: GPL-2.0
#include <bpf/btf.h>

int main(void)
{
	struct btf_dump_type_data_opts opts;

	opts.emit_strings = 0;
	return opts.emit_strings;
}
