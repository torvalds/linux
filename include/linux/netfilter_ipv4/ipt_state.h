#ifndef _IPT_STATE_H
#define _IPT_STATE_H

/* Backwards compatibility for old userspace */

#include <linux/netfilter/xt_state.h>

#define IPT_STATE_BIT		XT_STATE_BIT
#define IPT_STATE_INVALID	XT_STATE_INVALID

#define IPT_STATE_UNTRACKED	XT_STATE_UNTRACKED

#define ipt_state_info		xt_state_info

#endif /*_IPT_STATE_H*/
