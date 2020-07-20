/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_NETNS_H
#define _BPF_NETNS_H

#include <linux/mutex.h>
#include <uapi/linux/bpf.h>

enum netns_bpf_attach_type {
	NETNS_BPF_INVALID = -1,
	NETNS_BPF_FLOW_DISSECTOR = 0,
	MAX_NETNS_BPF_ATTACH_TYPE
};

static inline enum netns_bpf_attach_type
to_netns_bpf_attach_type(enum bpf_attach_type attach_type)
{
	switch (attach_type) {
	case BPF_FLOW_DISSECTOR:
		return NETNS_BPF_FLOW_DISSECTOR;
	default:
		return NETNS_BPF_INVALID;
	}
}

/* Protects updates to netns_bpf */
extern struct mutex netns_bpf_mutex;

union bpf_attr;
struct bpf_prog;

#ifdef CONFIG_NET
int netns_bpf_prog_query(const union bpf_attr *attr,
			 union bpf_attr __user *uattr);
int netns_bpf_prog_attach(const union bpf_attr *attr,
			  struct bpf_prog *prog);
int netns_bpf_prog_detach(const union bpf_attr *attr, enum bpf_prog_type ptype);
int netns_bpf_link_create(const union bpf_attr *attr,
			  struct bpf_prog *prog);
#else
static inline int netns_bpf_prog_query(const union bpf_attr *attr,
				       union bpf_attr __user *uattr)
{
	return -EOPNOTSUPP;
}

static inline int netns_bpf_prog_attach(const union bpf_attr *attr,
					struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}

static inline int netns_bpf_prog_detach(const union bpf_attr *attr,
					enum bpf_prog_type ptype)
{
	return -EOPNOTSUPP;
}

static inline int netns_bpf_link_create(const union bpf_attr *attr,
					struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* _BPF_NETNS_H */
