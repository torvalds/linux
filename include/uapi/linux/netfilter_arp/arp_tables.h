/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * 	Format of an ARP firewall descriptor
 *
 * 	src, tgt, src_mask, tgt_mask, arpop, arpop_mask are always stored in
 *	network byte order.
 * 	flags are stored in host byte order (of course).
 */

#ifndef _UAPI_ARPTABLES_H
#define _UAPI_ARPTABLES_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/if.h>
#include <linux/netfilter_arp.h>

#include <linux/netfilter/x_tables.h>

#ifndef __KERNEL__
#define ARPT_FUNCTION_MAXNAMELEN XT_FUNCTION_MAXNAMELEN
#define ARPT_TABLE_MAXNAMELEN XT_TABLE_MAXNAMELEN
#define arpt_entry_target xt_entry_target
#define arpt_standard_target xt_standard_target
#define arpt_error_target xt_error_target
#define ARPT_CONTINUE XT_CONTINUE
#define ARPT_RETURN XT_RETURN
#define arpt_counters_info xt_counters_info
#define arpt_counters xt_counters
#define ARPT_STANDARD_TARGET XT_STANDARD_TARGET
#define ARPT_ERROR_TARGET XT_ERROR_TARGET
#define ARPT_ENTRY_ITERATE(entries, size, fn, args...) \
	XT_ENTRY_ITERATE(struct arpt_entry, entries, size, fn, ## args)
#endif

#define ARPT_DEV_ADDR_LEN_MAX 16

struct arpt_devaddr_info {
	char addr[ARPT_DEV_ADDR_LEN_MAX];
	char mask[ARPT_DEV_ADDR_LEN_MAX];
};

/* Yes, Virginia, you have to zero the padding. */
struct arpt_arp {
	/* Source and target IP addr */
	struct in_addr src, tgt;
	/* Mask for src and target IP addr */
	struct in_addr smsk, tmsk;

	/* Device hw address length, src+target device addresses */
	__u8 arhln, arhln_mask;
	struct arpt_devaddr_info src_devaddr;
	struct arpt_devaddr_info tgt_devaddr;

	/* ARP operation code. */
	__be16 arpop, arpop_mask;

	/* ARP hardware address and protocol address format. */
	__be16 arhrd, arhrd_mask;
	__be16 arpro, arpro_mask;

	/* The protocol address length is only accepted if it is 4
	 * so there is no use in offering a way to do filtering on it.
	 */

	char iniface[IFNAMSIZ], outiface[IFNAMSIZ];
	unsigned char iniface_mask[IFNAMSIZ], outiface_mask[IFNAMSIZ];

	/* Flags word */
	__u8 flags;
	/* Inverse flags */
	__u16 invflags;
};

/* Values for "flag" field in struct arpt_ip (general arp structure).
 * No flags defined yet.
 */
#define ARPT_F_MASK		0x00	/* All possible flag bits mask. */

/* Values for "inv" field in struct arpt_arp. */
#define ARPT_INV_VIA_IN		0x0001	/* Invert the sense of IN IFACE. */
#define ARPT_INV_VIA_OUT	0x0002	/* Invert the sense of OUT IFACE */
#define ARPT_INV_SRCIP		0x0004	/* Invert the sense of SRC IP. */
#define ARPT_INV_TGTIP		0x0008	/* Invert the sense of TGT IP. */
#define ARPT_INV_SRCDEVADDR	0x0010	/* Invert the sense of SRC DEV ADDR. */
#define ARPT_INV_TGTDEVADDR	0x0020	/* Invert the sense of TGT DEV ADDR. */
#define ARPT_INV_ARPOP		0x0040	/* Invert the sense of ARP OP. */
#define ARPT_INV_ARPHRD		0x0080	/* Invert the sense of ARP HRD. */
#define ARPT_INV_ARPPRO		0x0100	/* Invert the sense of ARP PRO. */
#define ARPT_INV_ARPHLN		0x0200	/* Invert the sense of ARP HLN. */
#define ARPT_INV_MASK		0x03FF	/* All possible flag bits mask. */

/* This structure defines each of the firewall rules.  Consists of 3
   parts which are 1) general ARP header stuff 2) match specific
   stuff 3) the target to perform if the rule matches */
struct arpt_entry
{
	struct arpt_arp arp;

	/* Size of arpt_entry + matches */
	__u16 target_offset;
	/* Size of arpt_entry + matches + target */
	__u16 next_offset;

	/* Back pointer */
	unsigned int comefrom;

	/* Packet and byte counters. */
	struct xt_counters counters;

	/* The matches (if any), then the target. */
	unsigned char elems[0];
};

/*
 * New IP firewall options for [gs]etsockopt at the RAW IP level.
 * Unlike BSD Linux inherits IP options so you don't have to use a raw
 * socket for this. Instead we check rights in the calls.
 *
 * ATTENTION: check linux/in.h before adding new number here.
 */
#define ARPT_BASE_CTL		96

#define ARPT_SO_SET_REPLACE		(ARPT_BASE_CTL)
#define ARPT_SO_SET_ADD_COUNTERS	(ARPT_BASE_CTL + 1)
#define ARPT_SO_SET_MAX			ARPT_SO_SET_ADD_COUNTERS

#define ARPT_SO_GET_INFO		(ARPT_BASE_CTL)
#define ARPT_SO_GET_ENTRIES		(ARPT_BASE_CTL + 1)
/* #define ARPT_SO_GET_REVISION_MATCH	(APRT_BASE_CTL + 2) */
#define ARPT_SO_GET_REVISION_TARGET	(ARPT_BASE_CTL + 3)
#define ARPT_SO_GET_MAX			(ARPT_SO_GET_REVISION_TARGET)

/* The argument to ARPT_SO_GET_INFO */
struct arpt_getinfo {
	/* Which table: caller fills this in. */
	char name[XT_TABLE_MAXNAMELEN];

	/* Kernel fills these in. */
	/* Which hook entry points are valid: bitmask */
	unsigned int valid_hooks;

	/* Hook entry points: one per netfilter hook. */
	unsigned int hook_entry[NF_ARP_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_ARP_NUMHOOKS];

	/* Number of entries */
	unsigned int num_entries;

	/* Size of entries. */
	unsigned int size;
};

/* The argument to ARPT_SO_SET_REPLACE. */
struct arpt_replace {
	/* Which table. */
	char name[XT_TABLE_MAXNAMELEN];

	/* Which hook entry points are valid: bitmask.  You can't
           change this. */
	unsigned int valid_hooks;

	/* Number of entries */
	unsigned int num_entries;

	/* Total size of new entries */
	unsigned int size;

	/* Hook entry points. */
	unsigned int hook_entry[NF_ARP_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_ARP_NUMHOOKS];

	/* Information about old entries: */
	/* Number of counters (must be equal to current number of entries). */
	unsigned int num_counters;
	/* The old entries' counters. */
	struct xt_counters __user *counters;

	/* The entries (hang off end: not really an array). */
	struct arpt_entry entries[0];
};

/* The argument to ARPT_SO_GET_ENTRIES. */
struct arpt_get_entries {
	/* Which table: user fills this in. */
	char name[XT_TABLE_MAXNAMELEN];

	/* User fills this in: total entry size. */
	unsigned int size;

	/* The entries. */
	struct arpt_entry entrytable[0];
};

/* Helper functions */
static __inline__ struct xt_entry_target *arpt_get_target(struct arpt_entry *e)
{
	return (void *)e + e->target_offset;
}

/*
 *	Main firewall chains definitions and global var's definitions.
 */
#endif /* _UAPI_ARPTABLES_H */
