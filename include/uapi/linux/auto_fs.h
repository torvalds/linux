/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 1997 Transmeta Corporation - All Rights Reserved
 * Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 * Copyright 2005-2006,2013,2017-2018 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef _UAPI_LINUX_AUTO_FS_H
#define _UAPI_LINUX_AUTO_FS_H

#include <linux/types.h>
#include <linux/limits.h>
#ifndef __KERNEL__
#include <sys/ioctl.h>
#endif /* __KERNEL__ */

#define AUTOFS_PROTO_VERSION		5
#define AUTOFS_MIN_PROTO_VERSION	3
#define AUTOFS_MAX_PROTO_VERSION	5

#define AUTOFS_PROTO_SUBVERSION		5

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

#define AUTOFS_IOCTL 0x93

enum {
	AUTOFS_IOC_READY_CMD = 0x60,
	AUTOFS_IOC_FAIL_CMD,
	AUTOFS_IOC_CATATONIC_CMD,
	AUTOFS_IOC_PROTOVER_CMD,
	AUTOFS_IOC_SETTIMEOUT_CMD,
	AUTOFS_IOC_EXPIRE_CMD,
};

#define AUTOFS_IOC_READY        _IO(AUTOFS_IOCTL, AUTOFS_IOC_READY_CMD)
#define AUTOFS_IOC_FAIL         _IO(AUTOFS_IOCTL, AUTOFS_IOC_FAIL_CMD)
#define AUTOFS_IOC_CATATONIC    _IO(AUTOFS_IOCTL, AUTOFS_IOC_CATATONIC_CMD)
#define AUTOFS_IOC_PROTOVER     _IOR(AUTOFS_IOCTL, \
				     AUTOFS_IOC_PROTOVER_CMD, int)
#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(AUTOFS_IOCTL, \
				      AUTOFS_IOC_SETTIMEOUT_CMD, \
				      compat_ulong_t)
#define AUTOFS_IOC_SETTIMEOUT   _IOWR(AUTOFS_IOCTL, \
				      AUTOFS_IOC_SETTIMEOUT_CMD, \
				      unsigned long)
#define AUTOFS_IOC_EXPIRE       _IOR(AUTOFS_IOCTL, \
				     AUTOFS_IOC_EXPIRE_CMD, \
				     struct autofs_packet_expire)

/* autofs version 4 and later definitions */

/* Mask for expire behaviour */
#define AUTOFS_EXP_NORMAL		0x00
#define AUTOFS_EXP_IMMEDIATE		0x01
#define AUTOFS_EXP_LEAVES		0x02
#define AUTOFS_EXP_FORCED		0x04

#define AUTOFS_TYPE_ANY			0U
#define AUTOFS_TYPE_INDIRECT		1U
#define AUTOFS_TYPE_DIRECT		2U
#define AUTOFS_TYPE_OFFSET		4U

static inline void set_autofs_type_indirect(unsigned int *type)
{
	*type = AUTOFS_TYPE_INDIRECT;
}

static inline unsigned int autofs_type_indirect(unsigned int type)
{
	return (type == AUTOFS_TYPE_INDIRECT);
}

static inline void set_autofs_type_direct(unsigned int *type)
{
	*type = AUTOFS_TYPE_DIRECT;
}

static inline unsigned int autofs_type_direct(unsigned int type)
{
	return (type == AUTOFS_TYPE_DIRECT);
}

static inline void set_autofs_type_offset(unsigned int *type)
{
	*type = AUTOFS_TYPE_OFFSET;
}

static inline unsigned int autofs_type_offset(unsigned int type)
{
	return (type == AUTOFS_TYPE_OFFSET);
}

static inline unsigned int autofs_type_trigger(unsigned int type)
{
	return (type == AUTOFS_TYPE_DIRECT || type == AUTOFS_TYPE_OFFSET);
}

/*
 * This isn't really a type as we use it to say "no type set" to
 * indicate we want to search for "any" mount in the
 * autofs_dev_ioctl_ismountpoint() device ioctl function.
 */
static inline void set_autofs_type_any(unsigned int *type)
{
	*type = AUTOFS_TYPE_ANY;
}

static inline unsigned int autofs_type_any(unsigned int type)
{
	return (type == AUTOFS_TYPE_ANY);
}

/* Daemon notification packet types */
enum autofs_notify {
	NFY_NONE,
	NFY_MOUNT,
	NFY_EXPIRE
};

/* Kernel protocol version 4 packet types */

/* Expire entry (umount request) */
#define autofs_ptype_expire_multi	2

/* Kernel protocol version 5 packet types */

/* Indirect mount missing and expire requests. */
#define autofs_ptype_missing_indirect	3
#define autofs_ptype_expire_indirect	4

/* Direct mount missing and expire requests */
#define autofs_ptype_missing_direct	5
#define autofs_ptype_expire_direct	6

/* v4 multi expire (via pipe) */
struct autofs_packet_expire_multi {
	struct autofs_packet_hdr hdr;
	autofs_wqt_t wait_queue_token;
	int len;
	char name[NAME_MAX+1];
};

union autofs_packet_union {
	struct autofs_packet_hdr hdr;
	struct autofs_packet_missing missing;
	struct autofs_packet_expire expire;
	struct autofs_packet_expire_multi expire_multi;
};

/* autofs v5 common packet struct */
struct autofs_v5_packet {
	struct autofs_packet_hdr hdr;
	autofs_wqt_t wait_queue_token;
	__u32 dev;
	__u64 ino;
	__u32 uid;
	__u32 gid;
	__u32 pid;
	__u32 tgid;
	__u32 len;
	char name[NAME_MAX+1];
};

typedef struct autofs_v5_packet autofs_packet_missing_indirect_t;
typedef struct autofs_v5_packet autofs_packet_expire_indirect_t;
typedef struct autofs_v5_packet autofs_packet_missing_direct_t;
typedef struct autofs_v5_packet autofs_packet_expire_direct_t;

union autofs_v5_packet_union {
	struct autofs_packet_hdr hdr;
	struct autofs_v5_packet v5_packet;
	autofs_packet_missing_indirect_t missing_indirect;
	autofs_packet_expire_indirect_t expire_indirect;
	autofs_packet_missing_direct_t missing_direct;
	autofs_packet_expire_direct_t expire_direct;
};

enum {
	AUTOFS_IOC_EXPIRE_MULTI_CMD = 0x66, /* AUTOFS_IOC_EXPIRE_CMD + 1 */
	AUTOFS_IOC_PROTOSUBVER_CMD,
	AUTOFS_IOC_ASKUMOUNT_CMD = 0x70, /* AUTOFS_DEV_IOCTL_VERSION_CMD - 1 */
};

#define AUTOFS_IOC_EXPIRE_MULTI		_IOW(AUTOFS_IOCTL, \
					     AUTOFS_IOC_EXPIRE_MULTI_CMD, int)
#define AUTOFS_IOC_PROTOSUBVER		_IOR(AUTOFS_IOCTL, \
					     AUTOFS_IOC_PROTOSUBVER_CMD, int)
#define AUTOFS_IOC_ASKUMOUNT		_IOR(AUTOFS_IOCTL, \
					     AUTOFS_IOC_ASKUMOUNT_CMD, int)

#endif /* _UAPI_LINUX_AUTO_FS_H */
