/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_ICMPV6_H
#define _UAPI_LINUX_ICMPV6_H

#include <linux/types.h>
#include <asm/byteorder.h>

struct icmp6hdr {

	__u8		icmp6_type;
	__u8		icmp6_code;
	__sum16		icmp6_cksum;


	union {
		__be32			un_data32[1];
		__be16			un_data16[2];
		__u8			un_data8[4];

		struct icmpv6_echo {
			__be16		identifier;
			__be16		sequence;
		} u_echo;

                struct icmpv6_nd_advt {
#if defined(__LITTLE_ENDIAN_BITFIELD)
                        __u32		reserved:5,
                        		override:1,
                        		solicited:1,
                        		router:1,
					reserved2:24;
#elif defined(__BIG_ENDIAN_BITFIELD)
                        __u32		router:1,
					solicited:1,
                        		override:1,
                        		reserved:29;
#else
#error	"Please fix <asm/byteorder.h>"
#endif						
                } u_nd_advt;

                struct icmpv6_nd_ra {
			__u8		hop_limit;
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8		reserved:3,
					router_pref:2,
					home_agent:1,
					other:1,
					managed:1;

#elif defined(__BIG_ENDIAN_BITFIELD)
			__u8		managed:1,
					other:1,
					home_agent:1,
					router_pref:2,
					reserved:3;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
			__be16		rt_lifetime;
                } u_nd_ra;

	} icmp6_dataun;

#define icmp6_identifier	icmp6_dataun.u_echo.identifier
#define icmp6_sequence		icmp6_dataun.u_echo.sequence
#define icmp6_pointer		icmp6_dataun.un_data32[0]
#define icmp6_mtu		icmp6_dataun.un_data32[0]
#define icmp6_unused		icmp6_dataun.un_data32[0]
#define icmp6_maxdelay		icmp6_dataun.un_data16[0]
#define icmp6_router		icmp6_dataun.u_nd_advt.router
#define icmp6_solicited		icmp6_dataun.u_nd_advt.solicited
#define icmp6_override		icmp6_dataun.u_nd_advt.override
#define icmp6_ndiscreserved	icmp6_dataun.u_nd_advt.reserved
#define icmp6_hop_limit		icmp6_dataun.u_nd_ra.hop_limit
#define icmp6_addrconf_managed	icmp6_dataun.u_nd_ra.managed
#define icmp6_addrconf_other	icmp6_dataun.u_nd_ra.other
#define icmp6_rt_lifetime	icmp6_dataun.u_nd_ra.rt_lifetime
#define icmp6_router_pref	icmp6_dataun.u_nd_ra.router_pref
};


#define ICMPV6_ROUTER_PREF_LOW		0x3
#define ICMPV6_ROUTER_PREF_MEDIUM	0x0
#define ICMPV6_ROUTER_PREF_HIGH		0x1
#define ICMPV6_ROUTER_PREF_INVALID	0x2

#define ICMPV6_DEST_UNREACH		1
#define ICMPV6_PKT_TOOBIG		2
#define ICMPV6_TIME_EXCEED		3
#define ICMPV6_PARAMPROB		4

#define ICMPV6_ERRMSG_MAX       127

#define ICMPV6_INFOMSG_MASK		0x80

#define ICMPV6_ECHO_REQUEST		128
#define ICMPV6_ECHO_REPLY		129
#define ICMPV6_MGM_QUERY		130
#define ICMPV6_MGM_REPORT       	131
#define ICMPV6_MGM_REDUCTION    	132

#define ICMPV6_NI_QUERY			139
#define ICMPV6_NI_REPLY			140

#define ICMPV6_MLD2_REPORT		143

#define ICMPV6_DHAAD_REQUEST		144
#define ICMPV6_DHAAD_REPLY		145
#define ICMPV6_MOBILE_PREFIX_SOL	146
#define ICMPV6_MOBILE_PREFIX_ADV	147

#define ICMPV6_MRDISC_ADV		151

#define ICMPV6_MSG_MAX          255

/*
 *	Codes for Destination Unreachable
 */
#define ICMPV6_NOROUTE			0
#define ICMPV6_ADM_PROHIBITED		1
#define ICMPV6_NOT_NEIGHBOUR		2
#define ICMPV6_ADDR_UNREACH		3
#define ICMPV6_PORT_UNREACH		4
#define ICMPV6_POLICY_FAIL		5
#define ICMPV6_REJECT_ROUTE		6

/*
 *	Codes for Time Exceeded
 */
#define ICMPV6_EXC_HOPLIMIT		0
#define ICMPV6_EXC_FRAGTIME		1

/*
 *	Codes for Parameter Problem
 */
#define ICMPV6_HDR_FIELD		0
#define ICMPV6_UNK_NEXTHDR		1
#define ICMPV6_UNK_OPTION		2

/*
 *	constants for (set|get)sockopt
 */

#define ICMPV6_FILTER			1

/*
 *	ICMPV6 filter
 */

#define ICMPV6_FILTER_BLOCK		1
#define ICMPV6_FILTER_PASS		2
#define ICMPV6_FILTER_BLOCKOTHERS	3
#define ICMPV6_FILTER_PASSONLY		4

struct icmp6_filter {
	__u32		data[8];
};

/*
 *	Definitions for MLDv2
 */
#define MLD2_MODE_IS_INCLUDE	1
#define MLD2_MODE_IS_EXCLUDE	2
#define MLD2_CHANGE_TO_INCLUDE	3
#define MLD2_CHANGE_TO_EXCLUDE	4
#define MLD2_ALLOW_NEW_SOURCES	5
#define MLD2_BLOCK_OLD_SOURCES	6

#define MLD2_ALL_MCR_INIT { { { 0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0,0,0x16 } } }


#endif /* _UAPI_LINUX_ICMPV6_H */
