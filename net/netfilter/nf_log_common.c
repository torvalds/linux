// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter/xt_LOG.h>
#include <net/netfilter/nf_log.h>

int nf_log_dump_udp_header(struct nf_log_buf *m, const struct sk_buff *skb,
			   u8 proto, int fragment, unsigned int offset)
{
	struct udphdr _udph;
	const struct udphdr *uh;

	if (proto == IPPROTO_UDP)
		/* Max length: 10 "PROTO=UDP "     */
		nf_log_buf_add(m, "PROTO=UDP ");
	else	/* Max length: 14 "PROTO=UDPLITE " */
		nf_log_buf_add(m, "PROTO=UDPLITE ");

	if (fragment)
		goto out;

	/* Max length: 25 "INCOMPLETE [65535 bytes] " */
	uh = skb_header_pointer(skb, offset, sizeof(_udph), &_udph);
	if (uh == NULL) {
		nf_log_buf_add(m, "INCOMPLETE [%u bytes] ", skb->len - offset);

		return 1;
	}

	/* Max length: 20 "SPT=65535 DPT=65535 " */
	nf_log_buf_add(m, "SPT=%u DPT=%u LEN=%u ",
		       ntohs(uh->source), ntohs(uh->dest), ntohs(uh->len));

out:
	return 0;
}
EXPORT_SYMBOL_GPL(nf_log_dump_udp_header);

int nf_log_dump_tcp_header(struct nf_log_buf *m, const struct sk_buff *skb,
			   u8 proto, int fragment, unsigned int offset,
			   unsigned int logflags)
{
	struct tcphdr _tcph;
	const struct tcphdr *th;

	/* Max length: 10 "PROTO=TCP " */
	nf_log_buf_add(m, "PROTO=TCP ");

	if (fragment)
		return 0;

	/* Max length: 25 "INCOMPLETE [65535 bytes] " */
	th = skb_header_pointer(skb, offset, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		nf_log_buf_add(m, "INCOMPLETE [%u bytes] ", skb->len - offset);
		return 1;
	}

	/* Max length: 20 "SPT=65535 DPT=65535 " */
	nf_log_buf_add(m, "SPT=%u DPT=%u ",
		       ntohs(th->source), ntohs(th->dest));
	/* Max length: 30 "SEQ=4294967295 ACK=4294967295 " */
	if (logflags & NF_LOG_TCPSEQ) {
		nf_log_buf_add(m, "SEQ=%u ACK=%u ",
			       ntohl(th->seq), ntohl(th->ack_seq));
	}

	/* Max length: 13 "WINDOW=65535 " */
	nf_log_buf_add(m, "WINDOW=%u ", ntohs(th->window));
	/* Max length: 9 "RES=0x3C " */
	nf_log_buf_add(m, "RES=0x%02x ", (u_int8_t)(ntohl(tcp_flag_word(th) &
					    TCP_RESERVED_BITS) >> 22));
	/* Max length: 32 "CWR ECE URG ACK PSH RST SYN FIN " */
	if (th->cwr)
		nf_log_buf_add(m, "CWR ");
	if (th->ece)
		nf_log_buf_add(m, "ECE ");
	if (th->urg)
		nf_log_buf_add(m, "URG ");
	if (th->ack)
		nf_log_buf_add(m, "ACK ");
	if (th->psh)
		nf_log_buf_add(m, "PSH ");
	if (th->rst)
		nf_log_buf_add(m, "RST ");
	if (th->syn)
		nf_log_buf_add(m, "SYN ");
	if (th->fin)
		nf_log_buf_add(m, "FIN ");
	/* Max length: 11 "URGP=65535 " */
	nf_log_buf_add(m, "URGP=%u ", ntohs(th->urg_ptr));

	if ((logflags & NF_LOG_TCPOPT) && th->doff*4 > sizeof(struct tcphdr)) {
		u_int8_t _opt[60 - sizeof(struct tcphdr)];
		const u_int8_t *op;
		unsigned int i;
		unsigned int optsize = th->doff*4 - sizeof(struct tcphdr);

		op = skb_header_pointer(skb, offset + sizeof(struct tcphdr),
					optsize, _opt);
		if (op == NULL) {
			nf_log_buf_add(m, "OPT (TRUNCATED)");
			return 1;
		}

		/* Max length: 127 "OPT (" 15*4*2chars ") " */
		nf_log_buf_add(m, "OPT (");
		for (i = 0; i < optsize; i++)
			nf_log_buf_add(m, "%02X", op[i]);

		nf_log_buf_add(m, ") ");
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nf_log_dump_tcp_header);

void nf_log_dump_sk_uid_gid(struct net *net, struct nf_log_buf *m,
			    struct sock *sk)
{
	if (!sk || !sk_fullsock(sk) || !net_eq(net, sock_net(sk)))
		return;

	read_lock_bh(&sk->sk_callback_lock);
	if (sk->sk_socket && sk->sk_socket->file) {
		const struct cred *cred = sk->sk_socket->file->f_cred;
		nf_log_buf_add(m, "UID=%u GID=%u ",
			from_kuid_munged(&init_user_ns, cred->fsuid),
			from_kgid_munged(&init_user_ns, cred->fsgid));
	}
	read_unlock_bh(&sk->sk_callback_lock);
}
EXPORT_SYMBOL_GPL(nf_log_dump_sk_uid_gid);

void
nf_log_dump_packet_common(struct nf_log_buf *m, u_int8_t pf,
			  unsigned int hooknum, const struct sk_buff *skb,
			  const struct net_device *in,
			  const struct net_device *out,
			  const struct nf_loginfo *loginfo, const char *prefix)
{
	const struct net_device *physoutdev __maybe_unused;
	const struct net_device *physindev __maybe_unused;

	nf_log_buf_add(m, KERN_SOH "%c%sIN=%s OUT=%s ",
	       '0' + loginfo->u.log.level, prefix,
	       in ? in->name : "",
	       out ? out->name : "");
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	physindev = nf_bridge_get_physindev(skb);
	if (physindev && in != physindev)
		nf_log_buf_add(m, "PHYSIN=%s ", physindev->name);
	physoutdev = nf_bridge_get_physoutdev(skb);
	if (physoutdev && out != physoutdev)
		nf_log_buf_add(m, "PHYSOUT=%s ", physoutdev->name);
#endif
}
EXPORT_SYMBOL_GPL(nf_log_dump_packet_common);

/* bridge and netdev logging families share this code. */
void nf_log_l2packet(struct net *net, u_int8_t pf,
		     __be16 protocol,
		     unsigned int hooknum,
		     const struct sk_buff *skb,
		     const struct net_device *in,
		     const struct net_device *out,
		     const struct nf_loginfo *loginfo,
		     const char *prefix)
{
	switch (protocol) {
	case htons(ETH_P_IP):
		nf_log_packet(net, NFPROTO_IPV4, hooknum, skb, in, out,
			      loginfo, "%s", prefix);
		break;
	case htons(ETH_P_IPV6):
		nf_log_packet(net, NFPROTO_IPV6, hooknum, skb, in, out,
			      loginfo, "%s", prefix);
		break;
	case htons(ETH_P_ARP):
	case htons(ETH_P_RARP):
		nf_log_packet(net, NFPROTO_ARP, hooknum, skb, in, out,
			      loginfo, "%s", prefix);
		break;
	}
}
EXPORT_SYMBOL_GPL(nf_log_l2packet);

static int __init nf_log_common_init(void)
{
	return 0;
}

static void __exit nf_log_common_exit(void) {}

module_init(nf_log_common_init);
module_exit(nf_log_common_exit);

MODULE_LICENSE("GPL");
