/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_TEE_TARGET_H
#define _XT_TEE_TARGET_H

#include <linux/netfilter.h>

struct xt_tee_tginfo {
	union nf_inet_addr gw;
	char oif[16];

	/* used internally by the kernel */
	struct xt_tee_priv *priv __attribute__((aligned(8)));
};

#endif /* _XT_TEE_TARGET_H */
