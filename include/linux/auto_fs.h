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


#ifndef _LINUX_AUTO_FS_H
#define _LINUX_AUTO_FS_H

#include <linux/types.h>
#ifdef __KERNEL__
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

/* This file describes autofs v3 */
#define AUTOFS_PROTO_VERSION	3

/* Range of protocol versions defined */
#define AUTOFS_MAX_PROTO_VERSION	AUTOFS_PROTO_VERSION
#define AUTOFS_MIN_PROTO_VERSION	AUTOFS_PROTO_VERSION

/*
 * The wait_queue_token (autofs_wqt_t) is part of a structure which is passed
 * back to the kernel via ioctl from userspace. On architectures where 32- and
 * 64-bit userspace binaries can be executed it's important that the size of
 * autofs_wqt_t stays constant between 32- and 64-bit Linux kernels so that we
 * do not break the binary ABI interface by changing the structure size.
 */
#if defined(__ia64__) || defined(__alpha__) /* pure 64bit architectures */
typedef unsigned long autofs_wqt_t;
#else
typedef unsigned int autofs_wqt_t;
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

#endif /* _LINUX_AUTO_FS_H */
