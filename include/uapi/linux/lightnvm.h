/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2015 CNEX Labs.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 */

#ifndef _UAPI_LINUX_LIGHTNVM_H
#define _UAPI_LINUX_LIGHTNVM_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/ioctl.h>
#else /* __KERNEL__ */
#include <stdio.h>
#include <sys/ioctl.h>
#define DISK_NAME_LEN 32
#endif /* __KERNEL__ */

#include <linux/types.h>
#include <linux/ioctl.h>

#define NVM_TTYPE_NAME_MAX 48
#define NVM_TTYPE_MAX 63
#define NVM_MMTYPE_LEN 8

#define NVM_CTRL_FILE "/dev/lightnvm/control"

struct nvm_ioctl_info_tgt {
	__u32 version[3];
	__u32 reserved;
	char tgtname[NVM_TTYPE_NAME_MAX];
};

struct nvm_ioctl_info {
	__u32 version[3];	/* in/out - major, minor, patch */
	__u16 tgtsize;		/* number of targets */
	__u16 reserved16;	/* pad to 4K page */
	__u32 reserved[12];
	struct nvm_ioctl_info_tgt tgts[NVM_TTYPE_MAX];
};

enum {
	NVM_DEVICE_ACTIVE = 1 << 0,
};

struct nvm_ioctl_device_info {
	char devname[DISK_NAME_LEN];
	char bmname[NVM_TTYPE_NAME_MAX];
	__u32 bmversion[3];
	__u32 flags;
	__u32 reserved[8];
};

struct nvm_ioctl_get_devices {
	__u32 nr_devices;
	__u32 reserved[31];
	struct nvm_ioctl_device_info info[31];
};

struct nvm_ioctl_create_simple {
	__u32 lun_begin;
	__u32 lun_end;
};

struct nvm_ioctl_create_extended {
	__u16 lun_begin;
	__u16 lun_end;
	__u16 op;
	__u16 rsv;
};

enum {
	NVM_CONFIG_TYPE_SIMPLE = 0,
	NVM_CONFIG_TYPE_EXTENDED = 1,
};

struct nvm_ioctl_create_conf {
	__u32 type;
	union {
		struct nvm_ioctl_create_simple s;
		struct nvm_ioctl_create_extended e;
	};
};

enum {
	NVM_TARGET_FACTORY = 1 << 0,	/* Init target in factory mode */
};

struct nvm_ioctl_create {
	char dev[DISK_NAME_LEN];		/* open-channel SSD device */
	char tgttype[NVM_TTYPE_NAME_MAX];	/* target type name */
	char tgtname[DISK_NAME_LEN];		/* dev to expose target as */

	__u32 flags;

	struct nvm_ioctl_create_conf conf;
};

struct nvm_ioctl_remove {
	char tgtname[DISK_NAME_LEN];

	__u32 flags;
};

struct nvm_ioctl_dev_init {
	char dev[DISK_NAME_LEN];		/* open-channel SSD device */
	char mmtype[NVM_MMTYPE_LEN];		/* register to media manager */

	__u32 flags;
};

enum {
	NVM_FACTORY_ERASE_ONLY_USER	= 1 << 0, /* erase only blocks used as
						   * host blks or grown blks */
	NVM_FACTORY_RESET_HOST_BLKS	= 1 << 1, /* remove host blk marks */
	NVM_FACTORY_RESET_GRWN_BBLKS	= 1 << 2, /* remove grown blk marks */
	NVM_FACTORY_NR_BITS		= 1 << 3, /* stops here */
};

struct nvm_ioctl_dev_factory {
	char dev[DISK_NAME_LEN];

	__u32 flags;
};

struct nvm_user_vio {
	__u8 opcode;
	__u8 flags;
	__u16 control;
	__u16 nppas;
	__u16 rsvd;
	__u64 metadata;
	__u64 addr;
	__u64 ppa_list;
	__u32 metadata_len;
	__u32 data_len;
	__u64 status;
	__u32 result;
	__u32 rsvd3[3];
};

struct nvm_passthru_vio {
	__u8 opcode;
	__u8 flags;
	__u8 rsvd[2];
	__u32 nsid;
	__u32 cdw2;
	__u32 cdw3;
	__u64 metadata;
	__u64 addr;
	__u32 metadata_len;
	__u32 data_len;
	__u64 ppa_list;
	__u16 nppas;
	__u16 control;
	__u32 cdw13;
	__u32 cdw14;
	__u32 cdw15;
	__u64 status;
	__u32 result;
	__u32 timeout_ms;
};

/* The ioctl type, 'L', 0x20 - 0x2F documented in ioctl-number.txt */
enum {
	/* top level cmds */
	NVM_INFO_CMD = 0x20,
	NVM_GET_DEVICES_CMD,

	/* device level cmds */
	NVM_DEV_CREATE_CMD,
	NVM_DEV_REMOVE_CMD,

	/* Init a device to support LightNVM media managers */
	NVM_DEV_INIT_CMD,

	/* Factory reset device */
	NVM_DEV_FACTORY_CMD,

	/* Vector user I/O */
	NVM_DEV_VIO_ADMIN_CMD = 0x41,
	NVM_DEV_VIO_CMD = 0x42,
	NVM_DEV_VIO_USER_CMD = 0x43,
};

#define NVM_IOCTL 'L' /* 0x4c */

#define NVM_INFO		_IOWR(NVM_IOCTL, NVM_INFO_CMD, \
						struct nvm_ioctl_info)
#define NVM_GET_DEVICES		_IOR(NVM_IOCTL, NVM_GET_DEVICES_CMD, \
						struct nvm_ioctl_get_devices)
#define NVM_DEV_CREATE		_IOW(NVM_IOCTL, NVM_DEV_CREATE_CMD, \
						struct nvm_ioctl_create)
#define NVM_DEV_REMOVE		_IOW(NVM_IOCTL, NVM_DEV_REMOVE_CMD, \
						struct nvm_ioctl_remove)
#define NVM_DEV_INIT		_IOW(NVM_IOCTL, NVM_DEV_INIT_CMD, \
						struct nvm_ioctl_dev_init)
#define NVM_DEV_FACTORY		_IOW(NVM_IOCTL, NVM_DEV_FACTORY_CMD, \
						struct nvm_ioctl_dev_factory)

#define NVME_NVM_IOCTL_IO_VIO		_IOWR(NVM_IOCTL, NVM_DEV_VIO_USER_CMD, \
						struct nvm_passthru_vio)
#define NVME_NVM_IOCTL_ADMIN_VIO	_IOWR(NVM_IOCTL, NVM_DEV_VIO_ADMIN_CMD,\
						struct nvm_passthru_vio)
#define NVME_NVM_IOCTL_SUBMIT_VIO	_IOWR(NVM_IOCTL, NVM_DEV_VIO_CMD,\
						struct nvm_user_vio)

#define NVM_VERSION_MAJOR	1
#define NVM_VERSION_MINOR	0
#define NVM_VERSION_PATCHLEVEL	0

#endif
