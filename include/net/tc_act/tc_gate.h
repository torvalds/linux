/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2020 NXP */

#ifndef __NET_TC_GATE_H
#define __NET_TC_GATE_H

#include <net/act_api.h>
#include <linux/tc_act/tc_gate.h>

struct tcfg_gate_entry {
	int			index;
	u8			gate_state;
	u32			interval;
	s32			ipv;
	s32			maxoctets;
	struct list_head	list;
};

struct tcf_gate_params {
	s32			tcfg_priority;
	u64			tcfg_basetime;
	u64			tcfg_cycletime;
	u64			tcfg_cycletime_ext;
	u32			tcfg_flags;
	s32			tcfg_clockid;
	size_t			num_entries;
	struct list_head	entries;
};

#define GATE_ACT_GATE_OPEN	BIT(0)
#define GATE_ACT_PENDING	BIT(1)

struct tcf_gate {
	struct tc_action	common;
	struct tcf_gate_params	param;
	u8			current_gate_status;
	ktime_t			current_close_time;
	u32			current_entry_octets;
	s32			current_max_octets;
	struct tcfg_gate_entry	*next_entry;
	struct hrtimer		hitimer;
	enum tk_offsets		tk_offset;
};

#define to_gate(a) ((struct tcf_gate *)a)

#endif
