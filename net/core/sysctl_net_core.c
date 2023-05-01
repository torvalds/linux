// SPDX-License-Identifier: GPL-2.0
/* -*- linux-c -*-
 * sysctl_net_core.c: sysctl interface to net core subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/core directory entry (empty =) ). [MS]
 */

#include <linux/filter.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sched/isolation.h>

#include <net/ip.h>
#include <net/sock.h>
#include <net/net_ratelimit.h>
#include <net/busy_poll.h>
#include <net/pkt_sched.h>

#include "dev.h"

static int int_3600 = 3600;
static int min_sndbuf = SOCK_MIN_SNDBUF;
static int min_rcvbuf = SOCK_MIN_RCVBUF;
static int max_skb_frags = MAX_SKB_FRAGS;

static int net_msg_warn;	/* Unused, but still a sysctl */

int sysctl_fb_tunnels_only_for_init_net __read_mostly = 0;
EXPORT_SYMBOL(sysctl_fb_tunnels_only_for_init_net);

/* 0 - Keep current behavior:
 *     IPv4: inherit all current settings from init_net
 *     IPv6: reset all settings to default
 * 1 - Both inherit all current settings from init_net
 * 2 - Both reset all settings to default
 * 3 - Both inherit all settings from current netns
 */
int sysctl_devconf_inherit_init_net __read_mostly;
EXPORT_SYMBOL(sysctl_devconf_inherit_init_net);

#if IS_ENABLED(CONFIG_NET_FLOW_LIMIT) || IS_ENABLED(CONFIG_RPS)
static void dump_cpumask(void *buffer, size_t *lenp, loff_t *ppos,
			 struct cpumask *mask)
{
	char kbuf[128];
	int len;

	if (*ppos || !*lenp) {
		*lenp = 0;
		return;
	}

	len = min(sizeof(kbuf) - 1, *lenp);
	len = scnprintf(kbuf, len, "%*pb", cpumask_pr_args(mask));
	if (!len) {
		*lenp = 0;
		return;
	}

	if (len < *lenp)
		kbuf[len++] = '\n';
	memcpy(buffer, kbuf, len);
	*lenp = len;
	*ppos += len;
}
#endif

#ifdef CONFIG_RPS

static struct cpumask *rps_default_mask_cow_alloc(struct net *net)
{
	struct cpumask *rps_default_mask;

	if (net->core.rps_default_mask)
		return net->core.rps_default_mask;

	rps_default_mask = kzalloc(cpumask_size(), GFP_KERNEL);
	if (!rps_default_mask)
		return NULL;

	/* pairs with READ_ONCE in rx_queue_default_mask() */
	WRITE_ONCE(net->core.rps_default_mask, rps_default_mask);
	return rps_default_mask;
}

static int rps_default_mask_sysctl(struct ctl_table *table, int write,
				   void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net = (struct net *)table->data;
	int err = 0;

	rtnl_lock();
	if (write) {
		struct cpumask *rps_default_mask = rps_default_mask_cow_alloc(net);

		err = -ENOMEM;
		if (!rps_default_mask)
			goto done;

		err = cpumask_parse(buffer, rps_default_mask);
		if (err)
			goto done;

		err = rps_cpumask_housekeeping(rps_default_mask);
		if (err)
			goto done;
	} else {
		dump_cpumask(buffer, lenp, ppos,
			     net->core.rps_default_mask ? : cpu_none_mask);
	}

done:
	rtnl_unlock();
	return err;
}

static int rps_sock_flow_sysctl(struct ctl_table *table, int write,
				void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int orig_size, size;
	int ret, i;
	struct ctl_table tmp = {
		.data = &size,
		.maxlen = sizeof(size),
		.mode = table->mode
	};
	struct rps_sock_flow_table *orig_sock_table, *sock_table;
	static DEFINE_MUTEX(sock_flow_mutex);

	mutex_lock(&sock_flow_mutex);

	orig_sock_table = rcu_dereference_protected(rps_sock_flow_table,
					lockdep_is_held(&sock_flow_mutex));
	size = orig_size = orig_sock_table ? orig_sock_table->mask + 1 : 0;

	ret = proc_dointvec(&tmp, write, buffer, lenp, ppos);

	if (write) {
		if (size) {
			if (size > 1<<29) {
				/* Enforce limit to prevent overflow */
				mutex_unlock(&sock_flow_mutex);
				return -EINVAL;
			}
			size = roundup_pow_of_two(size);
			if (size != orig_size) {
				sock_table =
				    vmalloc(RPS_SOCK_FLOW_TABLE_SIZE(size));
				if (!sock_table) {
					mutex_unlock(&sock_flow_mutex);
					return -ENOMEM;
				}
				rps_cpu_mask = roundup_pow_of_two(nr_cpu_ids) - 1;
				sock_table->mask = size - 1;
			} else
				sock_table = orig_sock_table;

			for (i = 0; i < size; i++)
				sock_table->ents[i] = RPS_NO_CPU;
		} else
			sock_table = NULL;

		if (sock_table != orig_sock_table) {
			rcu_assign_pointer(rps_sock_flow_table, sock_table);
			if (sock_table) {
				static_branch_inc(&rps_needed);
				static_branch_inc(&rfs_needed);
			}
			if (orig_sock_table) {
				static_branch_dec(&rps_needed);
				static_branch_dec(&rfs_needed);
				kvfree_rcu(orig_sock_table);
			}
		}
	}

	mutex_unlock(&sock_flow_mutex);

	return ret;
}
#endif /* CONFIG_RPS */

#ifdef CONFIG_NET_FLOW_LIMIT
static DEFINE_MUTEX(flow_limit_update_mutex);

static int flow_limit_cpu_sysctl(struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct sd_flow_limit *cur;
	struct softnet_data *sd;
	cpumask_var_t mask;
	int i, len, ret = 0;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	if (write) {
		ret = cpumask_parse(buffer, mask);
		if (ret)
			goto done;

		mutex_lock(&flow_limit_update_mutex);
		len = sizeof(*cur) + netdev_flow_limit_table_len;
		for_each_possible_cpu(i) {
			sd = &per_cpu(softnet_data, i);
			cur = rcu_dereference_protected(sd->flow_limit,
				     lockdep_is_held(&flow_limit_update_mutex));
			if (cur && !cpumask_test_cpu(i, mask)) {
				RCU_INIT_POINTER(sd->flow_limit, NULL);
				kfree_rcu(cur);
			} else if (!cur && cpumask_test_cpu(i, mask)) {
				cur = kzalloc_node(len, GFP_KERNEL,
						   cpu_to_node(i));
				if (!cur) {
					/* not unwinding previous changes */
					ret = -ENOMEM;
					goto write_unlock;
				}
				cur->num_buckets = netdev_flow_limit_table_len;
				rcu_assign_pointer(sd->flow_limit, cur);
			}
		}
write_unlock:
		mutex_unlock(&flow_limit_update_mutex);
	} else {
		cpumask_clear(mask);
		rcu_read_lock();
		for_each_possible_cpu(i) {
			sd = &per_cpu(softnet_data, i);
			if (rcu_dereference(sd->flow_limit))
				cpumask_set_cpu(i, mask);
		}
		rcu_read_unlock();

		dump_cpumask(buffer, lenp, ppos, mask);
	}

done:
	free_cpumask_var(mask);
	return ret;
}

static int flow_limit_table_len_sysctl(struct ctl_table *table, int write,
				       void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int old, *ptr;
	int ret;

	mutex_lock(&flow_limit_update_mutex);

	ptr = table->data;
	old = *ptr;
	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!ret && write && !is_power_of_2(*ptr)) {
		*ptr = old;
		ret = -EINVAL;
	}

	mutex_unlock(&flow_limit_update_mutex);
	return ret;
}
#endif /* CONFIG_NET_FLOW_LIMIT */

#ifdef CONFIG_NET_SCHED
static int set_default_qdisc(struct ctl_table *table, int write,
			     void *buffer, size_t *lenp, loff_t *ppos)
{
	char id[IFNAMSIZ];
	struct ctl_table tbl = {
		.data = id,
		.maxlen = IFNAMSIZ,
	};
	int ret;

	qdisc_get_default(id, IFNAMSIZ);

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = qdisc_set_default(id);
	return ret;
}
#endif

static int proc_do_dev_weight(struct ctl_table *table, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	static DEFINE_MUTEX(dev_weight_mutex);
	int ret, weight;

	mutex_lock(&dev_weight_mutex);
	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!ret && write) {
		weight = READ_ONCE(weight_p);
		WRITE_ONCE(dev_rx_weight, weight * dev_weight_rx_bias);
		WRITE_ONCE(dev_tx_weight, weight * dev_weight_tx_bias);
	}
	mutex_unlock(&dev_weight_mutex);

	return ret;
}

static int proc_do_rss_key(struct ctl_table *table, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table fake_table;
	char buf[NETDEV_RSS_KEY_LEN * 3];

	snprintf(buf, sizeof(buf), "%*phC", NETDEV_RSS_KEY_LEN, netdev_rss_key);
	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);
	return proc_dostring(&fake_table, write, buffer, lenp, ppos);
}

#ifdef CONFIG_BPF_JIT
static int proc_dointvec_minmax_bpf_enable(struct ctl_table *table, int write,
					   void *buffer, size_t *lenp,
					   loff_t *ppos)
{
	int ret, jit_enable = *(int *)table->data;
	int min = *(int *)table->extra1;
	int max = *(int *)table->extra2;
	struct ctl_table tmp = *table;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	tmp.data = &jit_enable;
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	if (write && !ret) {
		if (jit_enable < 2 ||
		    (jit_enable == 2 && bpf_dump_raw_ok(current_cred()))) {
			*(int *)table->data = jit_enable;
			if (jit_enable == 2)
				pr_warn("bpf_jit_enable = 2 was set! NEVER use this in production, only for JIT debugging!\n");
		} else {
			ret = -EPERM;
		}
	}

	if (write && ret && min == max)
		pr_info_once("CONFIG_BPF_JIT_ALWAYS_ON is enabled, bpf_jit_enable is permanently set to 1.\n");

	return ret;
}

# ifdef CONFIG_HAVE_EBPF_JIT
static int
proc_dointvec_minmax_bpf_restricted(struct ctl_table *table, int write,
				    void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return proc_dointvec_minmax(table, write, buffer, lenp, ppos);
}
# endif /* CONFIG_HAVE_EBPF_JIT */

static int
proc_dolongvec_minmax_bpf_restricted(struct ctl_table *table, int write,
				     void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}
#endif

static struct ctl_table net_core_table[] = {
	{
		.procname	= "wmem_max",
		.data		= &sysctl_wmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_sndbuf,
	},
	{
		.procname	= "rmem_max",
		.data		= &sysctl_rmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_rcvbuf,
	},
	{
		.procname	= "wmem_default",
		.data		= &sysctl_wmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_sndbuf,
	},
	{
		.procname	= "rmem_default",
		.data		= &sysctl_rmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_rcvbuf,
	},
	{
		.procname	= "dev_weight",
		.data		= &weight_p,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_dev_weight,
	},
	{
		.procname	= "dev_weight_rx_bias",
		.data		= &dev_weight_rx_bias,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_dev_weight,
	},
	{
		.procname	= "dev_weight_tx_bias",
		.data		= &dev_weight_tx_bias,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_do_dev_weight,
	},
	{
		.procname	= "netdev_max_backlog",
		.data		= &netdev_max_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "netdev_rss_key",
		.data		= &netdev_rss_key,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_do_rss_key,
	},
#ifdef CONFIG_BPF_JIT
	{
		.procname	= "bpf_jit_enable",
		.data		= &bpf_jit_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_bpf_enable,
# ifdef CONFIG_BPF_JIT_ALWAYS_ON
		.extra1		= SYSCTL_ONE,
		.extra2		= SYSCTL_ONE,
# else
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
# endif
	},
# ifdef CONFIG_HAVE_EBPF_JIT
	{
		.procname	= "bpf_jit_harden",
		.data		= &bpf_jit_harden,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax_bpf_restricted,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "bpf_jit_kallsyms",
		.data		= &bpf_jit_kallsyms,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec_minmax_bpf_restricted,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
# endif
	{
		.procname	= "bpf_jit_limit",
		.data		= &bpf_jit_limit,
		.maxlen		= sizeof(long),
		.mode		= 0600,
		.proc_handler	= proc_dolongvec_minmax_bpf_restricted,
		.extra1		= SYSCTL_LONG_ONE,
		.extra2		= &bpf_jit_limit_max,
	},
#endif
	{
		.procname	= "netdev_tstamp_prequeue",
		.data		= &netdev_tstamp_prequeue,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "message_cost",
		.data		= &net_ratelimit_state.interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "message_burst",
		.data		= &net_ratelimit_state.burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "optmem_max",
		.data		= &sysctl_optmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "tstamp_allow_data",
		.data		= &sysctl_tstamp_allow_data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE
	},
#ifdef CONFIG_RPS
	{
		.procname	= "rps_sock_flow_entries",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= rps_sock_flow_sysctl
	},
#endif
#ifdef CONFIG_NET_FLOW_LIMIT
	{
		.procname	= "flow_limit_cpu_bitmap",
		.mode		= 0644,
		.proc_handler	= flow_limit_cpu_sysctl
	},
	{
		.procname	= "flow_limit_table_len",
		.data		= &netdev_flow_limit_table_len,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= flow_limit_table_len_sysctl
	},
#endif /* CONFIG_NET_FLOW_LIMIT */
#ifdef CONFIG_NET_RX_BUSY_POLL
	{
		.procname	= "busy_poll",
		.data		= &sysctl_net_busy_poll,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "busy_read",
		.data		= &sysctl_net_busy_read,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
#endif
#ifdef CONFIG_NET_SCHED
	{
		.procname	= "default_qdisc",
		.mode		= 0644,
		.maxlen		= IFNAMSIZ,
		.proc_handler	= set_default_qdisc
	},
#endif
	{
		.procname	= "netdev_budget",
		.data		= &netdev_budget,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "warnings",
		.data		= &net_msg_warn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "max_skb_frags",
		.data		= &sysctl_max_skb_frags,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= &max_skb_frags,
	},
	{
		.procname	= "netdev_budget_usecs",
		.data		= &netdev_budget_usecs,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "fb_tunnels_only_for_init_net",
		.data		= &sysctl_fb_tunnels_only_for_init_net,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{
		.procname	= "devconf_inherit_init_net",
		.data		= &sysctl_devconf_inherit_init_net,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_THREE,
	},
	{
		.procname	= "high_order_alloc_disable",
		.data		= &net_high_order_alloc_disable_key.key,
		.maxlen         = sizeof(net_high_order_alloc_disable_key),
		.mode		= 0644,
		.proc_handler	= proc_do_static_key,
	},
	{
		.procname	= "gro_normal_batch",
		.data		= &gro_normal_batch,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
	},
	{
		.procname	= "netdev_unregister_timeout_secs",
		.data		= &netdev_unregister_timeout_secs,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= &int_3600,
	},
	{
		.procname	= "skb_defer_max",
		.data		= &sysctl_skb_defer_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{ }
};

static struct ctl_table netns_core_table[] = {
#if IS_ENABLED(CONFIG_RPS)
	{
		.procname	= "rps_default_mask",
		.data		= &init_net,
		.mode		= 0644,
		.proc_handler	= rps_default_mask_sysctl
	},
#endif
	{
		.procname	= "somaxconn",
		.data		= &init_net.core.sysctl_somaxconn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.extra1		= SYSCTL_ZERO,
		.proc_handler	= proc_dointvec_minmax
	},
	{
		.procname	= "txrehash",
		.data		= &init_net.core.sysctl_txrehash,
		.maxlen		= sizeof(u8),
		.mode		= 0644,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
		.proc_handler	= proc_dou8vec_minmax,
	},
	{ }
};

static int __init fb_tunnels_only_for_init_net_sysctl_setup(char *str)
{
	/* fallback tunnels for initns only */
	if (!strncmp(str, "initns", 6))
		sysctl_fb_tunnels_only_for_init_net = 1;
	/* no fallback tunnels anywhere */
	else if (!strncmp(str, "none", 4))
		sysctl_fb_tunnels_only_for_init_net = 2;

	return 1;
}
__setup("fb_tunnels=", fb_tunnels_only_for_init_net_sysctl_setup);

static __net_init int sysctl_core_net_init(struct net *net)
{
	struct ctl_table *tbl, *tmp;

	tbl = netns_core_table;
	if (!net_eq(net, &init_net)) {
		tbl = kmemdup(tbl, sizeof(netns_core_table), GFP_KERNEL);
		if (tbl == NULL)
			goto err_dup;

		for (tmp = tbl; tmp->procname; tmp++)
			tmp->data += (char *)net - (char *)&init_net;
	}

	net->core.sysctl_hdr = register_net_sysctl(net, "net/core", tbl);
	if (net->core.sysctl_hdr == NULL)
		goto err_reg;

	return 0;

err_reg:
	if (tbl != netns_core_table)
		kfree(tbl);
err_dup:
	return -ENOMEM;
}

static __net_exit void sysctl_core_net_exit(struct net *net)
{
	struct ctl_table *tbl;

	tbl = net->core.sysctl_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->core.sysctl_hdr);
	BUG_ON(tbl == netns_core_table);
#if IS_ENABLED(CONFIG_RPS)
	kfree(net->core.rps_default_mask);
#endif
	kfree(tbl);
}

static __net_initdata struct pernet_operations sysctl_core_ops = {
	.init = sysctl_core_net_init,
	.exit = sysctl_core_net_exit,
};

static __init int sysctl_core_init(void)
{
	register_net_sysctl(&init_net, "net/core", net_core_table);
	return register_pernet_subsys(&sysctl_core_ops);
}

fs_initcall(sysctl_core_init);
