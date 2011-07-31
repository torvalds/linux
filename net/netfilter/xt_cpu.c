/* Kernel module to match running CPU */

/*
 * Might be used to distribute connections on several daemons, if
 * RPS (Remote Packet Steering) is enabled or NIC is multiqueue capable,
 * each RX queue IRQ affined to one CPU (1:1 mapping)
 *
 */

/* (C) 2010 Eric Dumazet
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/xt_cpu.h>
#include <linux/netfilter/x_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Dumazet <eric.dumazet@gmail.com>");
MODULE_DESCRIPTION("Xtables: CPU match");

static int cpu_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_cpu_info *info = par->matchinfo;

	if (info->invert & ~1)
		return -EINVAL;
	return 0;
}

static bool cpu_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_cpu_info *info = par->matchinfo;

	return (info->cpu == smp_processor_id()) ^ info->invert;
}

static struct xt_match cpu_mt_reg __read_mostly = {
	.name       = "cpu",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = cpu_mt_check,
	.match      = cpu_mt,
	.matchsize  = sizeof(struct xt_cpu_info),
	.me         = THIS_MODULE,
};

static int __init cpu_mt_init(void)
{
	return xt_register_match(&cpu_mt_reg);
}

static void __exit cpu_mt_exit(void)
{
	xt_unregister_match(&cpu_mt_reg);
}

module_init(cpu_mt_init);
module_exit(cpu_mt_exit);
