/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Header file for iptables xt_AUDIT target
 *
 * (C) 2010-2011 Thomas Graf <tgraf@redhat.com>
 * (C) 2010-2011 Red Hat, Inc.
 */

#ifndef _XT_AUDIT_TARGET_H
#define _XT_AUDIT_TARGET_H

#include <linux/types.h>

enum {
	XT_AUDIT_TYPE_ACCEPT = 0,
	XT_AUDIT_TYPE_DROP,
	XT_AUDIT_TYPE_REJECT,
	__XT_AUDIT_TYPE_MAX,
};

#define XT_AUDIT_TYPE_MAX (__XT_AUDIT_TYPE_MAX - 1)

struct xt_audit_info {
	__u8 type; /* XT_AUDIT_TYPE_* */
};

#endif /* _XT_AUDIT_TARGET_H */
