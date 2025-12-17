/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */
#ifndef _SMB1PROTO_H
#define _SMB1PROTO_H

struct cifs_unix_set_info_args {
	__u64	ctime;
	__u64	atime;
	__u64	mtime;
	__u64	mode;
	kuid_t	uid;
	kgid_t	gid;
	dev_t	device;
};

#ifdef CONFIG_CIFS_ALLOW_INSECURE_LEGACY

/*
 * cifssmb.c
 */

/*
 * smb1ops.c
 */
extern struct smb_version_operations smb1_operations;
extern struct smb_version_values smb1_values;

/*
 * smb1transport.c
 */

#endif /* CONFIG_CIFS_ALLOW_INSECURE_LEGACY */
#endif /* _SMB1PROTO_H */
