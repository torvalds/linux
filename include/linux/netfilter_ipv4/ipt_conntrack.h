/* Header file for kernel module to match connection tracking information.
 * GPL (C) 2001  Marc Boucher (marc@mbsi.ca).
 */

#ifndef _IPT_CONNTRACK_H
#define _IPT_CONNTRACK_H

#define IPT_CONNTRACK_STATE_BIT(ctinfo) (1 << ((ctinfo)%IP_CT_IS_REPLY+1))
#define IPT_CONNTRACK_STATE_INVALID (1 << 0)

#define IPT_CONNTRACK_STATE_SNAT (1 << (IP_CT_NUMBER + 1))
#define IPT_CONNTRACK_STATE_DNAT (1 << (IP_CT_NUMBER + 2))
#define IPT_CONNTRACK_STATE_UNTRACKED (1 << (IP_CT_NUMBER + 3))

/* flags, invflags: */
#define IPT_CONNTRACK_STATE	0x01
#define IPT_CONNTRACK_PROTO	0x02
#define IPT_CONNTRACK_ORIGSRC	0x04
#define IPT_CONNTRACK_ORIGDST	0x08
#define IPT_CONNTRACK_REPLSRC	0x10
#define IPT_CONNTRACK_REPLDST	0x20
#define IPT_CONNTRACK_STATUS	0x40
#define IPT_CONNTRACK_EXPIRES	0x80

/* This is exposed to userspace, so remains frozen in time. */
struct ip_conntrack_old_tuple
{
	struct {
		__u32 ip;
		union {
			__u16 all;
		} u;
	} src;

	struct {
		__u32 ip;
		union {
			__u16 all;
		} u;

		/* The protocol. */
		u16 protonum;
	} dst;
};

struct ipt_conntrack_info
{
	unsigned int statemask, statusmask;

	struct ip_conntrack_old_tuple tuple[IP_CT_DIR_MAX];
	struct in_addr sipmsk[IP_CT_DIR_MAX], dipmsk[IP_CT_DIR_MAX];

	unsigned long expires_min, expires_max;

	/* Flags word */
	u_int8_t flags;
	/* Inverse flags */
	u_int8_t invflags;
};
#endif /*_IPT_CONNTRACK_H*/
