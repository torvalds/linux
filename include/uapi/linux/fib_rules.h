#ifndef __LINUX_FIB_RULES_H
#define __LINUX_FIB_RULES_H

#include <linux/types.h>
#include <linux/rtnetlink.h>

/* rule is permanent, and cannot be deleted */
#define FIB_RULE_PERMANENT	0x00000001
#define FIB_RULE_INVERT		0x00000002
#define FIB_RULE_UNRESOLVED	0x00000004
#define FIB_RULE_IIF_DETACHED	0x00000008
#define FIB_RULE_DEV_DETACHED	FIB_RULE_IIF_DETACHED
#define FIB_RULE_OIF_DETACHED	0x00000010

/* try to find source address in routing lookups */
#define FIB_RULE_FIND_SADDR	0x00010000

struct fib_rule_hdr {
	__u8		family;
	__u8		dst_len;
	__u8		src_len;
	__u8		tos;

	__u8		table;
	__u8		res1;	/* reserved */
	__u8		res2;	/* reserved */
	__u8		action;

	__u32		flags;
};

enum {
	FRA_UNSPEC,
	FRA_DST,	/* destination address */
	FRA_SRC,	/* source address */
	FRA_IIFNAME,	/* interface name */
#define FRA_IFNAME	FRA_IIFNAME
	FRA_GOTO,	/* target to jump to (FR_ACT_GOTO) */
	FRA_UNUSED2,
	FRA_PRIORITY,	/* priority/preference */
	FRA_UNUSED3,
	FRA_UNUSED4,
	FRA_UNUSED5,
	FRA_FWMARK,	/* mark */
	FRA_FLOW,	/* flow/class id */
	FRA_UNUSED6,
	FRA_SUPPRESS_IFGROUP,
	FRA_TABLE_PREFIXLEN_MIN,
	FRA_TABLE,	/* Extended table id */
	FRA_FWMASK,	/* mask for netfilter mark */
	FRA_OIFNAME,
	__FRA_MAX
};

#define FRA_MAX (__FRA_MAX - 1)

enum {
	FR_ACT_UNSPEC,
	FR_ACT_TO_TBL,		/* Pass to fixed table */
	FR_ACT_GOTO,		/* Jump to another rule */
	FR_ACT_NOP,		/* No operation */
	FR_ACT_RES3,
	FR_ACT_RES4,
	FR_ACT_BLACKHOLE,	/* Drop without notification */
	FR_ACT_UNREACHABLE,	/* Drop with ENETUNREACH */
	FR_ACT_PROHIBIT,	/* Drop with EACCES */
	__FR_ACT_MAX,
};

#define FR_ACT_MAX (__FR_ACT_MAX - 1)

#endif
