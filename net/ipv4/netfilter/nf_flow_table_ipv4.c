#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_tables.h>

static struct nf_flowtable_type flowtable_ipv4 = {
	.family		= NFPROTO_IPV4,
	.init		= nf_flow_table_init,
	.free		= nf_flow_table_free,
	.hook		= nf_flow_offload_ip_hook,
	.owner		= THIS_MODULE,
};

static int __init nf_flow_ipv4_module_init(void)
{
	nft_register_flowtable_type(&flowtable_ipv4);

	return 0;
}

static void __exit nf_flow_ipv4_module_exit(void)
{
	nft_unregister_flowtable_type(&flowtable_ipv4);
}

module_init(nf_flow_ipv4_module_init);
module_exit(nf_flow_ipv4_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_ALIAS_NF_FLOWTABLE(AF_INET);
