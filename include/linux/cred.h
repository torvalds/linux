/* Credentials management
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_CRED_H
#define _LINUX_CRED_H

#define get_current_user()	(get_uid(current->user))

#define task_uid(task)		((task)->uid)
#define task_gid(task)		((task)->gid)
#define task_euid(task)		((task)->euid)
#define task_egid(task)		((task)->egid)

#define current_uid()		(current->uid)
#define current_gid()		(current->gid)
#define current_euid()		(current->euid)
#define current_egid()		(current->egid)
#define current_suid()		(current->suid)
#define current_sgid()		(current->sgid)
#define current_fsuid()		(current->fsuid)
#define current_fsgid()		(current->fsgid)
#define current_cap()		(current->cap_effective)

#define current_uid_gid(_uid, _gid)		\
do {						\
	*(_uid) = current->uid;			\
	*(_gid) = current->gid;			\
} while(0)

#define current_euid_egid(_uid, _gid)		\
do {						\
	*(_uid) = current->euid;		\
	*(_gid) = current->egid;		\
} while(0)

#define current_fsuid_fsgid(_uid, _gid)		\
do {						\
	*(_uid) = current->fsuid;		\
	*(_gid) = current->fsgid;		\
} while(0)

#endif /* _LINUX_CRED_H */
