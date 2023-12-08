/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_POLICY_H
#define _XT_POLICY_H

#include <linux/netfilter.h>
#include <linux/types.h>
#include <linux/in.h>
#include <linux/in6.h>

#define XT_POLICY_MAX_ELEM	4

enum xt_policy_flags {
	XT_POLICY_MATCH_IN	= 0x1,
	XT_POLICY_MATCH_OUT	= 0x2,
	XT_POLICY_MATCH_NONE	= 0x4,
	XT_POLICY_MATCH_STRICT	= 0x8,
};

enum xt_policy_modes {
	XT_POLICY_MODE_TRANSPORT,
	XT_POLICY_MODE_TUNNEL
};

struct xt_policy_spec {
	__u8	saddr:1,
			daddr:1,
			proto:1,
			mode:1,
			spi:1,
			reqid:1;
};

#ifndef __KERNEL__
union xt_policy_addr {
	struct in_addr	a4;
	struct in6_addr	a6;
};
#endif

struct xt_policy_elem {
	union {
#ifdef __KERNEL__
		struct {
			union nf_inet_addr saddr;
			union nf_inet_addr smask;
			union nf_inet_addr daddr;
			union nf_inet_addr dmask;
		};
#else
		struct {
			union xt_policy_addr saddr;
			union xt_policy_addr smask;
			union xt_policy_addr daddr;
			union xt_policy_addr dmask;
		};
#endif
	};
	__be32			spi;
	__u32		reqid;
	__u8		proto;
	__u8		mode;

	struct xt_policy_spec	match;
	struct xt_policy_spec	invert;
};

struct xt_policy_info {
	struct xt_policy_elem pol[XT_POLICY_MAX_ELEM];
	__u16 flags;
	__u16 len;
};

#endif /* _XT_POLICY_H */
