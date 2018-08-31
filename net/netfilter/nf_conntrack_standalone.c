// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <linux/netdevice.h>
#include <linux/security.h>
#include <net/net_namespace.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_acct.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_conntrack_timestamp.h>
#include <linux/rculist_nulls.h>

unsigned int nf_conntrack_net_id __read_mostly;

#ifdef CONFIG_NF_CONNTRACK_PROCFS
void
print_tuple(struct seq_file *s, const struct nf_conntrack_tuple *tuple,
            const struct nf_conntrack_l4proto *l4proto)
{
	switch (tuple->src.l3num) {
	case NFPROTO_IPV4:
		seq_printf(s, "src=%pI4 dst=%pI4 ",
			   &tuple->src.u3.ip, &tuple->dst.u3.ip);
		break;
	case NFPROTO_IPV6:
		seq_printf(s, "src=%pI6 dst=%pI6 ",
			   tuple->src.u3.ip6, tuple->dst.u3.ip6);
		break;
	default:
		break;
	}

	switch (l4proto->l4proto) {
	case IPPROTO_ICMP:
		seq_printf(s, "type=%u code=%u id=%u ",
			   tuple->dst.u.icmp.type,
			   tuple->dst.u.icmp.code,
			   ntohs(tuple->src.u.icmp.id));
		break;
	case IPPROTO_TCP:
		seq_printf(s, "sport=%hu dport=%hu ",
			   ntohs(tuple->src.u.tcp.port),
			   ntohs(tuple->dst.u.tcp.port));
		break;
	case IPPROTO_UDPLITE: /* fallthrough */
	case IPPROTO_UDP:
		seq_printf(s, "sport=%hu dport=%hu ",
			   ntohs(tuple->src.u.udp.port),
			   ntohs(tuple->dst.u.udp.port));

		break;
	case IPPROTO_DCCP:
		seq_printf(s, "sport=%hu dport=%hu ",
			   ntohs(tuple->src.u.dccp.port),
			   ntohs(tuple->dst.u.dccp.port));
		break;
	case IPPROTO_SCTP:
		seq_printf(s, "sport=%hu dport=%hu ",
			   ntohs(tuple->src.u.sctp.port),
			   ntohs(tuple->dst.u.sctp.port));
		break;
	case IPPROTO_ICMPV6:
		seq_printf(s, "type=%u code=%u id=%u ",
			   tuple->dst.u.icmp.type,
			   tuple->dst.u.icmp.code,
			   ntohs(tuple->src.u.icmp.id));
		break;
	case IPPROTO_GRE:
		seq_printf(s, "srckey=0x%x dstkey=0x%x ",
			   ntohs(tuple->src.u.gre.key),
			   ntohs(tuple->dst.u.gre.key));
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(print_tuple);

struct ct_iter_state {
	struct seq_net_private p;
	struct hlist_nulls_head *hash;
	unsigned int htable_size;
	unsigned int bucket;
	u_int64_t time_now;
};

static struct hlist_nulls_node *ct_get_first(struct seq_file *seq)
{
	struct ct_iter_state *st = seq->private;
	struct hlist_nulls_node *n;

	for (st->bucket = 0;
	     st->bucket < st->htable_size;
	     st->bucket++) {
		n = rcu_dereference(
			hlist_nulls_first_rcu(&st->hash[st->bucket]));
		if (!is_a_nulls(n))
			return n;
	}
	return NULL;
}

static struct hlist_nulls_node *ct_get_next(struct seq_file *seq,
				      struct hlist_nulls_node *head)
{
	struct ct_iter_state *st = seq->private;

	head = rcu_dereference(hlist_nulls_next_rcu(head));
	while (is_a_nulls(head)) {
		if (likely(get_nulls_value(head) == st->bucket)) {
			if (++st->bucket >= st->htable_size)
				return NULL;
		}
		head = rcu_dereference(
			hlist_nulls_first_rcu(&st->hash[st->bucket]));
	}
	return head;
}

static struct hlist_nulls_node *ct_get_idx(struct seq_file *seq, loff_t pos)
{
	struct hlist_nulls_node *head = ct_get_first(seq);

	if (head)
		while (pos && (head = ct_get_next(seq, head)))
			pos--;
	return pos ? NULL : head;
}

static void *ct_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct ct_iter_state *st = seq->private;

	st->time_now = ktime_get_real_ns();
	rcu_read_lock();

	nf_conntrack_get_ht(&st->hash, &st->htable_size);
	return ct_get_idx(seq, *pos);
}

static void *ct_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	return ct_get_next(s, v);
}

static void ct_seq_stop(struct seq_file *s, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

#ifdef CONFIG_NF_CONNTRACK_SECMARK
static void ct_show_secctx(struct seq_file *s, const struct nf_conn *ct)
{
	int ret;
	u32 len;
	char *secctx;

	ret = security_secid_to_secctx(ct->secmark, &secctx, &len);
	if (ret)
		return;

	seq_printf(s, "secctx=%s ", secctx);

	security_release_secctx(secctx, len);
}
#else
static inline void ct_show_secctx(struct seq_file *s, const struct nf_conn *ct)
{
}
#endif

#ifdef CONFIG_NF_CONNTRACK_ZONES
static void ct_show_zone(struct seq_file *s, const struct nf_conn *ct,
			 int dir)
{
	const struct nf_conntrack_zone *zone = nf_ct_zone(ct);

	if (zone->dir != dir)
		return;
	switch (zone->dir) {
	case NF_CT_DEFAULT_ZONE_DIR:
		seq_printf(s, "zone=%u ", zone->id);
		break;
	case NF_CT_ZONE_DIR_ORIG:
		seq_printf(s, "zone-orig=%u ", zone->id);
		break;
	case NF_CT_ZONE_DIR_REPL:
		seq_printf(s, "zone-reply=%u ", zone->id);
		break;
	default:
		break;
	}
}
#else
static inline void ct_show_zone(struct seq_file *s, const struct nf_conn *ct,
				int dir)
{
}
#endif

#ifdef CONFIG_NF_CONNTRACK_TIMESTAMP
static void ct_show_delta_time(struct seq_file *s, const struct nf_conn *ct)
{
	struct ct_iter_state *st = s->private;
	struct nf_conn_tstamp *tstamp;
	s64 delta_time;

	tstamp = nf_conn_tstamp_find(ct);
	if (tstamp) {
		delta_time = st->time_now - tstamp->start;
		if (delta_time > 0)
			delta_time = div_s64(delta_time, NSEC_PER_SEC);
		else
			delta_time = 0;

		seq_printf(s, "delta-time=%llu ",
			   (unsigned long long)delta_time);
	}
	return;
}
#else
static inline void
ct_show_delta_time(struct seq_file *s, const struct nf_conn *ct)
{
}
#endif

static const char* l3proto_name(u16 proto)
{
	switch (proto) {
	case AF_INET: return "ipv4";
	case AF_INET6: return "ipv6";
	}

	return "unknown";
}

static const char* l4proto_name(u16 proto)
{
	switch (proto) {
	case IPPROTO_ICMP: return "icmp";
	case IPPROTO_TCP: return "tcp";
	case IPPROTO_UDP: return "udp";
	case IPPROTO_DCCP: return "dccp";
	case IPPROTO_GRE: return "gre";
	case IPPROTO_SCTP: return "sctp";
	case IPPROTO_UDPLITE: return "udplite";
	}

	return "unknown";
}

/* return 0 on success, 1 in case of error */
static int ct_seq_show(struct seq_file *s, void *v)
{
	struct nf_conntrack_tuple_hash *hash = v;
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(hash);
	const struct nf_conntrack_l4proto *l4proto;
	struct net *net = seq_file_net(s);
	int ret = 0;

	WARN_ON(!ct);
	if (unlikely(!atomic_inc_not_zero(&ct->ct_general.use)))
		return 0;

	if (nf_ct_should_gc(ct)) {
		nf_ct_kill(ct);
		goto release;
	}

	/* we only want to print DIR_ORIGINAL */
	if (NF_CT_DIRECTION(hash))
		goto release;

	if (!net_eq(nf_ct_net(ct), net))
		goto release;

	l4proto = __nf_ct_l4proto_find(nf_ct_l3num(ct), nf_ct_protonum(ct));
	WARN_ON(!l4proto);

	ret = -ENOSPC;
	seq_printf(s, "%-8s %u %-8s %u ",
		   l3proto_name(nf_ct_l3num(ct)), nf_ct_l3num(ct),
		   l4proto_name(l4proto->l4proto), nf_ct_protonum(ct));

	if (!test_bit(IPS_OFFLOAD_BIT, &ct->status))
		seq_printf(s, "%ld ", nf_ct_expires(ct)  / HZ);

	if (l4proto->print_conntrack)
		l4proto->print_conntrack(s, ct);

	print_tuple(s, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
		    l4proto);

	ct_show_zone(s, ct, NF_CT_ZONE_DIR_ORIG);

	if (seq_has_overflowed(s))
		goto release;

	if (seq_print_acct(s, ct, IP_CT_DIR_ORIGINAL))
		goto release;

	if (!(test_bit(IPS_SEEN_REPLY_BIT, &ct->status)))
		seq_puts(s, "[UNREPLIED] ");

	print_tuple(s, &ct->tuplehash[IP_CT_DIR_REPLY].tuple, l4proto);

	ct_show_zone(s, ct, NF_CT_ZONE_DIR_REPL);

	if (seq_print_acct(s, ct, IP_CT_DIR_REPLY))
		goto release;

	if (test_bit(IPS_OFFLOAD_BIT, &ct->status))
		seq_puts(s, "[OFFLOAD] ");
	else if (test_bit(IPS_ASSURED_BIT, &ct->status))
		seq_puts(s, "[ASSURED] ");

	if (seq_has_overflowed(s))
		goto release;

#if defined(CONFIG_NF_CONNTRACK_MARK)
	seq_printf(s, "mark=%u ", ct->mark);
#endif

	ct_show_secctx(s, ct);
	ct_show_zone(s, ct, NF_CT_DEFAULT_ZONE_DIR);
	ct_show_delta_time(s, ct);

	seq_printf(s, "use=%u\n", atomic_read(&ct->ct_general.use));

	if (seq_has_overflowed(s))
		goto release;

	ret = 0;
release:
	nf_ct_put(ct);
	return ret;
}

static const struct seq_operations ct_seq_ops = {
	.start = ct_seq_start,
	.next  = ct_seq_next,
	.stop  = ct_seq_stop,
	.show  = ct_seq_show
};

static void *ct_cpu_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	int cpu;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (cpu = *pos-1; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return per_cpu_ptr(net->ct.stat, cpu);
	}

	return NULL;
}

static void *ct_cpu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	int cpu;

	for (cpu = *pos; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return per_cpu_ptr(net->ct.stat, cpu);
	}

	return NULL;
}

static void ct_cpu_seq_stop(struct seq_file *seq, void *v)
{
}

static int ct_cpu_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = seq_file_net(seq);
	unsigned int nr_conntracks = atomic_read(&net->ct.count);
	const struct ip_conntrack_stat *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "entries  searched found new invalid ignore delete delete_list insert insert_failed drop early_drop icmp_error  expect_new expect_create expect_delete search_restart\n");
		return 0;
	}

	seq_printf(seq, "%08x  %08x %08x %08x %08x %08x %08x %08x "
			"%08x %08x %08x %08x %08x  %08x %08x %08x %08x\n",
		   nr_conntracks,
		   0,
		   st->found,
		   0,
		   st->invalid,
		   st->ignore,
		   0,
		   0,
		   st->insert,
		   st->insert_failed,
		   st->drop,
		   st->early_drop,
		   st->error,

		   st->expect_new,
		   st->expect_create,
		   st->expect_delete,
		   st->search_restart
		);
	return 0;
}

static const struct seq_operations ct_cpu_seq_ops = {
	.start	= ct_cpu_seq_start,
	.next	= ct_cpu_seq_next,
	.stop	= ct_cpu_seq_stop,
	.show	= ct_cpu_seq_show,
};

static int nf_conntrack_standalone_init_proc(struct net *net)
{
	struct proc_dir_entry *pde;
	kuid_t root_uid;
	kgid_t root_gid;

	pde = proc_create_net("nf_conntrack", 0440, net->proc_net, &ct_seq_ops,
			sizeof(struct ct_iter_state));
	if (!pde)
		goto out_nf_conntrack;

	root_uid = make_kuid(net->user_ns, 0);
	root_gid = make_kgid(net->user_ns, 0);
	if (uid_valid(root_uid) && gid_valid(root_gid))
		proc_set_user(pde, root_uid, root_gid);

	pde = proc_create_net("nf_conntrack", 0444, net->proc_net_stat,
			&ct_cpu_seq_ops, sizeof(struct seq_net_private));
	if (!pde)
		goto out_stat_nf_conntrack;
	return 0;

out_stat_nf_conntrack:
	remove_proc_entry("nf_conntrack", net->proc_net);
out_nf_conntrack:
	return -ENOMEM;
}

static void nf_conntrack_standalone_fini_proc(struct net *net)
{
	remove_proc_entry("nf_conntrack", net->proc_net_stat);
	remove_proc_entry("nf_conntrack", net->proc_net);
}
#else
static int nf_conntrack_standalone_init_proc(struct net *net)
{
	return 0;
}

static void nf_conntrack_standalone_fini_proc(struct net *net)
{
}
#endif /* CONFIG_NF_CONNTRACK_PROCFS */

/* Sysctl support */

#ifdef CONFIG_SYSCTL
/* Log invalid packets of a given protocol */
static int log_invalid_proto_min __read_mostly;
static int log_invalid_proto_max __read_mostly = 255;

/* size the user *wants to set */
static unsigned int nf_conntrack_htable_size_user __read_mostly;

static int
nf_conntrack_hash_sysctl(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret < 0 || !write)
		return ret;

	/* update ret, we might not be able to satisfy request */
	ret = nf_conntrack_hash_resize(nf_conntrack_htable_size_user);

	/* update it to the actual value used by conntrack */
	nf_conntrack_htable_size_user = nf_conntrack_htable_size;
	return ret;
}

static struct ctl_table_header *nf_ct_netfilter_header;

static struct ctl_table nf_ct_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_max",
		.data		= &nf_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "nf_conntrack_count",
		.data		= &init_net.ct.count,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname       = "nf_conntrack_buckets",
		.data           = &nf_conntrack_htable_size_user,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = nf_conntrack_hash_sysctl,
	},
	{
		.procname	= "nf_conntrack_checksum",
		.data		= &init_net.ct.sysctl_checksum,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "nf_conntrack_log_invalid",
		.data		= &init_net.ct.sysctl_log_invalid,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &log_invalid_proto_min,
		.extra2		= &log_invalid_proto_max,
	},
	{
		.procname	= "nf_conntrack_expect_max",
		.data		= &nf_ct_expect_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static struct ctl_table nf_ct_netfilter_table[] = {
	{
		.procname	= "nf_conntrack_max",
		.data		= &nf_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static int nf_conntrack_standalone_init_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(nf_ct_sysctl_table, sizeof(nf_ct_sysctl_table),
			GFP_KERNEL);
	if (!table)
		goto out_kmemdup;

	table[1].data = &net->ct.count;
	table[3].data = &net->ct.sysctl_checksum;
	table[4].data = &net->ct.sysctl_log_invalid;

	/* Don't export sysctls to unprivileged users */
	if (net->user_ns != &init_user_ns)
		table[0].procname = NULL;

	if (!net_eq(&init_net, net))
		table[2].mode = 0444;

	net->ct.sysctl_header = register_net_sysctl(net, "net/netfilter", table);
	if (!net->ct.sysctl_header)
		goto out_unregister_netfilter;

	return 0;

out_unregister_netfilter:
	kfree(table);
out_kmemdup:
	return -ENOMEM;
}

static void nf_conntrack_standalone_fini_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = net->ct.sysctl_header->ctl_table_arg;
	unregister_net_sysctl_table(net->ct.sysctl_header);
	kfree(table);
}
#else
static int nf_conntrack_standalone_init_sysctl(struct net *net)
{
	return 0;
}

static void nf_conntrack_standalone_fini_sysctl(struct net *net)
{
}
#endif /* CONFIG_SYSCTL */

static int nf_conntrack_pernet_init(struct net *net)
{
	int ret;

	ret = nf_conntrack_init_net(net);
	if (ret < 0)
		goto out_init;

	ret = nf_conntrack_standalone_init_proc(net);
	if (ret < 0)
		goto out_proc;

	net->ct.sysctl_checksum = 1;
	net->ct.sysctl_log_invalid = 0;
	ret = nf_conntrack_standalone_init_sysctl(net);
	if (ret < 0)
		goto out_sysctl;

	return 0;

out_sysctl:
	nf_conntrack_standalone_fini_proc(net);
out_proc:
	nf_conntrack_cleanup_net(net);
out_init:
	return ret;
}

static void nf_conntrack_pernet_exit(struct list_head *net_exit_list)
{
	struct net *net;

	list_for_each_entry(net, net_exit_list, exit_list) {
		nf_conntrack_standalone_fini_sysctl(net);
		nf_conntrack_standalone_fini_proc(net);
	}
	nf_conntrack_cleanup_net_list(net_exit_list);
}

static struct pernet_operations nf_conntrack_net_ops = {
	.init		= nf_conntrack_pernet_init,
	.exit_batch	= nf_conntrack_pernet_exit,
	.id		= &nf_conntrack_net_id,
	.size = sizeof(struct nf_conntrack_net),
};

static int __init nf_conntrack_standalone_init(void)
{
	int ret = nf_conntrack_init_start();
	if (ret < 0)
		goto out_start;

	BUILD_BUG_ON(SKB_NFCT_PTRMASK != NFCT_PTRMASK);
	BUILD_BUG_ON(NFCT_INFOMASK <= IP_CT_NUMBER);

#ifdef CONFIG_SYSCTL
	nf_ct_netfilter_header =
		register_net_sysctl(&init_net, "net", nf_ct_netfilter_table);
	if (!nf_ct_netfilter_header) {
		pr_err("nf_conntrack: can't register to sysctl.\n");
		ret = -ENOMEM;
		goto out_sysctl;
	}

	nf_conntrack_htable_size_user = nf_conntrack_htable_size;
#endif

	ret = register_pernet_subsys(&nf_conntrack_net_ops);
	if (ret < 0)
		goto out_pernet;

	nf_conntrack_init_end();
	return 0;

out_pernet:
#ifdef CONFIG_SYSCTL
	unregister_net_sysctl_table(nf_ct_netfilter_header);
out_sysctl:
#endif
	nf_conntrack_cleanup_end();
out_start:
	return ret;
}

static void __exit nf_conntrack_standalone_fini(void)
{
	nf_conntrack_cleanup_start();
	unregister_pernet_subsys(&nf_conntrack_net_ops);
#ifdef CONFIG_SYSCTL
	unregister_net_sysctl_table(nf_ct_netfilter_header);
#endif
	nf_conntrack_cleanup_end();
}

module_init(nf_conntrack_standalone_init);
module_exit(nf_conntrack_standalone_fini);
