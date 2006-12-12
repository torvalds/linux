/*
 * workqueue.h --- work queue handling for Linux.
 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>
#include <linux/bitops.h>

struct workqueue_struct;

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);

struct work_struct {
	/* the first word is the work queue pointer and the flags rolled into
	 * one */
	unsigned long management;
#define WORK_STRUCT_PENDING 0		/* T if work item pending execution */
#define WORK_STRUCT_NOAUTOREL 1		/* F if work item automatically released on exec */
#define WORK_STRUCT_FLAG_MASK (3UL)
#define WORK_STRUCT_WQ_DATA_MASK (~WORK_STRUCT_FLAG_MASK)
	struct list_head entry;
	work_func_t func;
};

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;
};

struct execute_work {
	struct work_struct work;
};

#define __WORK_INITIALIZER(n, f) {				\
	.management = 0,					\
        .entry	= { &(n).entry, &(n).entry },			\
	.func = (f),						\
	}

#define __WORK_INITIALIZER_NAR(n, f) {				\
	.management = (1 << WORK_STRUCT_NOAUTOREL),		\
        .entry	= { &(n).entry, &(n).entry },			\
	.func = (f),						\
	}

#define __DELAYED_WORK_INITIALIZER(n, f) {			\
	.work = __WORK_INITIALIZER((n).work, (f)),		\
	.timer = TIMER_INITIALIZER(NULL, 0, 0),			\
	}

#define __DELAYED_WORK_INITIALIZER_NAR(n, f) {			\
	.work = __WORK_INITIALIZER_NAR((n).work, (f)),		\
	.timer = TIMER_INITIALIZER(NULL, 0, 0),			\
	}

#define DECLARE_WORK(n, f)					\
	struct work_struct n = __WORK_INITIALIZER(n, f)

#define DECLARE_WORK_NAR(n, f)					\
	struct work_struct n = __WORK_INITIALIZER_NAR(n, f)

#define DECLARE_DELAYED_WORK(n, f)				\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f)

#define DECLARE_DELAYED_WORK_NAR(n, f)			\
	struct dwork_struct n = __DELAYED_WORK_INITIALIZER_NAR(n, f)

/*
 * initialize a work item's function pointer
 */
#define PREPARE_WORK(_work, _func)				\
	do {							\
		(_work)->func = (_func);			\
	} while (0)

#define PREPARE_DELAYED_WORK(_work, _func)			\
	PREPARE_WORK(&(_work)->work, (_func))

/*
 * initialize all of a work item in one go
 */
#define INIT_WORK(_work, _func)					\
	do {							\
		(_work)->management = 0;			\
		INIT_LIST_HEAD(&(_work)->entry);		\
		PREPARE_WORK((_work), (_func));			\
	} while (0)

#define INIT_WORK_NAR(_work, _func)					\
	do {								\
		(_work)->management = (1 << WORK_STRUCT_NOAUTOREL);	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		PREPARE_WORK((_work), (_func));				\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)				\
	do {							\
		INIT_WORK(&(_work)->work, (_func));		\
		init_timer(&(_work)->timer);			\
	} while (0)

#define INIT_DELAYED_WORK_NAR(_work, _func)			\
	do {							\
		INIT_WORK_NAR(&(_work)->work, (_func));		\
		init_timer(&(_work)->timer);			\
	} while (0)

/**
 * work_pending - Find out whether a work item is currently pending
 * @work: The work item in question
 */
#define work_pending(work) \
	test_bit(WORK_STRUCT_PENDING, &(work)->management)

/**
 * delayed_work_pending - Find out whether a delayable work item is currently
 * pending
 * @work: The work item in question
 */
#define delayed_work_pending(work) \
	test_bit(WORK_STRUCT_PENDING, &(work)->work.management)

/**
 * work_release - Release a work item under execution
 * @work: The work item to release
 *
 * This is used to release a work item that has been initialised with automatic
 * release mode disabled (WORK_STRUCT_NOAUTOREL is set).  This gives the work
 * function the opportunity to grab auxiliary data from the container of the
 * work_struct before clearing the pending bit as the work_struct may be
 * subject to deallocation the moment the pending bit is cleared.
 *
 * In such a case, this should be called in the work function after it has
 * fetched any data it may require from the containter of the work_struct.
 * After this function has been called, the work_struct may be scheduled for
 * further execution or it may be deallocated unless other precautions are
 * taken.
 *
 * This should also be used to release a delayed work item.
 */
#define work_release(work) \
	clear_bit(WORK_STRUCT_PENDING, &(work)->management)


extern struct workqueue_struct *__create_workqueue(const char *name,
						    int singlethread,
						    int freezeable);
#define create_workqueue(name) __create_workqueue((name), 0, 0)
#define create_freezeable_workqueue(name) __create_workqueue((name), 0, 1)
#define create_singlethread_workqueue(name) __create_workqueue((name), 1, 0)

extern void destroy_workqueue(struct workqueue_struct *wq);

extern int FASTCALL(queue_work(struct workqueue_struct *wq, struct work_struct *work));
extern int FASTCALL(queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *work, unsigned long delay));
extern int queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
	struct delayed_work *work, unsigned long delay);
extern void FASTCALL(flush_workqueue(struct workqueue_struct *wq));

extern int FASTCALL(schedule_work(struct work_struct *work));
extern int FASTCALL(run_scheduled_work(struct work_struct *work));
extern int FASTCALL(schedule_delayed_work(struct delayed_work *work, unsigned long delay));

extern int schedule_delayed_work_on(int cpu, struct delayed_work *work, unsigned long delay);
extern int schedule_on_each_cpu(work_func_t func);
extern void flush_scheduled_work(void);
extern int current_is_keventd(void);
extern int keventd_up(void);

extern void init_workqueues(void);
void cancel_rearming_delayed_work(struct delayed_work *work);
void cancel_rearming_delayed_workqueue(struct workqueue_struct *,
				       struct delayed_work *);
int execute_in_process_context(work_func_t fn, struct execute_work *);

/*
 * Kill off a pending schedule_delayed_work().  Note that the work callback
 * function may still be running on return from cancel_delayed_work().  Run
 * flush_scheduled_work() to wait on it.
 */
static inline int cancel_delayed_work(struct delayed_work *work)
{
	int ret;

	ret = del_timer_sync(&work->timer);
	if (ret)
		clear_bit(WORK_STRUCT_PENDING, &work->work.management);
	return ret;
}

#endif
