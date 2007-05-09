/*
 * linux/kernel/workqueue.c
 *
 * Generic mechanism for defining kernel helper threads for running
 * arbitrary tasks in process context.
 *
 * Started by Ingo Molnar, Copyright (C) 2002
 *
 * Derived from the taskqueue/keventd code by:
 *
 *   David Woodhouse <dwmw2@infradead.org>
 *   Andrew Morton <andrewm@uow.edu.au>
 *   Kai Petzke <wpp@marie.physik.tu-berlin.de>
 *   Theodore Ts'o <tytso@mit.edu>
 *
 * Made to use alloc_percpu by Christoph Lameter <clameter@sgi.com>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/mempolicy.h>
#include <linux/freezer.h>
#include <linux/kallsyms.h>
#include <linux/debug_locks.h>

/*
 * The per-CPU workqueue (if single thread, we always use the first
 * possible cpu).
 */
struct cpu_workqueue_struct {

	spinlock_t lock;

	struct list_head worklist;
	wait_queue_head_t more_work;
	struct work_struct *current_work;

	struct workqueue_struct *wq;
	struct task_struct *thread;
	int should_stop;

	int run_depth;		/* Detect run_workqueue() recursion depth */
} ____cacheline_aligned;

/*
 * The externally visible workqueue abstraction is an array of
 * per-CPU workqueues:
 */
struct workqueue_struct {
	struct cpu_workqueue_struct *cpu_wq;
	struct list_head list;
	const char *name;
	int singlethread;
	int freezeable;		/* Freeze threads during suspend */
};

/* All the per-cpu workqueues on the system, for hotplug cpu to add/remove
   threads to each one as cpus come/go. */
static DEFINE_MUTEX(workqueue_mutex);
static LIST_HEAD(workqueues);

static int singlethread_cpu __read_mostly;
static cpumask_t cpu_singlethread_map __read_mostly;
/* optimization, we could use cpu_possible_map */
static cpumask_t cpu_populated_map __read_mostly;

/* If it's single threaded, it isn't in the list of workqueues. */
static inline int is_single_threaded(struct workqueue_struct *wq)
{
	return wq->singlethread;
}

static const cpumask_t *wq_cpu_map(struct workqueue_struct *wq)
{
	return is_single_threaded(wq)
		? &cpu_singlethread_map : &cpu_populated_map;
}

/*
 * Set the workqueue on which a work item is to be run
 * - Must *only* be called if the pending flag is set
 */
static inline void set_wq_data(struct work_struct *work, void *wq)
{
	unsigned long new;

	BUG_ON(!work_pending(work));

	new = (unsigned long) wq | (1UL << WORK_STRUCT_PENDING);
	new |= WORK_STRUCT_FLAG_MASK & *work_data_bits(work);
	atomic_long_set(&work->data, new);
}

static inline void *get_wq_data(struct work_struct *work)
{
	return (void *) (atomic_long_read(&work->data) & WORK_STRUCT_WQ_DATA_MASK);
}

static void insert_work(struct cpu_workqueue_struct *cwq,
				struct work_struct *work, int tail)
{
	set_wq_data(work, cwq);
	if (tail)
		list_add_tail(&work->entry, &cwq->worklist);
	else
		list_add(&work->entry, &cwq->worklist);
	wake_up(&cwq->more_work);
}

/* Preempt must be disabled. */
static void __queue_work(struct cpu_workqueue_struct *cwq,
			 struct work_struct *work)
{
	unsigned long flags;

	spin_lock_irqsave(&cwq->lock, flags);
	insert_work(cwq, work, 1);
	spin_unlock_irqrestore(&cwq->lock, flags);
}

/**
 * queue_work - queue work on a workqueue
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 *
 * We queue the work to the CPU it was submitted, but there is no
 * guarantee that it will be processed by that CPU.
 */
int fastcall queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	int ret = 0, cpu = get_cpu();

	if (!test_and_set_bit(WORK_STRUCT_PENDING, work_data_bits(work))) {
		if (unlikely(is_single_threaded(wq)))
			cpu = singlethread_cpu;
		BUG_ON(!list_empty(&work->entry));
		__queue_work(per_cpu_ptr(wq->cpu_wq, cpu), work);
		ret = 1;
	}
	put_cpu();
	return ret;
}
EXPORT_SYMBOL_GPL(queue_work);

void delayed_work_timer_fn(unsigned long __data)
{
	struct delayed_work *dwork = (struct delayed_work *)__data;
	struct workqueue_struct *wq = get_wq_data(&dwork->work);
	int cpu = smp_processor_id();

	if (unlikely(is_single_threaded(wq)))
		cpu = singlethread_cpu;

	__queue_work(per_cpu_ptr(wq->cpu_wq, cpu), &dwork->work);
}

/**
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 */
int fastcall queue_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay)
{
	int ret = 0;
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;

	timer_stats_timer_set_start_info(timer);
	if (delay == 0)
		return queue_work(wq, work);

	if (!test_and_set_bit(WORK_STRUCT_PENDING, work_data_bits(work))) {
		BUG_ON(timer_pending(timer));
		BUG_ON(!list_empty(&work->entry));

		/* This stores wq for the moment, for the timer_fn */
		set_wq_data(work, wq);
		timer->expires = jiffies + delay;
		timer->data = (unsigned long)dwork;
		timer->function = delayed_work_timer_fn;
		add_timer(timer);
		ret = 1;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(queue_delayed_work);

/**
 * queue_delayed_work_on - queue work on specific CPU after delay
 * @cpu: CPU number to execute work on
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Returns 0 if @work was already on a queue, non-zero otherwise.
 */
int queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay)
{
	int ret = 0;
	struct timer_list *timer = &dwork->timer;
	struct work_struct *work = &dwork->work;

	if (!test_and_set_bit(WORK_STRUCT_PENDING, work_data_bits(work))) {
		BUG_ON(timer_pending(timer));
		BUG_ON(!list_empty(&work->entry));

		/* This stores wq for the moment, for the timer_fn */
		set_wq_data(work, wq);
		timer->expires = jiffies + delay;
		timer->data = (unsigned long)dwork;
		timer->function = delayed_work_timer_fn;
		add_timer_on(timer, cpu);
		ret = 1;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(queue_delayed_work_on);

static void run_workqueue(struct cpu_workqueue_struct *cwq)
{
	spin_lock_irq(&cwq->lock);
	cwq->run_depth++;
	if (cwq->run_depth > 3) {
		/* morton gets to eat his hat */
		printk("%s: recursion depth exceeded: %d\n",
			__FUNCTION__, cwq->run_depth);
		dump_stack();
	}
	while (!list_empty(&cwq->worklist)) {
		struct work_struct *work = list_entry(cwq->worklist.next,
						struct work_struct, entry);
		work_func_t f = work->func;

		cwq->current_work = work;
		list_del_init(cwq->worklist.next);
		spin_unlock_irq(&cwq->lock);

		BUG_ON(get_wq_data(work) != cwq);
		if (!test_bit(WORK_STRUCT_NOAUTOREL, work_data_bits(work)))
			work_release(work);
		f(work);

		if (unlikely(in_atomic() || lockdep_depth(current) > 0)) {
			printk(KERN_ERR "BUG: workqueue leaked lock or atomic: "
					"%s/0x%08x/%d\n",
					current->comm, preempt_count(),
				       	current->pid);
			printk(KERN_ERR "    last function: ");
			print_symbol("%s\n", (unsigned long)f);
			debug_show_held_locks(current);
			dump_stack();
		}

		spin_lock_irq(&cwq->lock);
		cwq->current_work = NULL;
	}
	cwq->run_depth--;
	spin_unlock_irq(&cwq->lock);
}

/*
 * NOTE: the caller must not touch *cwq if this func returns true
 */
static int cwq_should_stop(struct cpu_workqueue_struct *cwq)
{
	int should_stop = cwq->should_stop;

	if (unlikely(should_stop)) {
		spin_lock_irq(&cwq->lock);
		should_stop = cwq->should_stop && list_empty(&cwq->worklist);
		if (should_stop)
			cwq->thread = NULL;
		spin_unlock_irq(&cwq->lock);
	}

	return should_stop;
}

static int worker_thread(void *__cwq)
{
	struct cpu_workqueue_struct *cwq = __cwq;
	DEFINE_WAIT(wait);
	struct k_sigaction sa;
	sigset_t blocked;

	if (!cwq->wq->freezeable)
		current->flags |= PF_NOFREEZE;

	set_user_nice(current, -5);

	/* Block and flush all signals */
	sigfillset(&blocked);
	sigprocmask(SIG_BLOCK, &blocked, NULL);
	flush_signals(current);

	/*
	 * We inherited MPOL_INTERLEAVE from the booting kernel.
	 * Set MPOL_DEFAULT to insure node local allocations.
	 */
	numa_default_policy();

	/* SIG_IGN makes children autoreap: see do_notify_parent(). */
	sa.sa.sa_handler = SIG_IGN;
	sa.sa.sa_flags = 0;
	siginitset(&sa.sa.sa_mask, sigmask(SIGCHLD));
	do_sigaction(SIGCHLD, &sa, (struct k_sigaction *)0);

	for (;;) {
		if (cwq->wq->freezeable)
			try_to_freeze();

		prepare_to_wait(&cwq->more_work, &wait, TASK_INTERRUPTIBLE);
		if (!cwq->should_stop && list_empty(&cwq->worklist))
			schedule();
		finish_wait(&cwq->more_work, &wait);

		if (cwq_should_stop(cwq))
			break;

		run_workqueue(cwq);
	}

	return 0;
}

struct wq_barrier {
	struct work_struct	work;
	struct completion	done;
};

static void wq_barrier_func(struct work_struct *work)
{
	struct wq_barrier *barr = container_of(work, struct wq_barrier, work);
	complete(&barr->done);
}

static void insert_wq_barrier(struct cpu_workqueue_struct *cwq,
					struct wq_barrier *barr, int tail)
{
	INIT_WORK(&barr->work, wq_barrier_func);
	__set_bit(WORK_STRUCT_PENDING, work_data_bits(&barr->work));

	init_completion(&barr->done);

	insert_work(cwq, &barr->work, tail);
}

static void flush_cpu_workqueue(struct cpu_workqueue_struct *cwq)
{
	if (cwq->thread == current) {
		/*
		 * Probably keventd trying to flush its own queue. So simply run
		 * it by hand rather than deadlocking.
		 */
		run_workqueue(cwq);
	} else {
		struct wq_barrier barr;
		int active = 0;

		spin_lock_irq(&cwq->lock);
		if (!list_empty(&cwq->worklist) || cwq->current_work != NULL) {
			insert_wq_barrier(cwq, &barr, 1);
			active = 1;
		}
		spin_unlock_irq(&cwq->lock);

		if (active)
			wait_for_completion(&barr.done);
	}
}

/**
 * flush_workqueue - ensure that any scheduled work has run to completion.
 * @wq: workqueue to flush
 *
 * Forces execution of the workqueue and blocks until its completion.
 * This is typically used in driver shutdown handlers.
 *
 * We sleep until all works which were queued on entry have been handled,
 * but we are not livelocked by new incoming ones.
 *
 * This function used to run the workqueues itself.  Now we just wait for the
 * helper threads to do it.
 */
void fastcall flush_workqueue(struct workqueue_struct *wq)
{
	const cpumask_t *cpu_map = wq_cpu_map(wq);
	int cpu;

	might_sleep();
	for_each_cpu_mask(cpu, *cpu_map)
		flush_cpu_workqueue(per_cpu_ptr(wq->cpu_wq, cpu));
}
EXPORT_SYMBOL_GPL(flush_workqueue);

static void wait_on_work(struct cpu_workqueue_struct *cwq,
				struct work_struct *work)
{
	struct wq_barrier barr;
	int running = 0;

	spin_lock_irq(&cwq->lock);
	if (unlikely(cwq->current_work == work)) {
		insert_wq_barrier(cwq, &barr, 0);
		running = 1;
	}
	spin_unlock_irq(&cwq->lock);

	if (unlikely(running))
		wait_for_completion(&barr.done);
}

/**
 * flush_work - block until a work_struct's callback has terminated
 * @wq: the workqueue on which the work is queued
 * @work: the work which is to be flushed
 *
 * flush_work() will attempt to cancel the work if it is queued.  If the work's
 * callback appears to be running, flush_work() will block until it has
 * completed.
 *
 * flush_work() is designed to be used when the caller is tearing down data
 * structures which the callback function operates upon.  It is expected that,
 * prior to calling flush_work(), the caller has arranged for the work to not
 * be requeued.
 */
void flush_work(struct workqueue_struct *wq, struct work_struct *work)
{
	const cpumask_t *cpu_map = wq_cpu_map(wq);
	struct cpu_workqueue_struct *cwq;
	int cpu;

	might_sleep();

	cwq = get_wq_data(work);
	/* Was it ever queued ? */
	if (!cwq)
		return;

	/*
	 * This work can't be re-queued, no need to re-check that
	 * get_wq_data() is still the same when we take cwq->lock.
	 */
	spin_lock_irq(&cwq->lock);
	list_del_init(&work->entry);
	work_release(work);
	spin_unlock_irq(&cwq->lock);

	for_each_cpu_mask(cpu, *cpu_map)
		wait_on_work(per_cpu_ptr(wq->cpu_wq, cpu), work);
}
EXPORT_SYMBOL_GPL(flush_work);


static struct workqueue_struct *keventd_wq;

/**
 * schedule_work - put work task in global workqueue
 * @work: job to be done
 *
 * This puts a job in the kernel-global workqueue.
 */
int fastcall schedule_work(struct work_struct *work)
{
	return queue_work(keventd_wq, work);
}
EXPORT_SYMBOL(schedule_work);

/**
 * schedule_delayed_work - put work task in global workqueue after delay
 * @dwork: job to be done
 * @delay: number of jiffies to wait or 0 for immediate execution
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue.
 */
int fastcall schedule_delayed_work(struct delayed_work *dwork,
					unsigned long delay)
{
	timer_stats_timer_set_start_info(&dwork->timer);
	return queue_delayed_work(keventd_wq, dwork, delay);
}
EXPORT_SYMBOL(schedule_delayed_work);

/**
 * schedule_delayed_work_on - queue work in global workqueue on CPU after delay
 * @cpu: cpu to use
 * @dwork: job to be done
 * @delay: number of jiffies to wait
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue on the specified CPU.
 */
int schedule_delayed_work_on(int cpu,
			struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work_on(cpu, keventd_wq, dwork, delay);
}
EXPORT_SYMBOL(schedule_delayed_work_on);

/**
 * schedule_on_each_cpu - call a function on each online CPU from keventd
 * @func: the function to call
 *
 * Returns zero on success.
 * Returns -ve errno on failure.
 *
 * Appears to be racy against CPU hotplug.
 *
 * schedule_on_each_cpu() is very slow.
 */
int schedule_on_each_cpu(work_func_t func)
{
	int cpu;
	struct work_struct *works;

	works = alloc_percpu(struct work_struct);
	if (!works)
		return -ENOMEM;

	preempt_disable();		/* CPU hotplug */
	for_each_online_cpu(cpu) {
		struct work_struct *work = per_cpu_ptr(works, cpu);

		INIT_WORK(work, func);
		set_bit(WORK_STRUCT_PENDING, work_data_bits(work));
		__queue_work(per_cpu_ptr(keventd_wq->cpu_wq, cpu), work);
	}
	preempt_enable();
	flush_workqueue(keventd_wq);
	free_percpu(works);
	return 0;
}

void flush_scheduled_work(void)
{
	flush_workqueue(keventd_wq);
}
EXPORT_SYMBOL(flush_scheduled_work);

void flush_work_keventd(struct work_struct *work)
{
	flush_work(keventd_wq, work);
}
EXPORT_SYMBOL(flush_work_keventd);

/**
 * cancel_rearming_delayed_workqueue - reliably kill off a delayed work whose handler rearms the delayed work.
 * @wq:   the controlling workqueue structure
 * @dwork: the delayed work struct
 */
void cancel_rearming_delayed_workqueue(struct workqueue_struct *wq,
				       struct delayed_work *dwork)
{
	/* Was it ever queued ? */
	if (!get_wq_data(&dwork->work))
		return;

	while (!cancel_delayed_work(dwork))
		flush_workqueue(wq);
}
EXPORT_SYMBOL(cancel_rearming_delayed_workqueue);

/**
 * cancel_rearming_delayed_work - reliably kill off a delayed keventd work whose handler rearms the delayed work.
 * @dwork: the delayed work struct
 */
void cancel_rearming_delayed_work(struct delayed_work *dwork)
{
	cancel_rearming_delayed_workqueue(keventd_wq, dwork);
}
EXPORT_SYMBOL(cancel_rearming_delayed_work);

/**
 * execute_in_process_context - reliably execute the routine with user context
 * @fn:		the function to execute
 * @ew:		guaranteed storage for the execute work structure (must
 *		be available when the work executes)
 *
 * Executes the function immediately if process context is available,
 * otherwise schedules the function for delayed execution.
 *
 * Returns:	0 - function was executed
 *		1 - function was scheduled for execution
 */
int execute_in_process_context(work_func_t fn, struct execute_work *ew)
{
	if (!in_interrupt()) {
		fn(&ew->work);
		return 0;
	}

	INIT_WORK(&ew->work, fn);
	schedule_work(&ew->work);

	return 1;
}
EXPORT_SYMBOL_GPL(execute_in_process_context);

int keventd_up(void)
{
	return keventd_wq != NULL;
}

int current_is_keventd(void)
{
	struct cpu_workqueue_struct *cwq;
	int cpu = smp_processor_id();	/* preempt-safe: keventd is per-cpu */
	int ret = 0;

	BUG_ON(!keventd_wq);

	cwq = per_cpu_ptr(keventd_wq->cpu_wq, cpu);
	if (current == cwq->thread)
		ret = 1;

	return ret;

}

static struct cpu_workqueue_struct *
init_cpu_workqueue(struct workqueue_struct *wq, int cpu)
{
	struct cpu_workqueue_struct *cwq = per_cpu_ptr(wq->cpu_wq, cpu);

	cwq->wq = wq;
	spin_lock_init(&cwq->lock);
	INIT_LIST_HEAD(&cwq->worklist);
	init_waitqueue_head(&cwq->more_work);

	return cwq;
}

static int create_workqueue_thread(struct cpu_workqueue_struct *cwq, int cpu)
{
	struct workqueue_struct *wq = cwq->wq;
	const char *fmt = is_single_threaded(wq) ? "%s" : "%s/%d";
	struct task_struct *p;

	p = kthread_create(worker_thread, cwq, fmt, wq->name, cpu);
	/*
	 * Nobody can add the work_struct to this cwq,
	 *	if (caller is __create_workqueue)
	 *		nobody should see this wq
	 *	else // caller is CPU_UP_PREPARE
	 *		cpu is not on cpu_online_map
	 * so we can abort safely.
	 */
	if (IS_ERR(p))
		return PTR_ERR(p);

	cwq->thread = p;
	cwq->should_stop = 0;

	return 0;
}

static void start_workqueue_thread(struct cpu_workqueue_struct *cwq, int cpu)
{
	struct task_struct *p = cwq->thread;

	if (p != NULL) {
		if (cpu >= 0)
			kthread_bind(p, cpu);
		wake_up_process(p);
	}
}

struct workqueue_struct *__create_workqueue(const char *name,
					    int singlethread, int freezeable)
{
	struct workqueue_struct *wq;
	struct cpu_workqueue_struct *cwq;
	int err = 0, cpu;

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return NULL;

	wq->cpu_wq = alloc_percpu(struct cpu_workqueue_struct);
	if (!wq->cpu_wq) {
		kfree(wq);
		return NULL;
	}

	wq->name = name;
	wq->singlethread = singlethread;
	wq->freezeable = freezeable;
	INIT_LIST_HEAD(&wq->list);

	if (singlethread) {
		cwq = init_cpu_workqueue(wq, singlethread_cpu);
		err = create_workqueue_thread(cwq, singlethread_cpu);
		start_workqueue_thread(cwq, -1);
	} else {
		mutex_lock(&workqueue_mutex);
		list_add(&wq->list, &workqueues);

		for_each_possible_cpu(cpu) {
			cwq = init_cpu_workqueue(wq, cpu);
			if (err || !cpu_online(cpu))
				continue;
			err = create_workqueue_thread(cwq, cpu);
			start_workqueue_thread(cwq, cpu);
		}
		mutex_unlock(&workqueue_mutex);
	}

	if (err) {
		destroy_workqueue(wq);
		wq = NULL;
	}
	return wq;
}
EXPORT_SYMBOL_GPL(__create_workqueue);

static void cleanup_workqueue_thread(struct cpu_workqueue_struct *cwq, int cpu)
{
	struct wq_barrier barr;
	int alive = 0;

	spin_lock_irq(&cwq->lock);
	if (cwq->thread != NULL) {
		insert_wq_barrier(cwq, &barr, 1);
		cwq->should_stop = 1;
		alive = 1;
	}
	spin_unlock_irq(&cwq->lock);

	if (alive) {
		wait_for_completion(&barr.done);

		while (unlikely(cwq->thread != NULL))
			cpu_relax();
		/*
		 * Wait until cwq->thread unlocks cwq->lock,
		 * it won't touch *cwq after that.
		 */
		smp_rmb();
		spin_unlock_wait(&cwq->lock);
	}
}

/**
 * destroy_workqueue - safely terminate a workqueue
 * @wq: target workqueue
 *
 * Safely destroy a workqueue. All work currently pending will be done first.
 */
void destroy_workqueue(struct workqueue_struct *wq)
{
	const cpumask_t *cpu_map = wq_cpu_map(wq);
	struct cpu_workqueue_struct *cwq;
	int cpu;

	mutex_lock(&workqueue_mutex);
	list_del(&wq->list);
	mutex_unlock(&workqueue_mutex);

	for_each_cpu_mask(cpu, *cpu_map) {
		cwq = per_cpu_ptr(wq->cpu_wq, cpu);
		cleanup_workqueue_thread(cwq, cpu);
	}

	free_percpu(wq->cpu_wq);
	kfree(wq);
}
EXPORT_SYMBOL_GPL(destroy_workqueue);

static int __devinit workqueue_cpu_callback(struct notifier_block *nfb,
						unsigned long action,
						void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct cpu_workqueue_struct *cwq;
	struct workqueue_struct *wq;

	switch (action) {
	case CPU_LOCK_ACQUIRE:
		mutex_lock(&workqueue_mutex);
		return NOTIFY_OK;

	case CPU_LOCK_RELEASE:
		mutex_unlock(&workqueue_mutex);
		return NOTIFY_OK;

	case CPU_UP_PREPARE:
		cpu_set(cpu, cpu_populated_map);
	}

	list_for_each_entry(wq, &workqueues, list) {
		cwq = per_cpu_ptr(wq->cpu_wq, cpu);

		switch (action) {
		case CPU_UP_PREPARE:
			if (!create_workqueue_thread(cwq, cpu))
				break;
			printk(KERN_ERR "workqueue for %i failed\n", cpu);
			return NOTIFY_BAD;

		case CPU_ONLINE:
			start_workqueue_thread(cwq, cpu);
			break;

		case CPU_UP_CANCELED:
			start_workqueue_thread(cwq, -1);
		case CPU_DEAD:
			cleanup_workqueue_thread(cwq, cpu);
			break;
		}
	}

	return NOTIFY_OK;
}

void __init init_workqueues(void)
{
	cpu_populated_map = cpu_online_map;
	singlethread_cpu = first_cpu(cpu_possible_map);
	cpu_singlethread_map = cpumask_of_cpu(singlethread_cpu);
	hotcpu_notifier(workqueue_cpu_callback, 0);
	keventd_wq = create_workqueue("events");
	BUG_ON(!keventd_wq);
}
