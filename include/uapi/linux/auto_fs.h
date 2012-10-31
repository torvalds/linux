/* -*- linux-c -*- ------------------------------------------------------- *
 *   
 * linux/include/linux/auto_fs.h
 *
 *   Copyright 1997 Transmeta Corporation - All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */


#ifndef _UAPI_LINUX_AUTO_FS_H
#define _UAPI_LINUX_AUTO_FS_H

#include <linux/types.h>
#ifndef __KERNEL__
#include <sys/ioctl.h>
#endif /* __KERNEL__ */


/* This file describes autofs v3 */
#define AUTOFS_PROTO_VERSION	3

/* Range of protocol versions defined */
#define AUTOFS_MAX_PROTO_VERSION	AUTOFS_PROTO_VERSION
#define AUTOFS_MIN_PROTO_VERSION	AUTOFS_PROTO_VERSION

/*
 * Architectures where both 32- and 64-bit binaries can be executed
 * on 64-bit kernels need this.  This keeps the structure format
 * uniform, and makes sure the wait_queue_token isn't too big to be
 * passed back down to the kernel.
 *
 * This assumes that on these architectures:
 * mode     32 bit    64 bit
 * -------------------------
 * int      32 bit    32 bit
 * long     32 bit    64 bit
 *
 * If so, 32-bit user-space code should be backwards compatible.
 */

#if defined(__sparc__) || defined(__mips__) || defined(__x86_64__) \
 || defined(__powerpc__) || defined(__s390__)
typedef unsigned int autofs_wqt_t;
#else
typedef unsigned long autofs_wqt_t;
#endif

/* Packet types */
#define autofs_ptype_missing	0	/* Missing entry (mount request) */
#define autofs_ptype_expire	1	/* Expire entry (umount request) */

struct autofs_packet_hdr {
	int proto_version;		/* Protocol version */
	int type;			/* Type of packet */
};

struct autofs_packet_missing {
	struct autofs_packet_hdr hdr;
        autofs_wqt_t wait_queue_token;
	int len;
	char name[NAME_MAX+1];
};	

/* v3 expire (via ioctl) */
struct autofs_packet_expire {
	struct autofs_packet_hdr hdr;
	int len;
	char name[NAME_MAX+1];
};

#define AUTOFS_IOC_READY      _IO(0x93,0x60)
#define AUTOFS_IOC_FAIL       _IO(0x93,0x61)
#define AUTOFS_IOC_CATATONIC  _IO(0x93,0x62)
#define AUTOFS_IOC_PROTOVER   _IOR(0x93,0x63,int)
#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(0x93,0x64,compat_ulong_t)
#define AUTOFS_IOC_SETTIMEOUT _IOWR(0x93,0x64,unsigned long)
#define AUTOFS_IOC_EXPIRE     _IOR(0x93,0x65,struct autofs_packet_expire)

#endif /* _UAPI_LINUX_AUTO_FS_H */
