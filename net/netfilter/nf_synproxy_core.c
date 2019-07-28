// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 Patrick McHardy <kaber@trash.net>
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <asm/unaligned.h>
#include <net/tcp.h>
#include <net/netns/generic.h>
#include <linux/proc_fs.h>

#include <linux/netfilter_ipv6.h>
#include <linux/netfilter/nf_synproxy.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_synproxy.h>

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
					opts->options |= NF_SYNPROXY_OPT_MSS;
				}
				break;
			case TCPOPT_WINDOW:
				if (opsize == TCPOLEN_WINDOW) {
					opts->wscale = *ptr;
					if (opts->wscale > TCP_MAX_WSCALE)
						opts->wscale = TCP_MAX_WSCALE;
					opts->options |= NF_SYNPROXY_OPT_WSCALE;
				}
				break;
			case TCPOPT_TIMESTAMP:
				if (opsize == TCPOLEN_TIMESTAMP) {
					opts->tsval = get_unaligned_be32(ptr);
					opts->tsecr = get_unaligned_be32(ptr + 4);
					opts->options |= NF_SYNPROXY_OPT_TIMESTAMP;
				}
				break;
			case TCPOPT_SACK_PERM:
				if (opsize == TCPOLEN_SACK_PERM)
					opts->options |= NF_SYNPROXY_OPT_SACK_PERM;
				break;
			}

			ptr += opsize - 2;
			length -= opsize;
		}
	}
	return true;
}
EXPORT_SYMBOL_GPL(synproxy_parse_options);

static unsigned int
synproxy_options_size(const struct synproxy_options *opts)
{
	unsigned int size = 0;

	if (opts->options & NF_SYNPROXY_OPT_MSS)
		size += TCPOLEN_MSS_ALIGNED;
	if (opts->options & NF_SYNPROXY_OPT_TIMESTAMP)
		size += TCPOLEN_TSTAMP_ALIGNED;
	else if (opts->options & NF_SYNPROXY_OPT_SACK_PERM)
		size += TCPOLEN_SACKPERM_ALIGNED;
	if (opts->options & NF_SYNPROXY_OPT_WSCALE)
		size += TCPOLEN_WSCALE_ALIGNED;

	return size;
}

static void
synproxy_build_options(struct tcphdr *th, const struct synproxy_options *opts)
{
	__be32 *ptr = (__be32 *)(th + 1);
	u8 options = opts->options;

	if (options & NF_SYNPROXY_OPT_MSS)
		*ptr++ = htonl((TCPOPT_MSS << 24) |
			       (TCPOLEN_MSS << 16) |
			       opts->mss);

	if (options & NF_SYNPROXY_OPT_TIMESTAMP) {
		if (options & NF_SYNPROXY_OPT_SACK_PERM)
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
	} else if (options & NF_SYNPROXY_OPT_SACK_PERM)
		*ptr++ = htonl((TCPOPT_NOP << 24) |
			       (TCPOPT_NOP << 16) |
			       (TCPOPT_SACK_PERM << 8) |
			       TCPOLEN_SACK_PERM);

	if (options & NF_SYNPROXY_OPT_WSCALE)
		*ptr++ = htonl((TCPOPT_NOP << 24) |
			       (TCPOPT_WINDOW << 16) |
			       (TCPOLEN_WINDOW << 8) |
			       opts->wscale);
}

void synproxy_init_timestamp_cookie(const struct nf_synproxy_info *info,
				    struct synproxy_options *opts)
{
	opts->tsecr = opts->tsval;
	opts->tsval = tcp_time_stamp_raw() & ~0x3f;

	if (opts->options & NF_SYNPROXY_OPT_WSCALE) {
		opts->tsval |= opts->wscale;
		opts->wscale = info->wscale;
	} else
		opts->tsval |= 0xf;

	if (opts->options & NF_SYNPROXY_OPT_SACK_PERM)
		opts->tsval |= 1 << 4;

	if (opts->options & NF_SYNPROXY_OPT_ECN)
		opts->tsval |= 1 << 5;
}
EXPORT_SYMBOL_GPL(synproxy_init_timestamp_cookie);

static void
synproxy_check_timestamp_cookie(struct synproxy_options *opts)
{
	opts->wscale = opts->tsecr & 0xf;
	if (opts->wscale != 0xf)
		opts->options |= NF_SYNPROXY_OPT_WSCALE;

	opts->options |= opts->tsecr & (1 << 4) ? NF_SYNPROXY_OPT_SACK_PERM : 0;

	opts->options |= opts->tsecr & (1 << 5) ? NF_SYNPROXY_OPT_ECN : 0;
}

static unsigned int
synproxy_tstamp_adjust(struct sk_buff *skb, unsigned int protoff,
		       struct tcphdr *th, struct nf_conn *ct,
		       enum ip_conntrack_info ctinfo,
		       const struct nf_conn_synproxy *synproxy)
{
	unsigned int optoff, optend;
	__be32 *ptr, old;

	if (synproxy->tsoff == 0)
		return 1;

	optoff = protoff + sizeof(struct tcphdr);
	optend = protoff + th->doff * 4;

	if (skb_ensure_writable(skb, optend))
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

static int __net_init synproxy_proc_init(struct net *net)
{
	if (!proc_create_net("synproxy", 0444, net->proc_net_stat,
			&synproxy_cpu_seq_ops, sizeof(struct seq_net_private)))
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

static struct iphdr *
synproxy_build_ip(struct net *net, struct sk_buff *skb, __be32 saddr,
		  __be32 daddr)
{
	struct iphdr *iph;

	skb_reset_network_header(skb);
	iph = skb_put(skb, sizeof(*iph));
	iph->version	= 4;
	iph->ihl	= sizeof(*iph) / 4;
	iph->tos	= 0;
	iph->id		= 0;
	iph->frag_off	= htons(IP_DF);
	iph->ttl	= net->ipv4.sysctl_ip_default_ttl;
	iph->protocol	= IPPROTO_TCP;
	iph->check	= 0;
	iph->saddr	= saddr;
	iph->daddr	= daddr;

	return iph;
}

static void
synproxy_send_tcp(struct net *net,
		  const struct sk_buff *skb, struct sk_buff *nskb,
		  struct nf_conntrack *nfct, enum ip_conntrack_info ctinfo,
		  struct iphdr *niph, struct tcphdr *nth,
		  unsigned int tcp_hdr_size)
{
	nth->check = ~tcp_v4_check(tcp_hdr_size, niph->saddr, niph->daddr, 0);
	nskb->ip_summed   = CHECKSUM_PARTIAL;
	nskb->csum_start  = (unsigned char *)nth - nskb->head;
	nskb->csum_offset = offsetof(struct tcphdr, check);

	skb_dst_set_noref(nskb, skb_dst(skb));
	nskb->protocol = htons(ETH_P_IP);
	if (ip_route_me_harder(net, nskb, RTN_UNSPEC))
		goto free_nskb;

	if (nfct) {
		nf_ct_set(nskb, (struct nf_conn *)nfct, ctinfo);
		nf_conntrack_get(nfct);
	}

	ip_local_out(net, nskb->sk, nskb);
	return;

free_nskb:
	kfree_skb(nskb);
}

void
synproxy_send_client_synack(struct net *net,
			    const struct sk_buff *skb, const struct tcphdr *th,
			    const struct synproxy_options *opts)
{
	struct sk_buff *nskb;
	struct iphdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;
	u16 mss = opts->mss_encode;

	iph = ip_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip(net, nskb, iph->daddr, iph->saddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->dest;
	nth->dest	= th->source;
	nth->seq	= htonl(__cookie_v4_init_sequence(iph, th, &mss));
	nth->ack_seq	= htonl(ntohl(th->seq) + 1);
	tcp_flag_word(nth) = TCP_FLAG_SYN | TCP_FLAG_ACK;
	if (opts->options & NF_SYNPROXY_OPT_ECN)
		tcp_flag_word(nth) |= TCP_FLAG_ECE;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= 0;
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp(net, skb, nskb, skb_nfct(skb),
			  IP_CT_ESTABLISHED_REPLY, niph, nth, tcp_hdr_size);
}
EXPORT_SYMBOL_GPL(synproxy_send_client_synack);

static void
synproxy_send_server_syn(struct net *net,
			 const struct sk_buff *skb, const struct tcphdr *th,
			 const struct synproxy_options *opts, u32 recv_seq)
{
	struct synproxy_net *snet = synproxy_pernet(net);
	struct sk_buff *nskb;
	struct iphdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;

	iph = ip_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip(net, nskb, iph->saddr, iph->daddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->source;
	nth->dest	= th->dest;
	nth->seq	= htonl(recv_seq - 1);
	/* ack_seq is used to relay our ISN to the synproxy hook to initialize
	 * sequence number translation once a connection tracking entry exists.
	 */
	nth->ack_seq	= htonl(ntohl(th->ack_seq) - 1);
	tcp_flag_word(nth) = TCP_FLAG_SYN;
	if (opts->options & NF_SYNPROXY_OPT_ECN)
		tcp_flag_word(nth) |= TCP_FLAG_ECE | TCP_FLAG_CWR;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= th->window;
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp(net, skb, nskb, &snet->tmpl->ct_general, IP_CT_NEW,
			  niph, nth, tcp_hdr_size);
}

static void
synproxy_send_server_ack(struct net *net,
			 const struct ip_ct_tcp *state,
			 const struct sk_buff *skb, const struct tcphdr *th,
			 const struct synproxy_options *opts)
{
	struct sk_buff *nskb;
	struct iphdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;

	iph = ip_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip(net, nskb, iph->daddr, iph->saddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->dest;
	nth->dest	= th->source;
	nth->seq	= htonl(ntohl(th->ack_seq));
	nth->ack_seq	= htonl(ntohl(th->seq) + 1);
	tcp_flag_word(nth) = TCP_FLAG_ACK;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= htons(state->seen[IP_CT_DIR_ORIGINAL].td_maxwin);
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp(net, skb, nskb, NULL, 0, niph, nth, tcp_hdr_size);
}

static void
synproxy_send_client_ack(struct net *net,
			 const struct sk_buff *skb, const struct tcphdr *th,
			 const struct synproxy_options *opts)
{
	struct sk_buff *nskb;
	struct iphdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;

	iph = ip_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip(net, nskb, iph->saddr, iph->daddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->source;
	nth->dest	= th->dest;
	nth->seq	= htonl(ntohl(th->seq) + 1);
	nth->ack_seq	= th->ack_seq;
	tcp_flag_word(nth) = TCP_FLAG_ACK;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= htons(ntohs(th->window) >> opts->wscale);
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp(net, skb, nskb, skb_nfct(skb),
			  IP_CT_ESTABLISHED_REPLY, niph, nth, tcp_hdr_size);
}

bool
synproxy_recv_client_ack(struct net *net,
			 const struct sk_buff *skb, const struct tcphdr *th,
			 struct synproxy_options *opts, u32 recv_seq)
{
	struct synproxy_net *snet = synproxy_pernet(net);
	int mss;

	mss = __cookie_v4_check(ip_hdr(skb), th, ntohl(th->ack_seq) - 1);
	if (mss == 0) {
		this_cpu_inc(snet->stats->cookie_invalid);
		return false;
	}

	this_cpu_inc(snet->stats->cookie_valid);
	opts->mss = mss;
	opts->options |= NF_SYNPROXY_OPT_MSS;

	if (opts->options & NF_SYNPROXY_OPT_TIMESTAMP)
		synproxy_check_timestamp_cookie(opts);

	synproxy_send_server_syn(net, skb, th, opts, recv_seq);
	return true;
}
EXPORT_SYMBOL_GPL(synproxy_recv_client_ack);

unsigned int
ipv4_synproxy_hook(void *priv, struct sk_buff *skb,
		   const struct nf_hook_state *nhs)
{
	struct net *net = nhs->net;
	struct synproxy_net *snet = synproxy_pernet(net);
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	struct nf_conn_synproxy *synproxy;
	struct synproxy_options opts = {};
	const struct ip_ct_tcp *state;
	struct tcphdr *th, _th;
	unsigned int thoff;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	synproxy = nfct_synproxy(ct);
	if (!synproxy)
		return NF_ACCEPT;

	if (nf_is_loopback_packet(skb) ||
	    ip_hdr(skb)->protocol != IPPROTO_TCP)
		return NF_ACCEPT;

	thoff = ip_hdrlen(skb);
	th = skb_header_pointer(skb, thoff, sizeof(_th), &_th);
	if (!th)
		return NF_DROP;

	state = &ct->proto.tcp;
	switch (state->state) {
	case TCP_CONNTRACK_CLOSE:
		if (th->rst && CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL) {
			nf_ct_seqadj_init(ct, ctinfo, synproxy->isn -
						      ntohl(th->seq) + 1);
			break;
		}

		if (!th->syn || th->ack ||
		    CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
			break;

		/* Reopened connection - reset the sequence number and timestamp
		 * adjustments, they will get initialized once the connection is
		 * reestablished.
		 */
		nf_ct_seqadj_init(ct, ctinfo, 0);
		synproxy->tsoff = 0;
		this_cpu_inc(snet->stats->conn_reopened);

		/* fall through */
	case TCP_CONNTRACK_SYN_SENT:
		if (!synproxy_parse_options(skb, thoff, th, &opts))
			return NF_DROP;

		if (!th->syn && th->ack &&
		    CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL) {
			/* Keep-Alives are sent with SEG.SEQ = SND.NXT-1,
			 * therefore we need to add 1 to make the SYN sequence
			 * number match the one of first SYN.
			 */
			if (synproxy_recv_client_ack(net, skb, th, &opts,
						     ntohl(th->seq) + 1)) {
				this_cpu_inc(snet->stats->cookie_retrans);
				consume_skb(skb);
				return NF_STOLEN;
			} else {
				return NF_DROP;
			}
		}

		synproxy->isn = ntohl(th->ack_seq);
		if (opts.options & NF_SYNPROXY_OPT_TIMESTAMP)
			synproxy->its = opts.tsecr;

		nf_conntrack_event_cache(IPCT_SYNPROXY, ct);
		break;
	case TCP_CONNTRACK_SYN_RECV:
		if (!th->syn || !th->ack)
			break;

		if (!synproxy_parse_options(skb, thoff, th, &opts))
			return NF_DROP;

		if (opts.options & NF_SYNPROXY_OPT_TIMESTAMP) {
			synproxy->tsoff = opts.tsval - synproxy->its;
			nf_conntrack_event_cache(IPCT_SYNPROXY, ct);
		}

		opts.options &= ~(NF_SYNPROXY_OPT_MSS |
				  NF_SYNPROXY_OPT_WSCALE |
				  NF_SYNPROXY_OPT_SACK_PERM);

		swap(opts.tsval, opts.tsecr);
		synproxy_send_server_ack(net, state, skb, th, &opts);

		nf_ct_seqadj_init(ct, ctinfo, synproxy->isn - ntohl(th->seq));
		nf_conntrack_event_cache(IPCT_SEQADJ, ct);

		swap(opts.tsval, opts.tsecr);
		synproxy_send_client_ack(net, skb, th, &opts);

		consume_skb(skb);
		return NF_STOLEN;
	default:
		break;
	}

	synproxy_tstamp_adjust(skb, thoff, th, ct, ctinfo, synproxy);
	return NF_ACCEPT;
}
EXPORT_SYMBOL_GPL(ipv4_synproxy_hook);

static const struct nf_hook_ops ipv4_synproxy_ops[] = {
	{
		.hook		= ipv4_synproxy_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM - 1,
	},
	{
		.hook		= ipv4_synproxy_hook,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM - 1,
	},
};

int nf_synproxy_ipv4_init(struct synproxy_net *snet, struct net *net)
{
	int err;

	if (snet->hook_ref4 == 0) {
		err = nf_register_net_hooks(net, ipv4_synproxy_ops,
					    ARRAY_SIZE(ipv4_synproxy_ops));
		if (err)
			return err;
	}

	snet->hook_ref4++;
	return 0;
}
EXPORT_SYMBOL_GPL(nf_synproxy_ipv4_init);

void nf_synproxy_ipv4_fini(struct synproxy_net *snet, struct net *net)
{
	snet->hook_ref4--;
	if (snet->hook_ref4 == 0)
		nf_unregister_net_hooks(net, ipv4_synproxy_ops,
					ARRAY_SIZE(ipv4_synproxy_ops));
}
EXPORT_SYMBOL_GPL(nf_synproxy_ipv4_fini);

#if IS_ENABLED(CONFIG_IPV6)
static struct ipv6hdr *
synproxy_build_ip_ipv6(struct net *net, struct sk_buff *skb,
		       const struct in6_addr *saddr,
		       const struct in6_addr *daddr)
{
	struct ipv6hdr *iph;

	skb_reset_network_header(skb);
	iph = skb_put(skb, sizeof(*iph));
	ip6_flow_hdr(iph, 0, 0);
	iph->hop_limit	= net->ipv6.devconf_all->hop_limit;
	iph->nexthdr	= IPPROTO_TCP;
	iph->saddr	= *saddr;
	iph->daddr	= *daddr;

	return iph;
}

static void
synproxy_send_tcp_ipv6(struct net *net,
		       const struct sk_buff *skb, struct sk_buff *nskb,
		       struct nf_conntrack *nfct, enum ip_conntrack_info ctinfo,
		       struct ipv6hdr *niph, struct tcphdr *nth,
		       unsigned int tcp_hdr_size)
{
	struct dst_entry *dst;
	struct flowi6 fl6;
	int err;

	nth->check = ~tcp_v6_check(tcp_hdr_size, &niph->saddr, &niph->daddr, 0);
	nskb->ip_summed   = CHECKSUM_PARTIAL;
	nskb->csum_start  = (unsigned char *)nth - nskb->head;
	nskb->csum_offset = offsetof(struct tcphdr, check);

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_proto = IPPROTO_TCP;
	fl6.saddr = niph->saddr;
	fl6.daddr = niph->daddr;
	fl6.fl6_sport = nth->source;
	fl6.fl6_dport = nth->dest;
	security_skb_classify_flow((struct sk_buff *)skb,
				   flowi6_to_flowi(&fl6));
	err = nf_ip6_route(net, &dst, flowi6_to_flowi(&fl6), false);
	if (err) {
		goto free_nskb;
	}

	dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), NULL, 0);
	if (IS_ERR(dst))
		goto free_nskb;

	skb_dst_set(nskb, dst);

	if (nfct) {
		nf_ct_set(nskb, (struct nf_conn *)nfct, ctinfo);
		nf_conntrack_get(nfct);
	}

	ip6_local_out(net, nskb->sk, nskb);
	return;

free_nskb:
	kfree_skb(nskb);
}

void
synproxy_send_client_synack_ipv6(struct net *net,
				 const struct sk_buff *skb,
				 const struct tcphdr *th,
				 const struct synproxy_options *opts)
{
	struct sk_buff *nskb;
	struct ipv6hdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;
	u16 mss = opts->mss_encode;

	iph = ipv6_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip_ipv6(net, nskb, &iph->daddr, &iph->saddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->dest;
	nth->dest	= th->source;
	nth->seq	= htonl(nf_ipv6_cookie_init_sequence(iph, th, &mss));
	nth->ack_seq	= htonl(ntohl(th->seq) + 1);
	tcp_flag_word(nth) = TCP_FLAG_SYN | TCP_FLAG_ACK;
	if (opts->options & NF_SYNPROXY_OPT_ECN)
		tcp_flag_word(nth) |= TCP_FLAG_ECE;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= 0;
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp_ipv6(net, skb, nskb, skb_nfct(skb),
			       IP_CT_ESTABLISHED_REPLY, niph, nth,
			       tcp_hdr_size);
}
EXPORT_SYMBOL_GPL(synproxy_send_client_synack_ipv6);

static void
synproxy_send_server_syn_ipv6(struct net *net, const struct sk_buff *skb,
			      const struct tcphdr *th,
			      const struct synproxy_options *opts, u32 recv_seq)
{
	struct synproxy_net *snet = synproxy_pernet(net);
	struct sk_buff *nskb;
	struct ipv6hdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;

	iph = ipv6_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip_ipv6(net, nskb, &iph->saddr, &iph->daddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->source;
	nth->dest	= th->dest;
	nth->seq	= htonl(recv_seq - 1);
	/* ack_seq is used to relay our ISN to the synproxy hook to initialize
	 * sequence number translation once a connection tracking entry exists.
	 */
	nth->ack_seq	= htonl(ntohl(th->ack_seq) - 1);
	tcp_flag_word(nth) = TCP_FLAG_SYN;
	if (opts->options & NF_SYNPROXY_OPT_ECN)
		tcp_flag_word(nth) |= TCP_FLAG_ECE | TCP_FLAG_CWR;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= th->window;
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp_ipv6(net, skb, nskb, &snet->tmpl->ct_general,
			       IP_CT_NEW, niph, nth, tcp_hdr_size);
}

static void
synproxy_send_server_ack_ipv6(struct net *net, const struct ip_ct_tcp *state,
			      const struct sk_buff *skb,
			      const struct tcphdr *th,
			      const struct synproxy_options *opts)
{
	struct sk_buff *nskb;
	struct ipv6hdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;

	iph = ipv6_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip_ipv6(net, nskb, &iph->daddr, &iph->saddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->dest;
	nth->dest	= th->source;
	nth->seq	= htonl(ntohl(th->ack_seq));
	nth->ack_seq	= htonl(ntohl(th->seq) + 1);
	tcp_flag_word(nth) = TCP_FLAG_ACK;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= htons(state->seen[IP_CT_DIR_ORIGINAL].td_maxwin);
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp_ipv6(net, skb, nskb, NULL, 0, niph, nth,
			       tcp_hdr_size);
}

static void
synproxy_send_client_ack_ipv6(struct net *net, const struct sk_buff *skb,
			      const struct tcphdr *th,
			      const struct synproxy_options *opts)
{
	struct sk_buff *nskb;
	struct ipv6hdr *iph, *niph;
	struct tcphdr *nth;
	unsigned int tcp_hdr_size;

	iph = ipv6_hdr(skb);

	tcp_hdr_size = sizeof(*nth) + synproxy_options_size(opts);
	nskb = alloc_skb(sizeof(*niph) + tcp_hdr_size + MAX_TCP_HEADER,
			 GFP_ATOMIC);
	if (!nskb)
		return;
	skb_reserve(nskb, MAX_TCP_HEADER);

	niph = synproxy_build_ip_ipv6(net, nskb, &iph->saddr, &iph->daddr);

	skb_reset_transport_header(nskb);
	nth = skb_put(nskb, tcp_hdr_size);
	nth->source	= th->source;
	nth->dest	= th->dest;
	nth->seq	= htonl(ntohl(th->seq) + 1);
	nth->ack_seq	= th->ack_seq;
	tcp_flag_word(nth) = TCP_FLAG_ACK;
	nth->doff	= tcp_hdr_size / 4;
	nth->window	= htons(ntohs(th->window) >> opts->wscale);
	nth->check	= 0;
	nth->urg_ptr	= 0;

	synproxy_build_options(nth, opts);

	synproxy_send_tcp_ipv6(net, skb, nskb, skb_nfct(skb),
			       IP_CT_ESTABLISHED_REPLY, niph, nth,
			       tcp_hdr_size);
}

bool
synproxy_recv_client_ack_ipv6(struct net *net,
			      const struct sk_buff *skb,
			      const struct tcphdr *th,
			      struct synproxy_options *opts, u32 recv_seq)
{
	struct synproxy_net *snet = synproxy_pernet(net);
	int mss;

	mss = nf_cookie_v6_check(ipv6_hdr(skb), th, ntohl(th->ack_seq) - 1);
	if (mss == 0) {
		this_cpu_inc(snet->stats->cookie_invalid);
		return false;
	}

	this_cpu_inc(snet->stats->cookie_valid);
	opts->mss = mss;
	opts->options |= NF_SYNPROXY_OPT_MSS;

	if (opts->options & NF_SYNPROXY_OPT_TIMESTAMP)
		synproxy_check_timestamp_cookie(opts);

	synproxy_send_server_syn_ipv6(net, skb, th, opts, recv_seq);
	return true;
}
EXPORT_SYMBOL_GPL(synproxy_recv_client_ack_ipv6);

unsigned int
ipv6_synproxy_hook(void *priv, struct sk_buff *skb,
		   const struct nf_hook_state *nhs)
{
	struct net *net = nhs->net;
	struct synproxy_net *snet = synproxy_pernet(net);
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	struct nf_conn_synproxy *synproxy;
	struct synproxy_options opts = {};
	const struct ip_ct_tcp *state;
	struct tcphdr *th, _th;
	__be16 frag_off;
	u8 nexthdr;
	int thoff;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	synproxy = nfct_synproxy(ct);
	if (!synproxy)
		return NF_ACCEPT;

	if (nf_is_loopback_packet(skb))
		return NF_ACCEPT;

	nexthdr = ipv6_hdr(skb)->nexthdr;
	thoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr,
				 &frag_off);
	if (thoff < 0 || nexthdr != IPPROTO_TCP)
		return NF_ACCEPT;

	th = skb_header_pointer(skb, thoff, sizeof(_th), &_th);
	if (!th)
		return NF_DROP;

	state = &ct->proto.tcp;
	switch (state->state) {
	case TCP_CONNTRACK_CLOSE:
		if (th->rst && CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL) {
			nf_ct_seqadj_init(ct, ctinfo, synproxy->isn -
						      ntohl(th->seq) + 1);
			break;
		}

		if (!th->syn || th->ack ||
		    CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
			break;

		/* Reopened connection - reset the sequence number and timestamp
		 * adjustments, they will get initialized once the connection is
		 * reestablished.
		 */
		nf_ct_seqadj_init(ct, ctinfo, 0);
		synproxy->tsoff = 0;
		this_cpu_inc(snet->stats->conn_reopened);

		/* fall through */
	case TCP_CONNTRACK_SYN_SENT:
		if (!synproxy_parse_options(skb, thoff, th, &opts))
			return NF_DROP;

		if (!th->syn && th->ack &&
		    CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL) {
			/* Keep-Alives are sent with SEG.SEQ = SND.NXT-1,
			 * therefore we need to add 1 to make the SYN sequence
			 * number match the one of first SYN.
			 */
			if (synproxy_recv_client_ack_ipv6(net, skb, th, &opts,
							  ntohl(th->seq) + 1)) {
				this_cpu_inc(snet->stats->cookie_retrans);
				consume_skb(skb);
				return NF_STOLEN;
			} else {
				return NF_DROP;
			}
		}

		synproxy->isn = ntohl(th->ack_seq);
		if (opts.options & NF_SYNPROXY_OPT_TIMESTAMP)
			synproxy->its = opts.tsecr;

		nf_conntrack_event_cache(IPCT_SYNPROXY, ct);
		break;
	case TCP_CONNTRACK_SYN_RECV:
		if (!th->syn || !th->ack)
			break;

		if (!synproxy_parse_options(skb, thoff, th, &opts))
			return NF_DROP;

		if (opts.options & NF_SYNPROXY_OPT_TIMESTAMP) {
			synproxy->tsoff = opts.tsval - synproxy->its;
			nf_conntrack_event_cache(IPCT_SYNPROXY, ct);
		}

		opts.options &= ~(NF_SYNPROXY_OPT_MSS |
				  NF_SYNPROXY_OPT_WSCALE |
				  NF_SYNPROXY_OPT_SACK_PERM);

		swap(opts.tsval, opts.tsecr);
		synproxy_send_server_ack_ipv6(net, state, skb, th, &opts);

		nf_ct_seqadj_init(ct, ctinfo, synproxy->isn - ntohl(th->seq));
		nf_conntrack_event_cache(IPCT_SEQADJ, ct);

		swap(opts.tsval, opts.tsecr);
		synproxy_send_client_ack_ipv6(net, skb, th, &opts);

		consume_skb(skb);
		return NF_STOLEN;
	default:
		break;
	}

	synproxy_tstamp_adjust(skb, thoff, th, ct, ctinfo, synproxy);
	return NF_ACCEPT;
}
EXPORT_SYMBOL_GPL(ipv6_synproxy_hook);

static const struct nf_hook_ops ipv6_synproxy_ops[] = {
	{
		.hook		= ipv6_synproxy_hook,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_LOCAL_IN,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM - 1,
	},
	{
		.hook		= ipv6_synproxy_hook,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_POST_ROUTING,
		.priority	= NF_IP_PRI_CONNTRACK_CONFIRM - 1,
	},
};

int
nf_synproxy_ipv6_init(struct synproxy_net *snet, struct net *net)
{
	int err;

	if (snet->hook_ref6 == 0) {
		err = nf_register_net_hooks(net, ipv6_synproxy_ops,
					    ARRAY_SIZE(ipv6_synproxy_ops));
		if (err)
			return err;
	}

	snet->hook_ref6++;
	return 0;
}
EXPORT_SYMBOL_GPL(nf_synproxy_ipv6_init);

void
nf_synproxy_ipv6_fini(struct synproxy_net *snet, struct net *net)
{
	snet->hook_ref6--;
	if (snet->hook_ref6 == 0)
		nf_unregister_net_hooks(net, ipv6_synproxy_ops,
					ARRAY_SIZE(ipv6_synproxy_ops));
}
EXPORT_SYMBOL_GPL(nf_synproxy_ipv6_fini);
#endif /* CONFIG_IPV6 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
