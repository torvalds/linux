/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Upcall description for nfsdcld communication
 *
 * Copyright (c) 2012 Red Hat, Inc.
 * Author(s): Jeff Layton <jlayton@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _NFSD_CLD_H
#define _NFSD_CLD_H

#include <linux/types.h>

/* latest upcall version available */
#define CLD_UPCALL_VERSION 2

/* defined by RFC3530 */
#define NFS4_OPAQUE_LIMIT 1024

#ifndef SHA256_DIGEST_SIZE
#define SHA256_DIGEST_SIZE      32
#endif

enum cld_command {
	Cld_Create,		/* create a record for this cm_id */
	Cld_Remove,		/* remove record of this cm_id */
	Cld_Check,		/* is this cm_id allowed? */
	Cld_GraceDone,		/* grace period is complete */
	Cld_GraceStart,		/* grace start (upload client records) */
	Cld_GetVersion,		/* query max supported upcall version */
};

/* representation of long-form NFSv4 client ID */
struct cld_name {
	__u16		cn_len;				/* length of cm_id */
	unsigned char	cn_id[NFS4_OPAQUE_LIMIT];	/* client-provided */
} __attribute__((packed));

/* sha256 hash of the kerberos principal */
struct cld_princhash {
	__u8		cp_len;				/* length of cp_data */
	unsigned char	cp_data[SHA256_DIGEST_SIZE];	/* hash of principal */
} __attribute__((packed));

struct cld_clntinfo {
	struct cld_name		cc_name;
	struct cld_princhash	cc_princhash;
} __attribute__((packed));

/* message struct for communication with userspace */
struct cld_msg {
	__u8		cm_vers;		/* upcall version */
	__u8		cm_cmd;			/* upcall command */
	__s16		cm_status;		/* return code */
	__u32		cm_xid;			/* transaction id */
	union {
		__s64		cm_gracetime;	/* grace period start time */
		struct cld_name	cm_name;
		__u8		cm_version;	/* for getting max version */
	} __attribute__((packed)) cm_u;
} __attribute__((packed));

/* version 2 message can include hash of kerberos principal */
struct cld_msg_v2 {
	__u8		cm_vers;		/* upcall version */
	__u8		cm_cmd;			/* upcall command */
	__s16		cm_status;		/* return code */
	__u32		cm_xid;			/* transaction id */
	union {
		struct cld_name	cm_name;
		__u8		cm_version;	/* for getting max version */
		struct cld_clntinfo cm_clntinfo; /* name & princ hash */
	} __attribute__((packed)) cm_u;
} __attribute__((packed));

struct cld_msg_hdr {
	__u8		cm_vers;		/* upcall version */
	__u8		cm_cmd;			/* upcall command */
	__s16		cm_status;		/* return code */
	__u32		cm_xid;			/* transaction id */
} __attribute__((packed));

#endif /* !_NFSD_CLD_H */
