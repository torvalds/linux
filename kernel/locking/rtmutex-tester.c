/*
 * RT-Mutex-tester: scriptable tester for rt mutexes
 *
 * started by Thomas Gleixner:
 *
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 */
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/freezer.h>
#include <linux/stat.h>

#include "rtmutex.h"

#define MAX_RT_TEST_THREADS	8
#define MAX_RT_TEST_MUTEXES	8

static spinlock_t rttest_lock;
static atomic_t rttest_event;

struct test_thread_data {
	int			opcode;
	int			opdata;
	int			mutexes[MAX_RT_TEST_MUTEXES];
	int			event;
	struct device		dev;
};

static struct test_thread_data thread_data[MAX_RT_TEST_THREADS];
static struct task_struct *threads[MAX_RT_TEST_THREADS];
static struct rt_mutex mutexes[MAX_RT_TEST_MUTEXES];

enum test_opcodes {
	RTTEST_NOP = 0,
	RTTEST_SCHEDOT,		/* 1 Sched other, data = nice */
	RTTEST_SCHEDRT,		/* 2 Sched fifo, data = prio */
	RTTEST_LOCK,		/* 3 Lock uninterruptible, data = lockindex */
	RTTEST_LOCKNOWAIT,	/* 4 Lock uninterruptible no wait in wakeup, data = lockindex */
	RTTEST_LOCKINT,		/* 5 Lock interruptible, data = lockindex */
	RTTEST_LOCKINTNOWAIT,	/* 6 Lock interruptible no wait in wakeup, data = lockindex */
	RTTEST_LOCKCONT,	/* 7 Continue locking after the wakeup delay */
	RTTEST_UNLOCK,		/* 8 Unlock, data = lockindex */
	/* 9, 10 - reserved for BKL commemoration */
	RTTEST_SIGNAL = 11,	/* 11 Signal other test thread, data = thread id */
	RTTEST_RESETEVENT = 98,	/* 98 Reset event counter */
	RTTEST_RESET = 99,	/* 99 Reset all pending operations */
};

static int handle_op(struct test_thread_data *td, int lockwakeup)
{
	int i, id, ret = -EINVAL;

	switch(td->opcode) {

	case RTTEST_NOP:
		return 0;

	case RTTEST_LOCKCONT:
		td->mutexes[td->opdata] = 1;
		td->event = atomic_add_return(1, &rttest_event);
		return 0;

	case RTTEST_RESET:
		for (i = 0; i < MAX_RT_TEST_MUTEXES; i++) {
			if (td->mutexes[i] == 4) {
				rt_mutex_unlock(&mutexes[i]);
				td->mutexes[i] = 0;
			}
		}
		return 0;

	case RTTEST_RESETEVENT:
		atomic_set(&rttest_event, 0);
		return 0;

	default:
		if (lockwakeup)
			return ret;
	}

	switch(td->opcode) {

	case RTTEST_LOCK:
	case RTTEST_LOCKNOWAIT:
		id = td->opdata;
		if (id < 0 || id >= MAX_RT_TEST_MUTEXES)
			return ret;

		td->mutexes[id] = 1;
		td->event = atomic_add_return(1, &rttest_event);
		rt_mutex_lock(&mutexes[id]);
		td->event = atomic_add_return(1, &rttest_event);
		td->mutexes[id] = 4;
		return 0;

	case RTTEST_LOCKINT:
	case RTTEST_LOCKINTNOWAIT:
		id = td->opdata;
		if (id < 0 || id >= MAX_RT_TEST_MUTEXES)
			return ret;

		td->mutexes[id] = 1;
		td->event = atomic_add_return(1, &rttest_event);
		ret = rt_mutex_lock_interruptible(&mutexes[id], 0);
		td->event = atomic_add_return(1, &rttest_event);
		td->mutexes[id] = ret ? 0 : 4;
		return ret ? -EINTR : 0;

	case RTTEST_UNLOCK:
		id = td->opdata;
		if (id < 0 || id >= MAX_RT_TEST_MUTEXES || td->mutexes[id] != 4)
			return ret;

		td->event = atomic_add_return(1, &rttest_event);
		rt_mutex_unlock(&mutexes[id]);
		td->event = atomic_add_return(1, &rttest_event);
		td->mutexes[id] = 0;
		return 0;

	default:
		break;
	}
	return ret;
}

/*
 * Schedule replacement for rtsem_down(). Only called for threads with
 * PF_MUTEX_TESTER set.
 *
 * This allows us to have finegrained control over the event flow.
 *
 */
void schedule_rt_mutex_test(struct rt_mutex *mutex)
{
	int tid, op, dat;
	struct test_thread_data *td;

	/* We have to lookup the task */
	for (tid = 0; tid < MAX_RT_TEST_THREADS; tid++) {
		if (threads[tid] == current)
			break;
	}

	BUG_ON(tid == MAX_RT_TEST_THREADS);

	td = &thread_data[tid];

	op = td->opcode;
	dat = td->opdata;

	switch (op) {
	case RTTEST_LOCK:
	case RTTEST_LOCKINT:
	case RTTEST_LOCKNOWAIT:
	case RTTEST_LOCKINTNOWAIT:
		if (mutex != &mutexes[dat])
			break;

		if (td->mutexes[dat] != 1)
			break;

		td->mutexes[dat] = 2;
		td->event = atomic_add_return(1, &rttest_event);
		break;

	default:
		break;
	}

	schedule();


	switch (op) {
	case RTTEST_LOCK:
	case RTTEST_LOCKINT:
		if (mutex != &mutexes[dat])
			return;

		if (td->mutexes[dat] != 2)
			return;

		td->mutexes[dat] = 3;
		td->event = atomic_add_return(1, &rttest_event);
		break;

	case RTTEST_LOCKNOWAIT:
	case RTTEST_LOCKINTNOWAIT:
		if (mutex != &mutexes[dat])
			return;

		if (td->mutexes[dat] != 2)
			return;

		td->mutexes[dat] = 1;
		td->event = atomic_add_return(1, &rttest_event);
		return;

	default:
		return;
	}

	td->opcode = 0;

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (td->opcode > 0) {
			int ret;

			set_current_state(TASK_RUNNING);
			ret = handle_op(td, 1);
			set_current_state(TASK_INTERRUPTIBLE);
			if (td->opcode == RTTEST_LOCKCONT)
				break;
			td->opcode = ret;
		}

		/* Wait for the next command to be executed */
		schedule();
	}

	/* Restore previous command and data */
	td->opcode = op;
	td->opdata = dat;
}

static int test_func(void *data)
{
	struct test_thread_data *td = data;
	int ret;

	current->flags |= PF_MUTEX_TESTER;
	set_freezable();
	allow_signal(SIGHUP);

	for(;;) {

		set_current_state(TASK_INTERRUPTIBLE);

		if (td->opcode > 0) {
			set_current_state(TASK_RUNNING);
			ret = handle_op(td, 0);
			set_current_state(TASK_INTERRUPTIBLE);
			td->opcode = ret;
		}

		/* Wait for the next command to be executed */
		schedule();
		try_to_freeze();

		if (signal_pending(current))
			flush_signals(current);

		if(kthread_should_stop())
			break;
	}
	return 0;
}

/**
 * sysfs_test_command - interface for test commands
 * @dev:	thread reference
 * @buf:	command for actual step
 * @count:	length of buffer
 *
 * command syntax:
 *
 * opcode:data
 */
static ssize_t sysfs_test_command(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct sched_param schedpar;
	struct test_thread_data *td;
	char cmdbuf[32];
	int op, dat, tid, ret;

	td = container_of(dev, struct test_thread_data, dev);
	tid = td->dev.id;

	/* strings from sysfs write are not 0 terminated! */
	if (count >= sizeof(cmdbuf))
		return -EINVAL;

	/* strip of \n: */
	if (buf[count-1] == '\n')
		count--;
	if (count < 1)
		return -EINVAL;

	memcpy(cmdbuf, buf, count);
	cmdbuf[count] = 0;

	if (sscanf(cmdbuf, "%d:%d", &op, &dat) != 2)
		return -EINVAL;

	switch (op) {
	case RTTEST_SCHEDOT:
		schedpar.sched_priority = 0;
		ret = sched_setscheduler(threads[tid], SCHED_NORMAL, &schedpar);
		if (ret)
			return ret;
		set_user_nice(current, 0);
		break;

	case RTTEST_SCHEDRT:
		schedpar.sched_priority = dat;
		ret = sched_setscheduler(threads[tid], SCHED_FIFO, &schedpar);
		if (ret)
			return ret;
		break;

	case RTTEST_SIGNAL:
		send_sig(SIGHUP, threads[tid], 0);
		break;

	default:
		if (td->opcode > 0)
			return -EBUSY;
		td->opdata = dat;
		td->opcode = op;
		wake_up_process(threads[tid]);
	}

	return count;
}

/**
 * sysfs_test_status - sysfs interface for rt tester
 * @dev:	thread to query
 * @buf:	char buffer to be filled with thread status info
 */
static ssize_t sysfs_test_status(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct test_thread_data *td;
	struct task_struct *tsk;
	char *curr = buf;
	int i;

	td = container_of(dev, struct test_thread_data, dev);
	tsk = threads[td->dev.id];

	spin_lock(&rttest_lock);

	curr += sprintf(curr,
		"O: %4d, E:%8d, S: 0x%08lx, P: %4d, N: %4d, B: %p, M:",
		td->opcode, td->event, tsk->state,
			(MAX_RT_PRIO - 1) - tsk->prio,
			(MAX_RT_PRIO - 1) - tsk->normal_prio,
		tsk->pi_blocked_on);

	for (i = MAX_RT_TEST_MUTEXES - 1; i >=0 ; i--)
		curr += sprintf(curr, "%d", td->mutexes[i]);

	spin_unlock(&rttest_lock);

	curr += sprintf(curr, ", T: %p, R: %p\n", tsk,
			mutexes[td->dev.id].owner);

	return curr - buf;
}

static DEVICE_ATTR(status, S_IRUSR, sysfs_test_status, NULL);
static DEVICE_ATTR(command, S_IWUSR, NULL, sysfs_test_command);

static struct bus_type rttest_subsys = {
	.name = "rttest",
	.dev_name = "rttest",
};

static int init_test_thread(int id)
{
	thread_data[id].dev.bus = &rttest_subsys;
	thread_data[id].dev.id = id;

	threads[id] = kthread_run(test_func, &thread_data[id], "rt-test-%d", id);
	if (IS_ERR(threads[id]))
		return PTR_ERR(threads[id]);

	return device_register(&thread_data[id].dev);
}

static int init_rttest(void)
{
	int ret, i;

	spin_lock_init(&rttest_lock);

	for (i = 0; i < MAX_RT_TEST_MUTEXES; i++)
		rt_mutex_init(&mutexes[i]);

	ret = subsys_system_register(&rttest_subsys, NULL);
	if (ret)
		return ret;

	for (i = 0; i < MAX_RT_TEST_THREADS; i++) {
		ret = init_test_thread(i);
		if (ret)
			break;
		ret = device_create_file(&thread_data[i].dev, &dev_attr_status);
		if (ret)
			break;
		ret = device_create_file(&thread_data[i].dev, &dev_attr_command);
		if (ret)
			break;
	}

	printk("Initializing RT-Tester: %s\n", ret ? "Failed" : "OK" );

	return ret;
}

device_initcall(init_rttest);
