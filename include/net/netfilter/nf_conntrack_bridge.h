#ifndef NF_CONNTRACK_BRIDGE_
#define NF_CONNTRACK_BRIDGE_

#include <linux/module.h>
#include <linux/types.h>
#include <uapi/linux/if_ether.h>

struct nf_ct_bridge_info {
#if IS_ENABLED(CONFIG_NETFILTER)
	struct nf_hook_ops	*ops;
#endif
	unsigned int		ops_size;
	struct module		*me;
};

void nf_ct_bridge_register(struct nf_ct_bridge_info *info);
void nf_ct_bridge_unregister(struct nf_ct_bridge_info *info);

struct nf_ct_bridge_frag_data {
	char	mac[ETH_HLEN];
	bool	vlan_present;
	u16	vlan_tci;
	__be16	vlan_proto;
};

#endif
