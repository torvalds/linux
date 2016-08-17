/*
 *  ZPIOS is a heavily modified version of the original PIOS test code.
 *  It is designed to have the test code running in the Linux kernel
 *  against ZFS while still being flexibly controled from user space.
 *
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  LLNL-CODE-403049
 *
 *  Original PIOS Test Code
 *  Copyright (C) 2004 Cluster File Systems, Inc.
 *  Written by Peter Braam <braam@clusterfs.com>
 *             Atul Vidwansa <atul@clusterfs.com>
 *             Milind Dumbare <milind@clusterfs.com>
 *
 *  This file is part of ZFS on Linux.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  ZPIOS is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  ZPIOS is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ZPIOS.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ZPIOS_INTERNAL_H
#define	_ZPIOS_INTERNAL_H

#include "zpios-ctl.h"

#define	OBJ_SIZE	64

struct run_args;

typedef struct dmu_obj {
	objset_t *os;
	uint64_t obj;
} dmu_obj_t;

/* thread doing the IO data */
typedef struct thread_data {
	struct run_args *run_args;
	int thread_no;
	int rc;
	zpios_stats_t stats;
	kmutex_t lock;
} thread_data_t;

/* region for IO data */
typedef struct zpios_region {
	__u64 wr_offset;
	__u64 rd_offset;
	__u64 init_offset;
	__u64 max_offset;
	dmu_obj_t obj;
	zpios_stats_t stats;
	kmutex_t lock;
} zpios_region_t;

/* arguments for one run */
typedef struct run_args {
	/* Config args */
	int id;
	char pool[ZPIOS_NAME_SIZE];
	__u64 chunk_size;
	__u32 thread_count;
	__u32 region_count;
	__u64 region_size;
	__u64 offset;
	__u32 region_noise;
	__u32 chunk_noise;
	__u32 thread_delay;
	__u32 flags;
	char pre[ZPIOS_PATH_SIZE];
	char post[ZPIOS_PATH_SIZE];
	char log[ZPIOS_PATH_SIZE];

	/* Control data */
	objset_t *os;
	wait_queue_head_t waitq;
	volatile uint64_t threads_done;
	kmutex_t lock_work;
	kmutex_t lock_ctl;
	__u32 region_next;

	/* Results data */
	struct file *file;
	zpios_stats_t stats;

	thread_data_t **threads;
	zpios_region_t regions[0]; /* Must be last element */
} run_args_t;

#define	ZPIOS_INFO_BUFFER_SIZE		65536
#define	ZPIOS_INFO_BUFFER_REDZONE	1024

typedef struct zpios_info {
	spinlock_t info_lock;
	int info_size;
	char *info_buffer;
	char *info_head;	/* Internal kernel use only */
} zpios_info_t;

#endif /* _ZPIOS_INTERNAL_H */
