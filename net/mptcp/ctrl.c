// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2019, Tessares SA.
 */

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include "protocol.h"

#define MPTCP_SYSCTL_PATH "net/mptcp"

static int mptcp_pernet_id;

#ifdef CONFIG_SYSCTL
static int mptcp_pm_type_max = __MPTCP_PM_TYPE_MAX;
#endif

struct mptcp_pernet {
#ifdef CONFIG_SYSCTL
	struct ctl_table_header *ctl_table_hdr;
#endif

	unsigned int add_addr_timeout;
	unsigned int close_timeout;
	unsigned int stale_loss_cnt;
	u8 mptcp_enabled;
	u8 checksum_enabled;
	u8 allow_join_initial_addr_port;
	u8 pm_type;
	char scheduler[MPTCP_SCHED_NAME_MAX];
};

static struct mptcp_pernet *mptcp_get_pernet(const struct net *net)
{
	return net_generic(net, mptcp_pernet_id);
}

int mptcp_is_enabled(const struct net *net)
{
	return mptcp_get_pernet(net)->mptcp_enabled;
}

unsigned int mptcp_get_add_addr_timeout(const struct net *net)
{
	return mptcp_get_pernet(net)->add_addr_timeout;
}

int mptcp_is_checksum_enabled(const struct net *net)
{
	return mptcp_get_pernet(net)->checksum_enabled;
}

int mptcp_allow_join_id0(const struct net *net)
{
	return mptcp_get_pernet(net)->allow_join_initial_addr_port;
}

unsigned int mptcp_stale_loss_cnt(const struct net *net)
{
	return mptcp_get_pernet(net)->stale_loss_cnt;
}

unsigned int mptcp_close_timeout(const struct sock *sk)
{
	if (sock_flag(sk, SOCK_DEAD))
		return TCP_TIMEWAIT_LEN;
	return mptcp_get_pernet(sock_net(sk))->close_timeout;
}

int mptcp_get_pm_type(const struct net *net)
{
	return mptcp_get_pernet(net)->pm_type;
}

const char *mptcp_get_scheduler(const struct net *net)
{
	return mptcp_get_pernet(net)->scheduler;
}

static void mptcp_pernet_set_defaults(struct mptcp_pernet *pernet)
{
	pernet->mptcp_enabled = 1;
	pernet->add_addr_timeout = TCP_RTO_MAX;
	pernet->close_timeout = TCP_TIMEWAIT_LEN;
	pernet->checksum_enabled = 0;
	pernet->allow_join_initial_addr_port = 1;
	pernet->stale_loss_cnt = 4;
	pernet->pm_type = MPTCP_PM_TYPE_KERNEL;
	strscpy(pernet->scheduler, "default", sizeof(pernet->scheduler));
}

#ifdef CONFIG_SYSCTL
static int mptcp_set_scheduler(const struct net *net, const char *name)
{
	struct mptcp_pernet *pernet = mptcp_get_pernet(net);
	struct mptcp_sched_ops *sched;
	int ret = 0;

	rcu_read_lock();
	sched = mptcp_sched_find(name);
	if (sched)
		strscpy(pernet->scheduler, name, MPTCP_SCHED_NAME_MAX);
	else
		ret = -ENOENT;
	rcu_read_unlock();

	return ret;
}

static int proc_scheduler(const struct ctl_table *ctl, int write,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	const struct net *net = current->nsproxy->net_ns;
	char val[MPTCP_SCHED_NAME_MAX];
	struct ctl_table tbl = {
		.data = val,
		.maxlen = MPTCP_SCHED_NAME_MAX,
	};
	int ret;

	strscpy(val, mptcp_get_scheduler(net), MPTCP_SCHED_NAME_MAX);

	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0)
		ret = mptcp_set_scheduler(net, val);

	return ret;
}

static int proc_available_schedulers(const struct ctl_table *ctl,
				     int write, void *buffer,
				     size_t *lenp, loff_t *ppos)
{
	struct ctl_table tbl = { .maxlen = MPTCP_SCHED_BUF_MAX, };
	int ret;

	tbl.data = kmalloc(tbl.maxlen, GFP_USER);
	if (!tbl.data)
		return -ENOMEM;

	mptcp_get_available_schedulers(tbl.data, MPTCP_SCHED_BUF_MAX);
	ret = proc_dostring(&tbl, write, buffer, lenp, ppos);
	kfree(tbl.data);

	return ret;
}

static struct ctl_table mptcp_sysctl_table[] = {
	{
		.procname = "enabled",
		.maxlen = sizeof(u8),
		.mode = 0644,
		/* users with CAP_NET_ADMIN or root (not and) can change this
		 * value, same as other sysctl or the 'net' tree.
		 */
		.proc_handler = proc_dou8vec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = SYSCTL_ONE
	},
	{
		.procname = "add_addr_timeout",
		.maxlen = sizeof(unsigned int),
		.mode = 0644,
		.proc_handler = proc_dointvec_jiffies,
	},
	{
		.procname = "checksum_enabled",
		.maxlen = sizeof(u8),
		.mode = 0644,
		.proc_handler = proc_dou8vec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = SYSCTL_ONE
	},
	{
		.procname = "allow_join_initial_addr_port",
		.maxlen = sizeof(u8),
		.mode = 0644,
		.proc_handler = proc_dou8vec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = SYSCTL_ONE
	},
	{
		.procname = "stale_loss_cnt",
		.maxlen = sizeof(unsigned int),
		.mode = 0644,
		.proc_handler = proc_douintvec_minmax,
	},
	{
		.procname = "pm_type",
		.maxlen = sizeof(u8),
		.mode = 0644,
		.proc_handler = proc_dou8vec_minmax,
		.extra1       = SYSCTL_ZERO,
		.extra2       = &mptcp_pm_type_max
	},
	{
		.procname = "scheduler",
		.maxlen	= MPTCP_SCHED_NAME_MAX,
		.mode = 0644,
		.proc_handler = proc_scheduler,
	},
	{
		.procname = "available_schedulers",
		.maxlen	= MPTCP_SCHED_BUF_MAX,
		.mode = 0644,
		.proc_handler = proc_available_schedulers,
	},
	{
		.procname = "close_timeout",
		.maxlen = sizeof(unsigned int),
		.mode = 0644,
		.proc_handler = proc_dointvec_jiffies,
	},
};

static int mptcp_pernet_new_table(struct net *net, struct mptcp_pernet *pernet)
{
	struct ctl_table_header *hdr;
	struct ctl_table *table;

	table = mptcp_sysctl_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(mptcp_sysctl_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;
	}

	table[0].data = &pernet->mptcp_enabled;
	table[1].data = &pernet->add_addr_timeout;
	table[2].data = &pernet->checksum_enabled;
	table[3].data = &pernet->allow_join_initial_addr_port;
	table[4].data = &pernet->stale_loss_cnt;
	table[5].data = &pernet->pm_type;
	table[6].data = &pernet->scheduler;
	/* table[7] is for available_schedulers which is read-only info */
	table[8].data = &pernet->close_timeout;

	hdr = register_net_sysctl_sz(net, MPTCP_SYSCTL_PATH, table,
				     ARRAY_SIZE(mptcp_sysctl_table));
	if (!hdr)
		goto err_reg;

	pernet->ctl_table_hdr = hdr;

	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void mptcp_pernet_del_table(struct mptcp_pernet *pernet)
{
	const struct ctl_table *table = pernet->ctl_table_hdr->ctl_table_arg;

	unregister_net_sysctl_table(pernet->ctl_table_hdr);

	kfree(table);
}

#else

static int mptcp_pernet_new_table(struct net *net, struct mptcp_pernet *pernet)
{
	return 0;
}

static void mptcp_pernet_del_table(struct mptcp_pernet *pernet) {}

#endif /* CONFIG_SYSCTL */

static int __net_init mptcp_net_init(struct net *net)
{
	struct mptcp_pernet *pernet = mptcp_get_pernet(net);

	mptcp_pernet_set_defaults(pernet);

	return mptcp_pernet_new_table(net, pernet);
}

/* Note: the callback will only be called per extra netns */
static void __net_exit mptcp_net_exit(struct net *net)
{
	struct mptcp_pernet *pernet = mptcp_get_pernet(net);

	mptcp_pernet_del_table(pernet);
}

static struct pernet_operations mptcp_pernet_ops = {
	.init = mptcp_net_init,
	.exit = mptcp_net_exit,
	.id = &mptcp_pernet_id,
	.size = sizeof(struct mptcp_pernet),
};

void __init mptcp_init(void)
{
	mptcp_join_cookie_init();
	mptcp_proto_init();

	if (register_pernet_subsys(&mptcp_pernet_ops) < 0)
		panic("Failed to register MPTCP pernet subsystem.\n");
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int __init mptcpv6_init(void)
{
	int err;

	err = mptcp_proto_v6_init();

	return err;
}
#endif
