/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Linux NET3:	Internet Group Management Protocol  [IGMP]
 *
 *	Authors:
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Extended to talk the BSD extended IGMP protocol of mrouted 3.6
 */
#ifndef _LINUX_IGMP_H
#define _LINUX_IGMP_H

#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/refcount.h>
#include <linux/sockptr.h>
#include <uapi/linux/igmp.h>

static inline struct igmphdr *igmp_hdr(const struct sk_buff *skb)
{
	return (struct igmphdr *)skb_transport_header(skb);
}

static inline struct igmpv3_report *
			igmpv3_report_hdr(const struct sk_buff *skb)
{
	return (struct igmpv3_report *)skb_transport_header(skb);
}

static inline struct igmpv3_query *
			igmpv3_query_hdr(const struct sk_buff *skb)
{
	return (struct igmpv3_query *)skb_transport_header(skb);
}

struct ip_sf_socklist {
	unsigned int		sl_max;
	unsigned int		sl_count;
	struct rcu_head		rcu;
	__be32			sl_addr[] __counted_by(sl_max);
};

#define IP_SFBLOCK	10	/* allocate this many at once */

/* ip_mc_socklist is real list now. Speed is not argument;
   this list never used in fast path code
 */

struct ip_mc_socklist {
	struct ip_mc_socklist __rcu *next_rcu;
	struct ip_mreqn		multi;
	unsigned int		sfmode;		/* MCAST_{INCLUDE,EXCLUDE} */
	struct ip_sf_socklist __rcu	*sflist;
	struct rcu_head		rcu;
};

struct ip_sf_list {
	struct ip_sf_list	*sf_next;
	unsigned long		sf_count[2];	/* include/exclude counts */
	__be32			sf_inaddr;
	unsigned char		sf_gsresp;	/* include in g & s response? */
	unsigned char		sf_oldin;	/* change state */
	unsigned char		sf_crcount;	/* retrans. left to send */
};

struct ip_mc_list {
	struct in_device	*interface;
	__be32			multiaddr;
	unsigned int		sfmode;
	struct ip_sf_list	*sources;
	struct ip_sf_list	*tomb;
	unsigned long		sfcount[2];
	union {
		struct ip_mc_list *next;
		struct ip_mc_list __rcu *next_rcu;
	};
	struct ip_mc_list __rcu *next_hash;
	struct timer_list	timer;
	int			users;
	refcount_t		refcnt;
	spinlock_t		lock;
	char			tm_running;
	char			reporter;
	char			unsolicit_count;
	char			loaded;
	unsigned char		gsquery;	/* check source marks? */
	unsigned char		crcount;
	unsigned long		mca_cstamp;
	unsigned long		mca_tstamp;
	struct rcu_head		rcu;
};

/* RFC3376, relevant sections:
 *  - 4.1.1. Maximum Response Code
 *  - 4.1.7. QQIC (Querier's Query Interval Code)
 *
 * For both MRC and QQIC, values >= 128 use the same floating-point
 * encoding as follows:
 *
 *  0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+
 * |1| exp | mant  |
 * +-+-+-+-+-+-+-+-+
 */
#define IGMPV3_FP_EXP(value)		(((value) >> 4) & 0x07)
#define IGMPV3_FP_MAN(value)		((value) & 0x0f)

/* IGMPv3 floating-point exponential field min threshold */
#define IGMPV3_EXP_MIN_THRESHOLD	128
/* IGMPv3 FP max threshold (mant = 0xF, exp = 7) -> 31744 */
#define IGMPV3_EXP_MAX_THRESHOLD	31744

/* V3 exponential field encoding */

/* IGMPv3 MRC/QQIC 8-bit exponential field encode
 *
 * RFC3376, 4.1.1 & 4.1.7. defines only the decoding formula:
 *     MRT/QQI = (mant | 0x10) << (exp + 3)
 *
 * but does NOT define the encoding procedure. To derive exponent:
 *
 * For any value of mantissa and exponent, the decoding formula
 * indicates that the "hidden bit" (0x10) is shifted 4 bits left
 * to sit above the 4-bit mantissa. The RFC again shifts this
 * entire block left by (exp + 3) to reconstruct the value.
 * So, 'hidden bit' is the MSB which is shifted by (4 + exp + 3).
 *
 * Total left shift of the 'hidden bit' = 4 + (exp + 3) = exp + 7.
 * This is the MSB at the 0-based bit position: (exp + 7).
 * Since fls() is 1-based, fls(value) - 1 = exp + 7.
 *
 * Therefore:
 *     exp  = fls(value) - 8
 *     mant = (value >> (exp + 3)) & 0x0F
 *
 * Final encoding formula:
 *     0x80 | (exp << 4) | mant
 *
 * Example (value = 3200):
 *  0               1
 *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0 0 0 0 1 1 0 0 1 0 0 0 0 0 0 0| (value = 3200)
 * |        ^-^-mant^ ^..(exp+3)..^| exp = 4, mant = 9
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Encoded:
 *   0x80 | (4 << 4) | 9 = 0xC9
 */
static inline u8 igmpv3_exp_field_encode(unsigned long value)
{
	u8 mc_exp, mc_man;

	/* MRC/QQIC < 128 is literal */
	if (value < IGMPV3_EXP_MIN_THRESHOLD)
		return value;

	/* Saturate at max representable (mant = 0xF, exp = 7) -> 31744 */
	if (value >= IGMPV3_EXP_MAX_THRESHOLD)
		return 0xFF;

	mc_exp  = fls(value) - 8;
	mc_man = (value >> (mc_exp + 3)) & 0x0F;

	return 0x80 | (mc_exp << 4) | mc_man;
}

/* Calculate Maximum Response Code from Max Resp Time
 *
 * RFC3376, relevant sections:
 *  - 4.1.1. Maximum Response Code
 *  - 8.3. Query Response Interval
 *
 * MRC represents the encoded form of Max Resp Time (MRT); once
 * decoded, the resulting value is in units of 0.1 seconds (100 ms).
 */
static inline u8 igmpv3_mrc(unsigned long mrt)
{
	return igmpv3_exp_field_encode(mrt);
}

/* Calculate Querier's Query Interval Code from Querier's Query Interval
 *
 * RFC3376, relevant sections:
 *  - 4.1.7. QQIC (Querier's Query Interval Code)
 *  - 8.2. Query Interval
 *  - 8.12. Older Version Querier Present Timeout
 *    (the [Query Interval] in the last Query received)
 *
 * QQIC represents the encoded form of Querier's Query Interval (QQI);
 * once decoded, the resulting value is in units of seconds.
 */
static inline u8 igmpv3_qqic(unsigned long qi)
{
	return igmpv3_exp_field_encode(qi);
}

/* V3 exponential field decoding */

/* IGMPv3 MRC/QQIC 8-bit exponential field decode
 *
 * RFC3376, 4.1.1 & 4.1.7. defines the decoding formula:
 *      0 1 2 3 4 5 6 7
 *     +-+-+-+-+-+-+-+-+
 *     |1| exp | mant  |
 *     +-+-+-+-+-+-+-+-+
 * Max Resp Time = (mant | 0x10) << (exp + 3)
 * QQI = (mant | 0x10) << (exp + 3)
 */
static inline unsigned long igmpv3_exp_field_decode(const u8 code)
{
	if (code < IGMPV3_EXP_MIN_THRESHOLD) {
		return code;
	} else {
		unsigned long mc_man, mc_exp;

		mc_exp = IGMPV3_FP_EXP(code);
		mc_man = IGMPV3_FP_MAN(code);

		return (mc_man | 0x10) << (mc_exp + 3);
	}
}

/* Calculate Max Resp Time from Maximum Response Code
 *
 * RFC3376, relevant sections:
 *  - 4.1.1. Maximum Response Code
 *  - 8.3. Query Response Interval
 *
 * After decode, MRC represents the Maximum Response Time (MRT) in
 * units of 0.1 seconds (100 ms).
 */
static inline unsigned long igmpv3_mrt(const struct igmpv3_query *ih3)
{
	return igmpv3_exp_field_decode(ih3->code);
}

/* Calculate Querier's Query Interval from Querier's Query Interval Code
 *
 * RFC3376, relevant sections:
 *  - 4.1.7. QQIC (Querier's Query Interval Code)
 *  - 8.2. Query Interval
 *  - 8.12. Older Version Querier Present Timeout
 *    (the [Query Interval] in the last Query received)
 *
 * After decode, QQIC represents the Querier's Query Interval in units
 * of seconds.
 */
static inline unsigned long igmpv3_qqi(const struct igmpv3_query *ih3)
{
	return igmpv3_exp_field_decode(ih3->qqic);
}

static inline int ip_mc_may_pull(struct sk_buff *skb, unsigned int len)
{
	if (skb_transport_offset(skb) + ip_transport_len(skb) < len)
		return 0;

	return pskb_may_pull(skb, len);
}

extern int ip_check_mc_rcu(struct in_device *dev, __be32 mc_addr, __be32 src_addr, u8 proto);
extern int igmp_rcv(struct sk_buff *);
extern int ip_mc_join_group(struct sock *sk, struct ip_mreqn *imr);
extern int ip_mc_join_group_ssm(struct sock *sk, struct ip_mreqn *imr,
				unsigned int mode);
extern int ip_mc_leave_group(struct sock *sk, struct ip_mreqn *imr);
extern void ip_mc_drop_socket(struct sock *sk);
extern int ip_mc_source(int add, int omode, struct sock *sk,
		struct ip_mreq_source *mreqs, int ifindex);
extern int ip_mc_msfilter(struct sock *sk, struct ip_msfilter *msf,int ifindex);
extern int ip_mc_msfget(struct sock *sk, struct ip_msfilter *msf,
			sockptr_t optval, sockptr_t optlen);
extern int ip_mc_gsfget(struct sock *sk, struct group_filter *gsf,
			sockptr_t optval, size_t offset);
extern int ip_mc_sf_allow(const struct sock *sk, __be32 local, __be32 rmt,
			  int dif, int sdif);
extern void ip_mc_init_dev(struct in_device *);
extern void ip_mc_destroy_dev(struct in_device *);
extern void ip_mc_up(struct in_device *);
extern void ip_mc_down(struct in_device *);
extern void ip_mc_unmap(struct in_device *);
extern void ip_mc_remap(struct in_device *);
extern void __ip_mc_dec_group(struct in_device *in_dev, __be32 addr, gfp_t gfp);
static inline void ip_mc_dec_group(struct in_device *in_dev, __be32 addr)
{
	return __ip_mc_dec_group(in_dev, addr, GFP_KERNEL);
}
extern void __ip_mc_inc_group(struct in_device *in_dev, __be32 addr,
			      gfp_t gfp);
extern void ip_mc_inc_group(struct in_device *in_dev, __be32 addr);
int ip_mc_check_igmp(struct sk_buff *skb);

#endif
