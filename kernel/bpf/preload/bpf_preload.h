/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_PRELOAD_H
#define _BPF_PRELOAD_H

struct bpf_preload_info {
	char link_name[16];
	struct bpf_link *link;
};

struct bpf_preload_ops {
	int (*preload)(struct bpf_preload_info *);
	struct module *owner;
};
extern struct bpf_preload_ops *bpf_preload_ops;
#define BPF_PRELOAD_LINKS 2
#endif
