#include <linux/types.h>
#include <linux/netfilter.h>
#include <net/tcp.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_seqadj.h>

int nf_ct_seqadj_init(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		      s32 off)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_conn_seqadj *seqadj;
	struct nf_ct_seqadj *this_way;

	if (off == 0)
		return 0;

	set_bit(IPS_SEQ_ADJUST_BIT, &ct->status);

	seqadj = nfct_seqadj(ct);
	this_way = &seqadj->seq[dir];
	this_way->offset_before	 = off;
	this_way->offset_after	 = off;
	return 0;
}
EXPORT_SYMBOL_GPL(nf_ct_seqadj_init);

int nf_ct_seqadj_set(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		     __be32 seq, s32 off)
{
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct nf_ct_seqadj *this_way;

	if (off == 0)
		return 0;

	if (unlikely(!seqadj)) {
		WARN_ONCE(1, "Missing nfct_seqadj_ext_add() setup call\n");
		return 0;
	}

	set_bit(IPS_SEQ_ADJUST_BIT, &ct->status);

	spin_lock_bh(&ct->lock);
	this_way = &seqadj->seq[dir];
	if (this_way->offset_before == this_way->offset_after ||
	    before(this_way->correction_pos, ntohl(seq))) {
		this_way->correction_pos = ntohl(seq);
		this_way->offset_before	 = this_way->offset_after;
		this_way->offset_after	+= off;
	}
	spin_unlock_bh(&ct->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(nf_ct_seqadj_set);

void nf_ct_tcp_seqadj_set(struct sk_buff *skb,
			  struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			  s32 off)
{
	const struct tcphdr *th;

	if (nf_ct_protonum(ct) != IPPROTO_TCP)
		return;

	th = (struct tcphdr *)(skb_network_header(skb) + ip_hdrlen(skb));
	nf_ct_seqadj_set(ct, ctinfo, th->seq, off);
}
EXPORT_SYMBOL_GPL(nf_ct_tcp_seqadj_set);

/* Adjust one found SACK option including checksum correction */
static void nf_ct_sack_block_adjust(struct sk_buff *skb,
				    struct tcphdr *tcph,
				    unsigned int sackoff,
				    unsigned int sackend,
				    struct nf_ct_seqadj *seq)
{
	while (sackoff < sackend) {
		struct tcp_sack_block_wire *sack;
		__be32 new_start_seq, new_end_seq;

		sack = (void *)skb->data + sackoff;
		if (after(ntohl(sack->start_seq) - seq->offset_before,
			  seq->correction_pos))
			new_start_seq = htonl(ntohl(sack->start_seq) -
					seq->offset_after);
		else
			new_start_seq = htonl(ntohl(sack->start_seq) -
					seq->offset_before);

		if (after(ntohl(sack->end_seq) - seq->offset_before,
			  seq->correction_pos))
			new_end_seq = htonl(ntohl(sack->end_seq) -
				      seq->offset_after);
		else
			new_end_seq = htonl(ntohl(sack->end_seq) -
				      seq->offset_before);

		pr_debug("sack_adjust: start_seq: %u->%u, end_seq: %u->%u\n",
			 ntohl(sack->start_seq), ntohl(new_start_seq),
			 ntohl(sack->end_seq), ntohl(new_end_seq));

		inet_proto_csum_replace4(&tcph->check, skb,
					 sack->start_seq, new_start_seq, false);
		inet_proto_csum_replace4(&tcph->check, skb,
					 sack->end_seq, new_end_seq, false);
		sack->start_seq = new_start_seq;
		sack->end_seq = new_end_seq;
		sackoff += sizeof(*sack);
	}
}

/* TCP SACK sequence number adjustment */
static unsigned int nf_ct_sack_adjust(struct sk_buff *skb,
				      unsigned int protoff,
				      struct tcphdr *tcph,
				      struct nf_conn *ct,
				      enum ip_conntrack_info ctinfo)
{
	unsigned int dir, optoff, optend;
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);

	optoff = protoff + sizeof(struct tcphdr);
	optend = protoff + tcph->doff * 4;

	if (!skb_make_writable(skb, optend))
		return 0;

	dir = CTINFO2DIR(ctinfo);

	while (optoff < optend) {
		/* Usually: option, length. */
		unsigned char *op = skb->data + optoff;

		switch (op[0]) {
		case TCPOPT_EOL:
			return 1;
		case TCPOPT_NOP:
			optoff++;
			continue;
		default:
			/* no partial options */
			if (optoff + 1 == optend ||
			    optoff + op[1] > optend ||
			    op[1] < 2)
				return 0;
			if (op[0] == TCPOPT_SACK &&
			    op[1] >= 2+TCPOLEN_SACK_PERBLOCK &&
			    ((op[1] - 2) % TCPOLEN_SACK_PERBLOCK) == 0)
				nf_ct_sack_block_adjust(skb, tcph, optoff + 2,
							optoff+op[1],
							&seqadj->seq[!dir]);
			optoff += op[1];
		}
	}
	return 1;
}

/* TCP sequence number adjustment.  Returns 1 on success, 0 on failure */
int nf_ct_seq_adjust(struct sk_buff *skb,
		     struct nf_conn *ct, enum ip_conntrack_info ctinfo,
		     unsigned int protoff)
{
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	struct tcphdr *tcph;
	__be32 newseq, newack;
	s32 seqoff, ackoff;
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	struct nf_ct_seqadj *this_way, *other_way;
	int res = 1;

	this_way  = &seqadj->seq[dir];
	other_way = &seqadj->seq[!dir];

	if (!skb_make_writable(skb, protoff + sizeof(*tcph)))
		return 0;

	tcph = (void *)skb->data + protoff;
	spin_lock_bh(&ct->lock);
	if (after(ntohl(tcph->seq), this_way->correction_pos))
		seqoff = this_way->offset_after;
	else
		seqoff = this_way->offset_before;

	newseq = htonl(ntohl(tcph->seq) + seqoff);
	inet_proto_csum_replace4(&tcph->check, skb, tcph->seq, newseq, false);
	pr_debug("Adjusting sequence number from %u->%u\n",
		 ntohl(tcph->seq), ntohl(newseq));
	tcph->seq = newseq;

	if (!tcph->ack)
		goto out;

	if (after(ntohl(tcph->ack_seq) - other_way->offset_before,
		  other_way->correction_pos))
		ackoff = other_way->offset_after;
	else
		ackoff = other_way->offset_before;

	newack = htonl(ntohl(tcph->ack_seq) - ackoff);
	inet_proto_csum_replace4(&tcph->check, skb, tcph->ack_seq, newack,
				 false);
	pr_debug("Adjusting ack number from %u->%u, ack from %u->%u\n",
		 ntohl(tcph->seq), ntohl(newseq), ntohl(tcph->ack_seq),
		 ntohl(newack));
	tcph->ack_seq = newack;

	res = nf_ct_sack_adjust(skb, protoff, tcph, ct, ctinfo);
out:
	spin_unlock_bh(&ct->lock);

	return res;
}
EXPORT_SYMBOL_GPL(nf_ct_seq_adjust);

s32 nf_ct_seq_offset(const struct nf_conn *ct,
		     enum ip_conntrack_dir dir,
		     u32 seq)
{
	struct nf_conn_seqadj *seqadj = nfct_seqadj(ct);
	struct nf_ct_seqadj *this_way;

	if (!seqadj)
		return 0;

	this_way = &seqadj->seq[dir];
	return after(seq, this_way->correction_pos) ?
		 this_way->offset_after : this_way->offset_before;
}
EXPORT_SYMBOL_GPL(nf_ct_seq_offset);

static struct nf_ct_ext_type nf_ct_seqadj_extend __read_mostly = {
	.len	= sizeof(struct nf_conn_seqadj),
	.align	= __alignof__(struct nf_conn_seqadj),
	.id	= NF_CT_EXT_SEQADJ,
};

int nf_conntrack_seqadj_init(void)
{
	return nf_ct_extend_register(&nf_ct_seqadj_extend);
}

void nf_conntrack_seqadj_fini(void)
{
	nf_ct_extend_unregister(&nf_ct_seqadj_extend);
}
