/* $Id: array.h,v 1.46 2010/02/05 06:57:43 mah Exp $ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2004-2011 HighPoint Technologies, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $FreeBSD$
 */
#include <dev/hptnr/hptnr_config.h>
#ifndef _HPT_ARRAY_H_
#define _HPT_ARRAY_H_

#define VERMAGIC_ARRAY 46

#if defined(__cplusplus)
extern "C" {
#endif

#define MAX_ARRAY_NAME 16

#ifndef MAX_MEMBERS
#define MAX_MEMBERS    16
#endif

#if MAX_MEMBERS<=16
typedef HPT_U16 HPT_MMASK;
#elif MAX_MEMBERS<=32
typedef HPT_U32 HPT_MMASK;
#elif MAX_MEMBERS<=64
typedef HPT_U64 HPT_MMASK;
#else 
#error "MAX_MEMBERS too large"
#endif

#define HPT_MMASK_VALUE(x) (HPT_MMASK)((HPT_MMASK)1<<(x))

#if MAX_MEMBERS<32
#define HPT_MMASK_VALUE_SAFE(x) HPT_MMASK_VALUE(x)
#else 
#define HPT_MMASK_VALUE_SAFE(x) ((x)>=MAX_MEMBERS? (HPT_MMASK)0 : HPT_MMASK_VALUE(x))
#endif

#define MAX_REBUILD_SECTORS 128

typedef struct _RAID_FLAGS {
	HPT_UINT rf_need_initialize : 1;    
	HPT_UINT rf_need_rebuild: 1;        
	HPT_UINT rf_need_sync: 1;           
	/* ioctl flags */
	HPT_UINT rf_auto_rebuild: 1;
	HPT_UINT rf_rebuilding: 1;          
	HPT_UINT rf_verifying: 1;
	HPT_UINT rf_initializing: 1;        
	HPT_UINT rf_abort_verifying: 1;
	HPT_UINT rf_raid15: 1;
	HPT_UINT rf_v3_format : 1;
	HPT_UINT rf_need_transform : 1;
	HPT_UINT rf_transforming : 1;
	HPT_UINT rf_abort_transform : 1;
	HPT_UINT rf_log_write: 1;           
} RAID_FLAGS;

typedef struct transform_cmd_ext
{
	HPT_LBA lba;
	HPT_U16 total_sectors;
	HPT_U16 finished_sectors;
} TRANSFORM_CMD_EXT , *PTRANSFORM_CMD_EXT;


#define TO_MOVE_DATA        0
#define TO_INITIALIZE       1
#define TO_INITIALIZE_ONLY  2
#define TO_MOVE_DATA_ONLY   3
typedef struct hpt_transform
{
	HPT_U32 stamp;
	PVDEV source;
	PVDEV target;
	struct list_head link;
	HPT_U8 transform_from_tail;
	struct tq_item task;
	
	struct lock_request lock;
	TRANSFORM_CMD_EXT cmdext;

	HPT_U64 transform_point;
	HPT_U16 transform_sectors_per_step;
	HPT_U8  operation;
	HPT_U8  disabled; 
} HPT_TRANSFORM, *PHPT_TRANSFORM;

typedef struct hpt_array
{
	HPT_U32 array_stamp;
	HPT_U32 data_stamp;  
	HPT_U32 array_sn; 

	HPT_U8  ndisk;
	HPT_U8  block_size_shift;
	HPT_U16 strip_width;
	HPT_U8  sector_size_shift; /*sector size = 512B<<sector_size_shift*/
	HPT_U8  jid; 
	HPT_U8  reserved[2];

	
	HPT_MMASK outdated_members;
	HPT_MMASK offline_members;

	PVDEV member[MAX_MEMBERS];

	RAID_FLAGS flags;

	HPT_U64 rebuilt_sectors;

	
	HPT_U8 name[MAX_ARRAY_NAME];
	PHPT_TRANSFORM transform;

	TIME_RECORD create_time;        
	HPT_U8  description[64];        
	HPT_U8  create_manager[16];     

#ifdef OS_SUPPORT_TASK
	int floating_priority;
	OSM_TASK ioctl_task;
	IOCTL_ARG ioctl_arg;
	
	char ioctl_inbuf[sizeof(PVDEV)+sizeof(HPT_U64)+sizeof(HPT_U16)];
	char ioctl_outbuf[sizeof(HPT_UINT)];
#endif

} HPT_ARRAY, *PHPT_ARRAY;

#ifdef OS_SUPPORT_TASK
void ldm_start_rebuild(struct _VDEV *pArray);
#else 
#define ldm_start_rebuild(pArray)
#endif

typedef struct _raw_partition{
	struct _raw_partition * next;
	__HPT_RAW_LBA start;
	__HPT_RAW_LBA capacity;
	PVDEV   vd_part;
} RAW_PARTITION, *PRAW_PARTITION;

typedef struct hpt_partiton
{
	PVDEV raw_disk;
	__HPT_RAW_LBA des_location;
	PRAW_PARTITION raw_part;
	HPT_U8  del_mbr;
	HPT_U8  reserved[3];
} HPT_PARTITION, *PHPT_PARTITION;

void ldm_check_array_online(PVDEV pArray);
void ldm_generic_member_failed(PVDEV member);
void ldm_sync_array_info(PVDEV pArray);
void ldm_sync_array_stamp(PVDEV pArray);
void ldm_add_spare_to_array(PVDEV pArray, PVDEV spare_partition);

#if defined(__cplusplus)
}
#endif
#endif
