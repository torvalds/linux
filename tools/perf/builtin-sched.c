#include "builtin.h"
#include "perf.h"

#include "util/util.h"
#include "util/evlist.h"
#include "util/cache.h"
#include "util/evsel.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/session.h"
#include "util/tool.h"

#include "util/parse-options.h"
#include "util/trace-event.h"

#include "util/debug.h"

#include <sys/prctl.h>
#include <sys/resource.h>

#include <semaphore.h>
#include <pthread.h>
#include <math.h>

static const char		*input_name;

static char			default_sort_order[] = "avg, max, switch, runtime";
static const char		*sort_order = default_sort_order;

static int			profile_cpu = -1;

#define PR_SET_NAME		15               /* Set process name */
#define MAX_CPUS		4096

static u64			run_measurement_overhead;
static u64			sleep_measurement_overhead;

#define COMM_LEN		20
#define SYM_LEN			129

#define MAX_PID			65536

static unsigned long		nr_tasks;

struct perf_sched {
	struct perf_tool    tool;
	struct perf_session *session;
};

struct sched_atom;

struct task_desc {
	unsigned long		nr;
	unsigned long		pid;
	char			comm[COMM_LEN];

	unsigned long		nr_events;
	unsigned long		curr_event;
	struct sched_atom	**atoms;

	pthread_t		thread;
	sem_t			sleep_sem;

	sem_t			ready_for_work;
	sem_t			work_done_sem;

	u64			cpu_usage;
};

enum sched_event_type {
	SCHED_EVENT_RUN,
	SCHED_EVENT_SLEEP,
	SCHED_EVENT_WAKEUP,
	SCHED_EVENT_MIGRATION,
};

struct sched_atom {
	enum sched_event_type	type;
	int			specific_wait;
	u64			timestamp;
	u64			duration;
	unsigned long		nr;
	sem_t			*wait_sem;
	struct task_desc	*wakee;
};

static struct task_desc		*pid_to_task[MAX_PID];

static struct task_desc		**tasks;

static pthread_mutex_t		start_work_mutex = PTHREAD_MUTEX_INITIALIZER;
static u64			start_time;

static pthread_mutex_t		work_done_wait_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned long		nr_run_events;
static unsigned long		nr_sleep_events;
static unsigned long		nr_wakeup_events;

static unsigned long		nr_sleep_corrections;
static unsigned long		nr_run_events_optimized;

static unsigned long		targetless_wakeups;
static unsigned long		multitarget_wakeups;

static u64			cpu_usage;
static u64			runavg_cpu_usage;
static u64			parent_cpu_usage;
static u64			runavg_parent_cpu_usage;

static unsigned long		nr_runs;
static u64			sum_runtime;
static u64			sum_fluct;
static u64			run_avg;

static unsigned int		replay_repeat = 10;
static unsigned long		nr_timestamps;
static unsigned long		nr_unordered_timestamps;
static unsigned long		nr_state_machine_bugs;
static unsigned long		nr_context_switch_bugs;
static unsigned long		nr_events;
static unsigned long		nr_lost_chunks;
static unsigned long		nr_lost_events;

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
	u64			sched_out_time;
	u64			wake_up_time;
	u64			sched_in_time;
	u64			runtime;
};

struct work_atoms {
	struct list_head	work_list;
	struct thread		*thread;
	struct rb_node		node;
	u64			max_lat;
	u64			max_lat_at;
	u64			total_lat;
	u64			nb_atoms;
	u64			total_runtime;
};

typedef int (*sort_fn_t)(struct work_atoms *, struct work_atoms *);

static struct rb_root		atom_root, sorted_atom_root;

static u64			all_runtime;
static u64			all_count;


static u64 get_nsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void burn_nsecs(u64 nsecs)
{
	u64 T0 = get_nsecs(), T1;

	do {
		T1 = get_nsecs();
	} while (T1 + run_measurement_overhead < T0 + nsecs);
}

static void sleep_nsecs(u64 nsecs)
{
	struct timespec ts;

	ts.tv_nsec = nsecs % 999999999;
	ts.tv_sec = nsecs / 999999999;

	nanosleep(&ts, NULL);
}

static void calibrate_run_measurement_overhead(void)
{
	u64 T0, T1, delta, min_delta = 1000000000ULL;
	int i;

	for (i = 0; i < 10; i++) {
		T0 = get_nsecs();
		burn_nsecs(0);
		T1 = get_nsecs();
		delta = T1-T0;
		min_delta = min(min_delta, delta);
	}
	run_measurement_overhead = min_delta;

	printf("run measurement overhead: %" PRIu64 " nsecs\n", min_delta);
}

static void calibrate_sleep_measurement_overhead(void)
{
	u64 T0, T1, delta, min_delta = 1000000000ULL;
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

	printf("sleep measurement overhead: %" PRIu64 " nsecs\n", min_delta);
}

static struct sched_atom *
get_new_event(struct task_desc *task, u64 timestamp)
{
	struct sched_atom *event = zalloc(sizeof(*event));
	unsigned long idx = task->nr_events;
	size_t size;

	event->timestamp = timestamp;
	event->nr = idx;

	task->nr_events++;
	size = sizeof(struct sched_atom *) * task->nr_events;
	task->atoms = realloc(task->atoms, size);
	BUG_ON(!task->atoms);

	task->atoms[idx] = event;

	return event;
}

static struct sched_atom *last_event(struct task_desc *task)
{
	if (!task->nr_events)
		return NULL;

	return task->atoms[task->nr_events - 1];
}

static void
add_sched_event_run(struct task_desc *task, u64 timestamp, u64 duration)
{
	struct sched_atom *event, *curr_event = last_event(task);

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

static void
add_sched_event_wakeup(struct task_desc *task, u64 timestamp,
		       struct task_desc *wakee)
{
	struct sched_atom *event, *wakee_event;

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

	wakee_event->wait_sem = zalloc(sizeof(*wakee_event->wait_sem));
	sem_init(wakee_event->wait_sem, 0, 0);
	wakee_event->specific_wait = 1;
	event->wait_sem = wakee_event->wait_sem;

	nr_wakeup_events++;
}

static void
add_sched_event_sleep(struct task_desc *task, u64 timestamp,
		      u64 task_state __used)
{
	struct sched_atom *event = get_new_event(task, timestamp);

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

	task = zalloc(sizeof(*task));
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
process_sched_event(struct task_desc *this_task __used, struct sched_atom *atom)
{
	int ret = 0;

	switch (atom->type) {
		case SCHED_EVENT_RUN:
			burn_nsecs(atom->duration);
			break;
		case SCHED_EVENT_SLEEP:
			if (atom->wait_sem)
				ret = sem_wait(atom->wait_sem);
			BUG_ON(ret);
			break;
		case SCHED_EVENT_WAKEUP:
			if (atom->wait_sem)
				ret = sem_post(atom->wait_sem);
			BUG_ON(ret);
			break;
		case SCHED_EVENT_MIGRATION:
			break;
		default:
			BUG_ON(1);
	}
}

static u64 get_cpu_usage_nsec_parent(void)
{
	struct rusage ru;
	u64 sum;
	int err;

	err = getrusage(RUSAGE_SELF, &ru);
	BUG_ON(err);

	sum =  ru.ru_utime.tv_sec*1e9 + ru.ru_utime.tv_usec*1e3;
	sum += ru.ru_stime.tv_sec*1e9 + ru.ru_stime.tv_usec*1e3;

	return sum;
}

static int self_open_counters(void)
{
	struct perf_event_attr attr;
	int fd;

	memset(&attr, 0, sizeof(attr));

	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_TASK_CLOCK;

	fd = sys_perf_event_open(&attr, 0, -1, -1, 0);

	if (fd < 0)
		die("Error: sys_perf_event_open() syscall returned"
		    "with %d (%s)\n", fd, strerror(errno));
	return fd;
}

static u64 get_cpu_usage_nsec_self(int fd)
{
	u64 runtime;
	int ret;

	ret = read(fd, &runtime, sizeof(runtime));
	BUG_ON(ret != sizeof(runtime));

	return runtime;
}

static void *thread_func(void *ctx)
{
	struct task_desc *this_task = ctx;
	u64 cpu_usage_0, cpu_usage_1;
	unsigned long i, ret;
	char comm2[22];
	int fd;

	sprintf(comm2, ":%s", this_task->comm);
	prctl(PR_SET_NAME, comm2);
	fd = self_open_counters();

again:
	ret = sem_post(&this_task->ready_for_work);
	BUG_ON(ret);
	ret = pthread_mutex_lock(&start_work_mutex);
	BUG_ON(ret);
	ret = pthread_mutex_unlock(&start_work_mutex);
	BUG_ON(ret);

	cpu_usage_0 = get_cpu_usage_nsec_self(fd);

	for (i = 0; i < this_task->nr_events; i++) {
		this_task->curr_event = i;
		process_sched_event(this_task, this_task->atoms[i]);
	}

	cpu_usage_1 = get_cpu_usage_nsec_self(fd);
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
	err = pthread_attr_setstacksize(&attr,
			(size_t) max(16 * 1024, PTHREAD_STACK_MIN));
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

static void wait_for_tasks(void)
{
	u64 cpu_usage_0, cpu_usage_1;
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

static void run_one_test(void)
{
	u64 T0, T1, delta, avg_delta, fluct;

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
	if (!run_avg)
		run_avg = delta;
	run_avg = (run_avg*9 + delta)/10;

	printf("#%-3ld: %0.3f, ",
		nr_runs, (double)delta/1000000.0);

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
	u64 T0, T1;

	T0 = get_nsecs();
	burn_nsecs(1e6);
	T1 = get_nsecs();

	printf("the run test took %" PRIu64 " nsecs\n", T1 - T0);

	T0 = get_nsecs();
	sleep_nsecs(1e6);
	T1 = get_nsecs();

	printf("the sleep test took %" PRIu64 " nsecs\n", T1 - T0);
}

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

struct trace_runtime_event {
	u32 size;

	u16 common_type;
	u8 common_flags;
	u8 common_preempt_count;
	u32 common_pid;
	u32 common_tgid;

	char comm[16];
	u32 pid;
	u64 runtime;
	u64 vruntime;
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

struct trace_migrate_task_event {
	u32 size;

	u16 common_type;
	u8 common_flags;
	u8 common_preempt_count;
	u32 common_pid;
	u32 common_tgid;

	char comm[16];
	u32 pid;

	u32 prio;
	u32 cpu;
};

struct trace_sched_handler {
	void (*switch_event)(struct trace_switch_event *,
			     struct machine *,
			     struct event_format *,
			     int cpu,
			     u64 timestamp,
			     struct thread *thread);

	void (*runtime_event)(struct trace_runtime_event *,
			      struct machine *,
			      struct event_format *,
			      int cpu,
			      u64 timestamp,
			      struct thread *thread);

	void (*wakeup_event)(struct trace_wakeup_event *,
			     struct machine *,
			     struct event_format *,
			     int cpu,
			     u64 timestamp,
			     struct thread *thread);

	void (*fork_event)(struct trace_fork_event *,
			   struct event_format *,
			   int cpu,
			   u64 timestamp,
			   struct thread *thread);

	void (*migrate_task_event)(struct trace_migrate_task_event *,
			   struct machine *machine,
			   struct event_format *,
			   int cpu,
			   u64 timestamp,
			   struct thread *thread);
};


static void
replay_wakeup_event(struct trace_wakeup_event *wakeup_event,
		    struct machine *machine __used,
		    struct event_format *event,
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

static u64 cpu_last_switched[MAX_CPUS];

static void
replay_switch_event(struct trace_switch_event *switch_event,
		    struct machine *machine __used,
		    struct event_format *event,
		    int cpu,
		    u64 timestamp,
		    struct thread *thread __used)
{
	struct task_desc *prev, __used *next;
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
		die("hm, delta: %" PRIu64 " < 0 ?\n", delta);

	if (verbose) {
		printf(" ... switch from %s/%d to %s/%d [ran %" PRIu64 " nsecs]\n",
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
		  struct event_format *event,
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

struct sort_dimension {
	const char		*name;
	sort_fn_t		cmp;
	struct list_head	list;
};

static LIST_HEAD(cmp_pid);

static int
thread_lat_cmp(struct list_head *list, struct work_atoms *l, struct work_atoms *r)
{
	struct sort_dimension *sort;
	int ret = 0;

	BUG_ON(list_empty(list));

	list_for_each_entry(sort, list, list) {
		ret = sort->cmp(l, r);
		if (ret)
			return ret;
	}

	return ret;
}

static struct work_atoms *
thread_atoms_search(struct rb_root *root, struct thread *thread,
			 struct list_head *sort_list)
{
	struct rb_node *node = root->rb_node;
	struct work_atoms key = { .thread = thread };

	while (node) {
		struct work_atoms *atoms;
		int cmp;

		atoms = container_of(node, struct work_atoms, node);

		cmp = thread_lat_cmp(sort_list, &key, atoms);
		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else {
			BUG_ON(thread != atoms->thread);
			return atoms;
		}
	}
	return NULL;
}

static void
__thread_latency_insert(struct rb_root *root, struct work_atoms *data,
			 struct list_head *sort_list)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct work_atoms *this;
		int cmp;

		this = container_of(*new, struct work_atoms, node);
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

static void thread_atoms_insert(struct thread *thread)
{
	struct work_atoms *atoms = zalloc(sizeof(*atoms));
	if (!atoms)
		die("No memory");

	atoms->thread = thread;
	INIT_LIST_HEAD(&atoms->work_list);
	__thread_latency_insert(&atom_root, atoms, &cmp_pid);
}

static void
latency_fork_event(struct trace_fork_event *fork_event __used,
		   struct event_format *event __used,
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
add_sched_out_event(struct work_atoms *atoms,
		    char run_state,
		    u64 timestamp)
{
	struct work_atom *atom = zalloc(sizeof(*atom));
	if (!atom)
		die("Non memory");

	atom->sched_out_time = timestamp;

	if (run_state == 'R') {
		atom->state = THREAD_WAIT_CPU;
		atom->wake_up_time = atom->sched_out_time;
	}

	list_add_tail(&atom->list, &atoms->work_list);
}

static void
add_runtime_event(struct work_atoms *atoms, u64 delta, u64 timestamp __used)
{
	struct work_atom *atom;

	BUG_ON(list_empty(&atoms->work_list));

	atom = list_entry(atoms->work_list.prev, struct work_atom, list);

	atom->runtime += delta;
	atoms->total_runtime += delta;
}

static void
add_sched_in_event(struct work_atoms *atoms, u64 timestamp)
{
	struct work_atom *atom;
	u64 delta;

	if (list_empty(&atoms->work_list))
		return;

	atom = list_entry(atoms->work_list.prev, struct work_atom, list);

	if (atom->state != THREAD_WAIT_CPU)
		return;

	if (timestamp < atom->wake_up_time) {
		atom->state = THREAD_IGNORE;
		return;
	}

	atom->state = THREAD_SCHED_IN;
	atom->sched_in_time = timestamp;

	delta = atom->sched_in_time - atom->wake_up_time;
	atoms->total_lat += delta;
	if (delta > atoms->max_lat) {
		atoms->max_lat = delta;
		atoms->max_lat_at = timestamp;
	}
	atoms->nb_atoms++;
}

static void
latency_switch_event(struct trace_switch_event *switch_event,
		     struct machine *machine,
		     struct event_format *event __used,
		     int cpu,
		     u64 timestamp,
		     struct thread *thread __used)
{
	struct work_atoms *out_events, *in_events;
	struct thread *sched_out, *sched_in;
	u64 timestamp0;
	s64 delta;

	BUG_ON(cpu >= MAX_CPUS || cpu < 0);

	timestamp0 = cpu_last_switched[cpu];
	cpu_last_switched[cpu] = timestamp;
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0)
		die("hm, delta: %" PRIu64 " < 0 ?\n", delta);


	sched_out = machine__findnew_thread(machine, switch_event->prev_pid);
	sched_in = machine__findnew_thread(machine, switch_event->next_pid);

	out_events = thread_atoms_search(&atom_root, sched_out, &cmp_pid);
	if (!out_events) {
		thread_atoms_insert(sched_out);
		out_events = thread_atoms_search(&atom_root, sched_out, &cmp_pid);
		if (!out_events)
			die("out-event: Internal tree error");
	}
	add_sched_out_event(out_events, sched_out_state(switch_event), timestamp);

	in_events = thread_atoms_search(&atom_root, sched_in, &cmp_pid);
	if (!in_events) {
		thread_atoms_insert(sched_in);
		in_events = thread_atoms_search(&atom_root, sched_in, &cmp_pid);
		if (!in_events)
			die("in-event: Internal tree error");
		/*
		 * Take came in we have not heard about yet,
		 * add in an initial atom in runnable state:
		 */
		add_sched_out_event(in_events, 'R', timestamp);
	}
	add_sched_in_event(in_events, timestamp);
}

static void
latency_runtime_event(struct trace_runtime_event *runtime_event,
		     struct machine *machine,
		     struct event_format *event __used,
		     int cpu,
		     u64 timestamp,
		     struct thread *this_thread __used)
{
	struct thread *thread = machine__findnew_thread(machine, runtime_event->pid);
	struct work_atoms *atoms = thread_atoms_search(&atom_root, thread, &cmp_pid);

	BUG_ON(cpu >= MAX_CPUS || cpu < 0);
	if (!atoms) {
		thread_atoms_insert(thread);
		atoms = thread_atoms_search(&atom_root, thread, &cmp_pid);
		if (!atoms)
			die("in-event: Internal tree error");
		add_sched_out_event(atoms, 'R', timestamp);
	}

	add_runtime_event(atoms, runtime_event->runtime, timestamp);
}

static void
latency_wakeup_event(struct trace_wakeup_event *wakeup_event,
		     struct machine *machine,
		     struct event_format *__event __used,
		     int cpu __used,
		     u64 timestamp,
		     struct thread *thread __used)
{
	struct work_atoms *atoms;
	struct work_atom *atom;
	struct thread *wakee;

	/* Note for later, it may be interesting to observe the failing cases */
	if (!wakeup_event->success)
		return;

	wakee = machine__findnew_thread(machine, wakeup_event->pid);
	atoms = thread_atoms_search(&atom_root, wakee, &cmp_pid);
	if (!atoms) {
		thread_atoms_insert(wakee);
		atoms = thread_atoms_search(&atom_root, wakee, &cmp_pid);
		if (!atoms)
			die("wakeup-event: Internal tree error");
		add_sched_out_event(atoms, 'S', timestamp);
	}

	BUG_ON(list_empty(&atoms->work_list));

	atom = list_entry(atoms->work_list.prev, struct work_atom, list);

	/*
	 * You WILL be missing events if you've recorded only
	 * one CPU, or are only looking at only one, so don't
	 * make useless noise.
	 */
	if (profile_cpu == -1 && atom->state != THREAD_SLEEPING)
		nr_state_machine_bugs++;

	nr_timestamps++;
	if (atom->sched_out_time > timestamp) {
		nr_unordered_timestamps++;
		return;
	}

	atom->state = THREAD_WAIT_CPU;
	atom->wake_up_time = timestamp;
}

static void
latency_migrate_task_event(struct trace_migrate_task_event *migrate_task_event,
		     struct machine *machine,
		     struct event_format *__event __used,
		     int cpu __used,
		     u64 timestamp,
		     struct thread *thread __used)
{
	struct work_atoms *atoms;
	struct work_atom *atom;
	struct thread *migrant;

	/*
	 * Only need to worry about migration when profiling one CPU.
	 */
	if (profile_cpu == -1)
		return;

	migrant = machine__findnew_thread(machine, migrate_task_event->pid);
	atoms = thread_atoms_search(&atom_root, migrant, &cmp_pid);
	if (!atoms) {
		thread_atoms_insert(migrant);
		register_pid(migrant->pid, migrant->comm);
		atoms = thread_atoms_search(&atom_root, migrant, &cmp_pid);
		if (!atoms)
			die("migration-event: Internal tree error");
		add_sched_out_event(atoms, 'R', timestamp);
	}

	BUG_ON(list_empty(&atoms->work_list));

	atom = list_entry(atoms->work_list.prev, struct work_atom, list);
	atom->sched_in_time = atom->sched_out_time = atom->wake_up_time = timestamp;

	nr_timestamps++;

	if (atom->sched_out_time > timestamp)
		nr_unordered_timestamps++;
}

static struct trace_sched_handler lat_ops  = {
	.wakeup_event		= latency_wakeup_event,
	.switch_event		= latency_switch_event,
	.runtime_event		= latency_runtime_event,
	.fork_event		= latency_fork_event,
	.migrate_task_event	= latency_migrate_task_event,
};

static void output_lat_thread(struct work_atoms *work_list)
{
	int i;
	int ret;
	u64 avg;

	if (!work_list->nb_atoms)
		return;
	/*
	 * Ignore idle threads:
	 */
	if (!strcmp(work_list->thread->comm, "swapper"))
		return;

	all_runtime += work_list->total_runtime;
	all_count += work_list->nb_atoms;

	ret = printf("  %s:%d ", work_list->thread->comm, work_list->thread->pid);

	for (i = 0; i < 24 - ret; i++)
		printf(" ");

	avg = work_list->total_lat / work_list->nb_atoms;

	printf("|%11.3f ms |%9" PRIu64 " | avg:%9.3f ms | max:%9.3f ms | max at: %9.6f s\n",
	      (double)work_list->total_runtime / 1e6,
		 work_list->nb_atoms, (double)avg / 1e6,
		 (double)work_list->max_lat / 1e6,
		 (double)work_list->max_lat_at / 1e9);
}

static int pid_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->thread->pid < r->thread->pid)
		return -1;
	if (l->thread->pid > r->thread->pid)
		return 1;

	return 0;
}

static struct sort_dimension pid_sort_dimension = {
	.name			= "pid",
	.cmp			= pid_cmp,
};

static int avg_cmp(struct work_atoms *l, struct work_atoms *r)
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
	.name			= "avg",
	.cmp			= avg_cmp,
};

static int max_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->max_lat < r->max_lat)
		return -1;
	if (l->max_lat > r->max_lat)
		return 1;

	return 0;
}

static struct sort_dimension max_sort_dimension = {
	.name			= "max",
	.cmp			= max_cmp,
};

static int switch_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->nb_atoms < r->nb_atoms)
		return -1;
	if (l->nb_atoms > r->nb_atoms)
		return 1;

	return 0;
}

static struct sort_dimension switch_sort_dimension = {
	.name			= "switch",
	.cmp			= switch_cmp,
};

static int runtime_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->total_runtime < r->total_runtime)
		return -1;
	if (l->total_runtime > r->total_runtime)
		return 1;

	return 0;
}

static struct sort_dimension runtime_sort_dimension = {
	.name			= "runtime",
	.cmp			= runtime_cmp,
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

static int sort_dimension__add(const char *tok, struct list_head *list)
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
		struct work_atoms *data;
		node = rb_first(&atom_root);
		if (!node)
			break;

		rb_erase(node, &atom_root);
		data = rb_entry(node, struct work_atoms, node);
		__thread_latency_insert(&sorted_atom_root, data, &sort_list);
	}
}

static struct trace_sched_handler *trace_handler;

static void
process_sched_wakeup_event(struct perf_tool *tool __used,
			   struct event_format *event,
			   struct perf_sample *sample,
			   struct machine *machine,
			   struct thread *thread)
{
	void *data = sample->raw_data;
	struct trace_wakeup_event wakeup_event;

	FILL_COMMON_FIELDS(wakeup_event, event, data);

	FILL_ARRAY(wakeup_event, comm, event, data);
	FILL_FIELD(wakeup_event, pid, event, data);
	FILL_FIELD(wakeup_event, prio, event, data);
	FILL_FIELD(wakeup_event, success, event, data);
	FILL_FIELD(wakeup_event, cpu, event, data);

	if (trace_handler->wakeup_event)
		trace_handler->wakeup_event(&wakeup_event, machine, event,
					    sample->cpu, sample->time, thread);
}

/*
 * Track the current task - that way we can know whether there's any
 * weird events, such as a task being switched away that is not current.
 */
static int max_cpu;

static u32 curr_pid[MAX_CPUS] = { [0 ... MAX_CPUS-1] = -1 };

static struct thread *curr_thread[MAX_CPUS];

static char next_shortname1 = 'A';
static char next_shortname2 = '0';

static void
map_switch_event(struct trace_switch_event *switch_event,
		 struct machine *machine,
		 struct event_format *event __used,
		 int this_cpu,
		 u64 timestamp,
		 struct thread *thread __used)
{
	struct thread *sched_out __used, *sched_in;
	int new_shortname;
	u64 timestamp0;
	s64 delta;
	int cpu;

	BUG_ON(this_cpu >= MAX_CPUS || this_cpu < 0);

	if (this_cpu > max_cpu)
		max_cpu = this_cpu;

	timestamp0 = cpu_last_switched[this_cpu];
	cpu_last_switched[this_cpu] = timestamp;
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0)
		die("hm, delta: %" PRIu64 " < 0 ?\n", delta);


	sched_out = machine__findnew_thread(machine, switch_event->prev_pid);
	sched_in = machine__findnew_thread(machine, switch_event->next_pid);

	curr_thread[this_cpu] = sched_in;

	printf("  ");

	new_shortname = 0;
	if (!sched_in->shortname[0]) {
		sched_in->shortname[0] = next_shortname1;
		sched_in->shortname[1] = next_shortname2;

		if (next_shortname1 < 'Z') {
			next_shortname1++;
		} else {
			next_shortname1='A';
			if (next_shortname2 < '9') {
				next_shortname2++;
			} else {
				next_shortname2='0';
			}
		}
		new_shortname = 1;
	}

	for (cpu = 0; cpu <= max_cpu; cpu++) {
		if (cpu != this_cpu)
			printf(" ");
		else
			printf("*");

		if (curr_thread[cpu]) {
			if (curr_thread[cpu]->pid)
				printf("%2s ", curr_thread[cpu]->shortname);
			else
				printf(".  ");
		} else
			printf("   ");
	}

	printf("  %12.6f secs ", (double)timestamp/1e9);
	if (new_shortname) {
		printf("%s => %s:%d\n",
			sched_in->shortname, sched_in->comm, sched_in->pid);
	} else {
		printf("\n");
	}
}

static void
process_sched_switch_event(struct perf_tool *tool __used,
			   struct event_format *event,
			   struct perf_sample *sample,
			   struct machine *machine,
			   struct thread *thread)
{
	int this_cpu = sample->cpu;
	void *data = sample->raw_data;
	struct trace_switch_event switch_event;

	FILL_COMMON_FIELDS(switch_event, event, data);

	FILL_ARRAY(switch_event, prev_comm, event, data);
	FILL_FIELD(switch_event, prev_pid, event, data);
	FILL_FIELD(switch_event, prev_prio, event, data);
	FILL_FIELD(switch_event, prev_state, event, data);
	FILL_ARRAY(switch_event, next_comm, event, data);
	FILL_FIELD(switch_event, next_pid, event, data);
	FILL_FIELD(switch_event, next_prio, event, data);

	if (curr_pid[this_cpu] != (u32)-1) {
		/*
		 * Are we trying to switch away a PID that is
		 * not current?
		 */
		if (curr_pid[this_cpu] != switch_event.prev_pid)
			nr_context_switch_bugs++;
	}
	if (trace_handler->switch_event)
		trace_handler->switch_event(&switch_event, machine, event,
					    this_cpu, sample->time, thread);

	curr_pid[this_cpu] = switch_event.next_pid;
}

static void
process_sched_runtime_event(struct perf_tool *tool __used,
			    struct event_format *event,
			    struct perf_sample *sample,
			    struct machine *machine,
			    struct thread *thread)
{
	void *data = sample->raw_data;
	struct trace_runtime_event runtime_event;

	FILL_ARRAY(runtime_event, comm, event, data);
	FILL_FIELD(runtime_event, pid, event, data);
	FILL_FIELD(runtime_event, runtime, event, data);
	FILL_FIELD(runtime_event, vruntime, event, data);

	if (trace_handler->runtime_event)
		trace_handler->runtime_event(&runtime_event, machine, event,
					     sample->cpu, sample->time, thread);
}

static void
process_sched_fork_event(struct perf_tool *tool __used,
			 struct event_format *event,
			 struct perf_sample *sample,
			 struct machine *machine __used,
			 struct thread *thread)
{
	void *data = sample->raw_data;
	struct trace_fork_event fork_event;

	FILL_COMMON_FIELDS(fork_event, event, data);

	FILL_ARRAY(fork_event, parent_comm, event, data);
	FILL_FIELD(fork_event, parent_pid, event, data);
	FILL_ARRAY(fork_event, child_comm, event, data);
	FILL_FIELD(fork_event, child_pid, event, data);

	if (trace_handler->fork_event)
		trace_handler->fork_event(&fork_event, event,
					  sample->cpu, sample->time, thread);
}

static void
process_sched_exit_event(struct perf_tool *tool __used,
			 struct event_format *event,
			 struct perf_sample *sample __used,
			 struct machine *machine __used,
			 struct thread *thread __used)
{
	if (verbose)
		printf("sched_exit event %p\n", event);
}

static void
process_sched_migrate_task_event(struct perf_tool *tool __used,
				 struct event_format *event,
				 struct perf_sample *sample,
				 struct machine *machine,
				 struct thread *thread)
{
	void *data = sample->raw_data;
	struct trace_migrate_task_event migrate_task_event;

	FILL_COMMON_FIELDS(migrate_task_event, event, data);

	FILL_ARRAY(migrate_task_event, comm, event, data);
	FILL_FIELD(migrate_task_event, pid, event, data);
	FILL_FIELD(migrate_task_event, prio, event, data);
	FILL_FIELD(migrate_task_event, cpu, event, data);

	if (trace_handler->migrate_task_event)
		trace_handler->migrate_task_event(&migrate_task_event, machine,
						  event, sample->cpu,
						  sample->time, thread);
}

typedef void (*tracepoint_handler)(struct perf_tool *tool, struct event_format *event,
				   struct perf_sample *sample,
				   struct machine *machine,
				   struct thread *thread);

static int perf_sched__process_tracepoint_sample(struct perf_tool *tool,
						 union perf_event *event __used,
						 struct perf_sample *sample,
						 struct perf_evsel *evsel,
						 struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);
	struct pevent *pevent = sched->session->pevent;
	struct thread *thread = machine__findnew_thread(machine, sample->pid);

	if (thread == NULL) {
		pr_debug("problem processing %s event, skipping it.\n",
			 perf_evsel__name(evsel));
		return -1;
	}

	evsel->hists.stats.total_period += sample->period;
	hists__inc_nr_events(&evsel->hists, PERF_RECORD_SAMPLE);

	if (evsel->handler.func != NULL) {
		tracepoint_handler f = evsel->handler.func;

		if (evsel->handler.data == NULL)
			evsel->handler.data = pevent_find_event(pevent,
							  evsel->attr.config);

		f(tool, evsel->handler.data, sample, machine, thread);
	}

	return 0;
}

static struct perf_sched perf_sched = {
	.tool = {
		.sample		 = perf_sched__process_tracepoint_sample,
		.comm		 = perf_event__process_comm,
		.lost		 = perf_event__process_lost,
		.fork		 = perf_event__process_task,
		.ordered_samples = true,
	},
};

static void read_events(bool destroy, struct perf_session **psession)
{
	int err = -EINVAL;
	const struct perf_evsel_str_handler handlers[] = {
		{ "sched:sched_switch",	      process_sched_switch_event, },
		{ "sched:sched_stat_runtime", process_sched_runtime_event, },
		{ "sched:sched_wakeup",	      process_sched_wakeup_event, },
		{ "sched:sched_wakeup_new",   process_sched_wakeup_event, },
		{ "sched:sched_process_fork", process_sched_fork_event, },
		{ "sched:sched_process_exit", process_sched_exit_event, },
		{ "sched:sched_migrate_task", process_sched_migrate_task_event, },
	};
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, 0, false,
				    &perf_sched.tool);
	if (session == NULL)
		die("No Memory");

	perf_sched.session = session;

	err = perf_session__set_tracepoints_handlers(session, handlers);
	assert(err == 0);

	if (perf_session__has_traces(session, "record -R")) {
		err = perf_session__process_events(session, &perf_sched.tool);
		if (err)
			die("Failed to process events, error %d", err);

		nr_events      = session->hists.stats.nr_events[0];
		nr_lost_events = session->hists.stats.total_lost;
		nr_lost_chunks = session->hists.stats.nr_events[PERF_RECORD_LOST];
	}

	if (destroy)
		perf_session__delete(session);

	if (psession)
		*psession = session;
}

static void print_bad_events(void)
{
	if (nr_unordered_timestamps && nr_timestamps) {
		printf("  INFO: %.3f%% unordered timestamps (%ld out of %ld)\n",
			(double)nr_unordered_timestamps/(double)nr_timestamps*100.0,
			nr_unordered_timestamps, nr_timestamps);
	}
	if (nr_lost_events && nr_events) {
		printf("  INFO: %.3f%% lost events (%ld out of %ld, in %ld chunks)\n",
			(double)nr_lost_events/(double)nr_events*100.0,
			nr_lost_events, nr_events, nr_lost_chunks);
	}
	if (nr_state_machine_bugs && nr_timestamps) {
		printf("  INFO: %.3f%% state machine bugs (%ld out of %ld)",
			(double)nr_state_machine_bugs/(double)nr_timestamps*100.0,
			nr_state_machine_bugs, nr_timestamps);
		if (nr_lost_events)
			printf(" (due to lost events?)");
		printf("\n");
	}
	if (nr_context_switch_bugs && nr_timestamps) {
		printf("  INFO: %.3f%% context switch bugs (%ld out of %ld)",
			(double)nr_context_switch_bugs/(double)nr_timestamps*100.0,
			nr_context_switch_bugs, nr_timestamps);
		if (nr_lost_events)
			printf(" (due to lost events?)");
		printf("\n");
	}
}

static void __cmd_lat(void)
{
	struct rb_node *next;
	struct perf_session *session;

	setup_pager();
	read_events(false, &session);
	sort_lat();

	printf("\n ---------------------------------------------------------------------------------------------------------------\n");
	printf("  Task                  |   Runtime ms  | Switches | Average delay ms | Maximum delay ms | Maximum delay at     |\n");
	printf(" ---------------------------------------------------------------------------------------------------------------\n");

	next = rb_first(&sorted_atom_root);

	while (next) {
		struct work_atoms *work_list;

		work_list = rb_entry(next, struct work_atoms, node);
		output_lat_thread(work_list);
		next = rb_next(next);
	}

	printf(" -----------------------------------------------------------------------------------------\n");
	printf("  TOTAL:                |%11.3f ms |%9" PRIu64 " |\n",
		(double)all_runtime/1e6, all_count);

	printf(" ---------------------------------------------------\n");

	print_bad_events();
	printf("\n");

	perf_session__delete(session);
}

static struct trace_sched_handler map_ops  = {
	.wakeup_event		= NULL,
	.switch_event		= map_switch_event,
	.runtime_event		= NULL,
	.fork_event		= NULL,
};

static void __cmd_map(void)
{
	max_cpu = sysconf(_SC_NPROCESSORS_CONF);

	setup_pager();
	read_events(true, NULL);
	print_bad_events();
}

static void __cmd_replay(void)
{
	unsigned long i;

	calibrate_run_measurement_overhead();
	calibrate_sleep_measurement_overhead();

	test_calibrations();

	read_events(true, NULL);

	printf("nr_run_events:        %ld\n", nr_run_events);
	printf("nr_sleep_events:      %ld\n", nr_sleep_events);
	printf("nr_wakeup_events:     %ld\n", nr_wakeup_events);

	if (targetless_wakeups)
		printf("target-less wakeups:  %ld\n", targetless_wakeups);
	if (multitarget_wakeups)
		printf("multi-target wakeups: %ld\n", multitarget_wakeups);
	if (nr_run_events_optimized)
		printf("run atoms optimized: %ld\n",
			nr_run_events_optimized);

	print_task_traces();
	add_cross_task_wakeups();

	create_tasks();
	printf("------------------------------------------------------------\n");
	for (i = 0; i < replay_repeat; i++)
		run_one_test();
}


static const char * const sched_usage[] = {
	"perf sched [<options>] {record|latency|map|replay|script}",
	NULL
};

static const struct option sched_options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_INCR('v', "verbose", &verbose,
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
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_INTEGER('C', "CPU", &profile_cpu,
		    "CPU to profile on"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_END()
};

static const char * const replay_usage[] = {
	"perf sched replay [<options>]",
	NULL
};

static const struct option replay_options[] = {
	OPT_UINTEGER('r', "repeat", &replay_repeat,
		     "repeat the workload replay N times (-1: infinite)"),
	OPT_INCR('v', "verbose", &verbose,
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

	sort_dimension__add("pid", &cmp_pid);
}

static const char *record_args[] = {
	"record",
	"-a",
	"-R",
	"-f",
	"-m", "1024",
	"-c", "1",
	"-e", "sched:sched_switch",
	"-e", "sched:sched_stat_wait",
	"-e", "sched:sched_stat_sleep",
	"-e", "sched:sched_stat_iowait",
	"-e", "sched:sched_stat_runtime",
	"-e", "sched:sched_process_exit",
	"-e", "sched:sched_process_fork",
	"-e", "sched:sched_wakeup",
	"-e", "sched:sched_migrate_task",
};

static int __cmd_record(int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_record(i, rec_argv, NULL);
}

int cmd_sched(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, sched_options, sched_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(sched_usage, sched_options);

	/*
	 * Aliased to 'perf script' for now:
	 */
	if (!strcmp(argv[0], "script"))
		return cmd_script(argc, argv, prefix);

	symbol__init();
	if (!strncmp(argv[0], "rec", 3)) {
		return __cmd_record(argc, argv);
	} else if (!strncmp(argv[0], "lat", 3)) {
		trace_handler = &lat_ops;
		if (argc > 1) {
			argc = parse_options(argc, argv, latency_options, latency_usage, 0);
			if (argc)
				usage_with_options(latency_usage, latency_options);
		}
		setup_sorting();
		__cmd_lat();
	} else if (!strcmp(argv[0], "map")) {
		trace_handler = &map_ops;
		setup_sorting();
		__cmd_map();
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
