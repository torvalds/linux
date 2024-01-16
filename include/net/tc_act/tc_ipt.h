/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_TC_IPT_H
#define __NET_TC_IPT_H

#include <net/act_api.h>

struct xt_entry_target;

struct tcf_ipt {
	struct tc_action	common;
	u32			tcfi_hook;
	char			*tcfi_tname;
	struct xt_entry_target	*tcfi_t;
};
#define to_ipt(a) ((struct tcf_ipt *)a)

#endif /* __NET_TC_IPT_H */
