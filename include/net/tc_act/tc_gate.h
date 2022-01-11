/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2020 NXP */

#ifndef __NET_TC_GATE_H
#define __NET_TC_GATE_H

#include <net/act_api.h>
#include <linux/tc_act/tc_gate.h>

struct action_gate_entry {
	u8			gate_state;
	u32			interval;
	s32			ipv;
	s32			maxoctets;
};

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

static inline bool is_tcf_gate(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_GATE)
		return true;
#endif
	return false;
}

static inline s32 tcf_gate_prio(const struct tc_action *a)
{
	s32 tcfg_prio;

	tcfg_prio = to_gate(a)->param.tcfg_priority;

	return tcfg_prio;
}

static inline u64 tcf_gate_basetime(const struct tc_action *a)
{
	u64 tcfg_basetime;

	tcfg_basetime = to_gate(a)->param.tcfg_basetime;

	return tcfg_basetime;
}

static inline u64 tcf_gate_cycletime(const struct tc_action *a)
{
	u64 tcfg_cycletime;

	tcfg_cycletime = to_gate(a)->param.tcfg_cycletime;

	return tcfg_cycletime;
}

static inline u64 tcf_gate_cycletimeext(const struct tc_action *a)
{
	u64 tcfg_cycletimeext;

	tcfg_cycletimeext = to_gate(a)->param.tcfg_cycletime_ext;

	return tcfg_cycletimeext;
}

static inline u32 tcf_gate_num_entries(const struct tc_action *a)
{
	u32 num_entries;

	num_entries = to_gate(a)->param.num_entries;

	return num_entries;
}

static inline struct action_gate_entry
			*tcf_gate_get_list(const struct tc_action *a)
{
	struct action_gate_entry *oe;
	struct tcf_gate_params *p;
	struct tcfg_gate_entry *entry;
	u32 num_entries;
	int i = 0;

	p = &to_gate(a)->param;
	num_entries = p->num_entries;

	list_for_each_entry(entry, &p->entries, list)
		i++;

	if (i != num_entries)
		return NULL;

	oe = kcalloc(num_entries, sizeof(*oe), GFP_ATOMIC);
	if (!oe)
		return NULL;

	i = 0;
	list_for_each_entry(entry, &p->entries, list) {
		oe[i].gate_state = entry->gate_state;
		oe[i].interval = entry->interval;
		oe[i].ipv = entry->ipv;
		oe[i].maxoctets = entry->maxoctets;
		i++;
	}

	return oe;
}
#endif
