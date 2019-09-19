/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#ifndef __LINUX_TC_MPLS_H
#define __LINUX_TC_MPLS_H

#include <linux/pkt_cls.h>

#define TCA_MPLS_ACT_POP	1
#define TCA_MPLS_ACT_PUSH	2
#define TCA_MPLS_ACT_MODIFY	3
#define TCA_MPLS_ACT_DEC_TTL	4

struct tc_mpls {
	tc_gen;		/* generic TC action fields. */
	int m_action;	/* action of type TCA_MPLS_ACT_*. */
};

enum {
	TCA_MPLS_UNSPEC,
	TCA_MPLS_TM,	/* struct tcf_t; time values associated with action. */
	TCA_MPLS_PARMS,	/* struct tc_mpls; action type and general TC fields. */
	TCA_MPLS_PAD,
	TCA_MPLS_PROTO,	/* be16; eth_type of pushed or next (for pop) header. */
	TCA_MPLS_LABEL,	/* u32; MPLS label. Lower 20 bits are used. */
	TCA_MPLS_TC,	/* u8; MPLS TC field. Lower 3 bits are used. */
	TCA_MPLS_TTL,	/* u8; MPLS TTL field. Must not be 0. */
	TCA_MPLS_BOS,	/* u8; MPLS BOS field. Either 1 or 0. */
	__TCA_MPLS_MAX,
};
#define TCA_MPLS_MAX (__TCA_MPLS_MAX - 1)

#endif
