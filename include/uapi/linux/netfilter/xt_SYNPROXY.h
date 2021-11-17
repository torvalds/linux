/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_SYNPROXY_H
#define _XT_SYNPROXY_H

#include <linux/netfilter/nf_synproxy.h>

#define XT_SYNPROXY_OPT_MSS		NF_SYNPROXY_OPT_MSS
#define XT_SYNPROXY_OPT_WSCALE		NF_SYNPROXY_OPT_WSCALE
#define XT_SYNPROXY_OPT_SACK_PERM	NF_SYNPROXY_OPT_SACK_PERM
#define XT_SYNPROXY_OPT_TIMESTAMP	NF_SYNPROXY_OPT_TIMESTAMP
#define XT_SYNPROXY_OPT_ECN		NF_SYNPROXY_OPT_ECN

#define xt_synproxy_info		nf_synproxy_info

#endif /* _XT_SYNPROXY_H */
