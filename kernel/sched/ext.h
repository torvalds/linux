/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BPF extensible scheduler class: Documentation/scheduler/sched-ext.rst
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifdef CONFIG_SCHED_CLASS_EXT

DECLARE_STATIC_KEY_FALSE(scx_ops_allow_queued_wakeup);

void scx_tick(struct rq *rq);
void init_scx_entity(struct sched_ext_entity *scx);
void scx_pre_fork(struct task_struct *p);
int scx_fork(struct task_struct *p);
void scx_post_fork(struct task_struct *p);
void scx_cancel_fork(struct task_struct *p);
bool scx_can_stop_tick(struct rq *rq);
void scx_rq_activate(struct rq *rq);
void scx_rq_deactivate(struct rq *rq);
int scx_check_setscheduler(struct task_struct *p, int policy);
bool task_should_scx(int policy);
void init_sched_ext_class(void);

static inline u32 scx_cpuperf_target(s32 cpu)
{
	if (scx_enabled())
		return cpu_rq(cpu)->scx.cpuperf_target;
	else
		return 0;
}

static inline bool task_on_scx(const struct task_struct *p)
{
	return scx_enabled() && p->sched_class == &ext_sched_class;
}

static inline bool scx_allow_ttwu_queue(const struct task_struct *p)
{
	return !scx_enabled() ||
		static_branch_likely(&scx_ops_allow_queued_wakeup) ||
		p->sched_class != &ext_sched_class;
}

#ifdef CONFIG_SCHED_CORE
bool scx_prio_less(const struct task_struct *a, const struct task_struct *b,
		   bool in_fi);
#endif

#else	/* CONFIG_SCHED_CLASS_EXT */

static inline void scx_tick(struct rq *rq) {}
static inline void scx_pre_fork(struct task_struct *p) {}
static inline int scx_fork(struct task_struct *p) { return 0; }
static inline void scx_post_fork(struct task_struct *p) {}
static inline void scx_cancel_fork(struct task_struct *p) {}
static inline u32 scx_cpuperf_target(s32 cpu) { return 0; }
static inline bool scx_can_stop_tick(struct rq *rq) { return true; }
static inline void scx_rq_activate(struct rq *rq) {}
static inline void scx_rq_deactivate(struct rq *rq) {}
static inline int scx_check_setscheduler(struct task_struct *p, int policy) { return 0; }
static inline bool task_on_scx(const struct task_struct *p) { return false; }
static inline bool scx_allow_ttwu_queue(const struct task_struct *p) { return true; }
static inline void init_sched_ext_class(void) {}

#endif	/* CONFIG_SCHED_CLASS_EXT */

#if defined(CONFIG_SCHED_CLASS_EXT) && defined(CONFIG_SMP)
void __scx_update_idle(struct rq *rq, bool idle, bool do_notify);

static inline void scx_update_idle(struct rq *rq, bool idle, bool do_notify)
{
	if (scx_enabled())
		__scx_update_idle(rq, idle, do_notify);
}
#else
static inline void scx_update_idle(struct rq *rq, bool idle, bool do_notify) {}
#endif

#ifdef CONFIG_CGROUP_SCHED
#ifdef CONFIG_EXT_GROUP_SCHED
int scx_tg_online(struct task_group *tg);
void scx_tg_offline(struct task_group *tg);
int scx_cgroup_can_attach(struct cgroup_taskset *tset);
void scx_cgroup_move_task(struct task_struct *p);
void scx_cgroup_finish_attach(void);
void scx_cgroup_cancel_attach(struct cgroup_taskset *tset);
void scx_group_set_weight(struct task_group *tg, unsigned long cgrp_weight);
void scx_group_set_idle(struct task_group *tg, bool idle);
#else	/* CONFIG_EXT_GROUP_SCHED */
static inline int scx_tg_online(struct task_group *tg) { return 0; }
static inline void scx_tg_offline(struct task_group *tg) {}
static inline int scx_cgroup_can_attach(struct cgroup_taskset *tset) { return 0; }
static inline void scx_cgroup_move_task(struct task_struct *p) {}
static inline void scx_cgroup_finish_attach(void) {}
static inline void scx_cgroup_cancel_attach(struct cgroup_taskset *tset) {}
static inline void scx_group_set_weight(struct task_group *tg, unsigned long cgrp_weight) {}
static inline void scx_group_set_idle(struct task_group *tg, bool idle) {}
#endif	/* CONFIG_EXT_GROUP_SCHED */
#endif	/* CONFIG_CGROUP_SCHED */
