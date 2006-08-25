#ifndef __LINUX_FIB_RULES_H
#define __LINUX_FIB_RULES_H

#include <linux/types.h>
#include <linux/rtnetlink.h>

/* rule is permanent, and cannot be deleted */
#define FIB_RULE_PERMANENT	1

struct fib_rule_hdr
{
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

enum
{
	FRA_UNSPEC,
	FRA_DST,	/* destination address */
	FRA_SRC,	/* source address */
	FRA_IFNAME,	/* interface name */
	FRA_UNUSED1,
	FRA_UNUSED2,
	FRA_PRIORITY,	/* priority/preference */
	FRA_UNUSED3,
	FRA_UNUSED4,
	FRA_UNUSED5,
	FRA_FWMARK,	/* netfilter mark */
	FRA_FLOW,	/* flow/class id */
	FRA_UNUSED6,
	FRA_UNUSED7,
	FRA_UNUSED8,
	FRA_TABLE,	/* Extended table id */
	FRA_FWMASK,	/* mask for netfilter mark */
	__FRA_MAX
};

#define FRA_MAX (__FRA_MAX - 1)

enum
{
	FR_ACT_UNSPEC,
	FR_ACT_TO_TBL,		/* Pass to fixed table */
	FR_ACT_RES1,
	FR_ACT_RES2,
	FR_ACT_RES3,
	FR_ACT_RES4,
	FR_ACT_BLACKHOLE,	/* Drop without notification */
	FR_ACT_UNREACHABLE,	/* Drop with ENETUNREACH */
	FR_ACT_PROHIBIT,	/* Drop with EACCES */
	__FR_ACT_MAX,
};

#define FR_ACT_MAX (__FR_ACT_MAX - 1)

#endif
