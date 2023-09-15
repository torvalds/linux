/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include "vmlinux.h"
#include "net_shared.h"
#include <linux/version.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

/* kprobe is NOT a stable ABI
 * kernel functions can be removed, renamed or completely change semantics.
 * Number of arguments and their positions can change, etc.
 * In such case this bpf+kprobe example will no longer be meaningful
 */
SEC("kprobe.multi/__netif_receive_skb_core*")
int bpf_prog1(struct pt_regs *ctx)
{
	/* attaches to kprobe __netif_receive_skb_core,
	 * looks for packets on loobpack device and prints them
	 * (wildcard is used for avoiding symbol mismatch due to optimization)
	 */
	char devname[IFNAMSIZ];
	struct net_device *dev;
	struct sk_buff *skb;
	int len;

	bpf_core_read(&skb, sizeof(skb), (void *)PT_REGS_PARM1(ctx));
	dev = BPF_CORE_READ(skb, dev);
	len = BPF_CORE_READ(skb, len);

	BPF_CORE_READ_STR_INTO(&devname, dev, name);

	if (devname[0] == 'l' && devname[1] == 'o') {
		char fmt[] = "skb %p len %d\n";
		/* using bpf_trace_printk() for DEBUG ONLY */
		bpf_trace_printk(fmt, sizeof(fmt), skb, len);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
