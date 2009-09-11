#include "builtin.h"

#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"

#include "util/parse-options.h"

#include "perf.h"
#include "util/debug.h"

#include "util/trace-event.h"
#include <sys/types.h>


#define MAX_CPUS 4096

static char			const *input_name = "perf.data";
static int			input;
static unsigned long		page_size;
static unsigned long		mmap_window = 32;

static unsigned long		total_comm = 0;

static struct rb_root		threads;
static struct thread		*last_match;

static struct perf_header	*header;
static u64			sample_type;

static char			default_sort_order[] = "avg, max, switch, runtime";
static char			*sort_order = default_sort_order;


/*
 * Scheduler benchmarks
 */
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/prctl.h>

#include <linux/unistd.h>

#include <semaphore.h>
#include <pthread.h>
#include <signal.h>
#include <values.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>

#include <stdio.h>

#define PR_SET_NAME	15               /* Set process name */

#define BUG_ON(x)	assert(!(x))

#define DEBUG		0

typedef unsigned long long nsec_t;

static nsec_t run_measurement_overhead;
static nsec_t sleep_measurement_overhead;

static nsec_t get_nsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void burn_nsecs(nsec_t nsecs)
{
	nsec_t T0 = get_nsecs(), T1;

	do {
		T1 = get_nsecs();
	} while (T1 + run_measurement_overhead < T0 + nsecs);
}

static void sleep_nsecs(nsec_t nsecs)
{
	struct timespec ts;

	ts.tv_nsec = nsecs % 999999999;
	ts.tv_sec = nsecs / 999999999;

	nanosleep(&ts, NULL);
}

static void calibrate_run_measurement_overhead(void)
{
	nsec_t T0, T1, delta, min_delta = 1000000000ULL;
	int i;

	for (i = 0; i < 10; i++) {
		T0 = get_nsecs();
		burn_nsecs(0);
		T1 = get_nsecs();
		delta = T1-T0;
		min_delta = min(min_delta, delta);
	}
	run_measurement_overhead = min_delta;

	printf("run measurement overhead: %Ld nsecs\n", min_delta);
}

static void calibrate_sleep_measurement_overhead(void)
{
	nsec_t T0, T1, delta, min_delta = 1000000000ULL;
	int i;

	for (i = 0; i < 10; i++) {
		T0 = get_nsecs();
		sleep_nsecs(10000);
		T1 = get_nsecs();
		delta = T1-T0;
		min_delta = min(min_delta, delta);
	}
	min_delta -= 10000;
	sleep_measurement_overhead = min_delta;

	printf("sleep measurement overhead: %Ld nsecs\n", min_delta);
}

#define COMM_LEN	20
#define SYM_LEN		129

#define MAX_PID		65536

static unsigned long nr_tasks;

struct sched_event;

struct task_desc {
	unsigned long		nr;
	unsigned long		pid;
	char			comm[COMM_LEN];

	unsigned long		nr_events;
	unsigned long		curr_event;
	struct sched_event	**events;

	pthread_t		thread;
	sem_t			sleep_sem;

	sem_t			ready_for_work;
	sem_t			work_done_sem;

	nsec_t			cpu_usage;
};

enum sched_event_type {
	SCHED_EVENT_RUN,
	SCHED_EVENT_SLEEP,
	SCHED_EVENT_WAKEUP,
};

struct sched_event {
	enum sched_event_type	type;
	nsec_t			timestamp;
	nsec_t			duration;
	unsigned long		nr;
	int			specific_wait;
	sem_t			*wait_sem;
	struct task_desc	*wakee;
};

static struct task_desc		*pid_to_task[MAX_PID];

static struct task_desc		**tasks;

static pthread_mutex_t		start_work_mutex = PTHREAD_MUTEX_INITIALIZER;
static nsec_t			start_time;

static pthread_mutex_t		work_done_wait_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned long		nr_run_events;
static unsigned long		nr_sleep_events;
static unsigned long		nr_wakeup_events;

static unsigned long		nr_sleep_corrections;
static unsigned long		nr_run_events_optimized;

static struct sched_event *
get_new_event(struct task_desc *task, nsec_t timestamp)
{
	struct sched_event *event = calloc(1, sizeof(*event));
	unsigned long idx = task->nr_events;
	size_t size;

	event->timestamp = timestamp;
	event->nr = idx;

	task->nr_events++;
	size = sizeof(struct sched_event *) * task->nr_events;
	task->events = realloc(task->events, size);
	BUG_ON(!task->events);

	task->events[idx] = event;

	return event;
}

static struct sched_event *last_event(struct task_desc *task)
{
	if (!task->nr_events)
		return NULL;

	return task->events[task->nr_events - 1];
}

static void
add_sched_event_run(struct task_desc *task, nsec_t timestamp, u64 duration)
{
	struct sched_event *event, *curr_event = last_event(task);

	/*
	 * optimize an existing RUN event by merging this one
	 * to it:
	 */
	if (curr_event && curr_event->type == SCHED_EVENT_RUN) {
		nr_run_events_optimized++;
		curr_event->duration += duration;
		return;
	}

	event = get_new_event(task, timestamp);

	event->type = SCHED_EVENT_RUN;
	event->duration = duration;

	nr_run_events++;
}

static unsigned long		targetless_wakeups;
static unsigned long		multitarget_wakeups;

static void
add_sched_event_wakeup(struct task_desc *task, nsec_t timestamp,
		       struct task_desc *wakee)
{
	struct sched_event *event, *wakee_event;

	event = get_new_event(task, timestamp);
	event->type = SCHED_EVENT_WAKEUP;
	event->wakee = wakee;

	wakee_event = last_event(wakee);
	if (!wakee_event || wakee_event->type != SCHED_EVENT_SLEEP) {
		targetless_wakeups++;
		return;
	}
	if (wakee_event->wait_sem) {
		multitarget_wakeups++;
		return;
	}

	wakee_event->wait_sem = calloc(1, sizeof(*wakee_event->wait_sem));
	sem_init(wakee_event->wait_sem, 0, 0);
	wakee_event->specific_wait = 1;
	event->wait_sem = wakee_event->wait_sem;

	nr_wakeup_events++;
}

static void
add_sched_event_sleep(struct task_desc *task, nsec_t timestamp,
		      u64 task_state __used)
{
	struct sched_event *event = get_new_event(task, timestamp);

	event->type = SCHED_EVENT_SLEEP;

	nr_sleep_events++;
}

static struct task_desc *register_pid(unsigned long pid, const char *comm)
{
	struct task_desc *task;

	BUG_ON(pid >= MAX_PID);

	task = pid_to_task[pid];

	if (task)
		return task;

	task = calloc(1, sizeof(*task));
	task->pid = pid;
	task->nr = nr_tasks;
	strcpy(task->comm, comm);
	/*
	 * every task starts in sleeping state - this gets ignored
	 * if there's no wakeup pointing to this sleep state:
	 */
	add_sched_event_sleep(task, 0, 0);

	pid_to_task[pid] = task;
	nr_tasks++;
	tasks = realloc(tasks, nr_tasks*sizeof(struct task_task *));
	BUG_ON(!tasks);
	tasks[task->nr] = task;

	if (verbose)
		printf("registered task #%ld, PID %ld (%s)\n", nr_tasks, pid, comm);

	return task;
}


static void print_task_traces(void)
{
	struct task_desc *task;
	unsigned long i;

	for (i = 0; i < nr_tasks; i++) {
		task = tasks[i];
		printf("task %6ld (%20s:%10ld), nr_events: %ld\n",
			task->nr, task->comm, task->pid, task->nr_events);
	}
}

static void add_cross_task_wakeups(void)
{
	struct task_desc *task1, *task2;
	unsigned long i, j;

	for (i = 0; i < nr_tasks; i++) {
		task1 = tasks[i];
		j = i + 1;
		if (j == nr_tasks)
			j = 0;
		task2 = tasks[j];
		add_sched_event_wakeup(task1, 0, task2);
	}
}

static void
process_sched_event(struct task_desc *this_task __used, struct sched_event *event)
{
	int ret = 0;
	nsec_t now;
	long long delta;

	now = get_nsecs();
	delta = start_time + event->timestamp - now;

	switch (event->type) {
		case SCHED_EVENT_RUN:
			burn_nsecs(event->duration);
			break;
		case SCHED_EVENT_SLEEP:
			if (event->wait_sem)
				ret = sem_wait(event->wait_sem);
			BUG_ON(ret);
			break;
		case SCHED_EVENT_WAKEUP:
			if (event->wait_sem)
				ret = sem_post(event->wait_sem);
			BUG_ON(ret);
			break;
		default:
			BUG_ON(1);
	}
}

static nsec_t get_cpu_usage_nsec_parent(void)
{
	struct rusage ru;
	nsec_t sum;
	int err;

	err = getrusage(RUSAGE_SELF, &ru);
	BUG_ON(err);

	sum =  ru.ru_utime.tv_sec*1e9 + ru.ru_utime.tv_usec*1e3;
	sum += ru.ru_stime.tv_sec*1e9 + ru.ru_stime.tv_usec*1e3;

	return sum;
}

static nsec_t get_cpu_usage_nsec_self(void)
{
	char filename [] = "/proc/1234567890/sched";
	unsigned long msecs, nsecs;
	char *line = NULL;
	nsec_t total = 0;
	size_t len = 0;
	ssize_t chars;
	FILE *file;
	int ret;

	sprintf(filename, "/proc/%d/sched", getpid());
	file = fopen(filename, "r");
	BUG_ON(!file);

	while ((chars = getline(&line, &len, file)) != -1) {
		ret = sscanf(line, "se.sum_exec_runtime : %ld.%06ld\n",
			&msecs, &nsecs);
		if (ret == 2) {
			total = msecs*1e6 + nsecs;
			break;
		}
	}
	if (line)
		free(line);
	fclose(file);

	return total;
}

static void *thread_func(void *ctx)
{
	struct task_desc *this_task = ctx;
	nsec_t cpu_usage_0, cpu_usage_1;
	unsigned long i, ret;
	char comm2[22];

	sprintf(comm2, ":%s", this_task->comm);
	prctl(PR_SET_NAME, comm2);

again:
	ret = sem_post(&this_task->ready_for_work);
	BUG_ON(ret);
	ret = pthread_mutex_lock(&start_work_mutex);
	BUG_ON(ret);
	ret = pthread_mutex_unlock(&start_work_mutex);
	BUG_ON(ret);

	cpu_usage_0 = get_cpu_usage_nsec_self();

	for (i = 0; i < this_task->nr_events; i++) {
		this_task->curr_event = i;
		process_sched_event(this_task, this_task->events[i]);
	}

	cpu_usage_1 = get_cpu_usage_nsec_self();
	this_task->cpu_usage = cpu_usage_1 - cpu_usage_0;

	ret = sem_post(&this_task->work_done_sem);
	BUG_ON(ret);

	ret = pthread_mutex_lock(&work_done_wait_mutex);
	BUG_ON(ret);
	ret = pthread_mutex_unlock(&work_done_wait_mutex);
	BUG_ON(ret);

	goto again;
}

static void create_tasks(void)
{
	struct task_desc *task;
	pthread_attr_t attr;
	unsigned long i;
	int err;

	err = pthread_attr_init(&attr);
	BUG_ON(err);
	err = pthread_attr_setstacksize(&attr, (size_t)(16*1024));
	BUG_ON(err);
	err = pthread_mutex_lock(&start_work_mutex);
	BUG_ON(err);
	err = pthread_mutex_lock(&work_done_wait_mutex);
	BUG_ON(err);
	for (i = 0; i < nr_tasks; i++) {
		task = tasks[i];
		sem_init(&task->sleep_sem, 0, 0);
		sem_init(&task->ready_for_work, 0, 0);
		sem_init(&task->work_done_sem, 0, 0);
		task->curr_event = 0;
		err = pthread_create(&task->thread, &attr, thread_func, task);
		BUG_ON(err);
	}
}

static nsec_t			cpu_usage;
static nsec_t			runavg_cpu_usage;
static nsec_t			parent_cpu_usage;
static nsec_t			runavg_parent_cpu_usage;

static void wait_for_tasks(void)
{
	nsec_t cpu_usage_0, cpu_usage_1;
	struct task_desc *task;
	unsigned long i, ret;

	start_time = get_nsecs();
	cpu_usage = 0;
	pthread_mutex_unlock(&work_done_wait_mutex);

	for (i = 0; i < nr_tasks; i++) {
		task = tasks[i];
		ret = sem_wait(&task->ready_for_work);
		BUG_ON(ret);
		sem_init(&task->ready_for_work, 0, 0);
	}
	ret = pthread_mutex_lock(&work_done_wait_mutex);
	BUG_ON(ret);

	cpu_usage_0 = get_cpu_usage_nsec_parent();

	pthread_mutex_unlock(&start_work_mutex);

	for (i = 0; i < nr_tasks; i++) {
		task = tasks[i];
		ret = sem_wait(&task->work_done_sem);
		BUG_ON(ret);
		sem_init(&task->work_done_sem, 0, 0);
		cpu_usage += task->cpu_usage;
		task->cpu_usage = 0;
	}

	cpu_usage_1 = get_cpu_usage_nsec_parent();
	if (!runavg_cpu_usage)
		runavg_cpu_usage = cpu_usage;
	runavg_cpu_usage = (runavg_cpu_usage*9 + cpu_usage)/10;

	parent_cpu_usage = cpu_usage_1 - cpu_usage_0;
	if (!runavg_parent_cpu_usage)
		runavg_parent_cpu_usage = parent_cpu_usage;
	runavg_parent_cpu_usage = (runavg_parent_cpu_usage*9 +
				   parent_cpu_usage)/10;

	ret = pthread_mutex_lock(&start_work_mutex);
	BUG_ON(ret);

	for (i = 0; i < nr_tasks; i++) {
		task = tasks[i];
		sem_init(&task->sleep_sem, 0, 0);
		task->curr_event = 0;
	}
}

static int read_events(void);

static unsigned long nr_runs;
static nsec_t sum_runtime;
static nsec_t sum_fluct;
static nsec_t run_avg;

static void run_one_test(void)
{
	nsec_t T0, T1, delta, avg_delta, fluct, std_dev;

	T0 = get_nsecs();
	wait_for_tasks();
	T1 = get_nsecs();

	delta = T1 - T0;
	sum_runtime += delta;
	nr_runs++;

	avg_delta = sum_runtime / nr_runs;
	if (delta < avg_delta)
		fluct = avg_delta - delta;
	else
		fluct = delta - avg_delta;
	sum_fluct += fluct;
	std_dev = sum_fluct / nr_runs / sqrt(nr_runs);
	if (!run_avg)
		run_avg = delta;
	run_avg = (run_avg*9 + delta)/10;

	printf("#%-3ld: %0.3f, ",
		nr_runs, (double)delta/1000000.0);

#if 0
	printf("%0.2f +- %0.2f, ",
		(double)avg_delta/1e6, (double)std_dev/1e6);
#endif
	printf("ravg: %0.2f, ",
		(double)run_avg/1e6);

	printf("cpu: %0.2f / %0.2f",
		(double)cpu_usage/1e6, (double)runavg_cpu_usage/1e6);

#if 0
	/*
	 * rusage statistics done by the parent, these are less
	 * accurate than the sum_exec_runtime based statistics:
	 */
	printf(" [%0.2f / %0.2f]",
		(double)parent_cpu_usage/1e6,
		(double)runavg_parent_cpu_usage/1e6);
#endif

	printf("\n");

	if (nr_sleep_corrections)
		printf(" (%ld sleep corrections)\n", nr_sleep_corrections);
	nr_sleep_corrections = 0;
}

static void test_calibrations(void)
{
	nsec_t T0, T1;

	T0 = get_nsecs();
	burn_nsecs(1e6);
	T1 = get_nsecs();

	printf("the run test took %Ld nsecs\n", T1-T0);

	T0 = get_nsecs();
	sleep_nsecs(1e6);
	T1 = get_nsecs();

	printf("the sleep test took %Ld nsecs\n", T1-T0);
}

static unsigned long replay_repeat = 10;

static void __cmd_replay(void)
{
	unsigned long i;

	calibrate_run_measurement_overhead();
	calibrate_sleep_measurement_overhead();

	test_calibrations();

	read_events();

	printf("nr_run_events:        %ld\n", nr_run_events);
	printf("nr_sleep_events:      %ld\n", nr_sleep_events);
	printf("nr_wakeup_events:     %ld\n", nr_wakeup_events);

	if (targetless_wakeups)
		printf("target-less wakeups:  %ld\n", targetless_wakeups);
	if (multitarget_wakeups)
		printf("multi-target wakeups: %ld\n", multitarget_wakeups);
	if (nr_run_events_optimized)
		printf("run events optimized: %ld\n",
			nr_run_events_optimized);

	print_task_traces();
	add_cross_task_wakeups();

	create_tasks();
	printf("------------------------------------------------------------\n");
	for (i = 0; i < replay_repeat; i++)
		run_one_test();
}

static int
process_comm_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread;

	thread = threads__findnew(event->comm.pid, &threads, &last_match);

	dump_printf("%p [%p]: PERF_EVENT_COMM: %s:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->comm.comm, event->comm.pid);

	if (thread == NULL ||
	    thread__set_comm(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_EVENT_COMM, skipping event.\n");
		return -1;
	}
	total_comm++;

	return 0;
}


struct raw_event_sample {
	u32 size;
	char data[0];
};

#define FILL_FIELD(ptr, field, event, data)	\
	ptr.field = (typeof(ptr.field)) raw_field_value(event, #field, data)

#define FILL_ARRAY(ptr, array, event, data)			\
do {								\
	void *__array = raw_field_ptr(event, #array, data);	\
	memcpy(ptr.array, __array, sizeof(ptr.array));	\
} while(0)

#define FILL_COMMON_FIELDS(ptr, event, data)			\
do {								\
	FILL_FIELD(ptr, common_type, event, data);		\
	FILL_FIELD(ptr, common_flags, event, data);		\
	FILL_FIELD(ptr, common_preempt_count, event, data);	\
	FILL_FIELD(ptr, common_pid, event, data);		\
	FILL_FIELD(ptr, common_tgid, event, data);		\
} while (0)



struct trace_switch_event {
	u32 size;

	u16 common_type;
	u8 common_flags;
	u8 common_preempt_count;
	u32 common_pid;
	u32 common_tgid;

	char prev_comm[16];
	u32 prev_pid;
	u32 prev_prio;
	u64 prev_state;
	char next_comm[16];
	u32 next_pid;
	u32 next_prio;
};


struct trace_wakeup_event {
	u32 size;

	u16 common_type;
	u8 common_flags;
	u8 common_preempt_count;
	u32 common_pid;
	u32 common_tgid;

	char comm[16];
	u32 pid;

	u32 prio;
	u32 success;
	u32 cpu;
};

struct trace_fork_event {
	u32 size;

	u16 common_type;
	u8 common_flags;
	u8 common_preempt_count;
	u32 common_pid;
	u32 common_tgid;

	char parent_comm[16];
	u32 parent_pid;
	char child_comm[16];
	u32 child_pid;
};

struct trace_sched_handler {
	void (*switch_event)(struct trace_switch_event *,
			     struct event *,
			     int cpu,
			     u64 timestamp,
			     struct thread *thread);

	void (*wakeup_event)(struct trace_wakeup_event *,
			     struct event *,
			     int cpu,
			     u64 timestamp,
			     struct thread *thread);

	void (*fork_event)(struct trace_fork_event *,
			   struct event *,
			   int cpu,
			   u64 timestamp,
			   struct thread *thread);
};


static void
replay_wakeup_event(struct trace_wakeup_event *wakeup_event,
		    struct event *event,
		    int cpu __used,
		    u64 timestamp __used,
		    struct thread *thread __used)
{
	struct task_desc *waker, *wakee;

	if (verbose) {
		printf("sched_wakeup event %p\n", event);

		printf(" ... pid %d woke up %s/%d\n",
			wakeup_event->common_pid,
			wakeup_event->comm,
			wakeup_event->pid);
	}

	waker = register_pid(wakeup_event->common_pid, "<unknown>");
	wakee = register_pid(wakeup_event->pid, wakeup_event->comm);

	add_sched_event_wakeup(waker, timestamp, wakee);
}

static unsigned long cpu_last_switched[MAX_CPUS];

static void
replay_switch_event(struct trace_switch_event *switch_event,
		    struct event *event,
		    int cpu,
		    u64 timestamp,
		    struct thread *thread __used)
{
	struct task_desc *prev, *next;
	u64 timestamp0;
	s64 delta;

	if (verbose)
		printf("sched_switch event %p\n", event);

	if (cpu >= MAX_CPUS || cpu < 0)
		return;

	timestamp0 = cpu_last_switched[cpu];
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0)
		die("hm, delta: %Ld < 0 ?\n", delta);

	if (verbose) {
		printf(" ... switch from %s/%d to %s/%d [ran %Ld nsecs]\n",
			switch_event->prev_comm, switch_event->prev_pid,
			switch_event->next_comm, switch_event->next_pid,
			delta);
	}

	prev = register_pid(switch_event->prev_pid, switch_event->prev_comm);
	next = register_pid(switch_event->next_pid, switch_event->next_comm);

	cpu_last_switched[cpu] = timestamp;

	add_sched_event_run(prev, timestamp, delta);
	add_sched_event_sleep(prev, timestamp, switch_event->prev_state);
}


static void
replay_fork_event(struct trace_fork_event *fork_event,
		  struct event *event,
		  int cpu __used,
		  u64 timestamp __used,
		  struct thread *thread __used)
{
	if (verbose) {
		printf("sched_fork event %p\n", event);
		printf("... parent: %s/%d\n", fork_event->parent_comm, fork_event->parent_pid);
		printf("...  child: %s/%d\n", fork_event->child_comm, fork_event->child_pid);
	}
	register_pid(fork_event->parent_pid, fork_event->parent_comm);
	register_pid(fork_event->child_pid, fork_event->child_comm);
}

static struct trace_sched_handler replay_ops  = {
	.wakeup_event		= replay_wakeup_event,
	.switch_event		= replay_switch_event,
	.fork_event		= replay_fork_event,
};

#define TASK_STATE_TO_CHAR_STR "RSDTtZX"

enum thread_state {
	THREAD_SLEEPING = 0,
	THREAD_WAIT_CPU,
	THREAD_SCHED_IN,
	THREAD_IGNORE
};

struct work_atom {
	struct list_head	list;
	enum thread_state	state;
	u64			wake_up_time;
	u64			sched_in_time;
	u64			runtime;
};

struct task_atoms {
	struct list_head	snapshot_list;
	struct thread		*thread;
	struct rb_node		node;
	u64			max_lat;
	u64			total_lat;
	u64			nb_atoms;
	u64			total_runtime;
};

typedef int (*sort_thread_lat)(struct task_atoms *, struct task_atoms *);

struct sort_dimension {
	const char 		*name;
	sort_thread_lat		cmp;
	struct list_head 	list;
};

static LIST_HEAD(cmp_pid);

static struct rb_root lat_snapshot_root, sorted_lat_snapshot_root;

static struct task_atoms *
thread_atom_list_search(struct rb_root *root, struct thread *thread)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct task_atoms *atoms;

		atoms = container_of(node, struct task_atoms, node);
		if (thread->pid > atoms->thread->pid)
			node = node->rb_left;
		else if (thread->pid < atoms->thread->pid)
			node = node->rb_right;
		else {
			return atoms;
		}
	}
	return NULL;
}

static int
thread_lat_cmp(struct list_head *list, struct task_atoms *l,
	       struct task_atoms *r)
{
	struct sort_dimension *sort;
	int ret = 0;

	list_for_each_entry(sort, list, list) {
		ret = sort->cmp(l, r);
		if (ret)
			return ret;
	}

	return ret;
}

static void
__thread_latency_insert(struct rb_root *root, struct task_atoms *data,
			 struct list_head *sort_list)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct task_atoms *this;
		int cmp;

		this = container_of(*new, struct task_atoms, node);
		parent = *new;

		cmp = thread_lat_cmp(sort_list, data, this);

		if (cmp > 0)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void thread_atom_list_insert(struct thread *thread)
{
	struct task_atoms *atoms;
	atoms = calloc(sizeof(*atoms), 1);
	if (!atoms)
		die("No memory");

	atoms->thread = thread;
	INIT_LIST_HEAD(&atoms->snapshot_list);
	__thread_latency_insert(&lat_snapshot_root, atoms, &cmp_pid);
}

static void
latency_fork_event(struct trace_fork_event *fork_event __used,
		   struct event *event __used,
		   int cpu __used,
		   u64 timestamp __used,
		   struct thread *thread __used)
{
	/* should insert the newcomer */
}

__used
static char sched_out_state(struct trace_switch_event *switch_event)
{
	const char *str = TASK_STATE_TO_CHAR_STR;

	return str[switch_event->prev_state];
}

static void
lat_sched_out(struct task_atoms *atoms,
	      struct trace_switch_event *switch_event __used,
	      u64 delta,
	      u64 timestamp)
{
	struct work_atom *snapshot;

	snapshot = calloc(sizeof(*snapshot), 1);
	if (!snapshot)
		die("Non memory");

	if (sched_out_state(switch_event) == 'R') {
		snapshot->state = THREAD_WAIT_CPU;
		snapshot->wake_up_time = timestamp;
	}

	snapshot->runtime = delta;
	list_add_tail(&snapshot->list, &atoms->snapshot_list);
}

static void
lat_sched_in(struct task_atoms *atoms, u64 timestamp)
{
	struct work_atom *snapshot;
	u64 delta;

	if (list_empty(&atoms->snapshot_list))
		return;

	snapshot = list_entry(atoms->snapshot_list.prev, struct work_atom,
			      list);

	if (snapshot->state != THREAD_WAIT_CPU)
		return;

	if (timestamp < snapshot->wake_up_time) {
		snapshot->state = THREAD_IGNORE;
		return;
	}

	snapshot->state = THREAD_SCHED_IN;
	snapshot->sched_in_time = timestamp;

	delta = snapshot->sched_in_time - snapshot->wake_up_time;
	atoms->total_lat += delta;
	if (delta > atoms->max_lat)
		atoms->max_lat = delta;
	atoms->nb_atoms++;
	atoms->total_runtime += snapshot->runtime;
}

static void
latency_switch_event(struct trace_switch_event *switch_event,
		     struct event *event __used,
		     int cpu,
		     u64 timestamp,
		     struct thread *thread __used)
{
	struct task_atoms *out_atoms, *in_atoms;
	struct thread *sched_out, *sched_in;
	u64 timestamp0;
	s64 delta;

	if (cpu >= MAX_CPUS || cpu < 0)
		return;

	timestamp0 = cpu_last_switched[cpu];
	cpu_last_switched[cpu] = timestamp;
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0)
		die("hm, delta: %Ld < 0 ?\n", delta);


	sched_out = threads__findnew(switch_event->prev_pid, &threads, &last_match);
	sched_in = threads__findnew(switch_event->next_pid, &threads, &last_match);

	in_atoms = thread_atom_list_search(&lat_snapshot_root, sched_in);
	if (!in_atoms) {
		thread_atom_list_insert(sched_in);
		in_atoms = thread_atom_list_search(&lat_snapshot_root, sched_in);
		if (!in_atoms)
			die("Internal latency tree error");
	}

	out_atoms = thread_atom_list_search(&lat_snapshot_root, sched_out);
	if (!out_atoms) {
		thread_atom_list_insert(sched_out);
		out_atoms = thread_atom_list_search(&lat_snapshot_root, sched_out);
		if (!out_atoms)
			die("Internal latency tree error");
	}

	lat_sched_in(in_atoms, timestamp);
	lat_sched_out(out_atoms, switch_event, delta, timestamp);
}

static void
latency_wakeup_event(struct trace_wakeup_event *wakeup_event,
		     struct event *event __used,
		     int cpu __used,
		     u64 timestamp,
		     struct thread *thread __used)
{
	struct task_atoms *atoms;
	struct work_atom *snapshot;
	struct thread *wakee;

	/* Note for later, it may be interesting to observe the failing cases */
	if (!wakeup_event->success)
		return;

	wakee = threads__findnew(wakeup_event->pid, &threads, &last_match);
	atoms = thread_atom_list_search(&lat_snapshot_root, wakee);
	if (!atoms) {
		thread_atom_list_insert(wakee);
		return;
	}

	if (list_empty(&atoms->snapshot_list))
		return;

	snapshot = list_entry(atoms->snapshot_list.prev, struct work_atom,
			      list);

	if (snapshot->state != THREAD_SLEEPING)
		return;

	snapshot->state = THREAD_WAIT_CPU;
	snapshot->wake_up_time = timestamp;
}

static struct trace_sched_handler lat_ops  = {
	.wakeup_event		= latency_wakeup_event,
	.switch_event		= latency_switch_event,
	.fork_event		= latency_fork_event,
};

static u64 all_runtime;
static u64 all_count;

static void output_lat_thread(struct task_atoms *atom_list)
{
	int i;
	int ret;
	u64 avg;

	if (!atom_list->nb_atoms)
		return;

	all_runtime += atom_list->total_runtime;
	all_count += atom_list->nb_atoms;

	ret = printf(" %s ", atom_list->thread->comm);

	for (i = 0; i < 19 - ret; i++)
		printf(" ");

	avg = atom_list->total_lat / atom_list->nb_atoms;

	printf("|%9.3f ms |%9llu | avg:%9.3f ms | max:%9.3f ms |\n",
	      (double)atom_list->total_runtime / 1e6,
		 atom_list->nb_atoms, (double)avg / 1e6,
		 (double)atom_list->max_lat / 1e6);
}

static int pid_cmp(struct task_atoms *l, struct task_atoms *r)
{

	if (l->thread->pid < r->thread->pid)
		return -1;
	if (l->thread->pid > r->thread->pid)
		return 1;

	return 0;
}

static struct sort_dimension pid_sort_dimension = {
	.name = "pid",
	.cmp = pid_cmp,
};

static int avg_cmp(struct task_atoms *l, struct task_atoms *r)
{
	u64 avgl, avgr;

	if (!l->nb_atoms)
		return -1;

	if (!r->nb_atoms)
		return 1;

	avgl = l->total_lat / l->nb_atoms;
	avgr = r->total_lat / r->nb_atoms;

	if (avgl < avgr)
		return -1;
	if (avgl > avgr)
		return 1;

	return 0;
}

static struct sort_dimension avg_sort_dimension = {
	.name 	= "avg",
	.cmp	= avg_cmp,
};

static int max_cmp(struct task_atoms *l, struct task_atoms *r)
{
	if (l->max_lat < r->max_lat)
		return -1;
	if (l->max_lat > r->max_lat)
		return 1;

	return 0;
}

static struct sort_dimension max_sort_dimension = {
	.name 	= "max",
	.cmp	= max_cmp,
};

static int switch_cmp(struct task_atoms *l, struct task_atoms *r)
{
	if (l->nb_atoms < r->nb_atoms)
		return -1;
	if (l->nb_atoms > r->nb_atoms)
		return 1;

	return 0;
}

static struct sort_dimension switch_sort_dimension = {
	.name 	= "switch",
	.cmp	= switch_cmp,
};

static int runtime_cmp(struct task_atoms *l, struct task_atoms *r)
{
	if (l->total_runtime < r->total_runtime)
		return -1;
	if (l->total_runtime > r->total_runtime)
		return 1;

	return 0;
}

static struct sort_dimension runtime_sort_dimension = {
	.name 	= "runtime",
	.cmp	= runtime_cmp,
};

static struct sort_dimension *available_sorts[] = {
	&pid_sort_dimension,
	&avg_sort_dimension,
	&max_sort_dimension,
	&switch_sort_dimension,
	&runtime_sort_dimension,
};

#define NB_AVAILABLE_SORTS	(int)(sizeof(available_sorts) / sizeof(struct sort_dimension *))

static LIST_HEAD(sort_list);

static int sort_dimension__add(char *tok, struct list_head *list)
{
	int i;

	for (i = 0; i < NB_AVAILABLE_SORTS; i++) {
		if (!strcmp(available_sorts[i]->name, tok)) {
			list_add_tail(&available_sorts[i]->list, list);

			return 0;
		}
	}

	return -1;
}

static void setup_sorting(void);

static void sort_lat(void)
{
	struct rb_node *node;

	for (;;) {
		struct task_atoms *data;
		node = rb_first(&lat_snapshot_root);
		if (!node)
			break;

		rb_erase(node, &lat_snapshot_root);
		data = rb_entry(node, struct task_atoms, node);
		__thread_latency_insert(&sorted_lat_snapshot_root, data, &sort_list);
	}
}

static void __cmd_lat(void)
{
	struct rb_node *next;

	setup_pager();
	read_events();
	sort_lat();

	printf("-----------------------------------------------------------------------------------\n");
	printf(" Task              |  Runtime ms | Switches | Average delay ms | Maximum delay ms |\n");
	printf("-----------------------------------------------------------------------------------\n");

	next = rb_first(&sorted_lat_snapshot_root);

	while (next) {
		struct task_atoms *atom_list;

		atom_list = rb_entry(next, struct task_atoms, node);
		output_lat_thread(atom_list);
		next = rb_next(next);
	}

	printf("-----------------------------------------------------------------------------------\n");
	printf(" TOTAL:            |%9.3f ms |%9Ld |\n",
		(double)all_runtime/1e6, all_count);
	printf("---------------------------------------------\n");
}

static struct trace_sched_handler *trace_handler;

static void
process_sched_wakeup_event(struct raw_event_sample *raw,
			   struct event *event,
			   int cpu __used,
			   u64 timestamp __used,
			   struct thread *thread __used)
{
	struct trace_wakeup_event wakeup_event;

	FILL_COMMON_FIELDS(wakeup_event, event, raw->data);

	FILL_ARRAY(wakeup_event, comm, event, raw->data);
	FILL_FIELD(wakeup_event, pid, event, raw->data);
	FILL_FIELD(wakeup_event, prio, event, raw->data);
	FILL_FIELD(wakeup_event, success, event, raw->data);
	FILL_FIELD(wakeup_event, cpu, event, raw->data);

	trace_handler->wakeup_event(&wakeup_event, event, cpu, timestamp, thread);
}

static void
process_sched_switch_event(struct raw_event_sample *raw,
			   struct event *event,
			   int cpu __used,
			   u64 timestamp __used,
			   struct thread *thread __used)
{
	struct trace_switch_event switch_event;

	FILL_COMMON_FIELDS(switch_event, event, raw->data);

	FILL_ARRAY(switch_event, prev_comm, event, raw->data);
	FILL_FIELD(switch_event, prev_pid, event, raw->data);
	FILL_FIELD(switch_event, prev_prio, event, raw->data);
	FILL_FIELD(switch_event, prev_state, event, raw->data);
	FILL_ARRAY(switch_event, next_comm, event, raw->data);
	FILL_FIELD(switch_event, next_pid, event, raw->data);
	FILL_FIELD(switch_event, next_prio, event, raw->data);

	trace_handler->switch_event(&switch_event, event, cpu, timestamp, thread);
}

static void
process_sched_fork_event(struct raw_event_sample *raw,
			 struct event *event,
			 int cpu __used,
			 u64 timestamp __used,
			 struct thread *thread __used)
{
	struct trace_fork_event fork_event;

	FILL_COMMON_FIELDS(fork_event, event, raw->data);

	FILL_ARRAY(fork_event, parent_comm, event, raw->data);
	FILL_FIELD(fork_event, parent_pid, event, raw->data);
	FILL_ARRAY(fork_event, child_comm, event, raw->data);
	FILL_FIELD(fork_event, child_pid, event, raw->data);

	trace_handler->fork_event(&fork_event, event, cpu, timestamp, thread);
}

static void
process_sched_exit_event(struct event *event,
			 int cpu __used,
			 u64 timestamp __used,
			 struct thread *thread __used)
{
	if (verbose)
		printf("sched_exit event %p\n", event);
}

static void
process_raw_event(event_t *raw_event __used, void *more_data,
		  int cpu, u64 timestamp, struct thread *thread)
{
	struct raw_event_sample *raw = more_data;
	struct event *event;
	int type;

	type = trace_parse_common_type(raw->data);
	event = trace_find_event(type);

	if (!strcmp(event->name, "sched_switch"))
		process_sched_switch_event(raw, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "sched_wakeup"))
		process_sched_wakeup_event(raw, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "sched_wakeup_new"))
		process_sched_wakeup_event(raw, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "sched_process_fork"))
		process_sched_fork_event(raw, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "sched_process_exit"))
		process_sched_exit_event(event, cpu, timestamp, thread);
}

static int
process_sample_event(event_t *event, unsigned long offset, unsigned long head)
{
	char level;
	int show = 0;
	struct dso *dso = NULL;
	struct thread *thread;
	u64 ip = event->ip.ip;
	u64 timestamp = -1;
	u32 cpu = -1;
	u64 period = 1;
	void *more_data = event->ip.__more_data;
	int cpumode;

	thread = threads__findnew(event->ip.pid, &threads, &last_match);

	if (sample_type & PERF_SAMPLE_TIME) {
		timestamp = *(u64 *)more_data;
		more_data += sizeof(u64);
	}

	if (sample_type & PERF_SAMPLE_CPU) {
		cpu = *(u32 *)more_data;
		more_data += sizeof(u32);
		more_data += sizeof(u32); /* reserved */
	}

	if (sample_type & PERF_SAMPLE_PERIOD) {
		period = *(u64 *)more_data;
		more_data += sizeof(u64);
	}

	dump_printf("%p [%p]: PERF_EVENT_SAMPLE (IP, %d): %d/%d: %p period: %Ld\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->header.misc,
		event->ip.pid, event->ip.tid,
		(void *)(long)ip,
		(long long)period);

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (thread == NULL) {
		eprintf("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	cpumode = event->header.misc & PERF_EVENT_MISC_CPUMODE_MASK;

	if (cpumode == PERF_EVENT_MISC_KERNEL) {
		show = SHOW_KERNEL;
		level = 'k';

		dso = kernel_dso;

		dump_printf(" ...... dso: %s\n", dso->name);

	} else if (cpumode == PERF_EVENT_MISC_USER) {

		show = SHOW_USER;
		level = '.';

	} else {
		show = SHOW_HV;
		level = 'H';

		dso = hypervisor_dso;

		dump_printf(" ...... dso: [hypervisor]\n");
	}

	if (sample_type & PERF_SAMPLE_RAW)
		process_raw_event(event, more_data, cpu, timestamp, thread);

	return 0;
}

static int
process_event(event_t *event, unsigned long offset, unsigned long head)
{
	trace_event(event);

	switch (event->header.type) {
	case PERF_EVENT_MMAP ... PERF_EVENT_LOST:
		return 0;

	case PERF_EVENT_COMM:
		return process_comm_event(event, offset, head);

	case PERF_EVENT_EXIT ... PERF_EVENT_READ:
		return 0;

	case PERF_EVENT_SAMPLE:
		return process_sample_event(event, offset, head);

	case PERF_EVENT_MAX:
	default:
		return -1;
	}

	return 0;
}

static int read_events(void)
{
	int ret, rc = EXIT_FAILURE;
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat perf_stat;
	event_t *event;
	uint32_t size;
	char *buf;

	trace_report();
	register_idle_thread(&threads, &last_match);

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		perror("failed to open file");
		exit(-1);
	}

	ret = fstat(input, &perf_stat);
	if (ret < 0) {
		perror("failed to stat file");
		exit(-1);
	}

	if (!perf_stat.st_size) {
		fprintf(stderr, "zero-sized file, nothing to do!\n");
		exit(0);
	}
	header = perf_header__read(input);
	head = header->data_offset;
	sample_type = perf_header__sample_type(header);

	if (!(sample_type & PERF_SAMPLE_RAW))
		die("No trace sample to read. Did you call perf record "
		    "without -R?");

	if (load_kernel() < 0) {
		perror("failed to load kernel symbols");
		return EXIT_FAILURE;
	}

remap:
	buf = (char *)mmap(NULL, page_size * mmap_window, PROT_READ,
			   MAP_SHARED, input, offset);
	if (buf == MAP_FAILED) {
		perror("failed to mmap file");
		exit(-1);
	}

more:
	event = (event_t *)(buf + head);

	size = event->header.size;
	if (!size)
		size = 8;

	if (head + event->header.size >= page_size * mmap_window) {
		unsigned long shift = page_size * (head / page_size);
		int res;

		res = munmap(buf, page_size * mmap_window);
		assert(res == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;


	if (!size || process_event(event, offset, head) < 0) {

		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */

		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}

	head += size;

	if (offset + head < (unsigned long)perf_stat.st_size)
		goto more;

	rc = EXIT_SUCCESS;
	close(input);

	return rc;
}

static const char * const sched_usage[] = {
	"perf sched [<options>] {record|latency|replay}",
	NULL
};

static const struct option sched_options[] = {
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_END()
};

static const char * const latency_usage[] = {
	"perf sched latency [<options>]",
	NULL
};

static const struct option latency_options[] = {
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): runtime, switch, avg, max"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_END()
};

static const char * const replay_usage[] = {
	"perf sched replay [<options>]",
	NULL
};

static const struct option replay_options[] = {
	OPT_INTEGER('r', "repeat", &replay_repeat,
		    "repeat the workload replay N times (-1: infinite)"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_END()
};

static void setup_sorting(void)
{
	char *tmp, *tok, *str = strdup(sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(tok, &sort_list) < 0) {
			error("Unknown --sort key: `%s'", tok);
			usage_with_options(latency_usage, latency_options);
		}
	}

	free(str);

	sort_dimension__add((char *)"pid", &cmp_pid);
}

int cmd_sched(int argc, const char **argv, const char *prefix __used)
{
	symbol__init();
	page_size = getpagesize();

	argc = parse_options(argc, argv, sched_options, sched_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(sched_usage, sched_options);

	if (!strncmp(argv[0], "lat", 3)) {
		trace_handler = &lat_ops;
		if (argc > 1) {
			argc = parse_options(argc, argv, latency_options, latency_usage, 0);
			if (argc)
				usage_with_options(latency_usage, latency_options);
			setup_sorting();
		}
		__cmd_lat();
	} else if (!strncmp(argv[0], "rep", 3)) {
		trace_handler = &replay_ops;
		if (argc) {
			argc = parse_options(argc, argv, replay_options, replay_usage, 0);
			if (argc)
				usage_with_options(replay_usage, replay_options);
		}
		__cmd_replay();
	} else {
		usage_with_options(sched_usage, sched_options);
	}


	return 0;
}
