/*
 * IPv6 raw table, a port of the IPv4 raw table to IPv6
 *
 * Copyright (C) 2003 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 */
#include <linux/module.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

#define RAW_VALID_HOOKS ((1 << NF_IP6_PRE_ROUTING) | (1 << NF_IP6_LOCAL_OUT))

#if 0
#define DEBUGP(x, args...)	printk(KERN_DEBUG x, ## args)
#else
#define DEBUGP(x, args...)
#endif

/* Standard entry. */
struct ip6t_standard
{
	struct ip6t_entry entry;
	struct ip6t_standard_target target;
};

struct ip6t_error_target
{
	struct ip6t_entry_target target;
	char errorname[IP6T_FUNCTION_MAXNAMELEN];
};

struct ip6t_error
{
	struct ip6t_entry entry;
	struct ip6t_error_target target;
};

static struct
{
	struct ip6t_replace repl;
	struct ip6t_standard entries[2];
	struct ip6t_error term;
} initial_table __initdata = {
	.repl = {
		.name = "raw",
		.valid_hooks = RAW_VALID_HOOKS,
		.num_entries = 3,
		.size = sizeof(struct ip6t_standard) * 2 + sizeof(struct ip6t_error),
		.hook_entry = {
			[NF_IP6_PRE_ROUTING] = 0,
			[NF_IP6_LOCAL_OUT] = sizeof(struct ip6t_standard)
		},
		.underflow = {
			[NF_IP6_PRE_ROUTING] = 0,
			[NF_IP6_LOCAL_OUT] = sizeof(struct ip6t_standard)
		},
	},
	.entries = {
		/* PRE_ROUTING */
		{
			.entry = {
				.target_offset = sizeof(struct ip6t_entry),
				.next_offset = sizeof(struct ip6t_standard),
			},
			.target = {
				.target = {
					.u = {
						.target_size = IP6T_ALIGN(sizeof(struct ip6t_standard_target)),
					},
				},
				.verdict = -NF_ACCEPT - 1,
			},
		},

		/* LOCAL_OUT */
		{
			.entry = {
				.target_offset = sizeof(struct ip6t_entry),
				.next_offset = sizeof(struct ip6t_standard),
			},
			.target = {
				.target = {
					.u = {
						.target_size = IP6T_ALIGN(sizeof(struct ip6t_standard_target)),
					},
				},
				.verdict = -NF_ACCEPT - 1,
			},
		},
	},
	/* ERROR */
	.term = {
		.entry = {
			.target_offset = sizeof(struct ip6t_entry),
			.next_offset = sizeof(struct ip6t_error),
		},
		.target = {
			.target = {
				.u = {
					.user = {
						.target_size = IP6T_ALIGN(sizeof(struct ip6t_error_target)),
						.name = IP6T_ERROR_TARGET,
					},
				},
			},
			.errorname = "ERROR",
		},
	}
};

static struct xt_table packet_raw = { 
	.name = "raw", 
	.valid_hooks = RAW_VALID_HOOKS, 
	.lock = RW_LOCK_UNLOCKED, 
	.me = THIS_MODULE,
	.af = AF_INET6,
};

/* The work comes in here from netfilter.c. */
static unsigned int
ip6t_hook(unsigned int hook,
	 struct sk_buff **pskb,
	 const struct net_device *in,
	 const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	return ip6t_do_table(pskb, hook, in, out, &packet_raw, NULL);
}

static struct nf_hook_ops ip6t_ops[] = { 
	{
	  .hook = ip6t_hook, 
	  .pf = PF_INET6,
	  .hooknum = NF_IP6_PRE_ROUTING,
	  .priority = NF_IP6_PRI_FIRST,
	  .owner = THIS_MODULE,
	},
	{
	  .hook = ip6t_hook, 
	  .pf = PF_INET6, 
	  .hooknum = NF_IP6_LOCAL_OUT,
	  .priority = NF_IP6_PRI_FIRST,
	  .owner = THIS_MODULE,
	},
};

static int __init ip6table_raw_init(void)
{
	int ret;

	/* Register table */
	ret = ip6t_register_table(&packet_raw, &initial_table.repl);
	if (ret < 0)
		return ret;

	/* Register hooks */
	ret = nf_register_hooks(ip6t_ops, ARRAY_SIZE(ip6t_ops));
	if (ret < 0)
		goto cleanup_table;

	return ret;

 cleanup_table:
	ip6t_unregister_table(&packet_raw);
	return ret;
}

static void __exit ip6table_raw_fini(void)
{
	nf_unregister_hooks(ip6t_ops, ARRAY_SIZE(ip6t_ops));
	ip6t_unregister_table(&packet_raw);
}

module_init(ip6table_raw_init);
module_exit(ip6table_raw_fini);
MODULE_LICENSE("GPL");
