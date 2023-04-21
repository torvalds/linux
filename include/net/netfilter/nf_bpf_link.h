/* SPDX-License-Identifier: GPL-2.0 */

#if IS_ENABLED(CONFIG_NETFILTER_BPF_LINK)
int bpf_nf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog);
#else
static inline int bpf_nf_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}
#endif
