/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_PRELOAD_H
#define _BPF_PRELOAD_H

#include <linux/usermode_driver.h>
#include "iterators/bpf_preload_common.h"

struct bpf_preload_ops {
        struct umd_info info;
	int (*preload)(struct bpf_preload_info *);
	int (*finish)(void);
	struct module *owner;
};
extern struct bpf_preload_ops *bpf_preload_ops;
#define BPF_PRELOAD_LINKS 2
#endif
