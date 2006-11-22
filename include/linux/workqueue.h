/*
 * workqueue.h --- work queue handling for Linux.
 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>
#include <linux/bitops.h>

struct workqueue_struct;

typedef void (*work_func_t)(void *data);

struct work_struct {
	unsigned long pending;
	struct list_head entry;
	work_func_t func;
	void *data;
	void *wq_data;
};

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;
};

struct execute_work {
	struct work_struct work;
};

#define __WORK_INITIALIZER(n, f, d) {				\
        .entry	= { &(n).entry, &(n).entry },			\
	.func = (f),						\
	.data = (d),						\
	}

#define __DELAYED_WORK_INITIALIZER(n, f, d) {			\
	.work = __WORK_INITIALIZER((n).work, (f), (d)),		\
	.timer = TIMER_INITIALIZER(NULL, 0, 0),			\
	}

#define DECLARE_WORK(n, f, d)					\
	struct work_struct n = __WORK_INITIALIZER(n, f, d)

#define DECLARE_DELAYED_WORK(n, f, d)				\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, d)

/*
 * initialize a work item's function and data pointers
 */
#define PREPARE_WORK(_work, _func, _data)			\
	do {							\
		(_work)->func = (_func);			\
		(_work)->data = (_data);			\
	} while (0)

#define PREPARE_DELAYED_WORK(_work, _func, _data)		\
	PREPARE_WORK(&(_work)->work, (_func), (_data))

/*
 * initialize all of a work item in one go
 */
#define INIT_WORK(_work, _func, _data)				\
	do {							\
		INIT_LIST_HEAD(&(_work)->entry);		\
		(_work)->pending = 0;				\
		PREPARE_WORK((_work), (_func), (_data));	\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func, _data)		\
	do {							\
		INIT_WORK(&(_work)->work, (_func), (_data));	\
		init_timer(&(_work)->timer);			\
	} while (0)


extern struct workqueue_struct *__create_workqueue(const char *name,
						    int singlethread);
#define create_workqueue(name) __create_workqueue((name), 0)
#define create_singlethread_workqueue(name) __create_workqueue((name), 1)

extern void destroy_workqueue(struct workqueue_struct *wq);

extern int FASTCALL(queue_work(struct workqueue_struct *wq, struct work_struct *work));
extern int FASTCALL(queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *work, unsigned long delay));
extern int queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
	struct delayed_work *work, unsigned long delay);
extern void FASTCALL(flush_workqueue(struct workqueue_struct *wq));

extern int FASTCALL(schedule_work(struct work_struct *work));
extern int FASTCALL(schedule_delayed_work(struct delayed_work *work, unsigned long delay));

extern int schedule_delayed_work_on(int cpu, struct delayed_work *work, unsigned long delay);
extern int schedule_on_each_cpu(work_func_t func, void *info);
extern void flush_scheduled_work(void);
extern int current_is_keventd(void);
extern int keventd_up(void);

extern void init_workqueues(void);
void cancel_rearming_delayed_work(struct delayed_work *work);
void cancel_rearming_delayed_workqueue(struct workqueue_struct *,
				       struct delayed_work *);
int execute_in_process_context(work_func_t fn, void *, struct execute_work *);

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
		clear_bit(0, &work->work.pending);
	return ret;
}

#endif
