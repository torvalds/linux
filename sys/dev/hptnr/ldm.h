/* $Id: ldm.h,v 1.78 2010/05/10 10:08:46 lcn Exp $ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2005-2011 HighPoint Technologies, Inc.
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
#ifndef _HPT_LDM_H_
#define _HPT_LDM_H_

#define VERMAGIC_LDM 75

#if defined(__cplusplus)
extern "C" {
#endif


#define __hpt_set_ver(x, v1, v2, v3, v4, v5) x ## _R_ ## v1 ## _ ## v2 ## _ ## v3 ## _ ## v4 ## _ ## v5
#define _hpt_set_ver(x, v1, v2, v3, v4, v5)  __hpt_set_ver(x, v1, v2, v3, v4, v5)
#define hpt_set_ver(x)               _hpt_set_ver(x, VERMAGIC_OSM, VERMAGIC_HIM, VERMAGIC_LDM, VERMAGIC_ARRAY, MAX_MEMBERS)

#define ldm_register_him        hpt_set_ver(ldm_register_him)
#define ldm_register_vdev_class hpt_set_ver(ldm_register_vdev_class)
#define ldm_alloc_cmds          hpt_set_ver(ldm_alloc_cmds)


#ifndef HPT_INTERFACE_VERSION
#define HPT_INTERFACE_VERSION 0x02010000
#endif

#ifndef MAX_PARTITIONS_PER_DISK
#define MAX_PARTITIONS_PER_DISK	4
#endif

#if defined(__MAX_PARTITIONS_PER_DISK) && MAX_PARTITIONS_PER_DISK > __MAX_PARTITIONS_PER_DISK
#error "Please redefine MAX_PARTITIONS_PER_DISK!!!"
#endif


typedef char check_HPT_TIME_is_unsigned[ (HPT_TIME)(-1) > 0 ? 1 : -1 ];

#define hpt_time_after_eq(a, b) ((int)(a) - (int)(b) >= 0)
#define hpt_time_after(a, b) ((int)(a) - (int)(b) > 0)



struct freelist {
	int dma;
	HPT_UINT alignment;
	HPT_UINT count;
	HPT_UINT size;
	void * head;
	struct freelist *next;
#if DBG
	char *tag;
	HPT_UINT reserved_count; 
	#define freelist_debug_tag(list, _tag) (list)->tag = _tag
#else 
	#define freelist_debug_tag(list, _tag)
#endif
};


void freelist_reserve(struct freelist *list, void *osext, HPT_UINT size, HPT_UINT count);

void *freelist_get(struct freelist *);
void freelist_put(struct freelist *, void *p);

void freelist_reserve_dma(struct freelist *list, void *osext, HPT_UINT size, HPT_UINT alignment, HPT_UINT count);
void *freelist_get_dma(struct freelist *, BUS_ADDRESS *busaddr);
void freelist_put_dma(struct freelist *, void *p, BUS_ADDRESS busaddr);


#define freelist_reserve_with_tag(list, osext, size, count) \
	do {\
		freelist_debug_tag(list, #list  " at " __FILE__);\
		freelist_reserve(list, osext, (HPT_UINT)(size), (HPT_UINT)(count));\
	}while(0)

#define freelist_reserve_dma_with_tag(list, osext, size, alignment, count) \
	do {\
		freelist_debug_tag(list, #list " at " __FILE__);\
		freelist_reserve_dma(list, osext, (HPT_UINT)(size), (HPT_UINT)(alignment), (HPT_UINT)(count));\
	}while(0)

struct lock_request {
	HPT_U64 start, end;
	struct lock_request *next;
	struct list_head waiters; /* blocked commands */
	struct tq_item callback;
	int lock_cc;
};

#define INIT_LOCK_REQUEST(req, _start, _end, _cb, _arg, _cc) \
	do {\
		(req)->next = 0;\
		(req)->start = _start;\
		(req)->end = _end;\
		INIT_TQ_ITEM(&(req)->callback, _cb, _arg);\
		INIT_LIST_HEAD(&(req)->waiters);\
		(req)->lock_cc = _cc;\
	} while (0)

struct task_queue {
	struct tq_item *head, *tail;
};

#define TQ_EMPTY(tq) ((tq)->head==0)

struct dmapool_order {
	HPT_UINT npages;
	struct tq_item wakeup_fn;
	struct dmapool_order *next;
};

struct dmapool_client {
	void * handle;
	HPT_UINT (*shrink)(void *handle, HPT_UINT npages);
	int (*resume)(void *handle);
	struct dmapool_client *next;
};

typedef struct _VBUS * PVBUS;
typedef struct _VDEV * PVDEV;


void dmapool_register_client(PVBUS vbus, struct dmapool_client *client);


void dmapool_active(PVBUS vbus);

/* return 0 if the request is immediately satisfied, non-zero otherwise. */
int dmapool_make_order(PVBUS vbus, struct dmapool_order *order);


void *dmapool_get_page(PVBUS vbus, BUS_ADDRESS *busaddr);
void *dmapool_get_page_at(PVBUS vbus, void *p, BUS_ADDRESS *busaddr);
void dmapool_put_page(PVBUS vbus, void *p, BUS_ADDRESS busaddr);
void dmapool_init(PVBUS vbus);
HPT_UINT dmapool_max_class_pages(PVBUS vbus);


struct timer_call {
	HPT_U32 interval; /*microseconds*/
	HPT_TIME expire_time; /*microseconds*/
	void (*proc)(void * arg);
	void * arg;
	struct timer_call ** pprev;
	struct timer_call * next;
};

#define ldm_init_timer(timer) do { (timer)->next=0; (timer)->pprev=0; } while (0)

#define INIT_TIMER_CALL(timer, _interval, _proc, _arg) \
	do { \
		HPT_ASSERT((timer)->next==0 && (timer)->pprev==0);\
		(timer)->interval = _interval;\
		(timer)->proc = _proc;\
		(timer)->arg = _arg;\
	} while(0)

void ldm_request_timer(PVBUS vbus, struct timer_call * tc);
void ldm_remove_timer(PVBUS vbus, struct timer_call * tc);
void ldm_on_timer(PVBUS vbus);


typedef struct _LDM_ADAPTER
{
	struct _LDM_ADAPTER *next;
	HIM  *him;
	void *him_handle;
	PVBUS vbus;
	struct freelist freelist_dev;
	int devid_start;
	struct freelist freelist_plugged_dpc;
	HPT_BOOL master;
}
LDM_ADAPTER, *PLDM_ADAPTER;

typedef struct _IOCTL_ARG
{
	struct list_head link;
	PVBUS vbus;
	HPT_U32 dwIoControlCode;
	HPT_U32 nInBufferSize;
	HPT_U32 nOutBufferSize;
	void *  lpInBuffer;
	void *  lpOutBuffer;
	HPT_U32 *lpBytesReturned;
	void *  ioctl_cmnd;
	void (* done)(struct _IOCTL_ARG *);
	int     result; /* HPT_IOCTL_RESULT_ */
	struct tq_item dpc;
} IOCTL_ARG;

#define HPT_IOCTL_RESULT_OK          0
#define HPT_IOCTL_RESULT_FAILED     (-1)
#define HPT_IOCTL_RESULT_INVALID    (-2)
#define HPT_IOCTL_RESULT_RETRY      (-3)
#define HPT_IOCTL_RESULT_WRONG_VBUS (-4)

void ldm_ioctl(	PVBUS vbus,	IOCTL_ARG *IAPnt);
void ldm_set_autorebuild(PVBUS vbus, int enable);
HPT_U32 ldm_get_device_id(PVDEV vd); /* for ioctl */

#ifndef __HPT_RAW_LBA
#define __HPT_RAW_LBA HPT_RAW_LBA
#endif

#include <dev/hptnr/array.h>

typedef struct hpt_raw_disk
{
#ifdef SUPPORT_ARRAY
	PRAW_PARTITION raw_part_list;
	__HPT_RAW_LBA max_available_capacity; 
	__HPT_RAW_LBA total_available_capacity;
#endif
	__HPT_RAW_LBA real_capacity;
	__HPT_RAW_LBA head_position;
	HPT_U32	logical_sector_size;
	HPT_U8   logicalsectors_per_physicalsector;
	HPT_U16 lowest_aligned;
	HPT_U16 max_sectors_per_cmd;
	HPT_U8  max_queue_depth;
	HPT_U8  user_select_mode;

	HPT_UINT  uninitialized : 1;
	HPT_UINT  legacy_disk : 1;
	HPT_UINT  is_spare : 1;
	HPT_UINT  v3_format : 1;
	HPT_UINT  need_sync : 1;
	HPT_UINT  temp_spare : 1;
	HPT_UINT  need_check_array : 1;
	HPT_UINT  df_user_mode_set: 1;

	HPT_UINT  df_read_ahead_set: 1;
	HPT_UINT  enable_read_ahead : 1;
	HPT_UINT  df_write_cache_set: 1;
	HPT_UINT  enable_write_cache : 1;
	HPT_UINT  df_tcq_set: 1;
	HPT_UINT  enable_tcq : 1;
	HPT_UINT  df_ncq_set: 1;
	HPT_UINT  enable_ncq : 1;

	HPT_UINT  bad_sector : 1;
	HPT_UINT  df_sas : 1;

	HIM  *				him;
	int 				index;
	PLDM_ADAPTER		adapter;
	void *				phy_dev;

	char    model[40];

	struct tq_item reset_dpc;
	int reset_pending;

	struct tq_item fail_dpc;
	int fail_pending;
}
HPT_RAW_DISK, *PHPT_RAW_DISK;

struct vdev_class
{
	struct vdev_class *next;

	HPT_U8   __type;
	HPT_U8   stripped;        /* RAID0,3,5,6 */
	HPT_U8   redundancy;      /* RAID1-1, RAID3/5-1, RAID6-2 */
	HPT_U8   must_init;       /* RAID3,5,6 */
	HPT_U8   docache;

	HPT_UINT vbus_ext_size;
	HPT_UINT vbus_ext_offset; /* used by LDM */
	HPT_UINT dev_ext_size;
	HPT_UINT cmd_ext_size;

	
	void (*get_mem_info)(PVBUS vbus, void *osext, int phydev_count);
	void (*queue_cmd)(PCOMMAND cmd);
	void (*member_failed)(struct _VDEV * vd); 

	
	void (*initialize)(PVBUS vbus);
	void (*release)(PVBUS vbus); 
	int  (*add)(PVDEV vd); 
	void (*remove)(PVDEV vd); 
	void (*reset)(PVDEV vd); 
	void (*sync_stamp)(PVDEV vd);
	int  (*support_type)(int type);
};


#define VDEV_CLASS_CONSTRUCTOR(type, prefix) { \
	0, \
	type, \
	prefix ## _stripped, \
	prefix ## _redundancy, \
	prefix ## _must_init, \
	0, \
	(HPT_UINT)(prefix ## _vbus_ext_size), \
	0, \
	(HPT_UINT)(prefix ## _dev_ext_size), \
	(HPT_UINT)(prefix ## _cmd_ext_size), \
	prefix ## _get_mem_info, \
	prefix ## _queue_cmd, \
	prefix ## _member_failed, \
	prefix ## _initialize, \
	prefix ## _release, \
	prefix ## _add, \
	prefix ## _remove, \
	prefix ## _reset, \
	prefix ## _sync_stamp, \
	0 \
}

#define VD_RAW       1
#define VD_PARTITION 4

#define mIsArray(vdev_type) ((vdev_type)>VD_PARTITION)

#define VD_RAID0     5
#define VD_RAID1     6
#define VD_JBOD      7
#define VD_RAID5     8
#define VD_RAID6     9
#define VD_RAID3     10
#define VD_RAID4     11
#define VD_RAID1E    12

#define MAX_VD_TYPE_ID  12

struct vdev_class *ldm_find_vdev_class(HPT_U8 type);

typedef struct _VDEV {
	PVBUS vbus;
	struct vdev_class *Class;
	HPT_U8 type;
	PVDEV parent;
	void * ext;
	HPT_U64 capacity;
	int     target_id;
	HPT_UINT cmds_per_request;

	union {
#ifdef SUPPORT_ARRAY
		HPT_ARRAY array;
		HPT_PARTITION partition;
#endif
		HPT_RAW_DISK raw;
	} u;

	HPT_U8 vf_online : 1;
	HPT_U8 vf_bootmark : 1;
	HPT_U8 vf_bootable : 1;
	HPT_U8 vf_resetting: 1;
	HPT_U8 vf_quiesced: 1;
	HPT_U8 vf_clslock: 1;

	HPT_U8 cache_policy; /* see CACHE_POLICY_* */

	HPT_UINT cq_len;
	HPT_UINT cmds_sent;

	struct list_head link;
	struct list_head cq_wait_send;
	struct list_head cq_sent;

	HPT_U32  last_active;
	int cq_priority;
	struct list_head cq_wait_lock;
	struct lock_request *locks_granted;
	struct lock_request *locks_wait;
	HPT_U32 ioctl_id; 
	void * cc_ext;
}
VDEV;

#define CACHE_POLICY_NONE 0
#define CACHE_POLICY_WRITE_THROUGH 1
#define CACHE_POLICY_WRITE_BACK 2


extern HIM *him_list;


void ldm_register_him(PHIM him);


void ldm_register_vdev_class(struct vdev_class *Class);


HPT_BOOL ldm_register_adapter(PLDM_ADAPTER adapter);


int init_config(void);

HPT_UINT ldm_get_vbus_size(void);


void ldm_create_vbus(PVBUS vbus, void *osext);


void ldm_get_mem_info(PVBUS vbus, void *osext);


void *ldm_get_vbus_ext(PVBUS vbus, struct vdev_class *Class);


PVBUS ldm_get_next_vbus(PVBUS vbus, void **posext);

#define ldm_for_each_vbus(vbus, vbus_ext) \
	for (vbus = ldm_get_next_vbus(0, (void **)(void *)&vbus_ext); vbus; \
		vbus = ldm_get_next_vbus(vbus, (void **)(void *)&vbus_ext))


void ldm_initialize_vbus_async(PVBUS vbus, PLDM_ADAPTER master_adapter, void (*done)(void *osext));

/* ldm_initialize_vbus is deprecated since it will hold the CPU too long. */
#define ldm_initialize_vbus(vbus, adapter) ldm_initialize_vbus_async(vbus, adapter, 0)


void ldm_release_vbus(PVBUS vbus);

PVDEV ldm_create_vdev(PVBUS vbus, HPT_U8 type);
void ldm_release_vdev(PVDEV vd);

PVDEV ldm_find_target(PVBUS vbus, int id);
PVDEV ldm_find_stamp(PVBUS vbus, HPT_U32 stamp, int seq);


PCOMMAND ldm_alloc_cmds(PVBUS vbus, HPT_UINT cnt);
void ldm_free_cmds(PCOMMAND cmd);

HPT_UINT ldm_get_cmd_size(void);
PCOMMAND ldm_alloc_cmds_from_list(PVBUS vbus, struct freelist *list, HPT_UINT cnt);
void ldm_free_cmds_to_list(struct freelist *list, PCOMMAND cmd);


PCOMMAND __ldm_alloc_cmd(struct freelist *list);

#ifdef OS_SUPPORT_TASK
#define CMD_SET_PRIORITY(cmd, pri) cmd->priority = (pri)
#else 
#define CMD_SET_PRIORITY(cmd, pri)
#endif


#define CMD_GROUP_GET(grp, cmd) \
	do {\
		grp->grplist->count++;\
		cmd = __ldm_alloc_cmd(grp->grplist);\
		cmd->vbus = grp->vbus;\
		cmd->grplist = grp->grplist;\
		CMD_SET_PRIORITY(cmd, grp->priority);\
	} while(0)

#define CMD_GROUP_PUT(grp, cmd) \
	do {\
		freelist_put(grp->grplist, cmd);\
		grp->grplist->count--;\
	} while (0)




void ldm_queue_cmd(PCOMMAND cmd);
void vdev_queue_cmd(PCOMMAND cmd);
void ldm_finish_cmd(PCOMMAND cmd);


int  ldm_acquire_lock(PVDEV vd, struct lock_request *req);
void ldm_release_lock(PVDEV vd, struct lock_request *req);

void ldm_queue_task(struct task_queue *tq, struct tq_item *t);
void ldm_queue_vbus_dpc(PVBUS vbus, struct tq_item *t);

HPT_BOOL ldm_intr(PVBUS vbus);
void ldm_run(PVBUS vbus);
int ldm_idle(PVBUS vbus);


int ldm_reset_vbus(PVBUS vbus);


void ldm_suspend(PVBUS vbus);
void ldm_resume(PVBUS vbus);
LDM_ADAPTER *ldm_resume_adapter(PVBUS vbus, PLDM_ADAPTER ldm_adapter);
void ldm_shutdown(PVBUS vbus);/*shutdown all the controllers*/


#define HIM_EVENT_DEVICE_REMOVED 1
#define HIM_EVENT_DEVICE_PLUGGED 2
#define HIM_EVENT_DEVICE_ERROR   3
#define HIM_EVENT_RESET_REQUIRED 4
#define HIM_EVENT_QUIESCE_DEVICE 5
#define HIM_EVENT_UNQUIESCE_DEVICE 6
#define HIM_EVENT_CONFIG_CHANGED 7

void ldm_event_notify(HPT_U32 event, void *arg1, void *arg2);

void log_sector_repair(PVDEV vd, int success, HPT_LBA lba, HPT_U16 nsectors);

void ldm_register_device(PVDEV vd);
void ldm_unregister_device(PVDEV vd);

PVBUS him_handle_to_vbus(void * him_handle);
void ldm_ide_fixstring (HPT_U8 *s, const int bytecount);
#if defined(__cplusplus)
}
#endif
#endif
