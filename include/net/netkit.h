/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Isovalent */
#ifndef __NET_NETKIT_H
#define __NET_NETKIT_H

#include <linux/bpf.h>

#ifdef CONFIG_NETKIT
int netkit_prog_attach(const union bpf_attr *attr, struct bpf_prog *prog);
int netkit_link_attach(const union bpf_attr *attr, struct bpf_prog *prog);
int netkit_prog_detach(const union bpf_attr *attr, struct bpf_prog *prog);
int netkit_prog_query(const union bpf_attr *attr, union bpf_attr __user *uattr);
INDIRECT_CALLABLE_DECLARE(struct net_device *netkit_peer_dev(struct net_device *dev));
#else
static inline int netkit_prog_attach(const union bpf_attr *attr,
				     struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int netkit_link_attach(const union bpf_attr *attr,
				     struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int netkit_prog_detach(const union bpf_attr *attr,
				     struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int netkit_prog_query(const union bpf_attr *attr,
				    union bpf_attr __user *uattr)
{
	return -EINVAL;
}

static inline struct net_device *netkit_peer_dev(struct net_device *dev)
{
	return NULL;
}
#endif /* CONFIG_NETKIT */
#endif /* __NET_NETKIT_H */
