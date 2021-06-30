// SPDX-License-Identifier: GPL-2.0-only
/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2002-2013 Jozsef Kadlecsik <kadlec@netfilter.org>
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 */

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <net/ip6_checksum.h>
#include <asm/unaligned.h>

#include <net/tcp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_synproxy.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <net/netfilter/nf_log.h>
#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/ipv6/nf_conntrack_ipv6.h>

  /* FIXME: Examine ipfilter's timeouts and conntrack transitions more
     closely.  They're more complex. --RR */

static const char *const tcp_conntrack_names[] = {
	"NONE",
	"SYN_SENT",
	"SYN_RECV",
	"ESTABLISHED",
	"FIN_WAIT",
	"CLOSE_WAIT",
	"LAST_ACK",
	"TIME_WAIT",
	"CLOSE",
	"SYN_SENT2",
};

#define SECS * HZ
#define MINS * 60 SECS
#define HOURS * 60 MINS
#define DAYS * 24 HOURS

static const unsigned int tcp_timeouts[TCP_CONNTRACK_TIMEOUT_MAX] = {
	[TCP_CONNTRACK_SYN_SENT]	= 2 MINS,
	[TCP_CONNTRACK_SYN_RECV]	= 60 SECS,
	[TCP_CONNTRACK_ESTABLISHED]	= 5 DAYS,
	[TCP_CONNTRACK_FIN_WAIT]	= 2 MINS,
	[TCP_CONNTRACK_CLOSE_WAIT]	= 60 SECS,
	[TCP_CONNTRACK_LAST_ACK]	= 30 SECS,
	[TCP_CONNTRACK_TIME_WAIT]	= 2 MINS,
	[TCP_CONNTRACK_CLOSE]		= 10 SECS,
	[TCP_CONNTRACK_SYN_SENT2]	= 2 MINS,
/* RFC1122 says the R2 limit should be at least 100 seconds.
   Linux uses 15 packets as limit, which corresponds
   to ~13-30min depending on RTO. */
	[TCP_CONNTRACK_RETRANS]		= 5 MINS,
	[TCP_CONNTRACK_UNACK]		= 5 MINS,
};

#define sNO TCP_CONNTRACK_NONE
#define sSS TCP_CONNTRACK_SYN_SENT
#define sSR TCP_CONNTRACK_SYN_RECV
#define sES TCP_CONNTRACK_ESTABLISHED
#define sFW TCP_CONNTRACK_FIN_WAIT
#define sCW TCP_CONNTRACK_CLOSE_WAIT
#define sLA TCP_CONNTRACK_LAST_ACK
#define sTW TCP_CONNTRACK_TIME_WAIT
#define sCL TCP_CONNTRACK_CLOSE
#define sS2 TCP_CONNTRACK_SYN_SENT2
#define sIV TCP_CONNTRACK_MAX
#define sIG TCP_CONNTRACK_IGNORE

/* What TCP flags are set from RST/SYN/FIN/ACK. */
enum tcp_bit_set {
	TCP_SYN_SET,
	TCP_SYNACK_SET,
	TCP_FIN_SET,
	TCP_ACK_SET,
	TCP_RST_SET,
	TCP_NONE_SET,
};

/*
 * The TCP state transition table needs a few words...
 *
 * We are the man in the middle. All the packets go through us
 * but might get lost in transit to the destination.
 * It is assumed that the destinations can't receive segments
 * we haven't seen.
 *
 * The checked segment is in window, but our windows are *not*
 * equivalent with the ones of the sender/receiver. We always
 * try to guess the state of the current sender.
 *
 * The meaning of the states are:
 *
 * NONE:	initial state
 * SYN_SENT:	SYN-only packet seen
 * SYN_SENT2:	SYN-only packet seen from reply dir, simultaneous open
 * SYN_RECV:	SYN-ACK packet seen
 * ESTABLISHED:	ACK packet seen
 * FIN_WAIT:	FIN packet seen
 * CLOSE_WAIT:	ACK seen (after FIN)
 * LAST_ACK:	FIN seen (after FIN)
 * TIME_WAIT:	last ACK seen
 * CLOSE:	closed connection (RST)
 *
 * Packets marked as IGNORED (sIG):
 *	if they may be either invalid or valid
 *	and the receiver may send back a connection
 *	closing RST or a SYN/ACK.
 *
 * Packets marked as INVALID (sIV):
 *	if we regard them as truly invalid packets
 */
static const u8 tcp_conntracks[2][6][TCP_CONNTRACK_MAX] = {
	{
/* ORIGINAL */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*syn*/	   { sSS, sSS, sIG, sIG, sIG, sIG, sIG, sSS, sSS, sS2 },
/*
 *	sNO -> sSS	Initialize a new connection
 *	sSS -> sSS	Retransmitted SYN
 *	sS2 -> sS2	Late retransmitted SYN
 *	sSR -> sIG
 *	sES -> sIG	Error: SYNs in window outside the SYN_SENT state
 *			are errors. Receiver will reply with RST
 *			and close the connection.
 *			Or we are not in sync and hold a dead connection.
 *	sFW -> sIG
 *	sCW -> sIG
 *	sLA -> sIG
 *	sTW -> sSS	Reopened connection (RFC 1122).
 *	sCL -> sSS
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*synack*/ { sIV, sIV, sSR, sIV, sIV, sIV, sIV, sIV, sIV, sSR },
/*
 *	sNO -> sIV	Too late and no reason to do anything
 *	sSS -> sIV	Client can't send SYN and then SYN/ACK
 *	sS2 -> sSR	SYN/ACK sent to SYN2 in simultaneous open
 *	sSR -> sSR	Late retransmitted SYN/ACK in simultaneous open
 *	sES -> sIV	Invalid SYN/ACK packets sent by the client
 *	sFW -> sIV
 *	sCW -> sIV
 *	sLA -> sIV
 *	sTW -> sIV
 *	sCL -> sIV
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*fin*/    { sIV, sIV, sFW, sFW, sLA, sLA, sLA, sTW, sCL, sIV },
/*
 *	sNO -> sIV	Too late and no reason to do anything...
 *	sSS -> sIV	Client migth not send FIN in this state:
 *			we enforce waiting for a SYN/ACK reply first.
 *	sS2 -> sIV
 *	sSR -> sFW	Close started.
 *	sES -> sFW
 *	sFW -> sLA	FIN seen in both directions, waiting for
 *			the last ACK.
 *			Migth be a retransmitted FIN as well...
 *	sCW -> sLA
 *	sLA -> sLA	Retransmitted FIN. Remain in the same state.
 *	sTW -> sTW
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*ack*/	   { sES, sIV, sES, sES, sCW, sCW, sTW, sTW, sCL, sIV },
/*
 *	sNO -> sES	Assumed.
 *	sSS -> sIV	ACK is invalid: we haven't seen a SYN/ACK yet.
 *	sS2 -> sIV
 *	sSR -> sES	Established state is reached.
 *	sES -> sES	:-)
 *	sFW -> sCW	Normal close request answered by ACK.
 *	sCW -> sCW
 *	sLA -> sTW	Last ACK detected (RFC5961 challenged)
 *	sTW -> sTW	Retransmitted last ACK. Remain in the same state.
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*rst*/    { sIV, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL },
/*none*/   { sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV }
	},
	{
/* REPLY */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*syn*/	   { sIV, sS2, sIV, sIV, sIV, sIV, sIV, sSS, sIV, sS2 },
/*
 *	sNO -> sIV	Never reached.
 *	sSS -> sS2	Simultaneous open
 *	sS2 -> sS2	Retransmitted simultaneous SYN
 *	sSR -> sIV	Invalid SYN packets sent by the server
 *	sES -> sIV
 *	sFW -> sIV
 *	sCW -> sIV
 *	sLA -> sIV
 *	sTW -> sSS	Reopened connection, but server may have switched role
 *	sCL -> sIV
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*synack*/ { sIV, sSR, sIG, sIG, sIG, sIG, sIG, sIG, sIG, sSR },
/*
 *	sSS -> sSR	Standard open.
 *	sS2 -> sSR	Simultaneous open
 *	sSR -> sIG	Retransmitted SYN/ACK, ignore it.
 *	sES -> sIG	Late retransmitted SYN/ACK?
 *	sFW -> sIG	Might be SYN/ACK answering ignored SYN
 *	sCW -> sIG
 *	sLA -> sIG
 *	sTW -> sIG
 *	sCL -> sIG
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*fin*/    { sIV, sIV, sFW, sFW, sLA, sLA, sLA, sTW, sCL, sIV },
/*
 *	sSS -> sIV	Server might not send FIN in this state.
 *	sS2 -> sIV
 *	sSR -> sFW	Close started.
 *	sES -> sFW
 *	sFW -> sLA	FIN seen in both directions.
 *	sCW -> sLA
 *	sLA -> sLA	Retransmitted FIN.
 *	sTW -> sTW
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*ack*/	   { sIV, sIG, sSR, sES, sCW, sCW, sTW, sTW, sCL, sIG },
/*
 *	sSS -> sIG	Might be a half-open connection.
 *	sS2 -> sIG
 *	sSR -> sSR	Might answer late resent SYN.
 *	sES -> sES	:-)
 *	sFW -> sCW	Normal close request answered by ACK.
 *	sCW -> sCW
 *	sLA -> sTW	Last ACK detected (RFC5961 challenged)
 *	sTW -> sTW	Retransmitted last ACK.
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sS2	*/
/*rst*/    { sIV, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL },
/*none*/   { sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV }
	}
};

#ifdef CONFIG_NF_CONNTRACK_PROCFS
/* Print out the private part of the conntrack. */
static void tcp_print_conntrack(struct seq_file *s, struct nf_conn *ct)
{
	if (test_bit(IPS_OFFLOAD_BIT, &ct->status))
		return;

	seq_printf(s, "%s ", tcp_conntrack_names[ct->proto.tcp.state]);
}
#endif

static unsigned int get_conntrack_index(const struct tcphdr *tcph)
{
	if (tcph->rst) return TCP_RST_SET;
	else if (tcph->syn) return (tcph->ack ? TCP_SYNACK_SET : TCP_SYN_SET);
	else if (tcph->fin) return TCP_FIN_SET;
	else if (tcph->ack) return TCP_ACK_SET;
	else return TCP_NONE_SET;
}

/* TCP connection tracking based on 'Real Stateful TCP Packet Filtering
   in IP Filter' by Guido van Rooij.

   http://www.sane.nl/events/sane2000/papers.html
   http://www.darkart.com/mirrors/www.obfuscation.org/ipf/

   The boundaries and the conditions are changed according to RFC793:
   the packet must intersect the window (i.e. segments may be
   after the right or before the left edge) and thus receivers may ACK
   segments after the right edge of the window.

	td_maxend = max(sack + max(win,1)) seen in reply packets
	td_maxwin = max(max(win, 1)) + (sack - ack) seen in sent packets
	td_maxwin += seq + len - sender.td_maxend
			if seq + len > sender.td_maxend
	td_end    = max(seq + len) seen in sent packets

   I.   Upper bound for valid data:	seq <= sender.td_maxend
   II.  Lower bound for valid data:	seq + len >= sender.td_end - receiver.td_maxwin
   III.	Upper bound for valid (s)ack:   sack <= receiver.td_end
   IV.	Lower bound for valid (s)ack:	sack >= receiver.td_end - MAXACKWINDOW

   where sack is the highest right edge of sack block found in the packet
   or ack in the case of packet without SACK option.

   The upper bound limit for a valid (s)ack is not ignored -
   we doesn't have to deal with fragments.
*/

static inline __u32 segment_seq_plus_len(__u32 seq,
					 size_t len,
					 unsigned int dataoff,
					 const struct tcphdr *tcph)
{
	/* XXX Should I use payload length field in IP/IPv6 header ?
	 * - YK */
	return (seq + len - dataoff - tcph->doff*4
		+ (tcph->syn ? 1 : 0) + (tcph->fin ? 1 : 0));
}

/* Fixme: what about big packets? */
#define MAXACKWINCONST			66000
#define MAXACKWINDOW(sender)						\
	((sender)->td_maxwin > MAXACKWINCONST ? (sender)->td_maxwin	\
					      : MAXACKWINCONST)

/*
 * Simplified tcp_parse_options routine from tcp_input.c
 */
static void tcp_options(const struct sk_buff *skb,
			unsigned int dataoff,
			const struct tcphdr *tcph,
			struct ip_ct_tcp_state *state)
{
	unsigned char buff[(15 * 4) - sizeof(struct tcphdr)];
	const unsigned char *ptr;
	int length = (tcph->doff*4) - sizeof(struct tcphdr);

	if (!length)
		return;

	ptr = skb_header_pointer(skb, dataoff + sizeof(struct tcphdr),
				 length, buff);
	if (!ptr)
		return;

	state->td_scale =
	state->flags = 0;

	while (length > 0) {
		int opcode=*ptr++;
		int opsize;

		switch (opcode) {
		case TCPOPT_EOL:
			return;
		case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
			length--;
			continue;
		default:
			if (length < 2)
				return;
			opsize=*ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			if (opsize > length)
				return;	/* don't parse partial options */

			if (opcode == TCPOPT_SACK_PERM
			    && opsize == TCPOLEN_SACK_PERM)
				state->flags |= IP_CT_TCP_FLAG_SACK_PERM;
			else if (opcode == TCPOPT_WINDOW
				 && opsize == TCPOLEN_WINDOW) {
				state->td_scale = *(u_int8_t *)ptr;

				if (state->td_scale > TCP_MAX_WSCALE)
					state->td_scale = TCP_MAX_WSCALE;

				state->flags |=
					IP_CT_TCP_FLAG_WINDOW_SCALE;
			}
			ptr += opsize - 2;
			length -= opsize;
		}
	}
}

static void tcp_sack(const struct sk_buff *skb, unsigned int dataoff,
                     const struct tcphdr *tcph, __u32 *sack)
{
	unsigned char buff[(15 * 4) - sizeof(struct tcphdr)];
	const unsigned char *ptr;
	int length = (tcph->doff*4) - sizeof(struct tcphdr);
	__u32 tmp;

	if (!length)
		return;

	ptr = skb_header_pointer(skb, dataoff + sizeof(struct tcphdr),
				 length, buff);
	if (!ptr)
		return;

	/* Fast path for timestamp-only option */
	if (length == TCPOLEN_TSTAMP_ALIGNED
	    && *(__be32 *)ptr == htonl((TCPOPT_NOP << 24)
				       | (TCPOPT_NOP << 16)
				       | (TCPOPT_TIMESTAMP << 8)
				       | TCPOLEN_TIMESTAMP))
		return;

	while (length > 0) {
		int opcode = *ptr++;
		int opsize, i;

		switch (opcode) {
		case TCPOPT_EOL:
			return;
		case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
			length--;
			continue;
		default:
			if (length < 2)
				return;
			opsize = *ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			if (opsize > length)
				return;	/* don't parse partial options */

			if (opcode == TCPOPT_SACK
			    && opsize >= (TCPOLEN_SACK_BASE
					  + TCPOLEN_SACK_PERBLOCK)
			    && !((opsize - TCPOLEN_SACK_BASE)
				 % TCPOLEN_SACK_PERBLOCK)) {
				for (i = 0;
				     i < (opsize - TCPOLEN_SACK_BASE);
				     i += TCPOLEN_SACK_PERBLOCK) {
					tmp = get_unaligned_be32((__be32 *)(ptr+i)+1);

					if (after(tmp, *sack))
						*sack = tmp;
				}
				return;
			}
			ptr += opsize - 2;
			length -= opsize;
		}
	}
}

static bool tcp_in_window(const struct nf_conn *ct,
			  struct ip_ct_tcp *state,
			  enum ip_conntrack_dir dir,
			  unsigned int index,
			  const struct sk_buff *skb,
			  unsigned int dataoff,
			  const struct tcphdr *tcph)
{
	struct net *net = nf_ct_net(ct);
	struct nf_tcp_net *tn = nf_tcp_pernet(net);
	struct ip_ct_tcp_state *sender = &state->seen[dir];
	struct ip_ct_tcp_state *receiver = &state->seen[!dir];
	const struct nf_conntrack_tuple *tuple = &ct->tuplehash[dir].tuple;
	__u32 seq, ack, sack, end, win, swin;
	u16 win_raw;
	s32 receiver_offset;
	bool res, in_recv_win;

	/*
	 * Get the required data from the packet.
	 */
	seq = ntohl(tcph->seq);
	ack = sack = ntohl(tcph->ack_seq);
	win_raw = ntohs(tcph->window);
	win = win_raw;
	end = segment_seq_plus_len(seq, skb->len, dataoff, tcph);

	if (receiver->flags & IP_CT_TCP_FLAG_SACK_PERM)
		tcp_sack(skb, dataoff, tcph, &sack);

	/* Take into account NAT sequence number mangling */
	receiver_offset = nf_ct_seq_offset(ct, !dir, ack - 1);
	ack -= receiver_offset;
	sack -= receiver_offset;

	pr_debug("tcp_in_window: START\n");
	pr_debug("tcp_in_window: ");
	nf_ct_dump_tuple(tuple);
	pr_debug("seq=%u ack=%u+(%d) sack=%u+(%d) win=%u end=%u\n",
		 seq, ack, receiver_offset, sack, receiver_offset, win, end);
	pr_debug("tcp_in_window: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);

	if (sender->td_maxwin == 0) {
		/*
		 * Initialize sender data.
		 */
		if (tcph->syn) {
			/*
			 * SYN-ACK in reply to a SYN
			 * or SYN from reply direction in simultaneous open.
			 */
			sender->td_end =
			sender->td_maxend = end;
			sender->td_maxwin = (win == 0 ? 1 : win);

			tcp_options(skb, dataoff, tcph, sender);
			/*
			 * RFC 1323:
			 * Both sides must send the Window Scale option
			 * to enable window scaling in either direction.
			 */
			if (!(sender->flags & IP_CT_TCP_FLAG_WINDOW_SCALE
			      && receiver->flags & IP_CT_TCP_FLAG_WINDOW_SCALE))
				sender->td_scale =
				receiver->td_scale = 0;
			if (!tcph->ack)
				/* Simultaneous open */
				return true;
		} else {
			/*
			 * We are in the middle of a connection,
			 * its history is lost for us.
			 * Let's try to use the data from the packet.
			 */
			sender->td_end = end;
			swin = win << sender->td_scale;
			sender->td_maxwin = (swin == 0 ? 1 : swin);
			sender->td_maxend = end + sender->td_maxwin;
			if (receiver->td_maxwin == 0) {
				/* We haven't seen traffic in the other
				 * direction yet but we have to tweak window
				 * tracking to pass III and IV until that
				 * happens.
				 */
				receiver->td_end = receiver->td_maxend = sack;
			} else if (sack == receiver->td_end + 1) {
				/* Likely a reply to a keepalive.
				 * Needed for III.
				 */
				receiver->td_end++;
			}

		}
	} else if (((state->state == TCP_CONNTRACK_SYN_SENT
		     && dir == IP_CT_DIR_ORIGINAL)
		   || (state->state == TCP_CONNTRACK_SYN_RECV
		     && dir == IP_CT_DIR_REPLY))
		   && after(end, sender->td_end)) {
		/*
		 * RFC 793: "if a TCP is reinitialized ... then it need
		 * not wait at all; it must only be sure to use sequence
		 * numbers larger than those recently used."
		 */
		sender->td_end =
		sender->td_maxend = end;
		sender->td_maxwin = (win == 0 ? 1 : win);

		tcp_options(skb, dataoff, tcph, sender);
	}

	if (!(tcph->ack)) {
		/*
		 * If there is no ACK, just pretend it was set and OK.
		 */
		ack = sack = receiver->td_end;
	} else if (((tcp_flag_word(tcph) & (TCP_FLAG_ACK|TCP_FLAG_RST)) ==
		    (TCP_FLAG_ACK|TCP_FLAG_RST))
		   && (ack == 0)) {
		/*
		 * Broken TCP stacks, that set ACK in RST packets as well
		 * with zero ack value.
		 */
		ack = sack = receiver->td_end;
	}

	if (tcph->rst && seq == 0 && state->state == TCP_CONNTRACK_SYN_SENT)
		/*
		 * RST sent answering SYN.
		 */
		seq = end = sender->td_end;

	pr_debug("tcp_in_window: ");
	nf_ct_dump_tuple(tuple);
	pr_debug("seq=%u ack=%u+(%d) sack=%u+(%d) win=%u end=%u\n",
		 seq, ack, receiver_offset, sack, receiver_offset, win, end);
	pr_debug("tcp_in_window: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);

	/* Is the ending sequence in the receive window (if available)? */
	in_recv_win = !receiver->td_maxwin ||
		      after(end, sender->td_end - receiver->td_maxwin - 1);

	pr_debug("tcp_in_window: I=%i II=%i III=%i IV=%i\n",
		 before(seq, sender->td_maxend + 1),
		 (in_recv_win ? 1 : 0),
		 before(sack, receiver->td_end + 1),
		 after(sack, receiver->td_end - MAXACKWINDOW(sender) - 1));

	if (before(seq, sender->td_maxend + 1) &&
	    in_recv_win &&
	    before(sack, receiver->td_end + 1) &&
	    after(sack, receiver->td_end - MAXACKWINDOW(sender) - 1)) {
		/*
		 * Take into account window scaling (RFC 1323).
		 */
		if (!tcph->syn)
			win <<= sender->td_scale;

		/*
		 * Update sender data.
		 */
		swin = win + (sack - ack);
		if (sender->td_maxwin < swin)
			sender->td_maxwin = swin;
		if (after(end, sender->td_end)) {
			sender->td_end = end;
			sender->flags |= IP_CT_TCP_FLAG_DATA_UNACKNOWLEDGED;
		}
		if (tcph->ack) {
			if (!(sender->flags & IP_CT_TCP_FLAG_MAXACK_SET)) {
				sender->td_maxack = ack;
				sender->flags |= IP_CT_TCP_FLAG_MAXACK_SET;
			} else if (after(ack, sender->td_maxack))
				sender->td_maxack = ack;
		}

		/*
		 * Update receiver data.
		 */
		if (receiver->td_maxwin != 0 && after(end, sender->td_maxend))
			receiver->td_maxwin += end - sender->td_maxend;
		if (after(sack + win, receiver->td_maxend - 1)) {
			receiver->td_maxend = sack + win;
			if (win == 0)
				receiver->td_maxend++;
		}
		if (ack == receiver->td_end)
			receiver->flags &= ~IP_CT_TCP_FLAG_DATA_UNACKNOWLEDGED;

		/*
		 * Check retransmissions.
		 */
		if (index == TCP_ACK_SET) {
			if (state->last_dir == dir
			    && state->last_seq == seq
			    && state->last_ack == ack
			    && state->last_end == end
			    && state->last_win == win_raw)
				state->retrans++;
			else {
				state->last_dir = dir;
				state->last_seq = seq;
				state->last_ack = ack;
				state->last_end = end;
				state->last_win = win_raw;
				state->retrans = 0;
			}
		}
		res = true;
	} else {
		res = false;
		if (sender->flags & IP_CT_TCP_FLAG_BE_LIBERAL ||
		    tn->tcp_be_liberal)
			res = true;
		if (!res) {
			nf_ct_l4proto_log_invalid(skb, ct,
			"%s",
			before(seq, sender->td_maxend + 1) ?
			in_recv_win ?
			before(sack, receiver->td_end + 1) ?
			after(sack, receiver->td_end - MAXACKWINDOW(sender) - 1) ? "BUG"
			: "ACK is under the lower bound (possible overly delayed ACK)"
			: "ACK is over the upper bound (ACKed data not seen yet)"
			: "SEQ is under the lower bound (already ACKed data retransmitted)"
			: "SEQ is over the upper bound (over the window of the receiver)");
		}
	}

	pr_debug("tcp_in_window: res=%u sender end=%u maxend=%u maxwin=%u "
		 "receiver end=%u maxend=%u maxwin=%u\n",
		 res, sender->td_end, sender->td_maxend, sender->td_maxwin,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin);

	return res;
}

/* table of valid flag combinations - PUSH, ECE and CWR are always valid */
static const u8 tcp_valid_flags[(TCPHDR_FIN|TCPHDR_SYN|TCPHDR_RST|TCPHDR_ACK|
				 TCPHDR_URG) + 1] =
{
	[TCPHDR_SYN]				= 1,
	[TCPHDR_SYN|TCPHDR_URG]			= 1,
	[TCPHDR_SYN|TCPHDR_ACK]			= 1,
	[TCPHDR_RST]				= 1,
	[TCPHDR_RST|TCPHDR_ACK]			= 1,
	[TCPHDR_FIN|TCPHDR_ACK]			= 1,
	[TCPHDR_FIN|TCPHDR_ACK|TCPHDR_URG]	= 1,
	[TCPHDR_ACK]				= 1,
	[TCPHDR_ACK|TCPHDR_URG]			= 1,
};

static void tcp_error_log(const struct sk_buff *skb,
			  const struct nf_hook_state *state,
			  const char *msg)
{
	nf_l4proto_log_invalid(skb, state->net, state->pf, IPPROTO_TCP, "%s", msg);
}

/* Protect conntrack agaist broken packets. Code taken from ipt_unclean.c.  */
static bool tcp_error(const struct tcphdr *th,
		      struct sk_buff *skb,
		      unsigned int dataoff,
		      const struct nf_hook_state *state)
{
	unsigned int tcplen = skb->len - dataoff;
	u8 tcpflags;

	/* Not whole TCP header or malformed packet */
	if (th->doff*4 < sizeof(struct tcphdr) || tcplen < th->doff*4) {
		tcp_error_log(skb, state, "truncated packet");
		return true;
	}

	/* Checksum invalid? Ignore.
	 * We skip checking packets on the outgoing path
	 * because the checksum is assumed to be correct.
	 */
	/* FIXME: Source route IP option packets --RR */
	if (state->net->ct.sysctl_checksum &&
	    state->hook == NF_INET_PRE_ROUTING &&
	    nf_checksum(skb, state->hook, dataoff, IPPROTO_TCP, state->pf)) {
		tcp_error_log(skb, state, "bad checksum");
		return true;
	}

	/* Check TCP flags. */
	tcpflags = (tcp_flag_byte(th) & ~(TCPHDR_ECE|TCPHDR_CWR|TCPHDR_PSH));
	if (!tcp_valid_flags[tcpflags]) {
		tcp_error_log(skb, state, "invalid tcp flag combination");
		return true;
	}

	return false;
}

static noinline bool tcp_new(struct nf_conn *ct, const struct sk_buff *skb,
			     unsigned int dataoff,
			     const struct tcphdr *th)
{
	enum tcp_conntrack new_state;
	struct net *net = nf_ct_net(ct);
	const struct nf_tcp_net *tn = nf_tcp_pernet(net);
	const struct ip_ct_tcp_state *sender = &ct->proto.tcp.seen[0];
	const struct ip_ct_tcp_state *receiver = &ct->proto.tcp.seen[1];

	/* Don't need lock here: this conntrack not in circulation yet */
	new_state = tcp_conntracks[0][get_conntrack_index(th)][TCP_CONNTRACK_NONE];

	/* Invalid: delete conntrack */
	if (new_state >= TCP_CONNTRACK_MAX) {
		pr_debug("nf_ct_tcp: invalid new deleting.\n");
		return false;
	}

	if (new_state == TCP_CONNTRACK_SYN_SENT) {
		memset(&ct->proto.tcp, 0, sizeof(ct->proto.tcp));
		/* SYN packet */
		ct->proto.tcp.seen[0].td_end =
			segment_seq_plus_len(ntohl(th->seq), skb->len,
					     dataoff, th);
		ct->proto.tcp.seen[0].td_maxwin = ntohs(th->window);
		if (ct->proto.tcp.seen[0].td_maxwin == 0)
			ct->proto.tcp.seen[0].td_maxwin = 1;
		ct->proto.tcp.seen[0].td_maxend =
			ct->proto.tcp.seen[0].td_end;

		tcp_options(skb, dataoff, th, &ct->proto.tcp.seen[0]);
	} else if (tn->tcp_loose == 0) {
		/* Don't try to pick up connections. */
		return false;
	} else {
		memset(&ct->proto.tcp, 0, sizeof(ct->proto.tcp));
		/*
		 * We are in the middle of a connection,
		 * its history is lost for us.
		 * Let's try to use the data from the packet.
		 */
		ct->proto.tcp.seen[0].td_end =
			segment_seq_plus_len(ntohl(th->seq), skb->len,
					     dataoff, th);
		ct->proto.tcp.seen[0].td_maxwin = ntohs(th->window);
		if (ct->proto.tcp.seen[0].td_maxwin == 0)
			ct->proto.tcp.seen[0].td_maxwin = 1;
		ct->proto.tcp.seen[0].td_maxend =
			ct->proto.tcp.seen[0].td_end +
			ct->proto.tcp.seen[0].td_maxwin;

		/* We assume SACK and liberal window checking to handle
		 * window scaling */
		ct->proto.tcp.seen[0].flags =
		ct->proto.tcp.seen[1].flags = IP_CT_TCP_FLAG_SACK_PERM |
					      IP_CT_TCP_FLAG_BE_LIBERAL;
	}

	/* tcp_packet will set them */
	ct->proto.tcp.last_index = TCP_NONE_SET;

	pr_debug("%s: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 __func__,
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);
	return true;
}

/* Returns verdict for packet, or -1 for invalid. */
int nf_conntrack_tcp_packet(struct nf_conn *ct,
			    struct sk_buff *skb,
			    unsigned int dataoff,
			    enum ip_conntrack_info ctinfo,
			    const struct nf_hook_state *state)
{
	struct net *net = nf_ct_net(ct);
	struct nf_tcp_net *tn = nf_tcp_pernet(net);
	struct nf_conntrack_tuple *tuple;
	enum tcp_conntrack new_state, old_state;
	unsigned int index, *timeouts;
	enum ip_conntrack_dir dir;
	const struct tcphdr *th;
	struct tcphdr _tcph;
	unsigned long timeout;

	th = skb_header_pointer(skb, dataoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return -NF_ACCEPT;

	if (tcp_error(th, skb, dataoff, state))
		return -NF_ACCEPT;

	if (!nf_ct_is_confirmed(ct) && !tcp_new(ct, skb, dataoff, th))
		return -NF_ACCEPT;

	spin_lock_bh(&ct->lock);
	old_state = ct->proto.tcp.state;
	dir = CTINFO2DIR(ctinfo);
	index = get_conntrack_index(th);
	new_state = tcp_conntracks[dir][index][old_state];
	tuple = &ct->tuplehash[dir].tuple;

	switch (new_state) {
	case TCP_CONNTRACK_SYN_SENT:
		if (old_state < TCP_CONNTRACK_TIME_WAIT)
			break;
		/* RFC 1122: "When a connection is closed actively,
		 * it MUST linger in TIME-WAIT state for a time 2xMSL
		 * (Maximum Segment Lifetime). However, it MAY accept
		 * a new SYN from the remote TCP to reopen the connection
		 * directly from TIME-WAIT state, if..."
		 * We ignore the conditions because we are in the
		 * TIME-WAIT state anyway.
		 *
		 * Handle aborted connections: we and the server
		 * think there is an existing connection but the client
		 * aborts it and starts a new one.
		 */
		if (((ct->proto.tcp.seen[dir].flags
		      | ct->proto.tcp.seen[!dir].flags)
		     & IP_CT_TCP_FLAG_CLOSE_INIT)
		    || (ct->proto.tcp.last_dir == dir
		        && ct->proto.tcp.last_index == TCP_RST_SET)) {
			/* Attempt to reopen a closed/aborted connection.
			 * Delete this connection and look up again. */
			spin_unlock_bh(&ct->lock);

			/* Only repeat if we can actually remove the timer.
			 * Destruction may already be in progress in process
			 * context and we must give it a chance to terminate.
			 */
			if (nf_ct_kill(ct))
				return -NF_REPEAT;
			return NF_DROP;
		}
		fallthrough;
	case TCP_CONNTRACK_IGNORE:
		/* Ignored packets:
		 *
		 * Our connection entry may be out of sync, so ignore
		 * packets which may signal the real connection between
		 * the client and the server.
		 *
		 * a) SYN in ORIGINAL
		 * b) SYN/ACK in REPLY
		 * c) ACK in reply direction after initial SYN in original.
		 *
		 * If the ignored packet is invalid, the receiver will send
		 * a RST we'll catch below.
		 */
		if (index == TCP_SYNACK_SET
		    && ct->proto.tcp.last_index == TCP_SYN_SET
		    && ct->proto.tcp.last_dir != dir
		    && ntohl(th->ack_seq) == ct->proto.tcp.last_end) {
			/* b) This SYN/ACK acknowledges a SYN that we earlier
			 * ignored as invalid. This means that the client and
			 * the server are both in sync, while the firewall is
			 * not. We get in sync from the previously annotated
			 * values.
			 */
			old_state = TCP_CONNTRACK_SYN_SENT;
			new_state = TCP_CONNTRACK_SYN_RECV;
			ct->proto.tcp.seen[ct->proto.tcp.last_dir].td_end =
				ct->proto.tcp.last_end;
			ct->proto.tcp.seen[ct->proto.tcp.last_dir].td_maxend =
				ct->proto.tcp.last_end;
			ct->proto.tcp.seen[ct->proto.tcp.last_dir].td_maxwin =
				ct->proto.tcp.last_win == 0 ?
					1 : ct->proto.tcp.last_win;
			ct->proto.tcp.seen[ct->proto.tcp.last_dir].td_scale =
				ct->proto.tcp.last_wscale;
			ct->proto.tcp.last_flags &= ~IP_CT_EXP_CHALLENGE_ACK;
			ct->proto.tcp.seen[ct->proto.tcp.last_dir].flags =
				ct->proto.tcp.last_flags;
			memset(&ct->proto.tcp.seen[dir], 0,
			       sizeof(struct ip_ct_tcp_state));
			break;
		}
		ct->proto.tcp.last_index = index;
		ct->proto.tcp.last_dir = dir;
		ct->proto.tcp.last_seq = ntohl(th->seq);
		ct->proto.tcp.last_end =
		    segment_seq_plus_len(ntohl(th->seq), skb->len, dataoff, th);
		ct->proto.tcp.last_win = ntohs(th->window);

		/* a) This is a SYN in ORIGINAL. The client and the server
		 * may be in sync but we are not. In that case, we annotate
		 * the TCP options and let the packet go through. If it is a
		 * valid SYN packet, the server will reply with a SYN/ACK, and
		 * then we'll get in sync. Otherwise, the server potentially
		 * responds with a challenge ACK if implementing RFC5961.
		 */
		if (index == TCP_SYN_SET && dir == IP_CT_DIR_ORIGINAL) {
			struct ip_ct_tcp_state seen = {};

			ct->proto.tcp.last_flags =
			ct->proto.tcp.last_wscale = 0;
			tcp_options(skb, dataoff, th, &seen);
			if (seen.flags & IP_CT_TCP_FLAG_WINDOW_SCALE) {
				ct->proto.tcp.last_flags |=
					IP_CT_TCP_FLAG_WINDOW_SCALE;
				ct->proto.tcp.last_wscale = seen.td_scale;
			}
			if (seen.flags & IP_CT_TCP_FLAG_SACK_PERM) {
				ct->proto.tcp.last_flags |=
					IP_CT_TCP_FLAG_SACK_PERM;
			}
			/* Mark the potential for RFC5961 challenge ACK,
			 * this pose a special problem for LAST_ACK state
			 * as ACK is intrepretated as ACKing last FIN.
			 */
			if (old_state == TCP_CONNTRACK_LAST_ACK)
				ct->proto.tcp.last_flags |=
					IP_CT_EXP_CHALLENGE_ACK;
		}
		spin_unlock_bh(&ct->lock);
		nf_ct_l4proto_log_invalid(skb, ct,
					  "packet (index %d) in dir %d ignored, state %s",
					  index, dir,
					  tcp_conntrack_names[old_state]);
		return NF_ACCEPT;
	case TCP_CONNTRACK_MAX:
		/* Special case for SYN proxy: when the SYN to the server or
		 * the SYN/ACK from the server is lost, the client may transmit
		 * a keep-alive packet while in SYN_SENT state. This needs to
		 * be associated with the original conntrack entry in order to
		 * generate a new SYN with the correct sequence number.
		 */
		if (nfct_synproxy(ct) && old_state == TCP_CONNTRACK_SYN_SENT &&
		    index == TCP_ACK_SET && dir == IP_CT_DIR_ORIGINAL &&
		    ct->proto.tcp.last_dir == IP_CT_DIR_ORIGINAL &&
		    ct->proto.tcp.seen[dir].td_end - 1 == ntohl(th->seq)) {
			pr_debug("nf_ct_tcp: SYN proxy client keep alive\n");
			spin_unlock_bh(&ct->lock);
			return NF_ACCEPT;
		}

		/* Invalid packet */
		pr_debug("nf_ct_tcp: Invalid dir=%i index=%u ostate=%u\n",
			 dir, get_conntrack_index(th), old_state);
		spin_unlock_bh(&ct->lock);
		nf_ct_l4proto_log_invalid(skb, ct, "invalid state");
		return -NF_ACCEPT;
	case TCP_CONNTRACK_TIME_WAIT:
		/* RFC5961 compliance cause stack to send "challenge-ACK"
		 * e.g. in response to spurious SYNs.  Conntrack MUST
		 * not believe this ACK is acking last FIN.
		 */
		if (old_state == TCP_CONNTRACK_LAST_ACK &&
		    index == TCP_ACK_SET &&
		    ct->proto.tcp.last_dir != dir &&
		    ct->proto.tcp.last_index == TCP_SYN_SET &&
		    (ct->proto.tcp.last_flags & IP_CT_EXP_CHALLENGE_ACK)) {
			/* Detected RFC5961 challenge ACK */
			ct->proto.tcp.last_flags &= ~IP_CT_EXP_CHALLENGE_ACK;
			spin_unlock_bh(&ct->lock);
			nf_ct_l4proto_log_invalid(skb, ct, "challenge-ack ignored");
			return NF_ACCEPT; /* Don't change state */
		}
		break;
	case TCP_CONNTRACK_SYN_SENT2:
		/* tcp_conntracks table is not smart enough to handle
		 * simultaneous open.
		 */
		ct->proto.tcp.last_flags |= IP_CT_TCP_SIMULTANEOUS_OPEN;
		break;
	case TCP_CONNTRACK_SYN_RECV:
		if (dir == IP_CT_DIR_REPLY && index == TCP_ACK_SET &&
		    ct->proto.tcp.last_flags & IP_CT_TCP_SIMULTANEOUS_OPEN)
			new_state = TCP_CONNTRACK_ESTABLISHED;
		break;
	case TCP_CONNTRACK_CLOSE:
		if (index != TCP_RST_SET)
			break;

		if (ct->proto.tcp.seen[!dir].flags & IP_CT_TCP_FLAG_MAXACK_SET) {
			u32 seq = ntohl(th->seq);

			if (before(seq, ct->proto.tcp.seen[!dir].td_maxack)) {
				/* Invalid RST  */
				spin_unlock_bh(&ct->lock);
				nf_ct_l4proto_log_invalid(skb, ct, "invalid rst");
				return -NF_ACCEPT;
			}

			if (!nf_conntrack_tcp_established(ct) ||
			    seq == ct->proto.tcp.seen[!dir].td_maxack)
				break;

			/* Check if rst is part of train, such as
			 *   foo:80 > bar:4379: P, 235946583:235946602(19) ack 42
			 *   foo:80 > bar:4379: R, 235946602:235946602(0)  ack 42
			 */
			if (ct->proto.tcp.last_index == TCP_ACK_SET &&
			    ct->proto.tcp.last_dir == dir &&
			    seq == ct->proto.tcp.last_end)
				break;

			/* ... RST sequence number doesn't match exactly, keep
			 * established state to allow a possible challenge ACK.
			 */
			new_state = old_state;
		}
		if (((test_bit(IPS_SEEN_REPLY_BIT, &ct->status)
			 && ct->proto.tcp.last_index == TCP_SYN_SET)
			|| (!test_bit(IPS_ASSURED_BIT, &ct->status)
			    && ct->proto.tcp.last_index == TCP_ACK_SET))
		    && ntohl(th->ack_seq) == ct->proto.tcp.last_end) {
			/* RST sent to invalid SYN or ACK we had let through
			 * at a) and c) above:
			 *
			 * a) SYN was in window then
			 * c) we hold a half-open connection.
			 *
			 * Delete our connection entry.
			 * We skip window checking, because packet might ACK
			 * segments we ignored. */
			goto in_window;
		}
		break;
	default:
		/* Keep compilers happy. */
		break;
	}

	if (!tcp_in_window(ct, &ct->proto.tcp, dir, index,
			   skb, dataoff, th)) {
		spin_unlock_bh(&ct->lock);
		return -NF_ACCEPT;
	}
     in_window:
	/* From now on we have got in-window packets */
	ct->proto.tcp.last_index = index;
	ct->proto.tcp.last_dir = dir;

	pr_debug("tcp_conntracks: ");
	nf_ct_dump_tuple(tuple);
	pr_debug("syn=%i ack=%i fin=%i rst=%i old=%i new=%i\n",
		 (th->syn ? 1 : 0), (th->ack ? 1 : 0),
		 (th->fin ? 1 : 0), (th->rst ? 1 : 0),
		 old_state, new_state);

	ct->proto.tcp.state = new_state;
	if (old_state != new_state
	    && new_state == TCP_CONNTRACK_FIN_WAIT)
		ct->proto.tcp.seen[dir].flags |= IP_CT_TCP_FLAG_CLOSE_INIT;

	timeouts = nf_ct_timeout_lookup(ct);
	if (!timeouts)
		timeouts = tn->timeouts;

	if (ct->proto.tcp.retrans >= tn->tcp_max_retrans &&
	    timeouts[new_state] > timeouts[TCP_CONNTRACK_RETRANS])
		timeout = timeouts[TCP_CONNTRACK_RETRANS];
	else if (unlikely(index == TCP_RST_SET))
		timeout = timeouts[TCP_CONNTRACK_CLOSE];
	else if ((ct->proto.tcp.seen[0].flags | ct->proto.tcp.seen[1].flags) &
		 IP_CT_TCP_FLAG_DATA_UNACKNOWLEDGED &&
		 timeouts[new_state] > timeouts[TCP_CONNTRACK_UNACK])
		timeout = timeouts[TCP_CONNTRACK_UNACK];
	else if (ct->proto.tcp.last_win == 0 &&
		 timeouts[new_state] > timeouts[TCP_CONNTRACK_RETRANS])
		timeout = timeouts[TCP_CONNTRACK_RETRANS];
	else
		timeout = timeouts[new_state];
	spin_unlock_bh(&ct->lock);

	if (new_state != old_state)
		nf_conntrack_event_cache(IPCT_PROTOINFO, ct);

	if (!test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
		/* If only reply is a RST, we can consider ourselves not to
		   have an established connection: this is a fairly common
		   problem case, so we can delete the conntrack
		   immediately.  --RR */
		if (th->rst) {
			nf_ct_kill_acct(ct, ctinfo, skb);
			return NF_ACCEPT;
		}
		/* ESTABLISHED without SEEN_REPLY, i.e. mid-connection
		 * pickup with loose=1. Avoid large ESTABLISHED timeout.
		 */
		if (new_state == TCP_CONNTRACK_ESTABLISHED &&
		    timeout > timeouts[TCP_CONNTRACK_UNACK])
			timeout = timeouts[TCP_CONNTRACK_UNACK];
	} else if (!test_bit(IPS_ASSURED_BIT, &ct->status)
		   && (old_state == TCP_CONNTRACK_SYN_RECV
		       || old_state == TCP_CONNTRACK_ESTABLISHED)
		   && new_state == TCP_CONNTRACK_ESTABLISHED) {
		/* Set ASSURED if we see valid ack in ESTABLISHED
		   after SYN_RECV or a valid answer for a picked up
		   connection. */
		set_bit(IPS_ASSURED_BIT, &ct->status);
		nf_conntrack_event_cache(IPCT_ASSURED, ct);
	}
	nf_ct_refresh_acct(ct, ctinfo, skb, timeout);

	return NF_ACCEPT;
}

static bool tcp_can_early_drop(const struct nf_conn *ct)
{
	switch (ct->proto.tcp.state) {
	case TCP_CONNTRACK_FIN_WAIT:
	case TCP_CONNTRACK_LAST_ACK:
	case TCP_CONNTRACK_TIME_WAIT:
	case TCP_CONNTRACK_CLOSE:
	case TCP_CONNTRACK_CLOSE_WAIT:
		return true;
	default:
		break;
	}

	return false;
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int tcp_to_nlattr(struct sk_buff *skb, struct nlattr *nla,
			 struct nf_conn *ct, bool destroy)
{
	struct nlattr *nest_parms;
	struct nf_ct_tcp_flags tmp = {};

	spin_lock_bh(&ct->lock);
	nest_parms = nla_nest_start(skb, CTA_PROTOINFO_TCP);
	if (!nest_parms)
		goto nla_put_failure;

	if (nla_put_u8(skb, CTA_PROTOINFO_TCP_STATE, ct->proto.tcp.state))
		goto nla_put_failure;

	if (destroy)
		goto skip_state;

	if (nla_put_u8(skb, CTA_PROTOINFO_TCP_WSCALE_ORIGINAL,
		       ct->proto.tcp.seen[0].td_scale) ||
	    nla_put_u8(skb, CTA_PROTOINFO_TCP_WSCALE_REPLY,
		       ct->proto.tcp.seen[1].td_scale))
		goto nla_put_failure;

	tmp.flags = ct->proto.tcp.seen[0].flags;
	if (nla_put(skb, CTA_PROTOINFO_TCP_FLAGS_ORIGINAL,
		    sizeof(struct nf_ct_tcp_flags), &tmp))
		goto nla_put_failure;

	tmp.flags = ct->proto.tcp.seen[1].flags;
	if (nla_put(skb, CTA_PROTOINFO_TCP_FLAGS_REPLY,
		    sizeof(struct nf_ct_tcp_flags), &tmp))
		goto nla_put_failure;
skip_state:
	spin_unlock_bh(&ct->lock);
	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	spin_unlock_bh(&ct->lock);
	return -1;
}

static const struct nla_policy tcp_nla_policy[CTA_PROTOINFO_TCP_MAX+1] = {
	[CTA_PROTOINFO_TCP_STATE]	    = { .type = NLA_U8 },
	[CTA_PROTOINFO_TCP_WSCALE_ORIGINAL] = { .type = NLA_U8 },
	[CTA_PROTOINFO_TCP_WSCALE_REPLY]    = { .type = NLA_U8 },
	[CTA_PROTOINFO_TCP_FLAGS_ORIGINAL]  = { .len = sizeof(struct nf_ct_tcp_flags) },
	[CTA_PROTOINFO_TCP_FLAGS_REPLY]	    = { .len = sizeof(struct nf_ct_tcp_flags) },
};

#define TCP_NLATTR_SIZE	( \
	NLA_ALIGN(NLA_HDRLEN + 1) + \
	NLA_ALIGN(NLA_HDRLEN + 1) + \
	NLA_ALIGN(NLA_HDRLEN + sizeof(struct nf_ct_tcp_flags)) + \
	NLA_ALIGN(NLA_HDRLEN + sizeof(struct nf_ct_tcp_flags)))

static int nlattr_to_tcp(struct nlattr *cda[], struct nf_conn *ct)
{
	struct nlattr *pattr = cda[CTA_PROTOINFO_TCP];
	struct nlattr *tb[CTA_PROTOINFO_TCP_MAX+1];
	int err;

	/* updates could not contain anything about the private
	 * protocol info, in that case skip the parsing */
	if (!pattr)
		return 0;

	err = nla_parse_nested_deprecated(tb, CTA_PROTOINFO_TCP_MAX, pattr,
					  tcp_nla_policy, NULL);
	if (err < 0)
		return err;

	if (tb[CTA_PROTOINFO_TCP_STATE] &&
	    nla_get_u8(tb[CTA_PROTOINFO_TCP_STATE]) >= TCP_CONNTRACK_MAX)
		return -EINVAL;

	spin_lock_bh(&ct->lock);
	if (tb[CTA_PROTOINFO_TCP_STATE])
		ct->proto.tcp.state = nla_get_u8(tb[CTA_PROTOINFO_TCP_STATE]);

	if (tb[CTA_PROTOINFO_TCP_FLAGS_ORIGINAL]) {
		struct nf_ct_tcp_flags *attr =
			nla_data(tb[CTA_PROTOINFO_TCP_FLAGS_ORIGINAL]);
		ct->proto.tcp.seen[0].flags &= ~attr->mask;
		ct->proto.tcp.seen[0].flags |= attr->flags & attr->mask;
	}

	if (tb[CTA_PROTOINFO_TCP_FLAGS_REPLY]) {
		struct nf_ct_tcp_flags *attr =
			nla_data(tb[CTA_PROTOINFO_TCP_FLAGS_REPLY]);
		ct->proto.tcp.seen[1].flags &= ~attr->mask;
		ct->proto.tcp.seen[1].flags |= attr->flags & attr->mask;
	}

	if (tb[CTA_PROTOINFO_TCP_WSCALE_ORIGINAL] &&
	    tb[CTA_PROTOINFO_TCP_WSCALE_REPLY] &&
	    ct->proto.tcp.seen[0].flags & IP_CT_TCP_FLAG_WINDOW_SCALE &&
	    ct->proto.tcp.seen[1].flags & IP_CT_TCP_FLAG_WINDOW_SCALE) {
		ct->proto.tcp.seen[0].td_scale =
			nla_get_u8(tb[CTA_PROTOINFO_TCP_WSCALE_ORIGINAL]);
		ct->proto.tcp.seen[1].td_scale =
			nla_get_u8(tb[CTA_PROTOINFO_TCP_WSCALE_REPLY]);
	}
	spin_unlock_bh(&ct->lock);

	return 0;
}

static unsigned int tcp_nlattr_tuple_size(void)
{
	static unsigned int size __read_mostly;

	if (!size)
		size = nla_policy_len(nf_ct_port_nla_policy, CTA_PROTO_MAX + 1);

	return size;
}
#endif

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_cttimeout.h>

static int tcp_timeout_nlattr_to_obj(struct nlattr *tb[],
				     struct net *net, void *data)
{
	struct nf_tcp_net *tn = nf_tcp_pernet(net);
	unsigned int *timeouts = data;
	int i;

	if (!timeouts)
		timeouts = tn->timeouts;
	/* set default TCP timeouts. */
	for (i=0; i<TCP_CONNTRACK_TIMEOUT_MAX; i++)
		timeouts[i] = tn->timeouts[i];

	if (tb[CTA_TIMEOUT_TCP_SYN_SENT]) {
		timeouts[TCP_CONNTRACK_SYN_SENT] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_SYN_SENT]))*HZ;
	}

	if (tb[CTA_TIMEOUT_TCP_SYN_RECV]) {
		timeouts[TCP_CONNTRACK_SYN_RECV] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_SYN_RECV]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_ESTABLISHED]) {
		timeouts[TCP_CONNTRACK_ESTABLISHED] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_ESTABLISHED]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_FIN_WAIT]) {
		timeouts[TCP_CONNTRACK_FIN_WAIT] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_FIN_WAIT]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_CLOSE_WAIT]) {
		timeouts[TCP_CONNTRACK_CLOSE_WAIT] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_CLOSE_WAIT]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_LAST_ACK]) {
		timeouts[TCP_CONNTRACK_LAST_ACK] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_LAST_ACK]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_TIME_WAIT]) {
		timeouts[TCP_CONNTRACK_TIME_WAIT] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_TIME_WAIT]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_CLOSE]) {
		timeouts[TCP_CONNTRACK_CLOSE] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_CLOSE]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_SYN_SENT2]) {
		timeouts[TCP_CONNTRACK_SYN_SENT2] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_SYN_SENT2]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_RETRANS]) {
		timeouts[TCP_CONNTRACK_RETRANS] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_RETRANS]))*HZ;
	}
	if (tb[CTA_TIMEOUT_TCP_UNACK]) {
		timeouts[TCP_CONNTRACK_UNACK] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_TCP_UNACK]))*HZ;
	}

	timeouts[CTA_TIMEOUT_TCP_UNSPEC] = timeouts[CTA_TIMEOUT_TCP_SYN_SENT];
	return 0;
}

static int
tcp_timeout_obj_to_nlattr(struct sk_buff *skb, const void *data)
{
	const unsigned int *timeouts = data;

	if (nla_put_be32(skb, CTA_TIMEOUT_TCP_SYN_SENT,
			htonl(timeouts[TCP_CONNTRACK_SYN_SENT] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_SYN_RECV,
			 htonl(timeouts[TCP_CONNTRACK_SYN_RECV] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_ESTABLISHED,
			 htonl(timeouts[TCP_CONNTRACK_ESTABLISHED] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_FIN_WAIT,
			 htonl(timeouts[TCP_CONNTRACK_FIN_WAIT] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_CLOSE_WAIT,
			 htonl(timeouts[TCP_CONNTRACK_CLOSE_WAIT] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_LAST_ACK,
			 htonl(timeouts[TCP_CONNTRACK_LAST_ACK] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_TIME_WAIT,
			 htonl(timeouts[TCP_CONNTRACK_TIME_WAIT] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_CLOSE,
			 htonl(timeouts[TCP_CONNTRACK_CLOSE] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_SYN_SENT2,
			 htonl(timeouts[TCP_CONNTRACK_SYN_SENT2] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_RETRANS,
			 htonl(timeouts[TCP_CONNTRACK_RETRANS] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_TCP_UNACK,
			 htonl(timeouts[TCP_CONNTRACK_UNACK] / HZ)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static const struct nla_policy tcp_timeout_nla_policy[CTA_TIMEOUT_TCP_MAX+1] = {
	[CTA_TIMEOUT_TCP_SYN_SENT]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_SYN_RECV]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_ESTABLISHED]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_FIN_WAIT]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_CLOSE_WAIT]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_LAST_ACK]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_TIME_WAIT]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_CLOSE]		= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_SYN_SENT2]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_RETRANS]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_TCP_UNACK]		= { .type = NLA_U32 },
};
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */

void nf_conntrack_tcp_init_net(struct net *net)
{
	struct nf_tcp_net *tn = nf_tcp_pernet(net);
	int i;

	for (i = 0; i < TCP_CONNTRACK_TIMEOUT_MAX; i++)
		tn->timeouts[i] = tcp_timeouts[i];

	/* timeouts[0] is unused, make it same as SYN_SENT so
	 * ->timeouts[0] contains 'new' timeout, like udp or icmp.
	 */
	tn->timeouts[0] = tcp_timeouts[TCP_CONNTRACK_SYN_SENT];

	/* If it is set to zero, we disable picking up already established
	 * connections.
	 */
	tn->tcp_loose = 1;

	/* "Be conservative in what you do,
	 *  be liberal in what you accept from others."
	 * If it's non-zero, we mark only out of window RST segments as INVALID.
	 */
	tn->tcp_be_liberal = 0;

	/* Max number of the retransmitted packets without receiving an (acceptable)
	 * ACK from the destination. If this number is reached, a shorter timer
	 * will be started.
	 */
	tn->tcp_max_retrans = 3;
}

const struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp =
{
	.l4proto 		= IPPROTO_TCP,
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	.print_conntrack 	= tcp_print_conntrack,
#endif
	.can_early_drop		= tcp_can_early_drop,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.to_nlattr		= tcp_to_nlattr,
	.from_nlattr		= nlattr_to_tcp,
	.tuple_to_nlattr	= nf_ct_port_tuple_to_nlattr,
	.nlattr_to_tuple	= nf_ct_port_nlattr_to_tuple,
	.nlattr_tuple_size	= tcp_nlattr_tuple_size,
	.nlattr_size		= TCP_NLATTR_SIZE,
	.nla_policy		= nf_ct_port_nla_policy,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	.ctnl_timeout		= {
		.nlattr_to_obj	= tcp_timeout_nlattr_to_obj,
		.obj_to_nlattr	= tcp_timeout_obj_to_nlattr,
		.nlattr_max	= CTA_TIMEOUT_TCP_MAX,
		.obj_size	= sizeof(unsigned int) *
					TCP_CONNTRACK_TIMEOUT_MAX,
		.nla_policy	= tcp_timeout_nla_policy,
	},
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */
};
