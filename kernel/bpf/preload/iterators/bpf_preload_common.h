/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_PRELOAD_COMMON_H
#define _BPF_PRELOAD_COMMON_H

#define BPF_PRELOAD_START 0x5555
#define BPF_PRELOAD_END 0xAAAA

struct bpf_preload_info {
	char link_name[16];
	int link_id;
};

#endif
