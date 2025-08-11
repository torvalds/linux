/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor basic global
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 */

#ifndef __APPARMOR_H
#define __APPARMOR_H

#include <linux/types.h>

/*
 * Class of mediation types in the AppArmor policy db
 */
#define AA_CLASS_NONE		0
#define AA_CLASS_UNKNOWN	1
#define AA_CLASS_FILE		2
#define AA_CLASS_CAP		3
#define AA_CLASS_DEPRECATED	4
#define AA_CLASS_RLIMITS	5
#define AA_CLASS_DOMAIN		6
#define AA_CLASS_MOUNT		7
#define AA_CLASS_PTRACE		9
#define AA_CLASS_SIGNAL		10
#define AA_CLASS_XMATCH		11
#define AA_CLASS_NET		14
#define AA_CLASS_NETV9		15
#define AA_CLASS_LABEL		16
#define AA_CLASS_POSIX_MQUEUE	17
#define AA_CLASS_MODULE		19
#define AA_CLASS_DISPLAY_LSM	20
#define AA_CLASS_NS		21
#define AA_CLASS_IO_URING	22

#define AA_CLASS_X		31
#define AA_CLASS_DBUS		32

/* NOTE: if AA_CLASS_LAST > 63 need to update label->mediates */
#define AA_CLASS_LAST		AA_CLASS_DBUS

/* Control parameters settable through module/boot flags */
extern enum audit_mode aa_g_audit;
extern bool aa_g_audit_header;
extern int aa_g_debug;
extern bool aa_g_hash_policy;
extern bool aa_g_export_binary;
extern int aa_g_rawdata_compression_level;
extern bool aa_g_lock_policy;
extern bool aa_g_logsyscall;
extern bool aa_g_paranoid_load;
extern unsigned int aa_g_path_max;

#ifdef CONFIG_SECURITY_APPARMOR_EXPORT_BINARY
#define AA_MIN_CLEVEL zstd_min_clevel()
#define AA_MAX_CLEVEL zstd_max_clevel()
#define AA_DEFAULT_CLEVEL ZSTD_CLEVEL_DEFAULT
#else
#define AA_MIN_CLEVEL 0
#define AA_MAX_CLEVEL 0
#define AA_DEFAULT_CLEVEL 0
#endif /* CONFIG_SECURITY_APPARMOR_EXPORT_BINARY */


#endif /* __APPARMOR_H */
