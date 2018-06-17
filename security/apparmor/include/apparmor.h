/*
 * AppArmor security module
 *
 * This file contains AppArmor basic global
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __APPARMOR_H
#define __APPARMOR_H

#include <linux/types.h>

/*
 * Class of mediation types in the AppArmor policy db
 */
#define AA_CLASS_ENTRY		0
#define AA_CLASS_UNKNOWN	1
#define AA_CLASS_FILE		2
#define AA_CLASS_CAP		3
#define AA_CLASS_NET_COMPAT	4
#define AA_CLASS_RLIMITS	5
#define AA_CLASS_DOMAIN		6
#define AA_CLASS_MOUNT		7
#define AA_CLASS_PTRACE		9
#define AA_CLASS_SIGNAL		10
#define AA_CLASS_NET		14
#define AA_CLASS_LABEL		16

#define AA_CLASS_LAST		AA_CLASS_LABEL

/* Control parameters settable through module/boot flags */
extern enum audit_mode aa_g_audit;
extern bool aa_g_audit_header;
extern bool aa_g_debug;
extern bool aa_g_hash_policy;
extern bool aa_g_lock_policy;
extern bool aa_g_logsyscall;
extern bool aa_g_paranoid_load;
extern unsigned int aa_g_path_max;

#endif /* __APPARMOR_H */
