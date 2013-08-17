/*
 *  linux/include/linux/mmc/mmc_trace.h
 *
 *  Copyright (C) 2013 Samsung Electronics Co., Ltd. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MMC_TRACE_H
#define MMC_TRACE_H

#if defined(CONFIG_BLK_DEV_IO_TRACE)
/* trace functions */
extern void mmc_add_trace(unsigned int type, void *m);
#define mmc_add_trace_msg(q, fmt, ...)				\
	do {							\
		struct blk_trace *bt;				\
		if (likely(q)) {				\
			bt = (q)->blk_trace;			\
			if (unlikely(bt))			\
				__trace_note_message(bt,	\
					fmt, ##__VA_ARGS__);	\
		}						\
	} while (0)

/* trace structures */
enum mmc_trace_act {
	__MMC_TA_ASYNC_REQ_FETCH,	/* fetched by arrived async request */
	__MMC_TA_REQ_FETCH,		/* fetched by normal queue thread */
	__MMC_TA_REQ_DONE,		/* one request done in core layer */
	__MMC_TA_PRE_DONE,		/* prepare done */
	__MMC_TA_MMC_ISSUE,		/* actual cmd issue to device */
	__MMC_TA_MMC_DONE,		/* actual cmd done from device */
	__MMC_TA_MMC_DMA_DONE,		/* DMA transfer done */
};

struct mmc_trace {
	u32		ta_type;	/* action type */
	char		ta_info[3];	/* action info to output */
	u32		cnt_async;	/* info for counting req of req list */
	u32		cnt_sync;
};

/* profiling functions */
extern int mmc_profile_alloc(u32 slot, u32 count);
extern int mmc_profile_free(u32 slot);
extern int mmc_profile_start(u32 slot);
extern int mmc_profile_end(u32 slot, u32 record);

extern u32 mmc_profile_get_count(u32 slot);
extern u32 mmc_profile_result_time(u32 slot, u32 start, u32 end);

/* profiling structures */
struct mmc_profile_data {
	u32 elap_time;
	u32 record;
};

struct mmc_profile_slot_info {
	u32 using;
	u32 count_cur;
	u32 count_max;
	struct timeval start, end;
	struct mmc_profile_data *data;
};

#else
#define mmc_add_trace(type, m)			do {} while (0)
#define mmc_add_trace_msg(q, fmt, ...)		do {} while (0)
#define mmc_profile_alloc(slot, count)		do {} while (0)
#define mmc_profile_free(slot)			do {} while (0)
#define mmc_profile_start(slot)			do {} while (0)
#define mmc_profile_end(slot, record)		do {} while (0)
#define mmc_profile_get_count(slot)		do {} while (0)
#define mmc_profile_result_time(slot, start, end)	do {} while (0)

#endif /* CONFIG_BLK_DEV_IO_TRACE */

#endif
