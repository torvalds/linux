/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef _UAPI_MSM_SYSSTATS_H_
#define _UAPI_MSM_SYSSTATS_H_

#include <linux/types.h>

#define SYSSTATS_GENL_NAME	"SYSSTATS"
#define SYSSTATS_GENL_VERSION	0x1

#define TS_COMM_LEN		32

#define	SYSSTATS_TYPE_UNSPEC 0
#define	SYSSTATS_TASK_TYPE_STATS 1
#define	SYSSTATS_TYPE_NULL 2
#define	SYSSTATS_TASK_TYPE_FOREACH 3
#define	SYSSTATS_MEMINFO_TYPE_STATS 4

#define	SYSSTATS_CMD_ATTR_UNSPEC 0
#define	SYSSTATS_TASK_CMD_ATTR_PID 1
#define	SYSSTATS_TASK_CMD_ATTR_FOREACH 2

#define	SYSSTATS_CMD_UNSPEC 0
#define	SYSSTATS_TASK_CMD_GET 1
#define	SYSSTATS_TASK_CMD_NEW 2
#define	SYSSTATS_MEMINFO_CMD_GET 3
#define	SYSSTATS_MEMINFO_CMD_NEW 4

struct sysstats_task {
	__u64 anon_rss;	/* KB */
	__u64 file_rss;	/* KB */
	__u64 swap_rss;	/* KB */
	__u64 shmem_rss;	/* KB */
	__u64 unreclaimable;	/* KB */
	__u64 utime;	/* User CPU time [usec] */
	__u64 stime;	/* System CPU time [usec] */
	__u64 cutime;	/* Cumulative User CPU time [usec] */
	__u64 cstime;	/* Cumulative System CPU time [usec] */
	__s16 oom_score;
	__s16 __padding;
	__u32 pid;
	__u32 uid;
	__u32 ppid;  /* Parent process ID */
	char name[TS_COMM_LEN];  /* Command name */
	char state[TS_COMM_LEN]; /* Process state */
};

/*
 * All values in KB.
 */
struct sysstats_mem {
	__u64 memtotal;
	__u64 misc_reclaimable;
	__u64 unreclaimable;
	__u64 zram_compressed;
	__u64 swap_used;
	__u64 swap_total;
	__u64 buffer;
	__u64 vmalloc_total;
	__u64 swapcache;
	__u64 slab_reclaimable;
	__u64 slab_unreclaimable;
	__u64 free_cma;
	__u64 file_mapped;
	__u64 pagetable;
	__u64 kernelstack;
	__u64 shmem;
	__u64 dma_nr_free;
	__u64 dma_nr_active_anon;
	__u64 dma_nr_inactive_anon;
	__u64 dma_nr_active_file;
	__u64 dma_nr_inactive_file;
	__u64 normal_nr_free;
	__u64 normal_nr_active_anon;
	__u64 normal_nr_inactive_anon;
	__u64 normal_nr_active_file;
	__u64 normal_nr_inactive_file;
	__u64 movable_nr_free;
	__u64 movable_nr_active_anon;
	__u64 movable_nr_inactive_anon;
	__u64 movable_nr_active_file;
	__u64 movable_nr_inactive_file;
	__u64 highmem_nr_free;
	__u64 highmem_nr_active_anon;
	__u64 highmem_nr_inactive_anon;
	__u64 highmem_nr_active_file;
	__u64 highmem_nr_inactive_file;
};

#endif /* _UAPI_MSM_SYSSTATS_H_ */
