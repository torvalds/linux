/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <net/tcp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_log.h>

/* Protects ct->proto.tcp */
static DEFINE_RWLOCK(tcp_lock);

/* "Be conservative in what you do,
    be liberal in what you accept from others."
    If it's non-zero, we mark only out of window RST segments as INVALID. */
static int nf_ct_tcp_be_liberal __read_mostly = 0;

/* If it is set to zero, we disable picking up already established
   connections. */
static int nf_ct_tcp_loose __read_mostly = 1;

/* Max number of the retransmitted packets without receiving an (acceptable)
   ACK from the destination. If this number is reached, a shorter timer
   will be started. */
static int nf_ct_tcp_max_retrans __read_mostly = 3;

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
	"LISTEN"
};

#define SECS * HZ
#define MINS * 60 SECS
#define HOURS * 60 MINS
#define DAYS * 24 HOURS

/* RFC1122 says the R2 limit should be at least 100 seconds.
   Linux uses 15 packets as limit, which corresponds
   to ~13-30min depending on RTO. */
static unsigned int nf_ct_tcp_timeout_max_retrans __read_mostly =   5 MINS;

static unsigned int tcp_timeouts[TCP_CONNTRACK_MAX] __read_mostly = {
	[TCP_CONNTRACK_SYN_SENT]	= 2 MINS,
	[TCP_CONNTRACK_SYN_RECV]	= 60 SECS,
	[TCP_CONNTRACK_ESTABLISHED]	= 5 DAYS,
	[TCP_CONNTRACK_FIN_WAIT]	= 2 MINS,
	[TCP_CONNTRACK_CLOSE_WAIT]	= 60 SECS,
	[TCP_CONNTRACK_LAST_ACK]	= 30 SECS,
	[TCP_CONNTRACK_TIME_WAIT]	= 2 MINS,
	[TCP_CONNTRACK_CLOSE]		= 10 SECS,
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
#define sLI TCP_CONNTRACK_LISTEN
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
 * SYN_RECV:	SYN-ACK packet seen
 * ESTABLISHED:	ACK packet seen
 * FIN_WAIT:	FIN packet seen
 * CLOSE_WAIT:	ACK seen (after FIN)
 * LAST_ACK:	FIN seen (after FIN)
 * TIME_WAIT:	last ACK seen
 * CLOSE:	closed connection
 *
 * LISTEN state is not used.
 *
 * Packets marked as IGNORED (sIG):
 *	if they may be either invalid or valid
 *	and the receiver may send back a connection
 *	closing RST or a SYN/ACK.
 *
 * Packets marked as INVALID (sIV):
 *	if they are invalid
 *	or we do not support the request (simultaneous open)
 */
static const u8 tcp_conntracks[2][6][TCP_CONNTRACK_MAX] = {
	{
/* ORIGINAL */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*syn*/	   { sSS, sSS, sIG, sIG, sIG, sIG, sIG, sSS, sSS, sIV },
/*
 *	sNO -> sSS	Initialize a new connection
 *	sSS -> sSS	Retransmitted SYN
 *	sSR -> sIG	Late retransmitted SYN?
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
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*synack*/ { sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV },
/*
 * A SYN/ACK from the client is always invalid:
 *	- either it tries to set up a simultaneous open, which is
 *	  not supported;
 *	- or the firewall has just been inserted between the two hosts
 *	  during the session set-up. The SYN will be retransmitted
 *	  by the true client (or it'll time out).
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*fin*/    { sIV, sIV, sFW, sFW, sLA, sLA, sLA, sTW, sCL, sIV },
/*
 *	sNO -> sIV	Too late and no reason to do anything...
 *	sSS -> sIV	Client migth not send FIN in this state:
 *			we enforce waiting for a SYN/ACK reply first.
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
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*ack*/	   { sES, sIV, sES, sES, sCW, sCW, sTW, sTW, sCL, sIV },
/*
 *	sNO -> sES	Assumed.
 *	sSS -> sIV	ACK is invalid: we haven't seen a SYN/ACK yet.
 *	sSR -> sES	Established state is reached.
 *	sES -> sES	:-)
 *	sFW -> sCW	Normal close request answered by ACK.
 *	sCW -> sCW
 *	sLA -> sTW	Last ACK detected.
 *	sTW -> sTW	Retransmitted last ACK. Remain in the same state.
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*rst*/    { sIV, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sIV },
/*none*/   { sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV }
	},
	{
/* REPLY */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*syn*/	   { sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV },
/*
 *	sNO -> sIV	Never reached.
 *	sSS -> sIV	Simultaneous open, not supported
 *	sSR -> sIV	Simultaneous open, not supported.
 *	sES -> sIV	Server may not initiate a connection.
 *	sFW -> sIV
 *	sCW -> sIV
 *	sLA -> sIV
 *	sTW -> sIV	Reopened connection, but server may not do it.
 *	sCL -> sIV
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*synack*/ { sIV, sSR, sSR, sIG, sIG, sIG, sIG, sIG, sIG, sIV },
/*
 *	sSS -> sSR	Standard open.
 *	sSR -> sSR	Retransmitted SYN/ACK.
 *	sES -> sIG	Late retransmitted SYN/ACK?
 *	sFW -> sIG	Might be SYN/ACK answering ignored SYN
 *	sCW -> sIG
 *	sLA -> sIG
 *	sTW -> sIG
 *	sCL -> sIG
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*fin*/    { sIV, sIV, sFW, sFW, sLA, sLA, sLA, sTW, sCL, sIV },
/*
 *	sSS -> sIV	Server might not send FIN in this state.
 *	sSR -> sFW	Close started.
 *	sES -> sFW
 *	sFW -> sLA	FIN seen in both directions.
 *	sCW -> sLA
 *	sLA -> sLA	Retransmitted FIN.
 *	sTW -> sTW
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*ack*/	   { sIV, sIG, sSR, sES, sCW, sCW, sTW, sTW, sCL, sIV },
/*
 *	sSS -> sIG	Might be a half-open connection.
 *	sSR -> sSR	Might answer late resent SYN.
 *	sES -> sES	:-)
 *	sFW -> sCW	Normal close request answered by ACK.
 *	sCW -> sCW
 *	sLA -> sTW	Last ACK detected.
 *	sTW -> sTW	Retransmitted last ACK.
 *	sCL -> sCL
 */
/* 	     sNO, sSS, sSR, sES, sFW, sCW, sLA, sTW, sCL, sLI	*/
/*rst*/    { sIV, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sCL, sIV },
/*none*/   { sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV, sIV }
	}
};

static int tcp_pkt_to_tuple(const struct sk_buff *skb,
			    unsigned int dataoff,
			    struct nf_conntrack_tuple *tuple)
{
	const struct tcphdr *hp;
	struct tcphdr _hdr;

	/* Actually only need first 8 bytes. */
	hp = skb_header_pointer(skb, dataoff, 8, &_hdr);
	if (hp == NULL)
		return 0;

	tuple->src.u.tcp.port = hp->source;
	tuple->dst.u.tcp.port = hp->dest;

	return 1;
}

static int tcp_invert_tuple(struct nf_conntrack_tuple *tuple,
			    const struct nf_conntrack_tuple *orig)
{
	tuple->src.u.tcp.port = orig->dst.u.tcp.port;
	tuple->dst.u.tcp.port = orig->src.u.tcp.port;
	return 1;
}

/* Print out the per-protocol part of the tuple. */
static int tcp_print_tuple(struct seq_file *s,
			   const struct nf_conntrack_tuple *tuple)
{
	return seq_printf(s, "sport=%hu dport=%hu ",
			  ntohs(tuple->src.u.tcp.port),
			  ntohs(tuple->dst.u.tcp.port));
}

/* Print out the private part of the conntrack. */
static int tcp_print_conntrack(struct seq_file *s, const struct nf_conn *ct)
{
	enum tcp_conntrack state;

	read_lock_bh(&tcp_lock);
	state = ct->proto.tcp.state;
	read_unlock_bh(&tcp_lock);

	return seq_printf(s, "%s ", tcp_conntrack_names[state]);
}

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

   http://www.nluug.nl/events/sane2000/papers.html
   http://www.iae.nl/users/guido/papers/tcp_filtering.ps.gz

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
   III.	Upper bound for valid ack:      sack <= receiver.td_end
   IV.	Lower bound for valid ack:	ack >= receiver.td_end - MAXACKWINDOW

   where sack is the highest right edge of sack block found in the packet.

   The upper bound limit for a valid ack is not ignored -
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
	BUG_ON(ptr == NULL);

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
			opsize=*ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			if (opsize > length)
				break;	/* don't parse partial options */

			if (opcode == TCPOPT_SACK_PERM
			    && opsize == TCPOLEN_SACK_PERM)
				state->flags |= IP_CT_TCP_FLAG_SACK_PERM;
			else if (opcode == TCPOPT_WINDOW
				 && opsize == TCPOLEN_WINDOW) {
				state->td_scale = *(u_int8_t *)ptr;

				if (state->td_scale > 14) {
					/* See RFC1323 */
					state->td_scale = 14;
				}
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
	BUG_ON(ptr == NULL);

	/* Fast path for timestamp-only option */
	if (length == TCPOLEN_TSTAMP_ALIGNED*4
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
			opsize = *ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			if (opsize > length)
				break;	/* don't parse partial options */

			if (opcode == TCPOPT_SACK
			    && opsize >= (TCPOLEN_SACK_BASE
					  + TCPOLEN_SACK_PERBLOCK)
			    && !((opsize - TCPOLEN_SACK_BASE)
				 % TCPOLEN_SACK_PERBLOCK)) {
				for (i = 0;
				     i < (opsize - TCPOLEN_SACK_BASE);
				     i += TCPOLEN_SACK_PERBLOCK) {
					tmp = ntohl(*((__be32 *)(ptr+i)+1));

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

static int tcp_in_window(const struct nf_conn *ct,
			 struct ip_ct_tcp *state,
			 enum ip_conntrack_dir dir,
			 unsigned int index,
			 const struct sk_buff *skb,
			 unsigned int dataoff,
			 const struct tcphdr *tcph,
			 int pf)
{
	struct ip_ct_tcp_state *sender = &state->seen[dir];
	struct ip_ct_tcp_state *receiver = &state->seen[!dir];
	const struct nf_conntrack_tuple *tuple = &ct->tuplehash[dir].tuple;
	__u32 seq, ack, sack, end, win, swin;
	int res;

	/*
	 * Get the required data from the packet.
	 */
	seq = ntohl(tcph->seq);
	ack = sack = ntohl(tcph->ack_seq);
	win = ntohs(tcph->window);
	end = segment_seq_plus_len(seq, skb->len, dataoff, tcph);

	if (receiver->flags & IP_CT_TCP_FLAG_SACK_PERM)
		tcp_sack(skb, dataoff, tcph, &sack);

	pr_debug("tcp_in_window: START\n");
	pr_debug("tcp_in_window: ");
	NF_CT_DUMP_TUPLE(tuple);
	pr_debug("seq=%u ack=%u sack=%u win=%u end=%u\n",
		 seq, ack, sack, win, end);
	pr_debug("tcp_in_window: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);

	if (sender->td_end == 0) {
		/*
		 * Initialize sender data.
		 */
		if (tcph->syn && tcph->ack) {
			/*
			 * Outgoing SYN-ACK in reply to a SYN.
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
		} else {
			/*
			 * We are in the middle of a connection,
			 * its history is lost for us.
			 * Let's try to use the data from the packet.
			 */
			sender->td_end = end;
			sender->td_maxwin = (win == 0 ? 1 : win);
			sender->td_maxend = end + sender->td_maxwin;
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

	if (seq == end
	    && (!tcph->rst
		|| (seq == 0 && state->state == TCP_CONNTRACK_SYN_SENT)))
		/*
		 * Packets contains no data: we assume it is valid
		 * and check the ack value only.
		 * However RST segments are always validated by their
		 * SEQ number, except when seq == 0 (reset sent answering
		 * SYN.
		 */
		seq = end = sender->td_end;

	pr_debug("tcp_in_window: ");
	NF_CT_DUMP_TUPLE(tuple);
	pr_debug("seq=%u ack=%u sack =%u win=%u end=%u\n",
		 seq, ack, sack, win, end);
	pr_debug("tcp_in_window: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);

	pr_debug("tcp_in_window: I=%i II=%i III=%i IV=%i\n",
		 before(seq, sender->td_maxend + 1),
		 after(end, sender->td_end - receiver->td_maxwin - 1),
		 before(sack, receiver->td_end + 1),
		 after(ack, receiver->td_end - MAXACKWINDOW(sender)));

	if (before(seq, sender->td_maxend + 1) &&
	    after(end, sender->td_end - receiver->td_maxwin - 1) &&
	    before(sack, receiver->td_end + 1) &&
	    after(ack, receiver->td_end - MAXACKWINDOW(sender))) {
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
		if (after(end, sender->td_end))
			sender->td_end = end;
		/*
		 * Update receiver data.
		 */
		if (after(end, sender->td_maxend))
			receiver->td_maxwin += end - sender->td_maxend;
		if (after(sack + win, receiver->td_maxend - 1)) {
			receiver->td_maxend = sack + win;
			if (win == 0)
				receiver->td_maxend++;
		}

		/*
		 * Check retransmissions.
		 */
		if (index == TCP_ACK_SET) {
			if (state->last_dir == dir
			    && state->last_seq == seq
			    && state->last_ack == ack
			    && state->last_end == end
			    && state->last_win == win)
				state->retrans++;
			else {
				state->last_dir = dir;
				state->last_seq = seq;
				state->last_ack = ack;
				state->last_end = end;
				state->last_win = win;
				state->retrans = 0;
			}
		}
		res = 1;
	} else {
		res = 0;
		if (sender->flags & IP_CT_TCP_FLAG_BE_LIBERAL ||
		    nf_ct_tcp_be_liberal)
			res = 1;
		if (!res && LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
			"nf_ct_tcp: %s ",
			before(seq, sender->td_maxend + 1) ?
			after(end, sender->td_end - receiver->td_maxwin - 1) ?
			before(sack, receiver->td_end + 1) ?
			after(ack, receiver->td_end - MAXACKWINDOW(sender)) ? "BUG"
			: "ACK is under the lower bound (possible overly delayed ACK)"
			: "ACK is over the upper bound (ACKed data not seen yet)"
			: "SEQ is under the lower bound (already ACKed data retransmitted)"
			: "SEQ is over the upper bound (over the window of the receiver)");
	}

	pr_debug("tcp_in_window: res=%i sender end=%u maxend=%u maxwin=%u "
		 "receiver end=%u maxend=%u maxwin=%u\n",
		 res, sender->td_end, sender->td_maxend, sender->td_maxwin,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin);

	return res;
}

#ifdef CONFIG_NF_NAT_NEEDED
/* Update sender->td_end after NAT successfully mangled the packet */
/* Caller must linearize skb at tcp header. */
void nf_conntrack_tcp_update(const struct sk_buff *skb,
			     unsigned int dataoff,
			     struct nf_conn *ct,
			     int dir)
{
	const struct tcphdr *tcph = (const void *)skb->data + dataoff;
	const struct ip_ct_tcp_state *sender = &ct->proto.tcp.seen[dir];
	const struct ip_ct_tcp_state *receiver = &ct->proto.tcp.seen[!dir];
	__u32 end;

	end = segment_seq_plus_len(ntohl(tcph->seq), skb->len, dataoff, tcph);

	write_lock_bh(&tcp_lock);
	/*
	 * We have to worry for the ack in the reply packet only...
	 */
	if (after(end, ct->proto.tcp.seen[dir].td_end))
		ct->proto.tcp.seen[dir].td_end = end;
	ct->proto.tcp.last_end = end;
	write_unlock_bh(&tcp_lock);
	pr_debug("tcp_update: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);
}
EXPORT_SYMBOL_GPL(nf_conntrack_tcp_update);
#endif

#define	TH_FIN	0x01
#define	TH_SYN	0x02
#define	TH_RST	0x04
#define	TH_PUSH	0x08
#define	TH_ACK	0x10
#define	TH_URG	0x20
#define	TH_ECE	0x40
#define	TH_CWR	0x80

/* table of valid flag combinations - PUSH, ECE and CWR are always valid */
static const u8 tcp_valid_flags[(TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG) + 1] =
{
	[TH_SYN]			= 1,
	[TH_SYN|TH_URG]			= 1,
	[TH_SYN|TH_ACK]			= 1,
	[TH_RST]			= 1,
	[TH_RST|TH_ACK]			= 1,
	[TH_FIN|TH_ACK]			= 1,
	[TH_FIN|TH_ACK|TH_URG]		= 1,
	[TH_ACK]			= 1,
	[TH_ACK|TH_URG]			= 1,
};

/* Protect conntrack agaist broken packets. Code taken from ipt_unclean.c.  */
static int tcp_error(struct sk_buff *skb,
		     unsigned int dataoff,
		     enum ip_conntrack_info *ctinfo,
		     int pf,
		     unsigned int hooknum)
{
	const struct tcphdr *th;
	struct tcphdr _tcph;
	unsigned int tcplen = skb->len - dataoff;
	u_int8_t tcpflags;

	/* Smaller that minimal TCP header? */
	th = skb_header_pointer(skb, dataoff, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		if (LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				"nf_ct_tcp: short packet ");
		return -NF_ACCEPT;
	}

	/* Not whole TCP header or malformed packet */
	if (th->doff*4 < sizeof(struct tcphdr) || tcplen < th->doff*4) {
		if (LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				"nf_ct_tcp: truncated/malformed packet ");
		return -NF_ACCEPT;
	}

	/* Checksum invalid? Ignore.
	 * We skip checking packets on the outgoing path
	 * because the checksum is assumed to be correct.
	 */
	/* FIXME: Source route IP option packets --RR */
	if (nf_conntrack_checksum && hooknum == NF_INET_PRE_ROUTING &&
	    nf_checksum(skb, hooknum, dataoff, IPPROTO_TCP, pf)) {
		if (LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				  "nf_ct_tcp: bad TCP checksum ");
		return -NF_ACCEPT;
	}

	/* Check TCP flags. */
	tcpflags = (((u_int8_t *)th)[13] & ~(TH_ECE|TH_CWR|TH_PUSH));
	if (!tcp_valid_flags[tcpflags]) {
		if (LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				  "nf_ct_tcp: invalid TCP flag combination ");
		return -NF_ACCEPT;
	}

	return NF_ACCEPT;
}

/* Returns verdict for packet, or -1 for invalid. */
static int tcp_packet(struct nf_conn *ct,
		      const struct sk_buff *skb,
		      unsigned int dataoff,
		      enum ip_conntrack_info ctinfo,
		      int pf,
		      unsigned int hooknum)
{
	struct nf_conntrack_tuple *tuple;
	enum tcp_conntrack new_state, old_state;
	enum ip_conntrack_dir dir;
	const struct tcphdr *th;
	struct tcphdr _tcph;
	unsigned long timeout;
	unsigned int index;

	th = skb_header_pointer(skb, dataoff, sizeof(_tcph), &_tcph);
	BUG_ON(th == NULL);

	write_lock_bh(&tcp_lock);
	old_state = ct->proto.tcp.state;
	dir = CTINFO2DIR(ctinfo);
	index = get_conntrack_index(th);
	new_state = tcp_conntracks[dir][index][old_state];
	tuple = &ct->tuplehash[dir].tuple;

	switch (new_state) {
	case TCP_CONNTRACK_SYN_SENT:
		if (old_state < TCP_CONNTRACK_TIME_WAIT)
			break;
		if ((ct->proto.tcp.seen[!dir].flags & IP_CT_TCP_FLAG_CLOSE_INIT)
		    || (ct->proto.tcp.last_dir == dir
		        && ct->proto.tcp.last_index == TCP_RST_SET)) {
			/* Attempt to reopen a closed/aborted connection.
			 * Delete this connection and look up again. */
			write_unlock_bh(&tcp_lock);
			if (del_timer(&ct->timeout))
				ct->timeout.function((unsigned long)ct);
			return -NF_REPEAT;
		}
		/* Fall through */
	case TCP_CONNTRACK_IGNORE:
		/* Ignored packets:
		 *
		 * a) SYN in ORIGINAL
		 * b) SYN/ACK in REPLY
		 * c) ACK in reply direction after initial SYN in original.
		 */
		if (index == TCP_SYNACK_SET
		    && ct->proto.tcp.last_index == TCP_SYN_SET
		    && ct->proto.tcp.last_dir != dir
		    && ntohl(th->ack_seq) == ct->proto.tcp.last_end) {
			/* This SYN/ACK acknowledges a SYN that we earlier
			 * ignored as invalid. This means that the client and
			 * the server are both in sync, while the firewall is
			 * not. We kill this session and block the SYN/ACK so
			 * that the client cannot but retransmit its SYN and
			 * thus initiate a clean new session.
			 */
			write_unlock_bh(&tcp_lock);
			if (LOG_INVALID(IPPROTO_TCP))
				nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
					  "nf_ct_tcp: killing out of sync session ");
			if (del_timer(&ct->timeout))
				ct->timeout.function((unsigned long)ct);
			return -NF_DROP;
		}
		ct->proto.tcp.last_index = index;
		ct->proto.tcp.last_dir = dir;
		ct->proto.tcp.last_seq = ntohl(th->seq);
		ct->proto.tcp.last_end =
		    segment_seq_plus_len(ntohl(th->seq), skb->len, dataoff, th);

		write_unlock_bh(&tcp_lock);
		if (LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				  "nf_ct_tcp: invalid packed ignored ");
		return NF_ACCEPT;
	case TCP_CONNTRACK_MAX:
		/* Invalid packet */
		pr_debug("nf_ct_tcp: Invalid dir=%i index=%u ostate=%u\n",
			 dir, get_conntrack_index(th), old_state);
		write_unlock_bh(&tcp_lock);
		if (LOG_INVALID(IPPROTO_TCP))
			nf_log_packet(pf, 0, skb, NULL, NULL, NULL,
				  "nf_ct_tcp: invalid state ");
		return -NF_ACCEPT;
	case TCP_CONNTRACK_CLOSE:
		if (index == TCP_RST_SET
		    && ((test_bit(IPS_SEEN_REPLY_BIT, &ct->status)
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
		/* Just fall through */
	default:
		/* Keep compilers happy. */
		break;
	}

	if (!tcp_in_window(ct, &ct->proto.tcp, dir, index,
			   skb, dataoff, th, pf)) {
		write_unlock_bh(&tcp_lock);
		return -NF_ACCEPT;
	}
     in_window:
	/* From now on we have got in-window packets */
	ct->proto.tcp.last_index = index;
	ct->proto.tcp.last_dir = dir;

	pr_debug("tcp_conntracks: ");
	NF_CT_DUMP_TUPLE(tuple);
	pr_debug("syn=%i ack=%i fin=%i rst=%i old=%i new=%i\n",
		 (th->syn ? 1 : 0), (th->ack ? 1 : 0),
		 (th->fin ? 1 : 0), (th->rst ? 1 : 0),
		 old_state, new_state);

	ct->proto.tcp.state = new_state;
	if (old_state != new_state
	    && (new_state == TCP_CONNTRACK_FIN_WAIT
		|| new_state == TCP_CONNTRACK_CLOSE))
		ct->proto.tcp.seen[dir].flags |= IP_CT_TCP_FLAG_CLOSE_INIT;
	timeout = ct->proto.tcp.retrans >= nf_ct_tcp_max_retrans
		  && tcp_timeouts[new_state] > nf_ct_tcp_timeout_max_retrans
		  ? nf_ct_tcp_timeout_max_retrans : tcp_timeouts[new_state];
	write_unlock_bh(&tcp_lock);

	nf_conntrack_event_cache(IPCT_PROTOINFO_VOLATILE, skb);
	if (new_state != old_state)
		nf_conntrack_event_cache(IPCT_PROTOINFO, skb);

	if (!test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
		/* If only reply is a RST, we can consider ourselves not to
		   have an established connection: this is a fairly common
		   problem case, so we can delete the conntrack
		   immediately.  --RR */
		if (th->rst) {
			if (del_timer(&ct->timeout))
				ct->timeout.function((unsigned long)ct);
			return NF_ACCEPT;
		}
	} else if (!test_bit(IPS_ASSURED_BIT, &ct->status)
		   && (old_state == TCP_CONNTRACK_SYN_RECV
		       || old_state == TCP_CONNTRACK_ESTABLISHED)
		   && new_state == TCP_CONNTRACK_ESTABLISHED) {
		/* Set ASSURED if we see see valid ack in ESTABLISHED
		   after SYN_RECV or a valid answer for a picked up
		   connection. */
		set_bit(IPS_ASSURED_BIT, &ct->status);
		nf_conntrack_event_cache(IPCT_STATUS, skb);
	}
	nf_ct_refresh_acct(ct, ctinfo, skb, timeout);

	return NF_ACCEPT;
}

/* Called when a new connection for this protocol found. */
static int tcp_new(struct nf_conn *ct,
		   const struct sk_buff *skb,
		   unsigned int dataoff)
{
	enum tcp_conntrack new_state;
	const struct tcphdr *th;
	struct tcphdr _tcph;
	const struct ip_ct_tcp_state *sender = &ct->proto.tcp.seen[0];
	const struct ip_ct_tcp_state *receiver = &ct->proto.tcp.seen[1];

	th = skb_header_pointer(skb, dataoff, sizeof(_tcph), &_tcph);
	BUG_ON(th == NULL);

	/* Don't need lock here: this conntrack not in circulation yet */
	new_state
		= tcp_conntracks[0][get_conntrack_index(th)]
		[TCP_CONNTRACK_NONE];

	/* Invalid: delete conntrack */
	if (new_state >= TCP_CONNTRACK_MAX) {
		pr_debug("nf_ct_tcp: invalid new deleting.\n");
		return 0;
	}

	if (new_state == TCP_CONNTRACK_SYN_SENT) {
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
		ct->proto.tcp.seen[1].flags = 0;
	} else if (nf_ct_tcp_loose == 0) {
		/* Don't try to pick up connections. */
		return 0;
	} else {
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
		ct->proto.tcp.seen[0].td_scale = 0;

		/* We assume SACK and liberal window checking to handle
		 * window scaling */
		ct->proto.tcp.seen[0].flags =
		ct->proto.tcp.seen[1].flags = IP_CT_TCP_FLAG_SACK_PERM |
					      IP_CT_TCP_FLAG_BE_LIBERAL;
	}

	ct->proto.tcp.seen[1].td_end = 0;
	ct->proto.tcp.seen[1].td_maxend = 0;
	ct->proto.tcp.seen[1].td_maxwin = 1;
	ct->proto.tcp.seen[1].td_scale = 0;

	/* tcp_packet will set them */
	ct->proto.tcp.state = TCP_CONNTRACK_NONE;
	ct->proto.tcp.last_index = TCP_NONE_SET;

	pr_debug("tcp_new: sender end=%u maxend=%u maxwin=%u scale=%i "
		 "receiver end=%u maxend=%u maxwin=%u scale=%i\n",
		 sender->td_end, sender->td_maxend, sender->td_maxwin,
		 sender->td_scale,
		 receiver->td_end, receiver->td_maxend, receiver->td_maxwin,
		 receiver->td_scale);
	return 1;
}

#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int tcp_to_nlattr(struct sk_buff *skb, struct nlattr *nla,
			 const struct nf_conn *ct)
{
	struct nlattr *nest_parms;
	struct nf_ct_tcp_flags tmp = {};

	read_lock_bh(&tcp_lock);
	nest_parms = nla_nest_start(skb, CTA_PROTOINFO_TCP | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	NLA_PUT_U8(skb, CTA_PROTOINFO_TCP_STATE, ct->proto.tcp.state);

	NLA_PUT_U8(skb, CTA_PROTOINFO_TCP_WSCALE_ORIGINAL,
		   ct->proto.tcp.seen[0].td_scale);

	NLA_PUT_U8(skb, CTA_PROTOINFO_TCP_WSCALE_REPLY,
		   ct->proto.tcp.seen[1].td_scale);

	tmp.flags = ct->proto.tcp.seen[0].flags;
	NLA_PUT(skb, CTA_PROTOINFO_TCP_FLAGS_ORIGINAL,
		sizeof(struct nf_ct_tcp_flags), &tmp);

	tmp.flags = ct->proto.tcp.seen[1].flags;
	NLA_PUT(skb, CTA_PROTOINFO_TCP_FLAGS_REPLY,
		sizeof(struct nf_ct_tcp_flags), &tmp);
	read_unlock_bh(&tcp_lock);

	nla_nest_end(skb, nest_parms);

	return 0;

nla_put_failure:
	read_unlock_bh(&tcp_lock);
	return -1;
}

static const struct nla_policy tcp_nla_policy[CTA_PROTOINFO_TCP_MAX+1] = {
	[CTA_PROTOINFO_TCP_STATE]	    = { .type = NLA_U8 },
	[CTA_PROTOINFO_TCP_WSCALE_ORIGINAL] = { .type = NLA_U8 },
	[CTA_PROTOINFO_TCP_WSCALE_REPLY]    = { .type = NLA_U8 },
	[CTA_PROTOINFO_TCP_FLAGS_ORIGINAL]  = { .len = sizeof(struct nf_ct_tcp_flags) },
	[CTA_PROTOINFO_TCP_FLAGS_REPLY]	    = { .len =  sizeof(struct nf_ct_tcp_flags) },
};

static int nlattr_to_tcp(struct nlattr *cda[], struct nf_conn *ct)
{
	struct nlattr *pattr = cda[CTA_PROTOINFO_TCP];
	struct nlattr *tb[CTA_PROTOINFO_TCP_MAX+1];
	int err;

	/* updates could not contain anything about the private
	 * protocol info, in that case skip the parsing */
	if (!pattr)
		return 0;

	err = nla_parse_nested(tb, CTA_PROTOINFO_TCP_MAX, pattr, tcp_nla_policy);
	if (err < 0)
		return err;

	if (!tb[CTA_PROTOINFO_TCP_STATE])
		return -EINVAL;

	write_lock_bh(&tcp_lock);
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
	write_unlock_bh(&tcp_lock);

	return 0;
}
#endif

#ifdef CONFIG_SYSCTL
static unsigned int tcp_sysctl_table_users;
static struct ctl_table_header *tcp_sysctl_header;
static struct ctl_table tcp_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_tcp_timeout_syn_sent",
		.data		= &tcp_timeouts[TCP_CONNTRACK_SYN_SENT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_syn_recv",
		.data		= &tcp_timeouts[TCP_CONNTRACK_SYN_RECV],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_established",
		.data		= &tcp_timeouts[TCP_CONNTRACK_ESTABLISHED],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_fin_wait",
		.data		= &tcp_timeouts[TCP_CONNTRACK_FIN_WAIT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_close_wait",
		.data		= &tcp_timeouts[TCP_CONNTRACK_CLOSE_WAIT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_last_ack",
		.data		= &tcp_timeouts[TCP_CONNTRACK_LAST_ACK],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_time_wait",
		.data		= &tcp_timeouts[TCP_CONNTRACK_TIME_WAIT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_close",
		.data		= &tcp_timeouts[TCP_CONNTRACK_CLOSE],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "nf_conntrack_tcp_timeout_max_retrans",
		.data		= &nf_ct_tcp_timeout_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_LOOSE,
		.procname	= "nf_conntrack_tcp_loose",
		.data		= &nf_ct_tcp_loose,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_BE_LIBERAL,
		.procname       = "nf_conntrack_tcp_be_liberal",
		.data           = &nf_ct_tcp_be_liberal,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
	{
		.ctl_name	= NET_NF_CONNTRACK_TCP_MAX_RETRANS,
		.procname	= "nf_conntrack_tcp_max_retrans",
		.data		= &nf_ct_tcp_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= 0
	}
};

#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
static struct ctl_table tcp_compat_sysctl_table[] = {
	{
		.procname	= "ip_conntrack_tcp_timeout_syn_sent",
		.data		= &tcp_timeouts[TCP_CONNTRACK_SYN_SENT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_syn_recv",
		.data		= &tcp_timeouts[TCP_CONNTRACK_SYN_RECV],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_established",
		.data		= &tcp_timeouts[TCP_CONNTRACK_ESTABLISHED],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_fin_wait",
		.data		= &tcp_timeouts[TCP_CONNTRACK_FIN_WAIT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_close_wait",
		.data		= &tcp_timeouts[TCP_CONNTRACK_CLOSE_WAIT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_last_ack",
		.data		= &tcp_timeouts[TCP_CONNTRACK_LAST_ACK],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_time_wait",
		.data		= &tcp_timeouts[TCP_CONNTRACK_TIME_WAIT],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_close",
		.data		= &tcp_timeouts[TCP_CONNTRACK_CLOSE],
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.procname	= "ip_conntrack_tcp_timeout_max_retrans",
		.data		= &nf_ct_tcp_timeout_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_LOOSE,
		.procname	= "ip_conntrack_tcp_loose",
		.data		= &nf_ct_tcp_loose,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_BE_LIBERAL,
		.procname	= "ip_conntrack_tcp_be_liberal",
		.data		= &nf_ct_tcp_be_liberal,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_MAX_RETRANS,
		.procname	= "ip_conntrack_tcp_max_retrans",
		.data		= &nf_ct_tcp_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= 0
	}
};
#endif /* CONFIG_NF_CONNTRACK_PROC_COMPAT */
#endif /* CONFIG_SYSCTL */

struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp4 __read_mostly =
{
	.l3proto		= PF_INET,
	.l4proto 		= IPPROTO_TCP,
	.name 			= "tcp",
	.pkt_to_tuple 		= tcp_pkt_to_tuple,
	.invert_tuple 		= tcp_invert_tuple,
	.print_tuple 		= tcp_print_tuple,
	.print_conntrack 	= tcp_print_conntrack,
	.packet 		= tcp_packet,
	.new 			= tcp_new,
	.error			= tcp_error,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.to_nlattr		= tcp_to_nlattr,
	.from_nlattr		= nlattr_to_tcp,
	.tuple_to_nlattr	= nf_ct_port_tuple_to_nlattr,
	.nlattr_to_tuple	= nf_ct_port_nlattr_to_tuple,
	.nla_policy		= nf_ct_port_nla_policy,
#endif
#ifdef CONFIG_SYSCTL
	.ctl_table_users	= &tcp_sysctl_table_users,
	.ctl_table_header	= &tcp_sysctl_header,
	.ctl_table		= tcp_sysctl_table,
#ifdef CONFIG_NF_CONNTRACK_PROC_COMPAT
	.ctl_compat_table	= tcp_compat_sysctl_table,
#endif
#endif
};
EXPORT_SYMBOL_GPL(nf_conntrack_l4proto_tcp4);

struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp6 __read_mostly =
{
	.l3proto		= PF_INET6,
	.l4proto 		= IPPROTO_TCP,
	.name 			= "tcp",
	.pkt_to_tuple 		= tcp_pkt_to_tuple,
	.invert_tuple 		= tcp_invert_tuple,
	.print_tuple 		= tcp_print_tuple,
	.print_conntrack 	= tcp_print_conntrack,
	.packet 		= tcp_packet,
	.new 			= tcp_new,
	.error			= tcp_error,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.to_nlattr		= tcp_to_nlattr,
	.from_nlattr		= nlattr_to_tcp,
	.tuple_to_nlattr	= nf_ct_port_tuple_to_nlattr,
	.nlattr_to_tuple	= nf_ct_port_nlattr_to_tuple,
	.nla_policy		= nf_ct_port_nla_policy,
#endif
#ifdef CONFIG_SYSCTL
	.ctl_table_users	= &tcp_sysctl_table_users,
	.ctl_table_header	= &tcp_sysctl_header,
	.ctl_table		= tcp_sysctl_table,
#endif
};
EXPORT_SYMBOL_GPL(nf_conntrack_l4proto_tcp6);
