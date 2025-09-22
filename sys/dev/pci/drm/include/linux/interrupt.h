/* Public domain. */

#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <sys/task.h>

#include <machine/intr.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/atomic.h>
#include <linux/irqreturn.h>

struct seq_file;

#define IRQF_SHARED	0x0001
#define IRQF_ONESHOT	0x0002
#define IRQF_NO_AUTOEN	0x0004

#define IRQF_TRIGGER_RISING	0x1000
#define IRQF_TRIGGER_FALLING	0x2000

#define request_irq(irq, hdlr, flags, name, dev)	(0)

static inline void
free_irq(unsigned int irq, void *dev)
{
}

static inline void
disable_irq(u_int irq)
{
}

static inline void
enable_irq(u_int irq)
{
}

typedef irqreturn_t (*irq_handler_t)(int, void *);

static inline int
devm_request_threaded_irq(struct device *dev, u_int irq, irq_handler_t handler,
    irq_handler_t thread_fn, u_int irqflags, const char *devname, void *arg)
{
	return 0;
}

struct tasklet_struct {
	union {
		void (*func)(unsigned long);
		void (*callback)(struct tasklet_struct *);
	};
	bool use_callback;
	unsigned long data;
	unsigned long state;
	atomic_t count;
	struct task task;
};

#define TASKLET_STATE_SCHED	1
#define TASKLET_STATE_RUN	0

#define from_tasklet(x, t, f) \
	container_of(t, typeof(*x), f)

extern struct taskq *taskletq;
void tasklet_run(void *);
void tasklet_unlock_wait(struct tasklet_struct *);
void tasklet_unlock_spin_wait(struct tasklet_struct *);

static inline void
tasklet_init(struct tasklet_struct *ts, void (*func)(unsigned long),
    unsigned long data)
{
	ts->func = func;
	ts->data = data;
	ts->state = 0;
	atomic_set(&ts->count, 0);
	ts->use_callback = false;
	task_set(&ts->task, tasklet_run, ts);
}

static inline void
tasklet_setup(struct tasklet_struct *ts,
    void (*callback)(struct tasklet_struct *))
{
	ts->callback = callback;
	ts->data = 0;
	ts->state = 0;
	atomic_set(&ts->count, 0);
	ts->use_callback = true;
	task_set(&ts->task, tasklet_run, ts);
}

static inline int
tasklet_trylock(struct tasklet_struct *ts)
{
	return !test_and_set_bit(TASKLET_STATE_RUN, &ts->state);
}

static inline void
tasklet_unlock(struct tasklet_struct *ts)
{
	smp_mb__before_atomic();
	clear_bit(TASKLET_STATE_RUN, &ts->state);
}

static inline void
tasklet_kill(struct tasklet_struct *ts)
{
	clear_bit(TASKLET_STATE_SCHED, &ts->state);
	task_del(taskletq, &ts->task);
	tasklet_unlock_wait(ts);
}

static inline void
tasklet_schedule(struct tasklet_struct *ts)
{
	set_bit(TASKLET_STATE_SCHED, &ts->state);
	task_add(taskletq, &ts->task);
}

static inline void
tasklet_hi_schedule(struct tasklet_struct *ts)
{
	set_bit(TASKLET_STATE_SCHED, &ts->state);
	task_add(taskletq, &ts->task);
}

static inline void 
tasklet_disable_nosync(struct tasklet_struct *ts)
{
	atomic_inc(&ts->count);
	smp_mb__after_atomic();
}

static inline void
tasklet_enable(struct tasklet_struct *ts)
{
	smp_mb__before_atomic();
	atomic_dec(&ts->count);
}

#endif
