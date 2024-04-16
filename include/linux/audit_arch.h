/* SPDX-License-Identifier: GPL-2.0-or-later */
/* audit_arch.h -- Arch layer specific support for audit
 *
 * Copyright 2021 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * Author: Richard Guy Briggs <rgb@redhat.com>
 */
#ifndef _LINUX_AUDIT_ARCH_H_
#define _LINUX_AUDIT_ARCH_H_

enum auditsc_class_t {
	AUDITSC_NATIVE = 0,
	AUDITSC_COMPAT,
	AUDITSC_OPEN,
	AUDITSC_OPENAT,
	AUDITSC_SOCKETCALL,
	AUDITSC_EXECVE,
	AUDITSC_OPENAT2,

	AUDITSC_NVALS /* count */
};

#endif
