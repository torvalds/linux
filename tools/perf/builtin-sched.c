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
#include "util/thread_map.h"
#include "util/color.h"
#include "util/stat.h"
#include "util/callchain.h"
#include "util/time-utils.h"

#include <subcmd/parse-options.h>
#include "util/trace-event.h"

#include "util/debug.h"

#include <linux/log2.h>
#include <sys/prctl.h>
#include <sys/resource.h>

#include <semaphore.h>
#include <pthread.h>
#include <math.h>
#include <api/fs/fs.h>
#include <linux/time64.h>

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

#define COLOR_PIDS PERF_COLOR_BLUE
#define COLOR_CPUS PERF_COLOR_BG_RED

struct perf_sched_map {
	DECLARE_BITMAP(comp_cpus_mask, MAX_CPUS);
	int			*comp_cpus;
	bool			 comp;
	struct thread_map	*color_pids;
	const char		*color_pids_str;
	struct cpu_map		*color_cpus;
	const char		*color_cpus_str;
	struct cpu_map		*cpus;
	const char		*cpus_str;
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
	struct perf_sched_map map;

	/* options for timehist command */
	bool		summary;
	bool		summary_only;
	bool		idle_hist;
	bool		show_callchain;
	unsigned int	max_stack;
	bool		show_cpu_visual;
	bool		show_wakeups;
	bool		show_migrations;
	u64		skipped_samples;
	const char	*time_str;
	struct perf_time_interval ptime;
	struct perf_time_interval hist_time;
};

/* per thread run time data */
struct thread_runtime {
	u64 last_time;      /* time of previous sched in/out event */
	u64 dt_run;         /* run time */
	u64 dt_wait;        /* time between CPU access (off cpu) */
	u64 dt_delay;       /* time between wakeup and sched-in */
	u64 ready_to_run;   /* time of wakeup */

	struct stats run_stats;
	u64 total_run_time;

	u64 migrations;
};

/* per event run time data */
struct evsel_runtime {
	u64 *last_time; /* time this event was last seen per cpu */
	u32 ncpu;       /* highest cpu slot allocated */
};

/* per cpu idle time data */
struct idle_thread_runtime {
	struct thread_runtime	tr;
	struct thread		*last_thread;
	struct rb_root		sorted_root;
	struct callchain_root	callchain;
	struct callchain_cursor	cursor;
};

/* track idle times per cpu */
static struct thread **idle_threads;
static int idle_max_cpu;
static char idle_comm[] = "<idle>";

static u64 get_nsecs(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
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
	u64 T0, T1, delta, min_delta = NSEC_PER_SEC;
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
	u64 T0, T1, delta, min_delta = NSEC_PER_SEC;
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

	sum =  ru.ru_utime.tv_sec * NSEC_PER_SEC + ru.ru_utime.tv_usec * NSEC_PER_USEC;
	sum += ru.ru_stime.tv_sec * NSEC_PER_SEC + ru.ru_stime.tv_usec * NSEC_PER_USEC;

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
		       str_error_r(errno, sbuf, sizeof(sbuf)), info);
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

	printf("#%-3ld: %0.3f, ", sched->nr_runs, (double)delta / NSEC_PER_MSEC);

	printf("ravg: %0.2f, ", (double)sched->run_avg / NSEC_PER_MSEC);

	printf("cpu: %0.2f / %0.2f",
		(double)sched->cpu_usage / NSEC_PER_MSEC, (double)sched->runavg_cpu_usage / NSEC_PER_MSEC);

#if 0
	/*
	 * rusage statistics done by the parent, these are less
	 * accurate than the sched->sum_exec_runtime based statistics:
	 */
	printf(" [%0.2f / %0.2f]",
		(double)sched->parent_cpu_usage / NSEC_PER_MSEC,
		(double)sched->runavg_parent_cpu_usage / NSEC_PER_MSEC);
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
	burn_nsecs(sched, NSEC_PER_MSEC);
	T1 = get_nsecs();

	printf("the run test took %" PRIu64 " nsecs\n", T1 - T0);

	T0 = get_nsecs();
	sleep_nsecs(NSEC_PER_MSEC);
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
	char max_lat_at[32];

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
	timestamp__scnprintf_usec(work_list->max_lat_at, max_lat_at, sizeof(max_lat_at));

	printf("|%11.3f ms |%9" PRIu64 " | avg:%9.3f ms | max:%9.3f ms | max at: %13s s\n",
	      (double)work_list->total_runtime / NSEC_PER_MSEC,
		 work_list->nb_atoms, (double)avg / NSEC_PER_MSEC,
		 (double)work_list->max_lat / NSEC_PER_MSEC,
		 max_lat_at);
}

static int pid_cmp(struct work_atoms *l, struct work_atoms *r)
{
	if (l->thread == r->thread)
		return 0;
	if (l->thread->tid < r->thread->tid)
		return -1;
	if (l->thread->tid > r->thread->tid)
		return 1;
	return (int)(l->thread - r->thread);
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

union map_priv {
	void	*ptr;
	bool	 color;
};

static bool thread__has_color(struct thread *thread)
{
	union map_priv priv = {
		.ptr = thread__priv(thread),
	};

	return priv.color;
}

static struct thread*
map__findnew_thread(struct perf_sched *sched, struct machine *machine, pid_t pid, pid_t tid)
{
	struct thread *thread = machine__findnew_thread(machine, pid, tid);
	union map_priv priv = {
		.color = false,
	};

	if (!sched->map.color_pids || !thread || thread__priv(thread))
		return thread;

	if (thread_map__has(sched->map.color_pids, tid))
		priv.color = true;

	thread__set_priv(thread, priv.ptr);
	return thread;
}

static int map_switch_event(struct perf_sched *sched, struct perf_evsel *evsel,
			    struct perf_sample *sample, struct machine *machine)
{
	const u32 next_pid = perf_evsel__intval(evsel, sample, "next_pid");
	struct thread *sched_in;
	int new_shortname;
	u64 timestamp0, timestamp = sample->time;
	s64 delta;
	int i, this_cpu = sample->cpu;
	int cpus_nr;
	bool new_cpu = false;
	const char *color = PERF_COLOR_NORMAL;
	char stimestamp[32];

	BUG_ON(this_cpu >= MAX_CPUS || this_cpu < 0);

	if (this_cpu > sched->max_cpu)
		sched->max_cpu = this_cpu;

	if (sched->map.comp) {
		cpus_nr = bitmap_weight(sched->map.comp_cpus_mask, MAX_CPUS);
		if (!test_and_set_bit(this_cpu, sched->map.comp_cpus_mask)) {
			sched->map.comp_cpus[cpus_nr++] = this_cpu;
			new_cpu = true;
		}
	} else
		cpus_nr = sched->max_cpu;

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

	sched_in = map__findnew_thread(sched, machine, -1, next_pid);
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

	for (i = 0; i < cpus_nr; i++) {
		int cpu = sched->map.comp ? sched->map.comp_cpus[i] : i;
		struct thread *curr_thread = sched->curr_thread[cpu];
		const char *pid_color = color;
		const char *cpu_color = color;

		if (curr_thread && thread__has_color(curr_thread))
			pid_color = COLOR_PIDS;

		if (sched->map.cpus && !cpu_map__has(sched->map.cpus, cpu))
			continue;

		if (sched->map.color_cpus && cpu_map__has(sched->map.color_cpus, cpu))
			cpu_color = COLOR_CPUS;

		if (cpu != this_cpu)
			color_fprintf(stdout, color, " ");
		else
			color_fprintf(stdout, cpu_color, "*");

		if (sched->curr_thread[cpu])
			color_fprintf(stdout, pid_color, "%2s ", sched->curr_thread[cpu]->shortname);
		else
			color_fprintf(stdout, color, "   ");
	}

	if (sched->map.cpus && !cpu_map__has(sched->map.cpus, this_cpu))
		goto out;

	timestamp__scnprintf_usec(timestamp, stimestamp, sizeof(stimestamp));
	color_fprintf(stdout, color, "  %12s secs ", stimestamp);
	if (new_shortname || (verbose && sched_in->tid)) {
		const char *pid_color = color;

		if (thread__has_color(sched_in))
			pid_color = COLOR_PIDS;

		color_fprintf(stdout, pid_color, "%s => %s:%d",
		       sched_in->shortname, thread__comm_str(sched_in), sched_in->tid);
	}

	if (sched->map.comp && new_cpu)
		color_fprintf(stdout, color, " (CPU %d)", this_cpu);

out:
	color_fprintf(stdout, color, "\n");

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

/*
 * scheduling times are printed as msec.usec
 */
static inline void print_sched_time(unsigned long long nsecs, int width)
{
	unsigned long msecs;
	unsigned long usecs;

	msecs  = nsecs / NSEC_PER_MSEC;
	nsecs -= msecs * NSEC_PER_MSEC;
	usecs  = nsecs / NSEC_PER_USEC;
	printf("%*lu.%03lu ", width, msecs, usecs);
}

/*
 * returns runtime data for event, allocating memory for it the
 * first time it is used.
 */
static struct evsel_runtime *perf_evsel__get_runtime(struct perf_evsel *evsel)
{
	struct evsel_runtime *r = evsel->priv;

	if (r == NULL) {
		r = zalloc(sizeof(struct evsel_runtime));
		evsel->priv = r;
	}

	return r;
}

/*
 * save last time event was seen per cpu
 */
static void perf_evsel__save_time(struct perf_evsel *evsel,
				  u64 timestamp, u32 cpu)
{
	struct evsel_runtime *r = perf_evsel__get_runtime(evsel);

	if (r == NULL)
		return;

	if ((cpu >= r->ncpu) || (r->last_time == NULL)) {
		int i, n = __roundup_pow_of_two(cpu+1);
		void *p = r->last_time;

		p = realloc(r->last_time, n * sizeof(u64));
		if (!p)
			return;

		r->last_time = p;
		for (i = r->ncpu; i < n; ++i)
			r->last_time[i] = (u64) 0;

		r->ncpu = n;
	}

	r->last_time[cpu] = timestamp;
}

/* returns last time this event was seen on the given cpu */
static u64 perf_evsel__get_time(struct perf_evsel *evsel, u32 cpu)
{
	struct evsel_runtime *r = perf_evsel__get_runtime(evsel);

	if ((r == NULL) || (r->last_time == NULL) || (cpu >= r->ncpu))
		return 0;

	return r->last_time[cpu];
}

static int comm_width = 30;

static char *timehist_get_commstr(struct thread *thread)
{
	static char str[32];
	const char *comm = thread__comm_str(thread);
	pid_t tid = thread->tid;
	pid_t pid = thread->pid_;
	int n;

	if (pid == 0)
		n = scnprintf(str, sizeof(str), "%s", comm);

	else if (tid != pid)
		n = scnprintf(str, sizeof(str), "%s[%d/%d]", comm, tid, pid);

	else
		n = scnprintf(str, sizeof(str), "%s[%d]", comm, tid);

	if (n > comm_width)
		comm_width = n;

	return str;
}

static void timehist_header(struct perf_sched *sched)
{
	u32 ncpus = sched->max_cpu + 1;
	u32 i, j;

	printf("%15s %6s ", "time", "cpu");

	if (sched->show_cpu_visual) {
		printf(" ");
		for (i = 0, j = 0; i < ncpus; ++i) {
			printf("%x", j++);
			if (j > 15)
				j = 0;
		}
		printf(" ");
	}

	printf(" %-*s  %9s  %9s  %9s", comm_width,
		"task name", "wait time", "sch delay", "run time");

	printf("\n");

	/*
	 * units row
	 */
	printf("%15s %-6s ", "", "");

	if (sched->show_cpu_visual)
		printf(" %*s ", ncpus, "");

	printf(" %-*s  %9s  %9s  %9s\n", comm_width,
	       "[tid/pid]", "(msec)", "(msec)", "(msec)");

	/*
	 * separator
	 */
	printf("%.15s %.6s ", graph_dotted_line, graph_dotted_line);

	if (sched->show_cpu_visual)
		printf(" %.*s ", ncpus, graph_dotted_line);

	printf(" %.*s  %.9s  %.9s  %.9s", comm_width,
		graph_dotted_line, graph_dotted_line, graph_dotted_line,
		graph_dotted_line);

	printf("\n");
}

static void timehist_print_sample(struct perf_sched *sched,
				  struct perf_sample *sample,
				  struct addr_location *al,
				  struct thread *thread,
				  u64 t)
{
	struct thread_runtime *tr = thread__priv(thread);
	u32 max_cpus = sched->max_cpu + 1;
	char tstr[64];

	timestamp__scnprintf_usec(t, tstr, sizeof(tstr));
	printf("%15s [%04d] ", tstr, sample->cpu);

	if (sched->show_cpu_visual) {
		u32 i;
		char c;

		printf(" ");
		for (i = 0; i < max_cpus; ++i) {
			/* flag idle times with 'i'; others are sched events */
			if (i == sample->cpu)
				c = (thread->tid == 0) ? 'i' : 's';
			else
				c = ' ';
			printf("%c", c);
		}
		printf(" ");
	}

	printf(" %-*s ", comm_width, timehist_get_commstr(thread));

	print_sched_time(tr->dt_wait, 6);
	print_sched_time(tr->dt_delay, 6);
	print_sched_time(tr->dt_run, 6);

	if (sched->show_wakeups)
		printf("  %-*s", comm_width, "");

	if (thread->tid == 0)
		goto out;

	if (sched->show_callchain)
		printf("  ");

	sample__fprintf_sym(sample, al, 0,
			    EVSEL__PRINT_SYM | EVSEL__PRINT_ONELINE |
			    EVSEL__PRINT_CALLCHAIN_ARROW |
			    EVSEL__PRINT_SKIP_IGNORED,
			    &callchain_cursor, stdout);

out:
	printf("\n");
}

/*
 * Explanation of delta-time stats:
 *
 *            t = time of current schedule out event
 *        tprev = time of previous sched out event
 *                also time of schedule-in event for current task
 *    last_time = time of last sched change event for current task
 *                (i.e, time process was last scheduled out)
 * ready_to_run = time of wakeup for current task
 *
 * -----|------------|------------|------------|------
 *    last         ready        tprev          t
 *    time         to run
 *
 *      |-------- dt_wait --------|
 *                   |- dt_delay -|-- dt_run --|
 *
 *   dt_run = run time of current task
 *  dt_wait = time between last schedule out event for task and tprev
 *            represents time spent off the cpu
 * dt_delay = time between wakeup and schedule-in of task
 */

static void timehist_update_runtime_stats(struct thread_runtime *r,
					 u64 t, u64 tprev)
{
	r->dt_delay   = 0;
	r->dt_wait    = 0;
	r->dt_run     = 0;
	if (tprev) {
		r->dt_run = t - tprev;
		if (r->ready_to_run) {
			if (r->ready_to_run > tprev)
				pr_debug("time travel: wakeup time for task > previous sched_switch event\n");
			else
				r->dt_delay = tprev - r->ready_to_run;
		}

		if (r->last_time > tprev)
			pr_debug("time travel: last sched out time for task > previous sched_switch event\n");
		else if (r->last_time)
			r->dt_wait = tprev - r->last_time;
	}

	update_stats(&r->run_stats, r->dt_run);
	r->total_run_time += r->dt_run;
}

static bool is_idle_sample(struct perf_sample *sample,
			   struct perf_evsel *evsel)
{
	/* pid 0 == swapper == idle task */
	if (strcmp(perf_evsel__name(evsel), "sched:sched_switch") == 0)
		return perf_evsel__intval(evsel, sample, "prev_pid") == 0;

	return sample->pid == 0;
}

static void save_task_callchain(struct perf_sched *sched,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct callchain_cursor *cursor = &callchain_cursor;
	struct thread *thread;

	/* want main thread for process - has maps */
	thread = machine__findnew_thread(machine, sample->pid, sample->pid);
	if (thread == NULL) {
		pr_debug("Failed to get thread for pid %d.\n", sample->pid);
		return;
	}

	if (!symbol_conf.use_callchain || sample->callchain == NULL)
		return;

	if (thread__resolve_callchain(thread, cursor, evsel, sample,
				      NULL, NULL, sched->max_stack + 2) != 0) {
		if (verbose)
			error("Failed to resolve callchain. Skipping\n");

		return;
	}

	callchain_cursor_commit(cursor);

	while (true) {
		struct callchain_cursor_node *node;
		struct symbol *sym;

		node = callchain_cursor_current(cursor);
		if (node == NULL)
			break;

		sym = node->sym;
		if (sym && sym->name) {
			if (!strcmp(sym->name, "schedule") ||
			    !strcmp(sym->name, "__schedule") ||
			    !strcmp(sym->name, "preempt_schedule"))
				sym->ignore = 1;
		}

		callchain_cursor_advance(cursor);
	}
}

static int init_idle_thread(struct thread *thread)
{
	struct idle_thread_runtime *itr;

	thread__set_comm(thread, idle_comm, 0);

	itr = zalloc(sizeof(*itr));
	if (itr == NULL)
		return -ENOMEM;

	init_stats(&itr->tr.run_stats);
	callchain_init(&itr->callchain);
	callchain_cursor_reset(&itr->cursor);
	thread__set_priv(thread, itr);

	return 0;
}

/*
 * Track idle stats per cpu by maintaining a local thread
 * struct for the idle task on each cpu.
 */
static int init_idle_threads(int ncpu)
{
	int i, ret;

	idle_threads = zalloc(ncpu * sizeof(struct thread *));
	if (!idle_threads)
		return -ENOMEM;

	idle_max_cpu = ncpu;

	/* allocate the actual thread struct if needed */
	for (i = 0; i < ncpu; ++i) {
		idle_threads[i] = thread__new(0, 0);
		if (idle_threads[i] == NULL)
			return -ENOMEM;

		ret = init_idle_thread(idle_threads[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void free_idle_threads(void)
{
	int i;

	if (idle_threads == NULL)
		return;

	for (i = 0; i < idle_max_cpu; ++i) {
		if ((idle_threads[i]))
			thread__delete(idle_threads[i]);
	}

	free(idle_threads);
}

static struct thread *get_idle_thread(int cpu)
{
	/*
	 * expand/allocate array of pointers to local thread
	 * structs if needed
	 */
	if ((cpu >= idle_max_cpu) || (idle_threads == NULL)) {
		int i, j = __roundup_pow_of_two(cpu+1);
		void *p;

		p = realloc(idle_threads, j * sizeof(struct thread *));
		if (!p)
			return NULL;

		idle_threads = (struct thread **) p;
		for (i = idle_max_cpu; i < j; ++i)
			idle_threads[i] = NULL;

		idle_max_cpu = j;
	}

	/* allocate a new thread struct if needed */
	if (idle_threads[cpu] == NULL) {
		idle_threads[cpu] = thread__new(0, 0);

		if (idle_threads[cpu]) {
			if (init_idle_thread(idle_threads[cpu]) < 0)
				return NULL;
		}
	}

	return idle_threads[cpu];
}

static void save_idle_callchain(struct idle_thread_runtime *itr,
				struct perf_sample *sample)
{
	if (!symbol_conf.use_callchain || sample->callchain == NULL)
		return;

	callchain_cursor__copy(&itr->cursor, &callchain_cursor);
}

/*
 * handle runtime stats saved per thread
 */
static struct thread_runtime *thread__init_runtime(struct thread *thread)
{
	struct thread_runtime *r;

	r = zalloc(sizeof(struct thread_runtime));
	if (!r)
		return NULL;

	init_stats(&r->run_stats);
	thread__set_priv(thread, r);

	return r;
}

static struct thread_runtime *thread__get_runtime(struct thread *thread)
{
	struct thread_runtime *tr;

	tr = thread__priv(thread);
	if (tr == NULL) {
		tr = thread__init_runtime(thread);
		if (tr == NULL)
			pr_debug("Failed to malloc memory for runtime data.\n");
	}

	return tr;
}

static struct thread *timehist_get_thread(struct perf_sched *sched,
					  struct perf_sample *sample,
					  struct machine *machine,
					  struct perf_evsel *evsel)
{
	struct thread *thread;

	if (is_idle_sample(sample, evsel)) {
		thread = get_idle_thread(sample->cpu);
		if (thread == NULL)
			pr_err("Failed to get idle thread for cpu %d.\n", sample->cpu);

	} else {
		/* there were samples with tid 0 but non-zero pid */
		thread = machine__findnew_thread(machine, sample->pid,
						 sample->tid ?: sample->pid);
		if (thread == NULL) {
			pr_debug("Failed to get thread for tid %d. skipping sample.\n",
				 sample->tid);
		}

		save_task_callchain(sched, sample, evsel, machine);
		if (sched->idle_hist) {
			struct thread *idle;
			struct idle_thread_runtime *itr;

			idle = get_idle_thread(sample->cpu);
			if (idle == NULL) {
				pr_err("Failed to get idle thread for cpu %d.\n", sample->cpu);
				return NULL;
			}

			itr = thread__priv(idle);
			if (itr == NULL)
				return NULL;

			itr->last_thread = thread;

			/* copy task callchain when entering to idle */
			if (perf_evsel__intval(evsel, sample, "next_pid") == 0)
				save_idle_callchain(itr, sample);
		}
	}

	return thread;
}

static bool timehist_skip_sample(struct perf_sched *sched,
				 struct thread *thread,
				 struct perf_evsel *evsel,
				 struct perf_sample *sample)
{
	bool rc = false;

	if (thread__is_filtered(thread)) {
		rc = true;
		sched->skipped_samples++;
	}

	if (sched->idle_hist) {
		if (strcmp(perf_evsel__name(evsel), "sched:sched_switch"))
			rc = true;
		else if (perf_evsel__intval(evsel, sample, "prev_pid") != 0 &&
			 perf_evsel__intval(evsel, sample, "next_pid") != 0)
			rc = true;
	}

	return rc;
}

static void timehist_print_wakeup_event(struct perf_sched *sched,
					struct perf_evsel *evsel,
					struct perf_sample *sample,
					struct machine *machine,
					struct thread *awakened)
{
	struct thread *thread;
	char tstr[64];

	thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL)
		return;

	/* show wakeup unless both awakee and awaker are filtered */
	if (timehist_skip_sample(sched, thread, evsel, sample) &&
	    timehist_skip_sample(sched, awakened, evsel, sample)) {
		return;
	}

	timestamp__scnprintf_usec(sample->time, tstr, sizeof(tstr));
	printf("%15s [%04d] ", tstr, sample->cpu);
	if (sched->show_cpu_visual)
		printf(" %*s ", sched->max_cpu + 1, "");

	printf(" %-*s ", comm_width, timehist_get_commstr(thread));

	/* dt spacer */
	printf("  %9s  %9s  %9s ", "", "", "");

	printf("awakened: %s", timehist_get_commstr(awakened));

	printf("\n");
}

static int timehist_sched_wakeup_event(struct perf_tool *tool,
				       union perf_event *event __maybe_unused,
				       struct perf_evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);
	struct thread *thread;
	struct thread_runtime *tr = NULL;
	/* want pid of awakened task not pid in sample */
	const u32 pid = perf_evsel__intval(evsel, sample, "pid");

	thread = machine__findnew_thread(machine, 0, pid);
	if (thread == NULL)
		return -1;

	tr = thread__get_runtime(thread);
	if (tr == NULL)
		return -1;

	if (tr->ready_to_run == 0)
		tr->ready_to_run = sample->time;

	/* show wakeups if requested */
	if (sched->show_wakeups &&
	    !perf_time__skip_sample(&sched->ptime, sample->time))
		timehist_print_wakeup_event(sched, evsel, sample, machine, thread);

	return 0;
}

static void timehist_print_migration_event(struct perf_sched *sched,
					struct perf_evsel *evsel,
					struct perf_sample *sample,
					struct machine *machine,
					struct thread *migrated)
{
	struct thread *thread;
	char tstr[64];
	u32 max_cpus = sched->max_cpu + 1;
	u32 ocpu, dcpu;

	if (sched->summary_only)
		return;

	max_cpus = sched->max_cpu + 1;
	ocpu = perf_evsel__intval(evsel, sample, "orig_cpu");
	dcpu = perf_evsel__intval(evsel, sample, "dest_cpu");

	thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL)
		return;

	if (timehist_skip_sample(sched, thread, evsel, sample) &&
	    timehist_skip_sample(sched, migrated, evsel, sample)) {
		return;
	}

	timestamp__scnprintf_usec(sample->time, tstr, sizeof(tstr));
	printf("%15s [%04d] ", tstr, sample->cpu);

	if (sched->show_cpu_visual) {
		u32 i;
		char c;

		printf("  ");
		for (i = 0; i < max_cpus; ++i) {
			c = (i == sample->cpu) ? 'm' : ' ';
			printf("%c", c);
		}
		printf("  ");
	}

	printf(" %-*s ", comm_width, timehist_get_commstr(thread));

	/* dt spacer */
	printf("  %9s  %9s  %9s ", "", "", "");

	printf("migrated: %s", timehist_get_commstr(migrated));
	printf(" cpu %d => %d", ocpu, dcpu);

	printf("\n");
}

static int timehist_migrate_task_event(struct perf_tool *tool,
				       union perf_event *event __maybe_unused,
				       struct perf_evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);
	struct thread *thread;
	struct thread_runtime *tr = NULL;
	/* want pid of migrated task not pid in sample */
	const u32 pid = perf_evsel__intval(evsel, sample, "pid");

	thread = machine__findnew_thread(machine, 0, pid);
	if (thread == NULL)
		return -1;

	tr = thread__get_runtime(thread);
	if (tr == NULL)
		return -1;

	tr->migrations++;

	/* show migrations if requested */
	timehist_print_migration_event(sched, evsel, sample, machine, thread);

	return 0;
}

static int timehist_sched_change_event(struct perf_tool *tool,
				       union perf_event *event,
				       struct perf_evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);
	struct perf_time_interval *ptime = &sched->ptime;
	struct addr_location al;
	struct thread *thread;
	struct thread_runtime *tr = NULL;
	u64 tprev, t = sample->time;
	int rc = 0;

	if (machine__resolve(machine, &al, sample) < 0) {
		pr_err("problem processing %d event. skipping it\n",
		       event->header.type);
		rc = -1;
		goto out;
	}

	thread = timehist_get_thread(sched, sample, machine, evsel);
	if (thread == NULL) {
		rc = -1;
		goto out;
	}

	if (timehist_skip_sample(sched, thread, evsel, sample))
		goto out;

	tr = thread__get_runtime(thread);
	if (tr == NULL) {
		rc = -1;
		goto out;
	}

	tprev = perf_evsel__get_time(evsel, sample->cpu);

	/*
	 * If start time given:
	 * - sample time is under window user cares about - skip sample
	 * - tprev is under window user cares about  - reset to start of window
	 */
	if (ptime->start && ptime->start > t)
		goto out;

	if (tprev && ptime->start > tprev)
		tprev = ptime->start;

	/*
	 * If end time given:
	 * - previous sched event is out of window - we are done
	 * - sample time is beyond window user cares about - reset it
	 *   to close out stats for time window interest
	 */
	if (ptime->end) {
		if (tprev > ptime->end)
			goto out;

		if (t > ptime->end)
			t = ptime->end;
	}

	if (!sched->idle_hist || thread->tid == 0) {
		timehist_update_runtime_stats(tr, t, tprev);

		if (sched->idle_hist) {
			struct idle_thread_runtime *itr = (void *)tr;
			struct thread_runtime *last_tr;

			BUG_ON(thread->tid != 0);

			if (itr->last_thread == NULL)
				goto out;

			/* add current idle time as last thread's runtime */
			last_tr = thread__get_runtime(itr->last_thread);
			if (last_tr == NULL)
				goto out;

			timehist_update_runtime_stats(last_tr, t, tprev);
			/*
			 * remove delta time of last thread as it's not updated
			 * and otherwise it will show an invalid value next
			 * time.  we only care total run time and run stat.
			 */
			last_tr->dt_run = 0;
			last_tr->dt_wait = 0;
			last_tr->dt_delay = 0;

			if (itr->cursor.nr)
				callchain_append(&itr->callchain, &itr->cursor, t - tprev);

			itr->last_thread = NULL;
		}
	}

	if (!sched->summary_only)
		timehist_print_sample(sched, sample, &al, thread, t);

out:
	if (sched->hist_time.start == 0 && t >= ptime->start)
		sched->hist_time.start = t;
	if (ptime->end == 0 || t <= ptime->end)
		sched->hist_time.end = t;

	if (tr) {
		/* time of this sched_switch event becomes last time task seen */
		tr->last_time = sample->time;

		/* sched out event for task so reset ready to run time */
		tr->ready_to_run = 0;
	}

	perf_evsel__save_time(evsel, sample->time, sample->cpu);

	return rc;
}

static int timehist_sched_switch_event(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_evsel *evsel,
			     struct perf_sample *sample,
			     struct machine *machine __maybe_unused)
{
	return timehist_sched_change_event(tool, event, evsel, sample, machine);
}

static int process_lost(struct perf_tool *tool __maybe_unused,
			union perf_event *event,
			struct perf_sample *sample,
			struct machine *machine __maybe_unused)
{
	char tstr[64];

	timestamp__scnprintf_usec(sample->time, tstr, sizeof(tstr));
	printf("%15s ", tstr);
	printf("lost %" PRIu64 " events on cpu %d\n", event->lost.lost, sample->cpu);

	return 0;
}


static void print_thread_runtime(struct thread *t,
				 struct thread_runtime *r)
{
	double mean = avg_stats(&r->run_stats);
	float stddev;

	printf("%*s   %5d  %9" PRIu64 " ",
	       comm_width, timehist_get_commstr(t), t->ppid,
	       (u64) r->run_stats.n);

	print_sched_time(r->total_run_time, 8);
	stddev = rel_stddev_stats(stddev_stats(&r->run_stats), mean);
	print_sched_time(r->run_stats.min, 6);
	printf(" ");
	print_sched_time((u64) mean, 6);
	printf(" ");
	print_sched_time(r->run_stats.max, 6);
	printf("  ");
	printf("%5.2f", stddev);
	printf("   %5" PRIu64, r->migrations);
	printf("\n");
}

struct total_run_stats {
	u64  sched_count;
	u64  task_count;
	u64  total_run_time;
};

static int __show_thread_runtime(struct thread *t, void *priv)
{
	struct total_run_stats *stats = priv;
	struct thread_runtime *r;

	if (thread__is_filtered(t))
		return 0;

	r = thread__priv(t);
	if (r && r->run_stats.n) {
		stats->task_count++;
		stats->sched_count += r->run_stats.n;
		stats->total_run_time += r->total_run_time;
		print_thread_runtime(t, r);
	}

	return 0;
}

static int show_thread_runtime(struct thread *t, void *priv)
{
	if (t->dead)
		return 0;

	return __show_thread_runtime(t, priv);
}

static int show_deadthread_runtime(struct thread *t, void *priv)
{
	if (!t->dead)
		return 0;

	return __show_thread_runtime(t, priv);
}

static size_t callchain__fprintf_folded(FILE *fp, struct callchain_node *node)
{
	const char *sep = " <- ";
	struct callchain_list *chain;
	size_t ret = 0;
	char bf[1024];
	bool first;

	if (node == NULL)
		return 0;

	ret = callchain__fprintf_folded(fp, node->parent);
	first = (ret == 0);

	list_for_each_entry(chain, &node->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		if (chain->ms.sym && chain->ms.sym->ignore)
			continue;
		ret += fprintf(fp, "%s%s", first ? "" : sep,
			       callchain_list__sym_name(chain, bf, sizeof(bf),
							false));
		first = false;
	}

	return ret;
}

static size_t timehist_print_idlehist_callchain(struct rb_root *root)
{
	size_t ret = 0;
	FILE *fp = stdout;
	struct callchain_node *chain;
	struct rb_node *rb_node = rb_first(root);

	printf("  %16s  %8s  %s\n", "Idle time (msec)", "Count", "Callchains");
	printf("  %.16s  %.8s  %.50s\n", graph_dotted_line, graph_dotted_line,
	       graph_dotted_line);

	while (rb_node) {
		chain = rb_entry(rb_node, struct callchain_node, rb_node);
		rb_node = rb_next(rb_node);

		ret += fprintf(fp, "  ");
		print_sched_time(chain->hit, 12);
		ret += 16;  /* print_sched_time returns 2nd arg + 4 */
		ret += fprintf(fp, " %8d  ", chain->count);
		ret += callchain__fprintf_folded(fp, chain);
		ret += fprintf(fp, "\n");
	}

	return ret;
}

static void timehist_print_summary(struct perf_sched *sched,
				   struct perf_session *session)
{
	struct machine *m = &session->machines.host;
	struct total_run_stats totals;
	u64 task_count;
	struct thread *t;
	struct thread_runtime *r;
	int i;
	u64 hist_time = sched->hist_time.end - sched->hist_time.start;

	memset(&totals, 0, sizeof(totals));

	if (sched->idle_hist) {
		printf("\nIdle-time summary\n");
		printf("%*s  parent  sched-out  ", comm_width, "comm");
		printf("  idle-time   min-idle    avg-idle    max-idle  stddev  migrations\n");
	} else {
		printf("\nRuntime summary\n");
		printf("%*s  parent   sched-in  ", comm_width, "comm");
		printf("   run-time    min-run     avg-run     max-run  stddev  migrations\n");
	}
	printf("%*s            (count)  ", comm_width, "");
	printf("     (msec)     (msec)      (msec)      (msec)       %%\n");
	printf("%.117s\n", graph_dotted_line);

	machine__for_each_thread(m, show_thread_runtime, &totals);
	task_count = totals.task_count;
	if (!task_count)
		printf("<no still running tasks>\n");

	printf("\nTerminated tasks:\n");
	machine__for_each_thread(m, show_deadthread_runtime, &totals);
	if (task_count == totals.task_count)
		printf("<no terminated tasks>\n");

	/* CPU idle stats not tracked when samples were skipped */
	if (sched->skipped_samples && !sched->idle_hist)
		return;

	printf("\nIdle stats:\n");
	for (i = 0; i < idle_max_cpu; ++i) {
		t = idle_threads[i];
		if (!t)
			continue;

		r = thread__priv(t);
		if (r && r->run_stats.n) {
			totals.sched_count += r->run_stats.n;
			printf("    CPU %2d idle for ", i);
			print_sched_time(r->total_run_time, 6);
			printf(" msec  (%6.2f%%)\n", 100.0 * r->total_run_time / hist_time);
		} else
			printf("    CPU %2d idle entire time window\n", i);
	}

	if (sched->idle_hist && symbol_conf.use_callchain) {
		callchain_param.mode  = CHAIN_FOLDED;
		callchain_param.value = CCVAL_PERIOD;

		callchain_register_param(&callchain_param);

		printf("\nIdle stats by callchain:\n");
		for (i = 0; i < idle_max_cpu; ++i) {
			struct idle_thread_runtime *itr;

			t = idle_threads[i];
			if (!t)
				continue;

			itr = thread__priv(t);
			if (itr == NULL)
				continue;

			callchain_param.sort(&itr->sorted_root, &itr->callchain,
					     0, &callchain_param);

			printf("  CPU %2d:", i);
			print_sched_time(itr->tr.total_run_time, 6);
			printf(" msec\n");
			timehist_print_idlehist_callchain(&itr->sorted_root);
			printf("\n");
		}
	}

	printf("\n"
	       "    Total number of unique tasks: %" PRIu64 "\n"
	       "Total number of context switches: %" PRIu64 "\n",
	       totals.task_count, totals.sched_count);

	printf("           Total run time (msec): ");
	print_sched_time(totals.total_run_time, 2);
	printf("\n");

	printf("    Total scheduling time (msec): ");
	print_sched_time(hist_time, 2);
	printf(" (x %d)\n", sched->max_cpu);
}

typedef int (*sched_handler)(struct perf_tool *tool,
			  union perf_event *event,
			  struct perf_evsel *evsel,
			  struct perf_sample *sample,
			  struct machine *machine);

static int perf_timehist__process_sample(struct perf_tool *tool,
					 union perf_event *event,
					 struct perf_sample *sample,
					 struct perf_evsel *evsel,
					 struct machine *machine)
{
	struct perf_sched *sched = container_of(tool, struct perf_sched, tool);
	int err = 0;
	int this_cpu = sample->cpu;

	if (this_cpu > sched->max_cpu)
		sched->max_cpu = this_cpu;

	if (evsel->handler != NULL) {
		sched_handler f = evsel->handler;

		err = f(tool, event, evsel, sample, machine);
	}

	return err;
}

static int timehist_check_attr(struct perf_sched *sched,
			       struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	struct evsel_runtime *er;

	list_for_each_entry(evsel, &evlist->entries, node) {
		er = perf_evsel__get_runtime(evsel);
		if (er == NULL) {
			pr_err("Failed to allocate memory for evsel runtime data\n");
			return -1;
		}

		if (sched->show_callchain &&
		    !(evsel->attr.sample_type & PERF_SAMPLE_CALLCHAIN)) {
			pr_info("Samples do not have callchains.\n");
			sched->show_callchain = 0;
			symbol_conf.use_callchain = 0;
		}
	}

	return 0;
}

static int perf_sched__timehist(struct perf_sched *sched)
{
	const struct perf_evsel_str_handler handlers[] = {
		{ "sched:sched_switch",       timehist_sched_switch_event, },
		{ "sched:sched_wakeup",	      timehist_sched_wakeup_event, },
		{ "sched:sched_wakeup_new",   timehist_sched_wakeup_event, },
	};
	const struct perf_evsel_str_handler migrate_handlers[] = {
		{ "sched:sched_migrate_task", timehist_migrate_task_event, },
	};
	struct perf_data_file file = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
		.force = sched->force,
	};

	struct perf_session *session;
	struct perf_evlist *evlist;
	int err = -1;

	/*
	 * event handlers for timehist option
	 */
	sched->tool.sample	 = perf_timehist__process_sample;
	sched->tool.mmap	 = perf_event__process_mmap;
	sched->tool.comm	 = perf_event__process_comm;
	sched->tool.exit	 = perf_event__process_exit;
	sched->tool.fork	 = perf_event__process_fork;
	sched->tool.lost	 = process_lost;
	sched->tool.attr	 = perf_event__process_attr;
	sched->tool.tracing_data = perf_event__process_tracing_data;
	sched->tool.build_id	 = perf_event__process_build_id;

	sched->tool.ordered_events = true;
	sched->tool.ordering_requires_timestamps = true;

	symbol_conf.use_callchain = sched->show_callchain;

	session = perf_session__new(&file, false, &sched->tool);
	if (session == NULL)
		return -ENOMEM;

	evlist = session->evlist;

	symbol__init(&session->header.env);

	if (perf_time__parse_str(&sched->ptime, sched->time_str) != 0) {
		pr_err("Invalid time string\n");
		return -EINVAL;
	}

	if (timehist_check_attr(sched, evlist) != 0)
		goto out;

	setup_pager();

	/* setup per-evsel handlers */
	if (perf_session__set_tracepoints_handlers(session, handlers))
		goto out;

	/* sched_switch event at a minimum needs to exist */
	if (!perf_evlist__find_tracepoint_by_name(session->evlist,
						  "sched:sched_switch")) {
		pr_err("No sched_switch events found. Have you run 'perf sched record'?\n");
		goto out;
	}

	if (sched->show_migrations &&
	    perf_session__set_tracepoints_handlers(session, migrate_handlers))
		goto out;

	/* pre-allocate struct for per-CPU idle stats */
	sched->max_cpu = session->header.env.nr_cpus_online;
	if (sched->max_cpu == 0)
		sched->max_cpu = 4;
	if (init_idle_threads(sched->max_cpu))
		goto out;

	/* summary_only implies summary option, but don't overwrite summary if set */
	if (sched->summary_only)
		sched->summary = sched->summary_only;

	if (!sched->summary_only)
		timehist_header(sched);

	err = perf_session__process_events(session);
	if (err) {
		pr_err("Failed to process events, error %d", err);
		goto out;
	}

	sched->nr_events      = evlist->stats.nr_events[0];
	sched->nr_lost_events = evlist->stats.total_lost;
	sched->nr_lost_chunks = evlist->stats.nr_events[PERF_RECORD_LOST];

	if (sched->summary)
		timehist_print_summary(sched, session);

out:
	free_idle_threads();
	perf_session__delete(session);

	return err;
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
		(double)sched->all_runtime / NSEC_PER_MSEC, sched->all_count);

	printf(" ---------------------------------------------------\n");

	print_bad_events(sched);
	printf("\n");

	return 0;
}

static int setup_map_cpus(struct perf_sched *sched)
{
	struct cpu_map *map;

	sched->max_cpu  = sysconf(_SC_NPROCESSORS_CONF);

	if (sched->map.comp) {
		sched->map.comp_cpus = zalloc(sched->max_cpu * sizeof(int));
		if (!sched->map.comp_cpus)
			return -1;
	}

	if (!sched->map.cpus_str)
		return 0;

	map = cpu_map__new(sched->map.cpus_str);
	if (!map) {
		pr_err("failed to get cpus map from %s\n", sched->map.cpus_str);
		return -1;
	}

	sched->map.cpus = map;
	return 0;
}

static int setup_color_pids(struct perf_sched *sched)
{
	struct thread_map *map;

	if (!sched->map.color_pids_str)
		return 0;

	map = thread_map__new_by_tid_str(sched->map.color_pids_str);
	if (!map) {
		pr_err("failed to get thread map from %s\n", sched->map.color_pids_str);
		return -1;
	}

	sched->map.color_pids = map;
	return 0;
}

static int setup_color_cpus(struct perf_sched *sched)
{
	struct cpu_map *map;

	if (!sched->map.color_cpus_str)
		return 0;

	map = cpu_map__new(sched->map.color_cpus_str);
	if (!map) {
		pr_err("failed to get thread map from %s\n", sched->map.color_cpus_str);
		return -1;
	}

	sched->map.color_cpus = map;
	return 0;
}

static int perf_sched__map(struct perf_sched *sched)
{
	if (setup_map_cpus(sched))
		return -1;

	if (setup_color_pids(sched))
		return -1;

	if (setup_color_cpus(sched))
		return -1;

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
		.show_callchain	      = 1,
		.max_stack            = 5,
	};
	const struct option sched_options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('f', "force", &sched.force, "don't complain, do it"),
	OPT_END()
	};
	const struct option latency_options[] = {
	OPT_STRING('s', "sort", &sched.sort_order, "key[,key2...]",
		   "sort by key(s): runtime, switch, avg, max"),
	OPT_INTEGER('C', "CPU", &sched.profile_cpu,
		    "CPU to profile on"),
	OPT_BOOLEAN('p', "pids", &sched.skip_merge,
		    "latency stats per pid instead of per comm"),
	OPT_PARENT(sched_options)
	};
	const struct option replay_options[] = {
	OPT_UINTEGER('r', "repeat", &sched.replay_repeat,
		     "repeat the workload replay N times (-1: infinite)"),
	OPT_PARENT(sched_options)
	};
	const struct option map_options[] = {
	OPT_BOOLEAN(0, "compact", &sched.map.comp,
		    "map output in compact mode"),
	OPT_STRING(0, "color-pids", &sched.map.color_pids_str, "pids",
		   "highlight given pids in map"),
	OPT_STRING(0, "color-cpus", &sched.map.color_cpus_str, "cpus",
                    "highlight given CPUs in map"),
	OPT_STRING(0, "cpus", &sched.map.cpus_str, "cpus",
                    "display given CPUs in map"),
	OPT_PARENT(sched_options)
	};
	const struct option timehist_options[] = {
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('g', "call-graph", &sched.show_callchain,
		    "Display call chains if present (default on)"),
	OPT_UINTEGER(0, "max-stack", &sched.max_stack,
		   "Maximum number of functions to display backtrace."),
	OPT_STRING(0, "symfs", &symbol_conf.symfs, "directory",
		    "Look for files with symbols relative to this directory"),
	OPT_BOOLEAN('s', "summary", &sched.summary_only,
		    "Show only syscall summary with statistics"),
	OPT_BOOLEAN('S', "with-summary", &sched.summary,
		    "Show all syscalls and summary with statistics"),
	OPT_BOOLEAN('w', "wakeups", &sched.show_wakeups, "Show wakeup events"),
	OPT_BOOLEAN('M', "migrations", &sched.show_migrations, "Show migration events"),
	OPT_BOOLEAN('V', "cpu-visual", &sched.show_cpu_visual, "Add CPU visual"),
	OPT_BOOLEAN('I', "idle-hist", &sched.idle_hist, "Show idle events only"),
	OPT_STRING(0, "time", &sched.time_str, "str",
		   "Time span for analysis (start,stop)"),
	OPT_PARENT(sched_options)
	};

	const char * const latency_usage[] = {
		"perf sched latency [<options>]",
		NULL
	};
	const char * const replay_usage[] = {
		"perf sched replay [<options>]",
		NULL
	};
	const char * const map_usage[] = {
		"perf sched map [<options>]",
		NULL
	};
	const char * const timehist_usage[] = {
		"perf sched timehist [<options>]",
		NULL
	};
	const char *const sched_subcommands[] = { "record", "latency", "map",
						  "replay", "script",
						  "timehist", NULL };
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
		if (argc) {
			argc = parse_options(argc, argv, map_options, map_usage, 0);
			if (argc)
				usage_with_options(map_usage, map_options);
		}
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
	} else if (!strcmp(argv[0], "timehist")) {
		if (argc) {
			argc = parse_options(argc, argv, timehist_options,
					     timehist_usage, 0);
			if (argc)
				usage_with_options(timehist_usage, timehist_options);
		}
		if (sched.show_wakeups && sched.summary_only) {
			pr_err(" Error: -s and -w are mutually exclusive.\n");
			parse_options_usage(timehist_usage, timehist_options, "s", true);
			parse_options_usage(NULL, timehist_options, "w", true);
			return -EINVAL;
		}

		return perf_sched__timehist(&sched);
	} else {
		usage_with_options(sched_usage, sched_options);
	}

	return 0;
}
