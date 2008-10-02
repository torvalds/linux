/* -*- c -*-
 * linux/include/linux/auto_fs4.h
 *
 * Copyright 1999-2000 Jeremy Fitzhardinge <jeremy@goop.org>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 */

#ifndef _LINUX_AUTO_FS4_H
#define _LINUX_AUTO_FS4_H

/* Include common v3 definitions */
#include <linux/auto_fs.h>

/* autofs v4 definitions */
#undef AUTOFS_PROTO_VERSION
#undef AUTOFS_MIN_PROTO_VERSION
#undef AUTOFS_MAX_PROTO_VERSION

#define AUTOFS_PROTO_VERSION		5
#define AUTOFS_MIN_PROTO_VERSION	3
#define AUTOFS_MAX_PROTO_VERSION	5

#define AUTOFS_PROTO_SUBVERSION		0

/* Mask for expire behaviour */
#define AUTOFS_EXP_IMMEDIATE		1
#define AUTOFS_EXP_LEAVES		2

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

#define AUTOFS_IOC_EXPIRE_MULTI		_IOW(0x93,0x66,int)
#define AUTOFS_IOC_EXPIRE_INDIRECT	AUTOFS_IOC_EXPIRE_MULTI
#define AUTOFS_IOC_EXPIRE_DIRECT	AUTOFS_IOC_EXPIRE_MULTI
#define AUTOFS_IOC_PROTOSUBVER		_IOR(0x93,0x67,int)
#define AUTOFS_IOC_ASKUMOUNT		_IOR(0x93,0x70,int)


#endif /* _LINUX_AUTO_FS4_H */
