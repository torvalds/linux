/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPLAT_CTL_H
#define _SPLAT_CTL_H

#include <linux/types.h>

/*
 * Contains shared definitions for both user space and kernel space.  To
 * ensure 32-bit/64-bit interoperability over ioctl()'s only types with
 * fixed sizes can be used.
 */
#define SPLAT_NAME			"splatctl"
#define SPLAT_DEV			"/dev/splatctl"

#define SPLAT_NAME_SIZE			20
#define SPLAT_DESC_SIZE			60

typedef struct splat_user {
	char name[SPLAT_NAME_SIZE];	/* Short name */
	char desc[SPLAT_DESC_SIZE];	/* Short description */
	__u32 id;			/* Unique numeric id */
} splat_user_t;

#define	SPLAT_CFG_MAGIC			0x15263748U
typedef struct splat_cfg {
	__u32 cfg_magic;		/* Unique magic */
	__u32 cfg_cmd;			/* Configure command */
	__s32 cfg_arg1;			/* Configure command arg 1 */
	__s32 cfg_rc1;			/* Configure response 1 */
	union {
		struct {
			__u32 size;
			splat_user_t descs[0];
		} splat_subsystems;
		struct {
			__u32 size;
			splat_user_t descs[0];
		} splat_tests;
	} cfg_data;
} splat_cfg_t;

#define	SPLAT_CMD_MAGIC			0x9daebfc0U
typedef struct splat_cmd {
	__u32 cmd_magic;		/* Unique magic */
	__u32 cmd_subsystem;		/* Target subsystem */
	__u32 cmd_test;			/* Subsystem test */
	__u32 cmd_data_size;		/* Opaque data size */
	char cmd_data_str[0];		/* Opaque data region */
} splat_cmd_t;

/* Valid ioctls */
#define SPLAT_CFG			_IOWR('f', 101, splat_cfg_t)
#define SPLAT_CMD			_IOWR('f', 102, splat_cmd_t)

/* Valid configuration commands */
#define SPLAT_CFG_BUFFER_CLEAR		0x001	/* Clear text buffer */
#define SPLAT_CFG_BUFFER_SIZE		0x002	/* Resize text buffer */
#define SPLAT_CFG_SUBSYSTEM_COUNT	0x101	/* Number of subsystem */
#define SPLAT_CFG_SUBSYSTEM_LIST	0x102	/* List of N subsystems */
#define SPLAT_CFG_TEST_COUNT		0x201	/* Number of tests */
#define SPLAT_CFG_TEST_LIST		0x202	/* List of N tests */

/*
 * Valid subsystem and test commands are defined in each subsystem as
 * SPLAT_SUBSYSTEM_*.  We do need to be careful to avoid collisions, the
 * currently defined subsystems are as follows:
 */
#define SPLAT_SUBSYSTEM_KMEM		0x0100
#define SPLAT_SUBSYSTEM_TASKQ		0x0200
#define SPLAT_SUBSYSTEM_KRNG		0x0300
#define SPLAT_SUBSYSTEM_MUTEX		0x0400
#define SPLAT_SUBSYSTEM_CONDVAR		0x0500
#define SPLAT_SUBSYSTEM_THREAD		0x0600
#define SPLAT_SUBSYSTEM_RWLOCK		0x0700
#define SPLAT_SUBSYSTEM_TIME		0x0800
#define SPLAT_SUBSYSTEM_VNODE		0x0900
#define SPLAT_SUBSYSTEM_KOBJ		0x0a00
#define SPLAT_SUBSYSTEM_ATOMIC		0x0b00
#define SPLAT_SUBSYSTEM_LIST		0x0c00
#define SPLAT_SUBSYSTEM_GENERIC		0x0d00
#define SPLAT_SUBSYSTEM_CRED		0x0e00
#define SPLAT_SUBSYSTEM_ZLIB		0x0f00
#define SPLAT_SUBSYSTEM_LINUX		0x1000
#define SPLAT_SUBSYSTEM_UNKNOWN		0xff00

#endif /* _SPLAT_CTL_H */
