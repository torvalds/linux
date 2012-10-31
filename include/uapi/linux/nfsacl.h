/*
 * File: linux/nfsacl.h
 *
 * (C) 2003 Andreas Gruenbacher <agruen@suse.de>
 */
#ifndef _UAPI__LINUX_NFSACL_H
#define _UAPI__LINUX_NFSACL_H

#define NFS_ACL_PROGRAM	100227

#define ACLPROC2_GETACL		1
#define ACLPROC2_SETACL		2
#define ACLPROC2_GETATTR	3
#define ACLPROC2_ACCESS		4

#define ACLPROC3_GETACL		1
#define ACLPROC3_SETACL		2


/* Flags for the getacl/setacl mode */
#define NFS_ACL			0x0001
#define NFS_ACLCNT		0x0002
#define NFS_DFACL		0x0004
#define NFS_DFACLCNT		0x0008

/* Flag for Default ACL entries */
#define NFS_ACL_DEFAULT		0x1000

#endif /* _UAPI__LINUX_NFSACL_H */
