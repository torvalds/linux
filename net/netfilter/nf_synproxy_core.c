/*
 * Copyright (c) 2013 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <asm/unaligned.h>
#include <net/tcp.h>
#include <net/netns/generic.h>
#include <linux/proc_fs.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter/xt_SYNPROXY.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#include <net/netfilter/nf_conntrack_zones.h>

unsigned int synproxy_net_id;
EXPORT_SYMBOL_GPL(synproxy_net_id);

bool
synproxy_parse_options(const struct sk_buff *skb, unsigned int doff,
		       const struct tcphdr *th, struct synproxy_options *opts)
{
	int length = (th->doff * 4) - sizeof(*th);
	u8 buf[40], *ptr;

	ptr = skb_header_pointer(skb, doff + sizeof(*th), length, buf);
	if (ptr == NULL)
		return false;

	opts->options = 0;
	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		switch (opcode) {
		case TCPOPT_EOL:
			return true;
		case TCPOPT_NOP:
			length--;
			continue;
		default:
			opsize = *ptr++;
			if (opsize < 2)
				return true;
			if (opsize > length)
				return true;

			switch (opcode) {
			case TCPOPT_MSS:
				if (opsize == TCPOLEN_MSS) {
					opts->mss = get_unaligned_be16(ptr);
					opts->options |= XT_SYNPROXY_OPT_MSS;
				}
				break;
			case TCPOPT_WINDOW:
				if (opsize == TCPOLEN_WINDOW) {
					opts->wscale = *ptr;
					if (opts->wscale > TCP_MAX_WSCALE)
						opts->wscale = TCP_MAX_WSCALE;
					opts->options |= XT_SYNPROXY_OPT_WSCALE;
				}
				break;
			case TCPOPT_TIMESTAMP:
				if (opsize == TCPOLEN_TIMESTAMP) {
					opts->tsval = get_unaligned_be32(ptr);
					opts->tsecr = get_unaligned_be32(ptr + 4);
					opts->options |= XT_SYNPROXY_OPT_TIMESTAMP;
				}
				break;
			case TCPOPT_SACK_PERM:
				if (opsize == TCPOLEN_SACK_PERM)
					opts->options |= XT_SYNPROXY_OPT_SACK_PERM;
				break;
			}

			ptr += opsize - 2;
			length -= opsize;
		}
	}
	return true;
}
EXPORT_SYMBOL_GPL(synproxy_parse_options);

unsigned int synproxy_options_size(const struct synproxy_options *opts)
{
	unsigned int size = 0;

	if (opts->options & XT_SYNPROXY_OPT_MSS)
		size += TCPOLEN_MSS_ALIGNED;
	if (opts->options & XT_SYNPROXY_OPT_TIMESTAMP)
		size += TCPOLEN_TSTAMP_ALIGNED;
	else if (opts->options & XT_SYNPROXY_OPT_SACK_PERM)
		size += TCPOLEN_SACKPERM_ALIGNED;
	if (opts->options & XT_SYNPROXY_OPT_WSCALE)
		size += TCPOLEN_WSCALE_ALIGNED;

	return size;
}
EXPORT_SYMBOL_GPL(synproxy_options_size);

void
synproxy_build_options(struct tcphdr *th, const struct synproxy_options *opts)
{
	__be32 *ptr = (__be32 *)(th + 1);
	u8 options = opts->options;

	if (options & XT_SYNPROXY_OPT_MSS)
		*ptr++ = htonl((TCPOPT_MSS << 24) |
			       (TCPOLEN_MSS << 16) |
			       opts->mss);

	if (options & XT_SYNPROXY_OPT_TIMESTAMP) {
		if (options & XT_SYNPROXY_OPT_SACK_PERM)
			*ptr++ = htonl((TCPOPT_SACK_PERM << 24) |
				       (TCPOLEN_SACK_PERM << 16) |
				       (TCPOPT_TIMESTAMP << 8) |
				       TCPOLEN_TIMESTAMP);
		else
			*ptr++ = htonl((TCPOPT_NOP << 24) |
				       (TCPOPT_NOP << 16) |
				       (TCPOPT_TIMESTAMP << 8) |
				       TCPOLEN_TIMESTAMP);

		*ptr++ = htonl(opts->tsval);
		*ptr++ = htonl(opts->tsecr);
	} else if (options & XT_SYNPROXY_OPT_SACK_PERM)
		*ptr++ = htonl((TCPOPT_NOP << 24) |
			       (TCPOPT_NOP << 16) |
			       (TCPOPT_SACK_PERM << 8) |
			       TCPOLEN_SACK_PERM);

	if (options & XT_SYNPROXY_OPT_WSCALE)
		*ptr++ = htonl((TCPOPT_NOP << 24) |
			       (TCPOPT_WINDOW << 16) |
			       (TCPOLEN_WINDOW << 8) |
			       opts->wscale);
}
EXPORT_SYMBOL_GPL(synproxy_build_options);

void synproxy_init_timestamp_cookie(const struct xt_synproxy_info *info,
				    struct synproxy_options *opts)
{
	opts->tsecr = opts->tsval;
	opts->tsval = tcp_time_stamp_raw() & ~0x3f;

	if (opts->options & XT_SYNPROXY_OPT_WSCALE) {
		opts->tsval |= opts->wscale;
		opts->wscale = info->wscale;
	} else
		opts->tsval |= 0xf;

	if (opts->options & XT_SYNPROXY_OPT_SACK_PERM)
		opts->tsval |= 1 << 4;

	if (opts->options & XT_SYNPROXY_OPT_ECN)
		opts->tsval |= 1 << 5;
}
EXPORT_SYMBOL_GPL(synproxy_init_timestamp_cookie);

void synproxy_check_timestamp_cookie(struct synproxy_options *opts)
{
	opts->wscale = opts->tsecr & 0xf;
	if (opts->wscale != 0xf)
		opts->options |= XT_SYNPROXY_OPT_WSCALE;

	opts->options |= opts->tsecr & (1 << 4) ? XT_SYNPROXY_OPT_SACK_PERM : 0;

	opts->options |= opts->tsecr & (1 << 5) ? XT_SYNPROXY_OPT_ECN : 0;
}
EXPORT_SYMBOL_GPL(synproxy_check_timestamp_cookie);

unsigned int synproxy_tstamp_adjust(struct sk_buff *skb,
				    unsigned int protoff,
				    struct tcphdr *th,
				    struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    const struct nf_conn_synproxy *synproxy)
{
	unsigned int optoff, optend;
	__be32 *ptr, old;

	if (synproxy->tsoff == 0)
		return 1;

	optoff = protoff + sizeof(struct tcphdr);
	optend = protoff + th->doff * 4;

	if (!skb_make_writable(skb, optend))
		return 0;

	while (optoff < optend) {
		unsigned char *op = skb->data + optoff;

		switch (op[0]) {
		case TCPOPT_EOL:
			return 1;
		case TCPOPT_NOP:
			optoff++;
			continue;
		default:
			if (optoff + 1 == optend ||
			    optoff + op[1] > optend ||
			    op[1] < 2)
				return 0;
			if (op[0] == TCPOPT_TIMESTAMP &&
			    op[1] == TCPOLEN_TIMESTAMP) {
				if (CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY) {
					ptr = (__be32 *)&op[2];
					old = *ptr;
					*ptr = htonl(ntohl(*ptr) -
						     synproxy->tsoff);
				} else {
					ptr = (__be32 *)&op[6];
					old = *ptr;
					*ptr = htonl(ntohl(*ptr) +
						     synproxy->tsoff);
				}
				inet_proto_csum_replace4(&th->check, skb,
							 old, *ptr, false);
				return 1;
			}
			optoff += op[1];
		}
	}
	return 1;
}
EXPORT_SYMBOL_GPL(synproxy_tstamp_adjust);

static struct nf_ct_ext_type nf_ct_synproxy_extend __read_mostly = {
	.len		= sizeof(struct nf_conn_synproxy),
	.align		= __alignof__(struct nf_conn_synproxy),
	.id		= NF_CT_EXT_SYNPROXY,
};

#ifdef CONFIG_PROC_FS
static void *synproxy_cpu_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct synproxy_net *snet = synproxy_pernet(seq_file_net(seq));
	int cpu;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (cpu = *pos - 1; cpu < nr_cpu_ids; cpu++) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return per_cpu_ptr(snet->stats, cpu);
	}

	return NULL;
}

static void *synproxy_cpu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct synproxy_net *snet = synproxy_pernet(seq_file_net(seq));
	int cpu;

	for (cpu = *pos; cpu < nr_cpu_ids; cpu++) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu + 1;
		return per_cpu_ptr(snet->stats, cpu);
	}

	return NULL;
}

static void synproxy_cpu_seq_stop(struct seq_file *seq, void *v)
{
	return;
}

static int synproxy_cpu_seq_show(struct seq_file *seq, void *v)
{
	struct synproxy_stats *stats = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "entries\t\tsyn_received\t"
			      "cookie_invalid\tcookie_valid\t"
			      "cookie_retrans\tconn_reopened\n");
		return 0;
	}

	seq_printf(seq, "%08x\t%08x\t%08x\t%08x\t%08x\t%08x\n", 0,
		   stats->syn_received,
		   stats->cookie_invalid,
		   stats->cookie_valid,
		   stats->cookie_retrans,
		   stats->conn_reopened);

	return 0;
}

static const struct seq_operations synproxy_cpu_seq_ops = {
	.start		= synproxy_cpu_seq_start,
	.next		= synproxy_cpu_seq_next,
	.stop		= synproxy_cpu_seq_stop,
	.show		= synproxy_cpu_seq_show,
};

static int synproxy_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &synproxy_cpu_seq_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations synproxy_cpu_seq_fops = {
	.open		= synproxy_cpu_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
};

static int __net_init synproxy_proc_init(struct net *net)
{
	if (!proc_create("synproxy", 0444, net->proc_net_stat,
			 &synproxy_cpu_seq_fops))
		return -ENOMEM;
	return 0;
}

static void __net_exit synproxy_proc_exit(struct net *net)
{
	remove_proc_entry("synproxy", net->proc_net_stat);
}
#else
static int __net_init synproxy_proc_init(struct net *net)
{
	return 0;
}

static void __net_exit synproxy_proc_exit(struct net *net)
{
	return;
}
#endif /* CONFIG_PROC_FS */

static int __net_init synproxy_net_init(struct net *net)
{
	struct synproxy_net *snet = synproxy_pernet(net);
	struct nf_conn *ct;
	int err = -ENOMEM;

	ct = nf_ct_tmpl_alloc(net, &nf_ct_zone_dflt, GFP_KERNEL);
	if (!ct)
		goto err1;

	if (!nfct_seqadj_ext_add(ct))
		goto err2;
	if (!nfct_synproxy_ext_add(ct))
		goto err2;

	__set_bit(IPS_CONFIRMED_BIT, &ct->status);
	nf_conntrack_get(&ct->ct_general);
	snet->tmpl = ct;

	snet->stats = alloc_percpu(struct synproxy_stats);
	if (snet->stats == NULL)
		goto err2;

	err = synproxy_proc_init(net);
	if (err < 0)
		goto err3;

	return 0;

err3:
	free_percpu(snet->stats);
err2:
	nf_ct_tmpl_free(ct);
err1:
	return err;
}

static void __net_exit synproxy_net_exit(struct net *net)
{
	struct synproxy_net *snet = synproxy_pernet(net);

	nf_ct_put(snet->tmpl);
	synproxy_proc_exit(net);
	free_percpu(snet->stats);
}

static struct pernet_operations synproxy_net_ops = {
	.init		= synproxy_net_init,
	.exit		= synproxy_net_exit,
	.id		= &synproxy_net_id,
	.size		= sizeof(struct synproxy_net),
	.async		= true,
};

static int __init synproxy_core_init(void)
{
	int err;

	err = nf_ct_extend_register(&nf_ct_synproxy_extend);
	if (err < 0)
		goto err1;

	err = register_pernet_subsys(&synproxy_net_ops);
	if (err < 0)
		goto err2;

	return 0;

err2:
	nf_ct_extend_unregister(&nf_ct_synproxy_extend);
err1:
	return err;
}

static void __exit synproxy_core_exit(void)
{
	unregister_pernet_subsys(&synproxy_net_ops);
	nf_ct_extend_unregister(&nf_ct_synproxy_extend);
}

module_init(synproxy_core_init);
module_exit(synproxy_core_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
