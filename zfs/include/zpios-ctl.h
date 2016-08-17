/*
 *  ZPIOS is a heavily modified version of the original PIOS test code.
 *  It is designed to have the test code running in the Linux kernel
 *  against ZFS while still being flexibly controlled from user space.
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
 *
 *  Copyright (c) 2015, Intel Corporation.
 */

#ifndef _ZPIOS_CTL_H
#define	_ZPIOS_CTL_H

/*
 * Contains shared definitions which both the userspace
 * and kernelspace portions of zpios must agree on.
 */
#ifndef _KERNEL
#include <stdint.h>
#endif

#define	ZPIOS_NAME			"zpios"
#define	ZPIOS_DEV			"/dev/zpios"

#define	DMU_IO				0x01

#define	DMU_WRITE			0x0001
#define	DMU_READ			0x0002
#define	DMU_VERIFY			0x0004
#define	DMU_REMOVE			0x0008
#define	DMU_FPP				0x0010
#define	DMU_WRITE_ZC			0x0020 /* Incompatible w/DMU_VERIFY */
#define	DMU_READ_ZC			0x0040 /* Incompatible w/DMU_VERIFY */
#define	DMU_WRITE_NOWAIT		0x0080
#define	DMU_READ_NOPF			0x0100

#define	ZPIOS_NAME_SIZE			16
#define	ZPIOS_PATH_SIZE			128

#define	PHASE_PRE_RUN			"pre-run"
#define	PHASE_PRE_CREATE		"pre-create"
#define	PHASE_PRE_WRITE			"pre-write"
#define	PHASE_PRE_READ			"pre-read"
#define	PHASE_PRE_REMOVE		"pre-remove"
#define	PHASE_POST_RUN			"post-run"
#define	PHASE_POST_CREATE		"post-create"
#define	PHASE_POST_WRITE		"post-write"
#define	PHASE_POST_READ			"post-read"
#define	PHASE_POST_REMOVE		"post-remove"

#define	ZPIOS_CFG_MAGIC			0x87237190U
typedef struct zpios_cfg {
	uint32_t cfg_magic;		/* Unique magic */
	int32_t cfg_cmd;		/* Config command */
	int32_t cfg_arg1;		/* Config command arg 1 */
	int32_t cfg_rc1;		/* Config response 1 */
} zpios_cfg_t;

typedef struct zpios_timespec {
	uint32_t ts_sec;
	uint32_t ts_nsec;
} zpios_timespec_t;

typedef struct zpios_time {
	zpios_timespec_t start;
	zpios_timespec_t stop;
	zpios_timespec_t delta;
} zpios_time_t;

typedef struct zpios_stats {
	zpios_time_t total_time;
	zpios_time_t cr_time;
	zpios_time_t rm_time;
	zpios_time_t wr_time;
	zpios_time_t rd_time;
	uint64_t wr_data;
	uint64_t wr_chunks;
	uint64_t rd_data;
	uint64_t rd_chunks;
} zpios_stats_t;

#define	ZPIOS_CMD_MAGIC			0x49715385U
typedef struct zpios_cmd {
	uint32_t cmd_magic;		/* Unique magic */
	uint32_t cmd_id;		/* Run ID */
	char cmd_pool[ZPIOS_NAME_SIZE];	/* Pool name */
	uint64_t cmd_chunk_size;	/* Chunk size */
	uint32_t cmd_thread_count;	/* Thread count */
	uint32_t cmd_region_count;	/* Region count */
	uint64_t cmd_region_size;	/* Region size */
	uint64_t cmd_offset;		/* Region offset */
	uint32_t cmd_region_noise;	/* Region noise */
	uint32_t cmd_chunk_noise;	/* Chunk noise */
	uint32_t cmd_thread_delay;	/* Thread delay */
	uint32_t cmd_flags;		/* Test flags */
	uint32_t cmd_block_size;	/* ZFS block size */
	char cmd_pre[ZPIOS_PATH_SIZE];	/* Pre-exec hook */
	char cmd_post[ZPIOS_PATH_SIZE];	/* Post-exec hook */
	char cmd_log[ZPIOS_PATH_SIZE];  /* Requested log dir */
	uint64_t cmd_data_size;		/* Opaque data size */
	char cmd_data_str[0];		/* Opaque data region */
} zpios_cmd_t;

/* Valid ioctls */
#define	ZPIOS_CFG			_IOWR('f', 101, zpios_cfg_t)
#define	ZPIOS_CMD			_IOWR('f', 102, zpios_cmd_t)

/* Valid configuration commands */
#define	ZPIOS_CFG_BUFFER_CLEAR		0x001	/* Clear text buffer */
#define	ZPIOS_CFG_BUFFER_SIZE		0x002	/* Resize text buffer */

#ifndef NSEC_PER_SEC
#define	NSEC_PER_SEC    1000000000L
#endif

static inline
void
zpios_timespec_normalize(zpios_timespec_t *ts, uint32_t sec, uint32_t nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		nsec -= NSEC_PER_SEC;
		sec++;
	}
	while (((int32_t)nsec) < 0) {
		nsec += NSEC_PER_SEC;
		sec--;
	}
	ts->ts_sec = sec;
	ts->ts_nsec = nsec;
}

static inline
zpios_timespec_t
zpios_timespec_add(zpios_timespec_t lhs, zpios_timespec_t rhs)
{
	zpios_timespec_t ts_delta;
	zpios_timespec_normalize(&ts_delta, lhs.ts_sec + rhs.ts_sec,
	    lhs.ts_nsec + rhs.ts_nsec);
	return (ts_delta);
}

static inline
zpios_timespec_t
zpios_timespec_sub(zpios_timespec_t lhs, zpios_timespec_t rhs)
{
	zpios_timespec_t ts_delta;
	zpios_timespec_normalize(&ts_delta, lhs.ts_sec - rhs.ts_sec,
	    lhs.ts_nsec - rhs.ts_nsec);
	return (ts_delta);
}

#ifdef _KERNEL

static inline
zpios_timespec_t
zpios_timespec_now(void)
{
	zpios_timespec_t zts_now;
	struct timespec ts_now;

	ts_now = current_kernel_time();
	zts_now.ts_sec  = ts_now.tv_sec;
	zts_now.ts_nsec = ts_now.tv_nsec;

	return (zts_now);
}

#else

static inline
double
zpios_timespec_to_double(zpios_timespec_t ts)
{
	return
	    ((double)(ts.ts_sec) +
	    ((double)(ts.ts_nsec) / (double)(NSEC_PER_SEC)));
}

#endif /* _KERNEL */

#endif /* _ZPIOS_CTL_H */
