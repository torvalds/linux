/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BLK_PM_H_
#define _BLK_PM_H_

struct device;
struct request_queue;

/*
 * block layer runtime pm functions
 */
#ifdef CONFIG_PM
extern void blk_pm_runtime_init(struct request_queue *q, struct device *dev);
extern int blk_pre_runtime_suspend(struct request_queue *q);
extern void blk_post_runtime_suspend(struct request_queue *q, int err);
extern void blk_pre_runtime_resume(struct request_queue *q);
extern void blk_post_runtime_resume(struct request_queue *q, int err);
extern void blk_set_runtime_active(struct request_queue *q);
#else
static inline void blk_pm_runtime_init(struct request_queue *q,
				       struct device *dev) {}
#endif

#endif /* _BLK_PM_H_ */
