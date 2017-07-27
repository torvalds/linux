/*
 * AppArmor security module
 *
 * This file contains AppArmor ipc mediation function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_IPC_H
#define __AA_IPC_H

#include <linux/sched.h>

struct aa_profile;

#define AA_PTRACE_TRACE		MAY_WRITE
#define AA_PTRACE_READ		MAY_READ
#define AA_MAY_BE_TRACED	AA_MAY_APPEND
#define AA_MAY_BE_READ		AA_MAY_CREATE
#define PTRACE_PERM_SHIFT	2

#define AA_PTRACE_PERM_MASK (AA_PTRACE_READ | AA_PTRACE_TRACE | \
			     AA_MAY_BE_READ | AA_MAY_BE_TRACED)

int aa_may_ptrace(struct aa_label *tracer, struct aa_label *tracee,
		  u32 request);

#endif /* __AA_IPC_H */
