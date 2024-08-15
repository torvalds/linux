/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_TCPMSS_H
#define _XT_TCPMSS_H

#include <linux/types.h>

struct xt_tcpmss_info {
	__u16 mss;
};

#define XT_TCPMSS_CLAMP_PMTU 0xffff

#endif /* _XT_TCPMSS_H */
