// SPDX-License-Identifier: GPL-2.0
/* -*- linux-c -*-
 * sysctl_net_core.c: sysctl interface to net core subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/core directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/ratelimit.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kmemleak.h>

#include <net/ip.h>
#include <net/sock.h>
#include <net/net_ratelimit.h>
#include <net/busy_poll.h>
#include <net/pkt_sched.h>

static int zero = 0;
static int one = 1;
static int min_sndbuf = SOCK_MIN_SNDBUF;
static int min_rcvbuf = SOCK_MIN_RCVBUF;
static int max_skb_frags = MAX_SKB_FRAGS;

static int net_msg_warn;	/* Unused, but still a sysctl */

#ifdef CONFIG_RPS
static int rps_sock_flow_sysctl(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
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
				static_key_slow_inc(&rps_needed);
				static_key_slow_inc(&rfs_needed);
			}
			if (orig_sock_table) {
				static_key_slow_dec(&rps_needed);
				static_key_slow_dec(&rfs_needed);
				synchronize_rcu();
				vfree(orig_sock_table);
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
				 void __user *buffer, size_t *lenp,
				 loff_t *ppos)
{
	struct sd_flow_limit *cur;
	struct softnet_data *sd;
	cpumask_var_t mask;
	int i, len, ret = 0;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	if (write) {
		ret = cpumask_parse_user(buffer, *lenp, mask);
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
				synchronize_rcu();
				kfree(cur);
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
		char kbuf[128];

		if (*ppos || !*lenp) {
			*lenp = 0;
			goto done;
		}

		cpumask_clear(mask);
		rcu_read_lock();
		for_each_possible_cpu(i) {
			sd = &per_cpu(softnet_data, i);
			if (rcu_dereference(sd->flow_limit))
				cpumask_set_cpu(i, mask);
		}
		rcu_read_unlock();

		len = min(sizeof(kbuf) - 1, *lenp);
		len = scnprintf(kbuf, len, "%*pb", cpumask_pr_args(mask));
		if (!len) {
			*lenp = 0;
			goto done;
		}
		if (len < *lenp)
			kbuf[len++] = '\n';
		if (copy_to_user(buffer, kbuf, len)) {
			ret = -EFAULT;
			goto done;
		}
		*lenp = len;
		*ppos += len;
	}

done:
	free_cpumask_var(mask);
	return ret;
}

static int flow_limit_table_len_sysctl(struct ctl_table *table, int write,
				       void __user *buffer, size_t *lenp,
				       loff_t *ppos)
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
			     void __user *buffer, size_t *lenp, loff_t *ppos)
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
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret != 0)
		return ret;

	dev_rx_weight = weight_p * dev_weight_rx_bias;
	dev_tx_weight = weight_p * dev_weight_tx_bias;

	return ret;
}

static int proc_do_rss_key(struct ctl_table *table, int write,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table fake_table;
	char buf[NETDEV_RSS_KEY_LEN * 3];

	snprintf(buf, sizeof(buf), "%*phC", NETDEV_RSS_KEY_LEN, netdev_rss_key);
	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);
	return proc_dostring(&fake_table, write, buffer, lenp, ppos);
}

static struct ctl_table net_core_table[] = {
#ifdef CONFIG_NET
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
#ifndef CONFIG_BPF_JIT_ALWAYS_ON
		.proc_handler	= proc_dointvec
#else
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &one,
#endif
	},
# ifdef CONFIG_HAVE_EBPF_JIT
	{
		.procname	= "bpf_jit_harden",
		.data		= &bpf_jit_harden,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "bpf_jit_kallsyms",
		.data		= &bpf_jit_kallsyms,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec,
	},
# endif
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
		.extra1		= &zero,
		.extra2		= &one
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
		.extra1		= &zero,
	},
	{
		.procname	= "busy_read",
		.data		= &sysctl_net_busy_read,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
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
#endif /* CONFIG_NET */
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
		.extra1		= &one,
		.extra2		= &max_skb_frags,
	},
	{
		.procname	= "netdev_budget_usecs",
		.data		= &netdev_budget_usecs,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
	{ }
};

static struct ctl_table netns_core_table[] = {
	{
		.procname	= "somaxconn",
		.data		= &init_net.core.sysctl_somaxconn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.extra1		= &zero,
		.proc_handler	= proc_dointvec_minmax
	},
	{ }
};

static __net_init int sysctl_core_net_init(struct net *net)
{
	struct ctl_table *tbl;

	tbl = netns_core_table;
	if (!net_eq(net, &init_net)) {
		tbl = kmemdup(tbl, sizeof(netns_core_table), GFP_KERNEL);
		if (tbl == NULL)
			goto err_dup;

		tbl[0].data = &net->core.sysctl_somaxconn;

		/* Don't export any sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns) {
			tbl[0].procname = NULL;
		}
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
