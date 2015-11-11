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
#include "util/cloexec.h"

#include "util/parse-options.h"
#include "util/trace-event.h"

#include "util/debug.h"

#include <sys/prctl.h>
#include <sys/resource.h>

#include <semaphore.h>
#include <pthread.h>
#include <math.h>
#include <api/fs/fs.h>

#define PR_SET_NAME		15               /* Set process name */
#define MAX_CPUS		4096
#define COMM_LEN		20
#define SYM_LEN			129
#define MAX_PID			1024000

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

#define TASK_STATE_TO_CHAR_STR "RSDTtZXxKWP"

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
	int			num_merged;
};

typedef int (*sort_fn_t)(struct work_atoms *, struct work_atoms *);

struct perf_sched;

struct trace_sched_handler {
	int (*switch_event)(struct perf_sched *sched, struct perf_evsel *evsel,
			    struct perf_sample *sample, struct machine *machine);

	int (*runtime_event)(struct perf_sched *sched, struct perf_evsel *evsel,
			     struct perf_sample *sample, struct machine *machine);

	int (*wakeup_event)(struct perf_sched *sched, struct perf_evsel *evsel,
			    struct perf_sample *sample, struct machine *machine);

	/* PERF_RECORD_FORK event, not sched_process_fork tracepoint */
	int (*fork_event)(struct perf_sched *sched, union perf_event *event,
			  struct machine *machine);

	int (*migrate_task_event)(struct perf_sched *sched,
				  struct perf_evsel *evsel,
				  struct perf_sample *sample,
				  struct machine *machine);
};

struct perf_sched {
	struct perf_tool tool;
	const char	 *sort_order;
	unsigned long	 nr_tasks;
	struct task_desc **pid_to_task;
	struct task_desc **tasks;
	const struct trace_sched_handler *tp_handler;
	pthread_mutex_t	 start_work_mutex;
	pthread_mutex_t	 work_done_wait_mutex;
	int		 profile_cpu;
/*
 * Track the current task - that way we can know whether there's any
 * weird events, such as a task being switched away that is not current.
 */
	int		 max_cpu;
	u32		 curr_pid[MAX_CPUS];
	struct thread	 *curr_thread[MAX_CPUS];
	char		 next_shortname1;
	char		 next_shortname2;
	unsigned int	 replay_repeat;
	unsigned long	 nr_run_events;
	unsigned long	 nr_sleep_events;
	unsigned long	 nr_wakeup_events;
	unsigned long	 nr_sleep_corrections;
	unsigned long	 nr_run_events_optimized;
	unsigned long	 targetless_wakeups;
	unsigned long	 multitarget_wakeups;
	unsigned long	 nr_runs;
	unsigned long	 nr_timestamps;
	unsigned long	 nr_unordered_timestamps;
	unsigned long	 nr_context_switch_bugs;
	unsigned long	 nr_events;
	unsigned long	 nr_lost_chunks;
	unsigned long	 nr_lost_events;
	u64		 run_measurement_overhead;
	u64		 sleep_measurement_overhead;
	u64		 start_time;
	u64		 cpu_usage;
	u64		 runavg_cpu_usage;
	u64		 parent_cpu_usage;
	u64		 runavg_parent_cpu_usage;
	u64		 sum_runtime;
	u64		 sum_fluct;
	u64		 run_avg;
	u64		 all_runtime;
	u64		 all_count;
	u64		 cpu_last_switched[MAX_CPUS];
	struct rb_root	 atom_root, sorted_atom_root, merged_atom_root;
	struct list_head sort_list, cmp_pid;
	bool force;
	bool skip_merge;
};

static u64 get_nsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void burn_nsecs(struct perf_sched *sched, u64 nsecs)
{
	u64 T0 = get_nsecs(), T1;

	do {
		T1 = get_nsecs();
	} while (T1 + sched->run_measurement_overhead < T0 + nsecs);
}

static void sleep_nsecs(u64 nsecs)
{
	struct timespec ts;

	ts.tv_nsec = nsecs % 999999999;
	ts.tv_sec = nsecs / 999999999;

	nanosleep(&ts, NULL);
}

static void calibrate_run_measurement_overhead(struct perf_sched *sched)
{
	u64 T0, T1, delta, min_delta = 1000000000ULL;
	int i;

	for (i = 0; i < 10; i++) {
		T0 = get_nsecs();
		burn_nsecs(sched, 0);
		T1 = get_nsecs();
		delta = T1-T0;
		min_delta = min(min_delta, delta);
	}
	sched->run_measurement_overhead = min_delta;

	printf("run measurement overhead: %" PRIu64 " nsecs\n", min_delta);
}

static void calibrate_sleep_measurement_overhead(struct perf_sched *sched)
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
	sched->sleep_measurement_overhead = min_delta;

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

static void add_sched_event_run(struct perf_sched *sched, struct task_desc *task,
				u64 timestamp, u64 duration)
{
	struct sched_atom *event, *curr_event = last_event(task);

	/*
	 * optimize an existing RUN event by merging this one
	 * to it:
	 */
	if (curr_event && curr_event->type == SCHED_EVENT_RUN) {
		sched->nr_run_events_optimized++;
		curr_event->duration += duration;
		return;
	}

	event = get_new_event(task, timestamp);

	event->type = SCHED_EVENT_RUN;
	event->duration = duration;

	sched->nr_run_events++;
}

static void add_sched_event_wakeup(struct perf_sched *sched, struct task_desc *task,
				   u64 timestamp, struct task_desc *wakee)
{
	struct sched_atom *event, *wakee_event;

	event = get_new_event(task, timestamp);
	event->type = SCHED_EVENT_WAKEUP;
	event->wakee = wakee;

	wakee_event = last_event(wakee);
	if (!wakee_event || wakee_event->type != SCHED_EVENT_SLEEP) {
		sched->targetless_wakeups++;
		return;
	}
	if (wakee_event->wait_sem) {
		sched->multitarget_wakeups++;
		return;
	}

	wakee_event->wait_sem = zalloc(sizeof(*wakee_event->wait_sem));
	sem_init(wakee_event->wait_sem, 0, 0);
	wakee_event->specific_wait = 1;
	event->wait_sem = wakee_event->wait_sem;

	sched->nr_wakeup_events++;
}

static void add_sched_event_sleep(struct perf_sched *sched, struct task_desc *task,
				  u64 timestamp, u64 task_state __maybe_unused)
{
	struct sched_atom *event = get_new_event(task, timestamp);

	event->type = SCHED_EVENT_SLEEP;

	sched->nr_sleep_events++;
}

static struct task_desc *register_pid(struct perf_sched *sched,
				      unsigned long pid, const char *comm)
{
	struct task_desc *task;
	static int pid_max;

	if (sched->pid_to_task == NULL) {
		if (sysctl__read_int("kernel/pid_max", &pid_max) < 0)
			pid_max = MAX_PID;
		BUG_ON((sched->pid_to_task = calloc(pid_max, sizeof(struct task_desc *))) == NULL);
	}
	if (pid >= (unsigned long)pid_max) {
		BUG_ON((sched->pid_to_task = realloc(sched->pid_to_task, (pid + 1) *
			sizeof(struct task_desc *))) == NULL);
		while (pid >= (unsigned long)pid_max)
			sched->pid_to_task[pid_max++] = NULL;
	}

	task = sched->pid_to_task[pid];

	if (task)
		return task;

	task = zalloc(sizeof(*task));
	task->pid = pid;
	task->nr = sched->nr_tasks;
	strcpy(task->comm, comm);
	/*
	 * every task starts in sleeping state - this gets ignored
	 * if there's no wakeup pointing to this sleep state:
	 */
	add_sched_event_sleep(sched, task, 0, 0);

	sched->pid_to_task[pid] = task;
	sched->nr_tasks++;
	sched->tasks = realloc(sched->tasks, sched->nr_tasks * sizeof(struct task_desc *));
	BUG_ON(!sched->tasks);
	sched->tasks[task->nr] = task;

	if (verbose)
		printf("registered task #%ld, PID %ld (%s)\n", sched->nr_tasks, pid, comm);

	return task;
}


static void print_task_traces(struct perf_sched *sched)
{
	struct task_desc *task;
	unsigned long i;

	for (i = 0; i < sched->nr_tasks; i++) {
		task = sched->tasks[i];
		printf("task %6ld (%20s:%10ld), nr_events: %ld\n",
			task->nr, task->comm, task->pid, task->nr_events);
	}
}

static void add_cross_task_wakeups(struct perf_sched *sched)
{
	struct task_desc *task1, *task2;
	unsigned long i, j;

	for (i = 0; i < sched->nr_tasks; i++) {
		task1 = sched->tasks[i];
		j = i + 1;
		if (j == sched->nr_tasks)
			j = 0;
		task2 = sched->tasks[j];
		add_sched_event_wakeup(sched, task1, 0, task2);
	}
}

static void perf_sched__process_event(struct perf_sched *sched,
				      struct sched_atom *atom)
{
	int ret = 0;

	switch (atom->type) {
		case SCHED_EVENT_RUN:
			burn_nsecs(sched, atom->duration);
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

static int self_open_counters(struct perf_sched *sched, unsigned long cur_task)
{
	struct perf_event_attr attr;
	char sbuf[STRERR_BUFSIZE], info[STRERR_BUFSIZE];
	int fd;
	struct rlimit limit;
	bool need_privilege = false;

	memset(&attr, 0, sizeof(attr));

	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_TASK_CLOCK;

force_again:
	fd = sys_perf_event_open(&attr, 0, -1, -1,
				 perf_event_open_cloexec_flag());

	if (fd < 0) {
		if (errno == EMFILE) {
			if (sched->force) {
				BUG_ON(getrlimit(RLIMIT_NOFILE, &limit) == -1);
				limit.rlim_cur += sched->nr_tasks - cur_task;
				if (limit.rlim_cur > limit.rlim_max) {
					limit.rlim_max = limit.rlim_cur;
					need_privilege = true;
				}
				if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
					if (need_privilege && errno == EPERM)
						strcpy(info, "Need privilege\n");
				} else
					goto force_again;
			} else
				strcpy(info, "Have a try with -f option\n");
		}
		pr_err("Error: sys_perf_event_open() syscall returned "
		       "with %d (%s)\n%s", fd,
		       strerror_r(errno, sbuf, sizeof(sbuf)), info);
		exit(EXIT_FAILURE);
	}
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

struct sched_thread_parms {
	struct task_desc  *task;
	struct perf_sched *sched;
	int fd;
};

static void *thread_func(void *ctx)
{
	struct sched_thread_parms *parms = ctx;
	struct task_desc *this_task = parms->task;
	struct perf_sched *sched = parms->sched;
	u64 cpu_usage_0, cpu_usage_1;
	unsigned long i, ret;
	char comm2[22];
	int fd = parms->fd;

	zfree(&parms);

	sprintf(comm2, ":%s", this_task->comm);
	prctl(PR_SET_NAME, comm2);
	if (fd < 0)
		return NULL;
again:
	ret = sem_post(&this_task->ready_for_work);
	BUG_ON(ret);
	ret = pthread_mutex_lock(&sched->start_work_mutex);
	BUG_ON(ret);
	ret = pthread_mutex_unlock(&sched->start_work_mutex);
	BUG_ON(ret);

	cpu_usage_0 = get_cpu_usage_nsec_self(fd);

	for (i = 0; i < this_task->nr_events; i++) {
		this_task->curr_event = i;
		perf_sched__process_event(sched, this_task->atoms[i]);
	}

	cpu_usage_1 = get_cpu_usage_nsec_self(fd);
	this_task->cpu_usage = cpu_usage_1 - cpu_usage_0;
	ret = sem_post(&this_task->work_done_sem);
	BUG_ON(ret);

	ret = pthread_mutex_lock(&sched->work_done_wait_mutex);
	BUG_ON(ret);
	ret = pthread_mutex_unlock(&sched->work_done_wait_mutex);
	BUG_ON(ret);

	goto again;
}

static void create_tasks(struct perf_sched *sched)
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
	err = pthread_mutex_lock(&sched->start_work_mutex);
	BUG_ON(err);
	err = pthread_mutex_lock(&sched->work_done_wait_mutex);
	BUG_ON(err);
	for (i = 0; i < sched->nr_tasks; i++) {
		struct sched_thread_parms *parms = malloc(sizeof(*parms));
		BUG_ON(parms == NULL);
		parms->task = task = sched->tasks[i];
		parms->sched = sched;
		parms->fd = self_open_counters(sched, i);
		sem_init(&task->sleep_sem, 0, 0);
		sem_init(&task->ready_for_work, 0, 0);
		sem_init(&task->work_done_sem, 0, 0);
		task->curr_event = 0;
		err = pthread_create(&task->thread, &attr, thread_func, parms);
		BUG_ON(err);
	}
}

static void wait_for_tasks(struct perf_sched *sched)
{
	u64 cpu_usage_0, cpu_usage_1;
	struct task_desc *task;
	unsigned long i, ret;

	sched->start_time = get_nsecs();
	sched->cpu_usage = 0;
	pthread_mutex_unlock(&sched->work_done_wait_mutex);

	for (i = 0; i < sched->nr_tasks; i++) {
		task = sched->tasks[i];
		ret = sem_wait(&task->ready_for_work);
		BUG_ON(ret);
		sem_init(&task->ready_for_work, 0, 0);
	}
	ret = pthread_mutex_lock(&sched->work_done_wait_mutex);
	BUG_ON(ret);

	cpu_usage_0 = get_cpu_usage_nsec_parent();

	pthread_mutex_unlock(&sched->start_work_mutex);

	for (i = 0; i < sched->nr_tasks; i++) {
		task = sched->tasks[i];
		ret = sem_wait(&task->work_done_sem);
		BUG_ON(ret);
		sem_init(&task->work_done_sem, 0, 0);
		sched->cpu_usage += task->cpu_usage;
		task->cpu_usage = 0;
	}

	cpu_usage_1 = get_cpu_usage_nsec_parent();
	if (!sched->runavg_cpu_usage)
		sched->runavg_cpu_usage = sched->cpu_usage;
	sched->runavg_cpu_usage = (sched->runavg_cpu_usage * (sched->replay_repeat - 1) + sched->cpu_usage) / sched->replay_repeat;

	sched->parent_cpu_usage = cpu_usage_1 - cpu_usage_0;
	if (!sched->runavg_parent_cpu_usage)
		sched->runavg_parent_cpu_usage = sched->parent_cpu_usage;
	sched->runavg_parent_cpu_usage = (sched->runavg_parent_cpu_usage * (sched->replay_repeat - 1) +
					 sched->parent_cpu_usage)/sched->replay_repeat;

	ret = pthread_mutex_lock(&sched->start_work_mutex);
	BUG_ON(ret);

	for (i = 0; i < sched->nr_tasks; i++) {
		task = sched->tasks[i];
		sem_init(&task->sleep_sem, 0, 0);
		task->curr_event = 0;
	}
}

static void run_one_test(struct perf_sched *sched)
{
	u64 T0, T1, delta, avg_delta, fluct;

	T0 = get_nsecs();
	wait_for_tasks(sched);
	T1 = get_nsecs();

	delta = T1 - T0;
	sched->sum_runtime += delta;
	sched->nr_runs++;

	avg_delta = sched->sum_runtime / sched->nr_runs;
	if (delta < avg_delta)
		fluct = avg_delta - delta;
	else
		fluct = delta - avg_delta;
	sched->sum_fluct += fluct;
	if (!sched->run_avg)
		sched->run_avg = delta;
	sched->run_avg = (sched->run_avg * (sched->replay_repeat - 1) + delta) / sched->replay_repeat;

	printf("#%-3ld: %0.3f, ", sched->nr_runs, (double)delta / 1000000.0);

	printf("ravg: %0.2f, ", (double)sched->run_avg / 1e6);

	printf("cpu: %0.2f / %0.2f",
		(double)sched->cpu_usage / 1e6, (double)sched->runavg_cpu_usage / 1e6);

#if 0
	/*
	 * rusage statistics done by the parent, these are less
	 * accurate than the sched->sum_exec_runtime based statistics:
	 */
	printf(" [%0.2f / %0.2f]",
		(double)sched->parent_cpu_usage/1e6,
		(double)sched->runavg_parent_cpu_usage/1e6);
#endif

	printf("\n");

	if (sched->nr_sleep_corrections)
		printf(" (%ld sleep corrections)\n", sched->nr_sleep_corrections);
	sched->nr_sleep_corrections = 0;
}

static void test_calibrations(struct perf_sched *sched)
{
	u64 T0, T1;

	T0 = get_nsecs();
	burn_nsecs(sched, 1e6);
	T1 = get_nsecs();

	printf("the run test took %" PRIu64 " nsecs\n", T1 - T0);

	T0 = get_nsecs();
	sleep_nsecs(1e6);
	T1 = get_nsecs();

	printf("the sleep test took %" PRIu64 " nsecs\n", T1 - T0);
}

static int
replay_wakeup_event(struct perf_sched *sched,
		    struct perf_evsel *evsel, struct perf_sample *sample,
		    struct machine *machine __maybe_unused)
{
	const char *comm = perf_evsel__strval(evsel, sample, "comm");
	const u32 pid	 = perf_evsel__intval(evsel, sample, "pid");
	struct task_desc *waker, *wakee;

	if (verbose) {
		printf("sched_wakeup event %p\n", evsel);

		printf(" ... pid %d woke up %s/%d\n", sample->tid, comm, pid);
	}

	waker = register_pid(sched, sample->tid, "<unknown>");
	wakee = register_pid(sched, pid, comm);

	add_sched_event_wakeup(sched, waker, sample->time, wakee);
	return 0;
}

static int replay_switch_event(struct perf_sched *sched,
			       struct perf_evsel *evsel,
			       struct perf_sample *sample,
			       struct machine *machine __maybe_unused)
{
	const char *prev_comm  = perf_evsel__strval(evsel, sample, "prev_comm"),
		   *next_comm  = perf_evsel__strval(evsel, sample, "next_comm");
	const u32 prev_pid = perf_evsel__intval(evsel, sample, "prev_pid"),
		  next_pid = perf_evsel__intval(evsel, sample, "next_pid");
	const u64 prev_state = perf_evsel__intval(evsel, sample, "prev_state");
	struct task_desc *prev, __maybe_unused *next;
	u64 timestamp0, timestamp = sample->time;
	int cpu = sample->cpu;
	s64 delta;

	if (verbose)
		printf("sched_switch event %p\n", evsel);

	if (cpu >= MAX_CPUS || cpu < 0)
		return 0;

	timestamp0 = sched->cpu_last_switched[cpu];
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0) {
		pr_err("hm, delta: %" PRIu64 " < 0 ?\n", delta);
		return -1;
	}

	pr_debug(" ... switch from %s/%d to %s/%d [ran %" PRIu64 " nsecs]\n",
		 prev_comm, prev_pid, next_comm, next_pid, delta);

	prev = register_pid(sched, prev_pid, prev_comm);
	next = register_pid(sched, next_pid, next_comm);

	sched->cpu_last_switched[cpu] = timestamp;

	add_sched_event_run(sched, prev, timestamp, delta);
	add_sched_event_sleep(sched, prev, timestamp, prev_state);

	return 0;
}

static int replay_fork_event(struct perf_sched *sched,
			     union perf_event *event,
			     struct machine *machine)
{
	struct thread *child, *parent;

	child = machine__findnew_thread(machine, event->fork.pid,
					event->fork.tid);
	parent = machine__findnew_thread(machine, event->fork.ppid,
					 event->fork.ptid);

	if (child == NULL || parent == NULL) {
		pr_debug("thread does not exist on fork event: child %p, parent %p\n",
				 child, parent);
		goto out_put;
	}

	if (verbose) {
		printf("fork event\n");
		printf("... parent: %s/%d\n", thread__comm_str(parent), parent->tid);
		printf("...  child: %s/%d\n", thread__comm_str(child), child->tid);
	}

	register_pid(sched, parent->tid, thread__comm_str(parent));
	register_pid(sched, child->tid, thread__comm_str(child));
out_put:
	thread__put(child);
	thread__put(parent);
	return 0;
}

struct sort_dimension {
	const char		*name;
	sort_fn_t		cmp;
	struct list_head	list;
};

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

static int thread_atoms_insert(struct perf_sched *sched, struct thread *thread)
{
	struct work_atoms *atoms = zalloc(sizeof(*atoms));
	if (!atoms) {
		pr_err("No memory at %s\n", __func__);
		return -1;
	}

	atoms->thread = thread__get(thread);
	INIT_LIST_HEAD(&atoms->work_list);
	__thread_latency_insert(&sched->atom_root, atoms, &sched->cmp_pid);
	return 0;
}

static char sched_out_state(u64 prev_state)
{
	const char *str = TASK_STATE_TO_CHAR_STR;

	return str[prev_state];
}

static int
add_sched_out_event(struct work_atoms *atoms,
		    char run_state,
		    u64 timestamp)
{
	struct work_atom *atom = zalloc(sizeof(*atom));
	if (!atom) {
		pr_err("Non memory at %s", __func__);
		return -1;
	}

	atom->sched_out_time = timestamp;

	if (run_state == 'R') {
		atom->state = THREAD_WAIT_CPU;
		atom->wake_up_time = atom->sched_out_time;
	}

	list_add_tail(&atom->list, &atoms->work_list);
	return 0;
}

static void
add_runtime_event(struct work_atoms *atoms, u64 delta,
		  u64 timestamp __maybe_unused)
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

static int latency_switch_event(struct perf_sched *sched,
				struct perf_evsel *evsel,
				struct perf_sample *sample,
				struct machine *machine)
{
	const u32 prev_pid = perf_evsel__intval(evsel, sample, "prev_pid"),
		  next_pid = perf_evsel__intval(evsel, sample, "next_pid");
	const u64 prev_state = perf_evsel__intval(evsel, sample, "prev_state");
	struct work_atoms *out_events, *in_events;
	struct thread *sched_out, *sched_in;
	u64 timestamp0, timestamp = sample->time;
	int cpu = sample->cpu, err = -1;
	s64 delta;

	BUG_ON(cpu >= MAX_CPUS || cpu < 0);

	timestamp0 = sched->cpu_last_switched[cpu];
	sched->cpu_last_switched[cpu] = timestamp;
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0) {
		pr_err("hm, delta: %" PRIu64 " < 0 ?\n", delta);
		return -1;
	}

	sched_out = machine__findnew_thread(machine, -1, prev_pid);
	sched_in = machine__findnew_thread(machine, -1, next_pid);
	if (sched_out == NULL || sched_in == NULL)
		goto out_put;

	out_events = thread_atoms_search(&sched->atom_root, sched_out, &sched->cmp_pid);
	if (!out_events) {
		if (thread_atoms_insert(sched, sched_out))
			goto out_put;
		out_events = thread_atoms_search(&sched->atom_root, sched_out, &sched->cmp_pid);
		if (!out_events) {
			pr_err("out-event: Internal tree error");
			goto out_put;
		}
	}
	if (add_sched_out_event(out_events, sched_out_state(prev_state), timestamp))
		return -1;

	in_events = thread_atoms_search(&sched->atom_root, sched_in, &sched->cmp_pid);
	if (!in_events) {
		if (thread_atoms_insert(sched, sched_in))
			goto out_put;
		in_events = thread_atoms_search(&sched->atom_root, sched_in, &sched->cmp_pid);
		if (!in_events) {
			pr_err("in-event: Internal tree error");
			goto out_put;
		}
		/*
		 * Take came in we have not heard about yet,
		 * add in an initial atom in runnable state:
		 */
		if (add_sched_out_event(in_events, 'R', timestamp))
			goto out_put;
	}
	add_sched_in_event(in_events, timestamp);
	err = 0;
out_put:
	thread__put(sched_out);
	thread__put(sched_in);
	return err;
}

static int latency_runtime_event(struct perf_sched *sched,
				 struct perf_evsel *evsel,
				 struct perf_sample *sample,
				 struct machine *machine)
{
	const u32 pid	   = perf_evsel__intval(evsel, sample, "pid");
	const u64 runtime  = perf_evsel__intval(evsel, sample, "runtime");
	struct thread *thread = machine__findnew_thread(machine, -1, pid);
	struct work_atoms *atoms = thread_atoms_search(&sched->atom_root, thread, &sched->cmp_pid);
	u64 timestamp = sample->time;
	int cpu = sample->cpu, err = -1;

	if (thread == NULL)
		return -1;

	BUG_ON(cpu >= MAX_CPUS || cpu < 0);
	if (!atoms) {
		if (thread_atoms_insert(sched, thread))
			goto out_put;
		atoms = thread_atoms_search(&sched->atom_root, thread, &sched->cmp_pid);
		if (!atoms) {
			pr_err("in-event: Internal tree error");
			goto out_put;
		}
		if (add_sched_out_event(atoms, 'R', timestamp))
			goto out_put;
	}

	add_runtime_event(atoms, runtime, timestamp);
	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int latency_wakeup_event(struct perf_sched *sched,
				struct perf_evsel *evsel,
				struct perf_sample *sample,
				struct machine *machine)
{
	const u32 pid	  = perf_evsel__intval(evsel, sample, "pid");
	struct work_atoms *atoms;
	struct work_atom *atom;
	struct thread *wakee;
	u64 timestamp = sample->time;
	int err = -1;

	wakee = machine__findnew_thread(machine, -1, pid);
	if (wakee == NULL)
		return -1;
	atoms = thread_atoms_search(&sched->atom_root, wakee, &sched->cmp_pid);
	if (!atoms) {
		if (thread_atoms_insert(sched, wakee))
			goto out_put;
		atoms = thread_atoms_search(&sched->atom_root, wakee, &sched->cmp_pid);
		if (!atoms) {
			pr_err("wakeup-event: Internal tree error");
			goto out_put;
		}
		if (add_sched_out_event(atoms, 'S', timestamp))
			goto out_put;
	}

	BUG_ON(list_empty(&atoms->work_list));

	atom = list_entry(atoms->work_list.prev, struct work_atom, list);

	/*
	 * As we do not guarantee the wakeup event happens when
	 * task is out of run queue, also may happen when task is
	 * on run queue and wakeup only change ->state to TASK_RUNNING,
	 * then we should not set the ->wake_up_time when wake up a
	 * task which is on run queue.
	 *
	 * You WILL be missing events if you've recorded only
	 * one CPU, or are only looking at only one, so don't
	 * skip in this case.
	 */
	if (sched->profile_cpu == -1 && atom->state != THREAD_SLEEPING)
		goto out_ok;

	sched->nr_timestamps++;
	if (atom->sched_out_time > timestamp) {
		sched->nr_unordered_timestamps++;
		goto out_ok;
	}

	atom->state = THREAD_WAIT_CPU;
	atom->wake_up_time = timestamp;
out_ok:
	err = 0;
out_put:
	thread__put(wakee);
	return err;
}

static int latency_migrate_task_event(struct perf_sched *sched,
				      struct perf_evsel *evsel,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	const u32 pid = perf_evsel__intval(evsel, sample, "pid");
	u64 timestamp = sample->time;
	struct work_atoms *atoms;
	struct work_atom *atom;
	struct thread *migrant;
	int err = -1;

	/*
	 * Only need to worry about migration when profiling one CPU.
	 */
	if (sched->profile_cpu == -1)
		return 0;

	migrant = machine__findnew_thread(machine, -1, pid);
	if (migrant == NULL)
		return -1;
	atoms = thread_atoms_search(&sched->atom_root, migrant, &sched->cmp_pid);
	if (!atoms) {
		if (thread_atoms_insert(sched, migrant))
			goto out_put;
		register_pid(sched, migrant->tid, thread__comm_str(migrant));
		atoms = thread_atoms_search(&sched->atom_root, migrant, &sched->cmp_pid);
		if (!atoms) {
			pr_err("migration-event: Internal tree error");
			goto out_put;
		}
		if (add_sched_out_event(atoms, 'R', timestamp))
			goto out_put;
	}

	BUG_ON(list_empty(&atoms->work_list));

	atom = list_entry(atoms->work_list.prev, struct work_atom, list);
	atom->sched_in_time = atom->sched_out_time = atom->wake_up_time = timestamp;

	sched->nr_timestamps++;

	if (atom->sched_out_time > timestamp)
		sched->nr_unordered_timestamps++;
	err = 0;
out_put:
	thread__put(migrant);
	return err;
}

static void output_lat_thread(struct perf_sched *sched, struct work_atoms *work_list)
{
	int i;
	int ret;
	u64 avg;

	if (!work_list->nb_atoms)
		return;
	/*
	 * Ignore idle threads:
	 */
	if (!strcmp(thread__comm_str(work_list->thread), "swapper"))
		return;

	sched->all_runtime += work_list->total_runtime;
	sched->all_count   += work_list->nb_atoms;

	if (work_list->num_merged > 1)
		ret = printf("  %s:(%d) ", thread__comm_str(work_list->thread), work_list->num_merged);
	else
		ret = printf("  %s:%d ", thread__comm_str(work_list->thread), work_list->thread->tid);

	for (i = 0; i < 24 - ret; i++)
		printf(" ");

	avg = work_list->total_lat / work_list->nb_atoms;

	printf("|%11.3f ms |%9" PRIu64 " | avg:%9.3f ms | max:%9.3f ms | max at: %13.6f s\n",
	      (double)work_list->total_runtime / 1e6,
		 work_list->nb_atoms, (double)avg / 1e6,
		 (double)work_list->max_lat / 1e6,
		 (double)work_list->max_lat_at / 1e9);
}

static int pid_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->thread->tid < r->thread->tid)
		return -1;
	if (l->thread->tid > r->thread->tid)
		return 1;

	return 0;
}

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

static int max_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->max_lat < r->max_lat)
		return -1;
	if (l->max_lat > r->max_lat)
		return 1;

	return 0;
}

static int switch_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->nb_atoms < r->nb_atoms)
		return -1;
	if (l->nb_atoms > r->nb_atoms)
		return 1;

	return 0;
}

static int runtime_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->total_runtime < r->total_runtime)
		return -1;
	if (l->total_runtime > r->total_runtime)
		return 1;

	return 0;
}

static int sort_dimension__add(const char *tok, struct list_head *list)
{
	size_t i;
	static struct sort_dimension avg_sort_dimension = {
		.name = "avg",
		.cmp  = avg_cmp,
	};
	static struct sort_dimension max_sort_dimension = {
		.name = "max",
		.cmp  = max_cmp,
	};
	static struct sort_dimension pid_sort_dimension = {
		.name = "pid",
		.cmp  = pid_cmp,
	};
	static struct sort_dimension runtime_sort_dimension = {
		.name = "runtime",
		.cmp  = runtime_cmp,
	};
	static struct sort_dimension switch_sort_dimension = {
		.name = "switch",
		.cmp  = switch_cmp,
	};
	struct sort_dimension *available_sorts[] = {
		&pid_sort_dimension,
		&avg_sort_dimension,
		&max_sort_dimension,
		&switch_sort_dimension,
		&runtime_sort_dimension,
	};

	for (i = 0; i < ARRAY_SIZE(available_sorts); i++) {
		if (!strcmp(available_sorts[i]->name, tok)) {
			list_add_tail(&available_sorts[i]->list, list);

			return 0;
		}
	}

	return -1;
}

static void perf_sched__sort_lat(struct perf_sched *sched)
{
	struct rb_node *node;
	struct rb_root *root = &sched->atom_root;
again:
	for (;;) {
		struct work_atoms *data;
		node = rb_first(root);
		if (!node)
			break;

		rb_erase(node, root);
		data = rb_entry(node, struct work_atoms, node);
		__thread_latency_insert(&sched->sorted_atom_root, data, &sched->sort_list);
	}
	if (root == &sched->atom_root) {
		root = &sched->merged_atom_root;
		goto again;
	}
}

static int process_sched_wakeup_event(struct perf_tool *tool,
				      struct perf_evsel *evsel,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);

	if (sched->tp_handler->wakeup_event)
		return sched->tp_handler->wakeup_event(sched, evsel, sample, machine);

	return 0;
}

static int map_switch_event(struct perf_sched *sched, struct perf_evsel *evsel,
			    struct perf_sample *sample, struct machine *machine)
{
	const u32 next_pid = perf_evsel__intval(evsel, sample, "next_pid");
	struct thread *sched_in;
	int new_shortname;
	u64 timestamp0, timestamp = sample->time;
	s64 delta;
	int cpu, this_cpu = sample->cpu;

	BUG_ON(this_cpu >= MAX_CPUS || this_cpu < 0);

	if (this_cpu > sched->max_cpu)
		sched->max_cpu = this_cpu;

	timestamp0 = sched->cpu_last_switched[this_cpu];
	sched->cpu_last_switched[this_cpu] = timestamp;
	if (timestamp0)
		delta = timestamp - timestamp0;
	else
		delta = 0;

	if (delta < 0) {
		pr_err("hm, delta: %" PRIu64 " < 0 ?\n", delta);
		return -1;
	}

	sched_in = machine__findnew_thread(machine, -1, next_pid);
	if (sched_in == NULL)
		return -1;

	sched->curr_thread[this_cpu] = thread__get(sched_in);

	printf("  ");

	new_shortname = 0;
	if (!sched_in->shortname[0]) {
		if (!strcmp(thread__comm_str(sched_in), "swapper")) {
			/*
			 * Don't allocate a letter-number for swapper:0
			 * as a shortname. Instead, we use '.' for it.
			 */
			sched_in->shortname[0] = '.';
			sched_in->shortname[1] = ' ';
		} else {
			sched_in->shortname[0] = sched->next_shortname1;
			sched_in->shortname[1] = sched->next_shortname2;

			if (sched->next_shortname1 < 'Z') {
				sched->next_shortname1++;
			} else {
				sched->next_shortname1 = 'A';
				if (sched->next_shortname2 < '9')
					sched->next_shortname2++;
				else
					sched->next_shortname2 = '0';
			}
		}
		new_shortname = 1;
	}

	for (cpu = 0; cpu <= sched->max_cpu; cpu++) {
		if (cpu != this_cpu)
			printf(" ");
		else
			printf("*");

		if (sched->curr_thread[cpu])
			printf("%2s ", sched->curr_thread[cpu]->shortname);
		else
			printf("   ");
	}

	printf("  %12.6f secs ", (double)timestamp/1e9);
	if (new_shortname) {
		printf("%s => %s:%d\n",
		       sched_in->shortname, thread__comm_str(sched_in), sched_in->tid);
	} else {
		printf("\n");
	}

	thread__put(sched_in);

	return 0;
}

static int process_sched_switch_event(struct perf_tool *tool,
				      struct perf_evsel *evsel,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);
	int this_cpu = sample->cpu, err = 0;
	u32 prev_pid = perf_evsel__intval(evsel, sample, "prev_pid"),
	    next_pid = perf_evsel__intval(evsel, sample, "next_pid");

	if (sched->curr_pid[this_cpu] != (u32)-1) {
		/*
		 * Are we trying to switch away a PID that is
		 * not current?
		 */
		if (sched->curr_pid[this_cpu] != prev_pid)
			sched->nr_context_switch_bugs++;
	}

	if (sched->tp_handler->switch_event)
		err = sched->tp_handler->switch_event(sched, evsel, sample, machine);

	sched->curr_pid[this_cpu] = next_pid;
	return err;
}

static int process_sched_runtime_event(struct perf_tool *tool,
				       struct perf_evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);

	if (sched->tp_handler->runtime_event)
		return sched->tp_handler->runtime_event(sched, evsel, sample, machine);

	return 0;
}

static int perf_sched__process_fork_event(struct perf_tool *tool,
					  union perf_event *event,
					  struct perf_sample *sample,
					  struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);

	/* run the fork event through the perf machineruy */
	perf_event__process_fork(tool, event, sample, machine);

	/* and then run additional processing needed for this command */
	if (sched->tp_handler->fork_event)
		return sched->tp_handler->fork_event(sched, event, machine);

	return 0;
}

static int process_sched_migrate_task_event(struct perf_tool *tool,
					    struct perf_evsel *evsel,
					    struct perf_sample *sample,
					    struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);

	if (sched->tp_handler->migrate_task_event)
		return sched->tp_handler->migrate_task_event(sched, evsel, sample, machine);

	return 0;
}

typedef int (*tracepoint_handler)(struct perf_tool *tool,
				  struct perf_evsel *evsel,
				  struct perf_sample *sample,
				  struct machine *machine);

static int perf_sched__process_tracepoint_sample(struct perf_tool *tool __maybe_unused,
						 union perf_event *event __maybe_unused,
						 struct perf_sample *sample,
						 struct perf_evsel *evsel,
						 struct machine *machine)
{
	int err = 0;

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;
		err = f(tool, evsel, sample, machine);
	}

	return err;
}

static int perf_sched__read_events(struct perf_sched *sched)
{
	const struct perf_evsel_str_handler handlers[] = {
		{ "sched:sched_switch",	      process_sched_switch_event, },
		{ "sched:sched_stat_runtime", process_sched_runtime_event, },
		{ "sched:sched_wakeup",	      process_sched_wakeup_event, },
		{ "sched:sched_wakeup_new",   process_sched_wakeup_event, },
		{ "sched:sched_migrate_task", process_sched_migrate_task_event, },
	};
	struct perf_session *session;
	struct perf_data_file file = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
		.force = sched->force,
	};
	int rc = -1;

	session = perf_session__new(&file, false, &sched->tool);
	if (session == NULL) {
		pr_debug("No Memory for session\n");
		return -1;
	}

	symbol__init(&session->header.env);

	if (perf_session__set_tracepoints_handlers(session, handlers))
		goto out_delete;

	if (perf_session__has_traces(session, "record -R")) {
		int err = perf_session__process_events(session);
		if (err) {
			pr_err("Failed to process events, error %d", err);
			goto out_delete;
		}

		sched->nr_events      = session->evlist->stats.nr_events[0];
		sched->nr_lost_events = session->evlist->stats.total_lost;
		sched->nr_lost_chunks = session->evlist->stats.nr_events[PERF_RECORD_LOST];
	}

	rc = 0;
out_delete:
	perf_session__delete(session);
	return rc;
}

static void print_bad_events(struct perf_sched *sched)
{
	if (sched->nr_unordered_timestamps && sched->nr_timestamps) {
		printf("  INFO: %.3f%% unordered timestamps (%ld out of %ld)\n",
			(double)sched->nr_unordered_timestamps/(double)sched->nr_timestamps*100.0,
			sched->nr_unordered_timestamps, sched->nr_timestamps);
	}
	if (sched->nr_lost_events && sched->nr_events) {
		printf("  INFO: %.3f%% lost events (%ld out of %ld, in %ld chunks)\n",
			(double)sched->nr_lost_events/(double)sched->nr_events * 100.0,
			sched->nr_lost_events, sched->nr_events, sched->nr_lost_chunks);
	}
	if (sched->nr_context_switch_bugs && sched->nr_timestamps) {
		printf("  INFO: %.3f%% context switch bugs (%ld out of %ld)",
			(double)sched->nr_context_switch_bugs/(double)sched->nr_timestamps*100.0,
			sched->nr_context_switch_bugs, sched->nr_timestamps);
		if (sched->nr_lost_events)
			printf(" (due to lost events?)");
		printf("\n");
	}
}

static void __merge_work_atoms(struct rb_root *root, struct work_atoms *data)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct work_atoms *this;
	const char *comm = thread__comm_str(data->thread), *this_comm;

	while (*new) {
		int cmp;

		this = container_of(*new, struct work_atoms, node);
		parent = *new;

		this_comm = thread__comm_str(this->thread);
		cmp = strcmp(comm, this_comm);
		if (cmp > 0) {
			new = &((*new)->rb_left);
		} else if (cmp < 0) {
			new = &((*new)->rb_right);
		} else {
			this->num_merged++;
			this->total_runtime += data->total_runtime;
			this->nb_atoms += data->nb_atoms;
			this->total_lat += data->total_lat;
			list_splice(&data->work_list, &this->work_list);
			if (this->max_lat < data->max_lat) {
				this->max_lat = data->max_lat;
				this->max_lat_at = data->max_lat_at;
			}
			zfree(&data);
			return;
		}
	}

	data->num_merged++;
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static void perf_sched__merge_lat(struct perf_sched *sched)
{
	struct work_atoms *data;
	struct rb_node *node;

	if (sched->skip_merge)
		return;

	while ((node = rb_first(&sched->atom_root))) {
		rb_erase(node, &sched->atom_root);
		data = rb_entry(node, struct work_atoms, node);
		__merge_work_atoms(&sched->merged_atom_root, data);
	}
}

static int perf_sched__lat(struct perf_sched *sched)
{
	struct rb_node *next;

	setup_pager();

	if (perf_sched__read_events(sched))
		return -1;

	perf_sched__merge_lat(sched);
	perf_sched__sort_lat(sched);

	printf("\n -----------------------------------------------------------------------------------------------------------------\n");
	printf("  Task                  |   Runtime ms  | Switches | Average delay ms | Maximum delay ms | Maximum delay at       |\n");
	printf(" -----------------------------------------------------------------------------------------------------------------\n");

	next = rb_first(&sched->sorted_atom_root);

	while (next) {
		struct work_atoms *work_list;

		work_list = rb_entry(next, struct work_atoms, node);
		output_lat_thread(sched, work_list);
		next = rb_next(next);
		thread__zput(work_list->thread);
	}

	printf(" -----------------------------------------------------------------------------------------------------------------\n");
	printf("  TOTAL:                |%11.3f ms |%9" PRIu64 " |\n",
		(double)sched->all_runtime / 1e6, sched->all_count);

	printf(" ---------------------------------------------------\n");

	print_bad_events(sched);
	printf("\n");

	return 0;
}

static int perf_sched__map(struct perf_sched *sched)
{
	sched->max_cpu = sysconf(_SC_NPROCESSORS_CONF);

	setup_pager();
	if (perf_sched__read_events(sched))
		return -1;
	print_bad_events(sched);
	return 0;
}

static int perf_sched__replay(struct perf_sched *sched)
{
	unsigned long i;

	calibrate_run_measurement_overhead(sched);
	calibrate_sleep_measurement_overhead(sched);

	test_calibrations(sched);

	if (perf_sched__read_events(sched))
		return -1;

	printf("nr_run_events:        %ld\n", sched->nr_run_events);
	printf("nr_sleep_events:      %ld\n", sched->nr_sleep_events);
	printf("nr_wakeup_events:     %ld\n", sched->nr_wakeup_events);

	if (sched->targetless_wakeups)
		printf("target-less wakeups:  %ld\n", sched->targetless_wakeups);
	if (sched->multitarget_wakeups)
		printf("multi-target wakeups: %ld\n", sched->multitarget_wakeups);
	if (sched->nr_run_events_optimized)
		printf("run atoms optimized: %ld\n",
			sched->nr_run_events_optimized);

	print_task_traces(sched);
	add_cross_task_wakeups(sched);

	create_tasks(sched);
	printf("------------------------------------------------------------\n");
	for (i = 0; i < sched->replay_repeat; i++)
		run_one_test(sched);

	return 0;
}

static void setup_sorting(struct perf_sched *sched, const struct option *options,
			  const char * const usage_msg[])
{
	char *tmp, *tok, *str = strdup(sched->sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(tok, &sched->sort_list) < 0) {
			usage_with_options_msg(usage_msg, options,
					"Unknown --sort key: `%s'", tok);
		}
	}

	free(str);

	sort_dimension__add("pid", &sched->cmp_pid);
}

static int __cmd_record(int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;
	const char * const record_args[] = {
		"record",
		"-a",
		"-R",
		"-m", "1024",
		"-c", "1",
		"-e", "sched:sched_switch",
		"-e", "sched:sched_stat_wait",
		"-e", "sched:sched_stat_sleep",
		"-e", "sched:sched_stat_iowait",
		"-e", "sched:sched_stat_runtime",
		"-e", "sched:sched_process_fork",
		"-e", "sched:sched_wakeup",
		"-e", "sched:sched_wakeup_new",
		"-e", "sched:sched_migrate_task",
	};

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

int cmd_sched(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char default_sort_order[] = "avg, max, switch, runtime";
	struct perf_sched sched = {
		.tool = {
			.sample		 = perf_sched__process_tracepoint_sample,
			.comm		 = perf_event__process_comm,
			.lost		 = perf_event__process_lost,
			.fork		 = perf_sched__process_fork_event,
			.ordered_events = true,
		},
		.cmp_pid	      = LIST_HEAD_INIT(sched.cmp_pid),
		.sort_list	      = LIST_HEAD_INIT(sched.sort_list),
		.start_work_mutex     = PTHREAD_MUTEX_INITIALIZER,
		.work_done_wait_mutex = PTHREAD_MUTEX_INITIALIZER,
		.sort_order	      = default_sort_order,
		.replay_repeat	      = 10,
		.profile_cpu	      = -1,
		.next_shortname1      = 'A',
		.next_shortname2      = '0',
		.skip_merge           = 0,
	};
	const struct option latency_options[] = {
	OPT_STRING('s', "sort", &sched.sort_order, "key[,key2...]",
		   "sort by key(s): runtime, switch, avg, max"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_INTEGER('C', "CPU", &sched.profile_cpu,
		    "CPU to profile on"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('p', "pids", &sched.skip_merge,
		    "latency stats per pid instead of per comm"),
	OPT_END()
	};
	const struct option replay_options[] = {
	OPT_UINTEGER('r', "repeat", &sched.replay_repeat,
		     "repeat the workload replay N times (-1: infinite)"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('f', "force", &sched.force, "don't complain, do it"),
	OPT_END()
	};
	const struct option sched_options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_END()
	};
	const char * const latency_usage[] = {
		"perf sched latency [<options>]",
		NULL
	};
	const char * const replay_usage[] = {
		"perf sched replay [<options>]",
		NULL
	};
	const char *const sched_subcommands[] = { "record", "latency", "map",
						  "replay", "script", NULL };
	const char *sched_usage[] = {
		NULL,
		NULL
	};
	struct trace_sched_handler lat_ops  = {
		.wakeup_event	    = latency_wakeup_event,
		.switch_event	    = latency_switch_event,
		.runtime_event	    = latency_runtime_event,
		.migrate_task_event = latency_migrate_task_event,
	};
	struct trace_sched_handler map_ops  = {
		.switch_event	    = map_switch_event,
	};
	struct trace_sched_handler replay_ops  = {
		.wakeup_event	    = replay_wakeup_event,
		.switch_event	    = replay_switch_event,
		.fork_event	    = replay_fork_event,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sched.curr_pid); i++)
		sched.curr_pid[i] = -1;

	argc = parse_options_subcommand(argc, argv, sched_options, sched_subcommands,
					sched_usage, PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(sched_usage, sched_options);

	/*
	 * Aliased to 'perf script' for now:
	 */
	if (!strcmp(argv[0], "script"))
		return cmd_script(argc, argv, prefix);

	if (!strncmp(argv[0], "rec", 3)) {
		return __cmd_record(argc, argv);
	} else if (!strncmp(argv[0], "lat", 3)) {
		sched.tp_handler = &lat_ops;
		if (argc > 1) {
			argc = parse_options(argc, argv, latency_options, latency_usage, 0);
			if (argc)
				usage_with_options(latency_usage, latency_options);
		}
		setup_sorting(&sched, latency_options, latency_usage);
		return perf_sched__lat(&sched);
	} else if (!strcmp(argv[0], "map")) {
		sched.tp_handler = &map_ops;
		setup_sorting(&sched, latency_options, latency_usage);
		return perf_sched__map(&sched);
	} else if (!strncmp(argv[0], "rep", 3)) {
		sched.tp_handler = &replay_ops;
		if (argc) {
			argc = parse_options(argc, argv, replay_options, replay_usage, 0);
			if (argc)
				usage_with_options(replay_usage, replay_options);
		}
		return perf_sched__replay(&sched);
	} else {
		usage_with_options(sched_usage, sched_options);
	}

	return 0;
}
