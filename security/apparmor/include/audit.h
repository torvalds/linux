/*
 * AppArmor security module
 *
 * This file contains AppArmor auditing function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_AUDIT_H
#define __AA_AUDIT_H

#include <linux/audit.h>
#include <linux/fs.h>
#include <linux/lsm_audit.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "file.h"
#include "label.h"

extern const char *const audit_mode_names[];
#define AUDIT_MAX_INDEX 5
enum audit_mode {
	AUDIT_NORMAL,		/* follow normal auditing of accesses */
	AUDIT_QUIET_DENIED,	/* quiet all denied access messages */
	AUDIT_QUIET,		/* quiet all messages */
	AUDIT_NOQUIET,		/* do not quiet audit messages */
	AUDIT_ALL		/* audit all accesses */
};

enum audit_type {
	AUDIT_APPARMOR_AUDIT,
	AUDIT_APPARMOR_ALLOWED,
	AUDIT_APPARMOR_DENIED,
	AUDIT_APPARMOR_HINT,
	AUDIT_APPARMOR_STATUS,
	AUDIT_APPARMOR_ERROR,
	AUDIT_APPARMOR_KILL,
	AUDIT_APPARMOR_AUTO
};

#define OP_NULL NULL

#define OP_SYSCTL "sysctl"
#define OP_CAPABLE "capable"

#define OP_UNLINK "unlink"
#define OP_MKDIR "mkdir"
#define OP_RMDIR "rmdir"
#define OP_MKNOD "mknod"
#define OP_TRUNC "truncate"
#define OP_LINK "link"
#define OP_SYMLINK "symlink"
#define OP_RENAME_SRC "rename_src"
#define OP_RENAME_DEST "rename_dest"
#define OP_CHMOD "chmod"
#define OP_CHOWN "chown"
#define OP_GETATTR "getattr"
#define OP_OPEN "open"

#define OP_FRECEIVE "file_receive"
#define OP_FPERM "file_perm"
#define OP_FLOCK "file_lock"
#define OP_FMMAP "file_mmap"
#define OP_FMPROT "file_mprotect"
#define OP_INHERIT "file_inherit"

#define OP_PIVOTROOT "pivotroot"
#define OP_MOUNT "mount"
#define OP_UMOUNT "umount"

#define OP_CREATE "create"
#define OP_POST_CREATE "post_create"
#define OP_BIND "bind"
#define OP_CONNECT "connect"
#define OP_LISTEN "listen"
#define OP_ACCEPT "accept"
#define OP_SENDMSG "sendmsg"
#define OP_RECVMSG "recvmsg"
#define OP_GETSOCKNAME "getsockname"
#define OP_GETPEERNAME "getpeername"
#define OP_GETSOCKOPT "getsockopt"
#define OP_SETSOCKOPT "setsockopt"
#define OP_SHUTDOWN "socket_shutdown"

#define OP_PTRACE "ptrace"
#define OP_SIGNAL "signal"

#define OP_EXEC "exec"

#define OP_CHANGE_HAT "change_hat"
#define OP_CHANGE_PROFILE "change_profile"
#define OP_CHANGE_ONEXEC "change_onexec"
#define OP_STACK "stack"
#define OP_STACK_ONEXEC "stack_onexec"

#define OP_SETPROCATTR "setprocattr"
#define OP_SETRLIMIT "setrlimit"

#define OP_PROF_REPL "profile_replace"
#define OP_PROF_LOAD "profile_load"
#define OP_PROF_RM "profile_remove"


struct apparmor_audit_data {
	int error;
	int type;
	const char *op;
	struct aa_label *label;
	const char *name;
	const char *info;
	u32 request;
	u32 denied;
	union {
		/* these entries require a custom callback fn */
		struct {
			struct aa_label *peer;
			union {
				struct {
					const char *target;
					kuid_t ouid;
				} fs;
				struct {
					int rlim;
					unsigned long max;
				} rlim;
				int signal;
			};
		};
		struct {
			struct aa_profile *profile;
			const char *ns;
			long pos;
		} iface;
		struct {
			const char *src_name;
			const char *type;
			const char *trans;
			const char *data;
			unsigned long flags;
		} mnt;
	};
};

/* macros for dealing with  apparmor_audit_data structure */
#define aad(SA) ((SA)->apparmor_audit_data)
#define DEFINE_AUDIT_DATA(NAME, T, X)					\
	/* TODO: cleanup audit init so we don't need _aad = {0,} */	\
	struct apparmor_audit_data NAME ## _aad = { .op = (X), };	\
	struct common_audit_data NAME =					\
	{								\
	.type = (T),							\
	.u.tsk = NULL,							\
	};								\
	NAME.apparmor_audit_data = &(NAME ## _aad)

void aa_audit_msg(int type, struct common_audit_data *sa,
		  void (*cb) (struct audit_buffer *, void *));
int aa_audit(int type, struct aa_profile *profile, struct common_audit_data *sa,
	     void (*cb) (struct audit_buffer *, void *));

#define aa_audit_error(ERROR, SA, CB)				\
({								\
	aad((SA))->error = (ERROR);				\
	aa_audit_msg(AUDIT_APPARMOR_ERROR, (SA), (CB));		\
	aad((SA))->error;					\
})


static inline int complain_error(int error)
{
	if (error == -EPERM || error == -EACCES)
		return 0;
	return error;
}

#endif /* __AA_AUDIT_H */
