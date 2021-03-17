#ifndef NF_CONNTRACK_BRIDGE_
#define NF_CONNTRACK_BRIDGE_

#include <linux/module.h>
#include <linux/types.h>
#include <uapi/linux/if_ether.h>

struct nf_hook_ops;

struct nf_ct_bridge_info {
	struct nf_hook_ops	*ops;
	unsigned int		ops_size;
	struct module		*me;
};

void nf_ct_bridge_register(struct nf_ct_bridge_info *info);
void nf_ct_bridge_unregister(struct nf_ct_bridge_info *info);

#endif
