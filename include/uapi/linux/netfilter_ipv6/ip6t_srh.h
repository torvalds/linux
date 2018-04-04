/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _IP6T_SRH_H
#define _IP6T_SRH_H

#include <linux/types.h>
#include <linux/netfilter.h>

/* Values for "mt_flags" field in struct ip6t_srh */
#define IP6T_SRH_NEXTHDR        0x0001
#define IP6T_SRH_LEN_EQ         0x0002
#define IP6T_SRH_LEN_GT         0x0004
#define IP6T_SRH_LEN_LT         0x0008
#define IP6T_SRH_SEGS_EQ        0x0010
#define IP6T_SRH_SEGS_GT        0x0020
#define IP6T_SRH_SEGS_LT        0x0040
#define IP6T_SRH_LAST_EQ        0x0080
#define IP6T_SRH_LAST_GT        0x0100
#define IP6T_SRH_LAST_LT        0x0200
#define IP6T_SRH_TAG            0x0400
#define IP6T_SRH_MASK           0x07FF

/* Values for "mt_invflags" field in struct ip6t_srh */
#define IP6T_SRH_INV_NEXTHDR    0x0001
#define IP6T_SRH_INV_LEN_EQ     0x0002
#define IP6T_SRH_INV_LEN_GT     0x0004
#define IP6T_SRH_INV_LEN_LT     0x0008
#define IP6T_SRH_INV_SEGS_EQ    0x0010
#define IP6T_SRH_INV_SEGS_GT    0x0020
#define IP6T_SRH_INV_SEGS_LT    0x0040
#define IP6T_SRH_INV_LAST_EQ    0x0080
#define IP6T_SRH_INV_LAST_GT    0x0100
#define IP6T_SRH_INV_LAST_LT    0x0200
#define IP6T_SRH_INV_TAG        0x0400
#define IP6T_SRH_INV_MASK       0x07FF

/**
 *      struct ip6t_srh - SRH match options
 *      @ next_hdr: Next header field of SRH
 *      @ hdr_len: Extension header length field of SRH
 *      @ segs_left: Segments left field of SRH
 *      @ last_entry: Last entry field of SRH
 *      @ tag: Tag field of SRH
 *      @ mt_flags: match options
 *      @ mt_invflags: Invert the sense of match options
 */

struct ip6t_srh {
	__u8                    next_hdr;
	__u8                    hdr_len;
	__u8                    segs_left;
	__u8                    last_entry;
	__u16                   tag;
	__u16                   mt_flags;
	__u16                   mt_invflags;
};

#endif /*_IP6T_SRH_H*/
