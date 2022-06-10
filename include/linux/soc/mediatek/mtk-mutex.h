/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_MUTEX_H
#define MTK_MUTEX_H

struct regmap;
struct device;
struct mtk_mutex;

enum mtk_mutex_mod_index {
	/* MDP table index */
	MUTEX_MOD_IDX_MDP_RDMA0,
	MUTEX_MOD_IDX_MDP_RSZ0,
	MUTEX_MOD_IDX_MDP_RSZ1,
	MUTEX_MOD_IDX_MDP_TDSHP0,
	MUTEX_MOD_IDX_MDP_WROT0,
	MUTEX_MOD_IDX_MDP_WDMA,
	MUTEX_MOD_IDX_MDP_AAL0,
	MUTEX_MOD_IDX_MDP_CCORR0,

	MUTEX_MOD_IDX_MAX		/* ALWAYS keep at the end */
};

enum mtk_mutex_sof_index {
	MUTEX_SOF_IDX_SINGLE_MODE,

	MUTEX_SOF_IDX_MAX		/* ALWAYS keep at the end */
};

struct mtk_mutex *mtk_mutex_get(struct device *dev);
int mtk_mutex_prepare(struct mtk_mutex *mutex);
void mtk_mutex_add_comp(struct mtk_mutex *mutex,
			enum mtk_ddp_comp_id id);
void mtk_mutex_enable(struct mtk_mutex *mutex);
int mtk_mutex_enable_by_cmdq(struct mtk_mutex *mutex,
			     void *pkt);
void mtk_mutex_disable(struct mtk_mutex *mutex);
void mtk_mutex_remove_comp(struct mtk_mutex *mutex,
			   enum mtk_ddp_comp_id id);
void mtk_mutex_unprepare(struct mtk_mutex *mutex);
void mtk_mutex_put(struct mtk_mutex *mutex);
void mtk_mutex_acquire(struct mtk_mutex *mutex);
void mtk_mutex_release(struct mtk_mutex *mutex);
int mtk_mutex_write_mod(struct mtk_mutex *mutex,
			enum mtk_mutex_mod_index idx,
			bool clear);
int mtk_mutex_write_sof(struct mtk_mutex *mutex,
			enum mtk_mutex_sof_index idx);

#endif /* MTK_MUTEX_H */
