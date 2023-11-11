// SPDX-License-Identifier: GPL-2.0
#include <bpf/bpf.h>

int main(void)
{
	return bpf_map_create(0 /* map_type */, NULL /* map_name */, 0, /* key_size */,
			      0 /* value_size */, 0 /* max_entries */, NULL /* opts */);
}
