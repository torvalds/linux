/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/sched/deadline.h>
#include <asm/syscall.h>
#include <uapi/linux/sched/types.h>
#include <trace/events/sched.h>

/*
 * Dummy values if not available
 */
#ifndef __NR_sched_setscheduler
#define __NR_sched_setscheduler -__COUNTER__
#endif
#ifndef __NR_sched_setattr
#define __NR_sched_setattr -__COUNTER__
#endif

extern struct rv_monitor rv_deadline;
/* Initialised when registering the deadline container */
extern struct sched_class *rv_ext_sched_class;

/*
 * If both have dummy values, the syscalls are not supported and we don't even
 * need to register the handler.
 */
static inline bool should_skip_syscall_handle(void)
{
	return __NR_sched_setattr < 0 && __NR_sched_setscheduler < 0;
}

/*
 * is_supported_type - return true if @type is supported by the deadline monitors
 */
static inline bool is_supported_type(u8 type)
{
	return type == DL_TASK || type == DL_SERVER_FAIR || type == DL_SERVER_EXT;
}

/*
 * is_server_type - return true if @type is a supported server
 */
static inline bool is_server_type(u8 type)
{
	return is_supported_type(type) && type != DL_TASK;
}

/*
 * Use negative numbers for the server.
 * Currently only one fair server per CPU, may change in the future.
 */
#define fair_server_id(cpu) (-cpu)
#define ext_server_id(cpu) (-cpu - num_possible_cpus())
#define NO_SERVER_ID (-2 * num_possible_cpus())
/*
 * Get a unique id used for dl entities
 *
 * The cpu is not required for tasks as the pid is used there, if this function
 * is called on a dl_se that for sure corresponds to a task, DL_TASK can be
 * used in place of cpu.
 * We need the cpu for servers as it is provided in the tracepoint and we
 * cannot easily retrieve it from the dl_se (requires the struct rq definition).
 */
static inline int get_entity_id(struct sched_dl_entity *dl_se, int cpu, u8 type)
{
	if (dl_server(dl_se) && type != DL_TASK) {
		if (type == DL_SERVER_FAIR)
			return fair_server_id(cpu);
		if (type == DL_SERVER_EXT)
			return ext_server_id(cpu);
		return NO_SERVER_ID;
	}
	return dl_task_of(dl_se)->pid;
}

static inline bool task_is_scx_enabled(struct task_struct *tsk)
{
	return IS_ENABLED(CONFIG_SCHED_CLASS_EXT) &&
	       tsk->sched_class == rv_ext_sched_class;
}

/* Expand id and target as arguments for da functions */
#define EXPAND_ID(dl_se, cpu, type) get_entity_id(dl_se, cpu, type), dl_se
#define EXPAND_ID_TASK(tsk) get_entity_id(&tsk->dl, task_cpu(tsk), DL_TASK), &tsk->dl

static inline u8 get_server_type(struct task_struct *tsk)
{
	if (tsk->policy == SCHED_NORMAL || tsk->policy == SCHED_EXT ||
	    tsk->policy == SCHED_BATCH || tsk->policy == SCHED_IDLE)
		return task_is_scx_enabled(tsk) ? DL_SERVER_EXT : DL_SERVER_FAIR;
	return DL_OTHER;
}

static inline int extract_params(struct pt_regs *regs, long id, pid_t *pid_out)
{
	size_t size = offsetofend(struct sched_attr, sched_flags);
	struct sched_attr __user *uattr, attr;
	int new_policy = -1, ret;
	unsigned long args[6];

	switch (id) {
	case __NR_sched_setscheduler:
		syscall_get_arguments(current, regs, args);
		*pid_out = args[0];
		new_policy = args[1];
		break;
	case __NR_sched_setattr:
		syscall_get_arguments(current, regs, args);
		*pid_out = args[0];
		uattr = (struct sched_attr __user *)args[1];
		/*
		 * Just copy up to sched_flags, we are not interested after that
		 */
		ret = copy_struct_from_user(&attr, size, uattr, size);
		if (ret)
			return ret;
		if (attr.sched_flags & SCHED_FLAG_KEEP_POLICY)
			return -EINVAL;
		new_policy = attr.sched_policy;
		break;
	default:
		return -EINVAL;
	}

	return new_policy & ~SCHED_RESET_ON_FORK;
}

/* Helper functions requiring DA/HA utilities */
#ifdef RV_MON_TYPE

/*
 * get_fair_server - get the fair server associated to a task
 *
 * If the task is a boosted task, the server is available in the task_struct,
 * otherwise grab the dl entity saved for the CPU where the task is enqueued.
 * This function assumes the task is enqueued somewhere.
 */
static inline struct sched_dl_entity *get_server(struct task_struct *tsk, u8 type)
{
	if (tsk->dl_server && get_server_type(tsk) == type)
		return tsk->dl_server;
	if (type == DL_SERVER_FAIR)
		return da_get_target_by_id(fair_server_id(task_cpu(tsk)));
	if (type == DL_SERVER_EXT)
		return da_get_target_by_id(ext_server_id(task_cpu(tsk)));
	return NULL;
}

/*
 * Initialise monitors for all tasks and pre-allocate the storage for servers.
 * This is necessary since we don't have access to the servers here and
 * allocation can cause deadlocks from their tracepoints. We can only fill
 * pre-initialised storage from there.
 */
static inline int init_storage(bool skip_tasks)
{
	struct task_struct *g, *p;
	int cpu;

	for_each_possible_cpu(cpu) {
		if (!da_create_empty_storage(fair_server_id(cpu)))
			goto fail;
		if (IS_ENABLED(CONFIG_SCHED_CLASS_EXT) &&
		    !da_create_empty_storage(ext_server_id(cpu)))
			goto fail;
	}

	if (skip_tasks)
		return 0;

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		if (p->policy == SCHED_DEADLINE) {
			if (!da_create_storage(EXPAND_ID_TASK(p), NULL)) {
				read_unlock(&tasklist_lock);
				goto fail;
			}
		}
	}
	read_unlock(&tasklist_lock);
	return 0;

fail:
	da_monitor_destroy();
	return -ENOMEM;
}

static void __maybe_unused handle_newtask(void *data, struct task_struct *task, u64 flags)
{
	/* Might be superfluous as tasks are not started with this policy.. */
	if (task->policy == SCHED_DEADLINE)
		da_create_storage(EXPAND_ID_TASK(task), NULL);
}

static void __maybe_unused handle_exit(void *data, struct task_struct *p, bool group_dead)
{
	if (p->policy == SCHED_DEADLINE)
		da_destroy_storage(get_entity_id(&p->dl, DL_TASK, DL_TASK));
}

#endif
