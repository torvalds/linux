#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_tables.h>

static unsigned int
nf_flow_offload_inet_hook(void *priv, struct sk_buff *skb,
			  const struct nf_hook_state *state)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return nf_flow_offload_ip_hook(priv, skb, state);
	case htons(ETH_P_IPV6):
		return nf_flow_offload_ipv6_hook(priv, skb, state);
	}

	return NF_ACCEPT;
}

static struct nf_flowtable_type flowtable_inet = {
	.family		= NFPROTO_INET,
	.params		= &nf_flow_offload_rhash_params,
	.gc		= nf_flow_offload_work_gc,
	.free		= nf_flow_table_free,
	.hook		= nf_flow_offload_inet_hook,
	.owner		= THIS_MODULE,
};

static int __init nf_flow_inet_module_init(void)
{
	nft_register_flowtable_type(&flowtable_inet);

	return 0;
}

static void __exit nf_flow_inet_module_exit(void)
{
	nft_unregister_flowtable_type(&flowtable_inet);
}

module_init(nf_flow_inet_module_init);
module_exit(nf_flow_inet_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NF_FLOWTABLE(1); /* NFPROTO_INET */
