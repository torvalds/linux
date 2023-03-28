// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017, 2018, 2019, 2021 BMW Car IT GmbH
 * Author: Viktor Rosendahl (viktor.rosendahl@bmw.de)
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sched.h>
#include <linux/unistd.h>
#include <signal.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <pthread.h>
#include <tracefs.h>

static const char *prg_name;
static const char *prg_unknown = "unknown program name";

static int fd_stdout;

static int sched_policy;
static bool sched_policy_set;

static int sched_pri;
static bool sched_pri_set;

static bool trace_enable = true;
static bool setup_ftrace = true;
static bool use_random_sleep;

#define TRACE_OPTS				\
	C(FUNC_TR, "function-trace"),		\
	C(DISP_GR, "display-graph"),		\
	C(NR,       NULL)

#undef C
#define C(a, b) OPTIDX_##a

enum traceopt {
	TRACE_OPTS
};

#undef C
#define C(a, b)  b

static const char *const optstr[] = {
	TRACE_OPTS
};

enum errhandling {
	ERR_EXIT = 0,
	ERR_WARN,
	ERR_CLEANUP,
};

static bool use_options[OPTIDX_NR];

static char inotify_buffer[655360];

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#define bool2str(x)    (x ? "true":"false")

#define DEFAULT_NR_PRINTER_THREADS (3)
static unsigned int nr_threads = DEFAULT_NR_PRINTER_THREADS;

#define DEFAULT_TABLE_SIZE (2)
static unsigned int table_startsize = DEFAULT_TABLE_SIZE;

static int verbosity;

#define verbose_sizechange() (verbosity >= 1)
#define verbose_lostevent()  (verbosity >= 2)
#define verbose_ftrace()     (verbosity >= 1)

#define was_changed(ORIG, CUR) (strcmp(ORIG, CUR) != 0)
#define needs_change(CUR, WANTED) (strcmp(CUR, WANTED) != 0)

static const char *debug_tracefile;
static const char *debug_tracefile_dflt;
static const char *debug_maxlat;
static const char *debug_maxlat_dflt;
static const char * const DEBUG_NOFILE = "[file not found]";

static const char * const TR_MAXLAT  = "tracing_max_latency";
static const char * const TR_THRESH  = "tracing_thresh";
static const char * const TR_CURRENT = "current_tracer";
static const char * const TR_OPTIONS = "trace_options";

static const char * const NOP_TRACER = "nop";

static const char * const OPT_NO_PREFIX = "no";

#define DFLT_THRESHOLD_US "0"
static const char *threshold = DFLT_THRESHOLD_US;

#define DEV_URANDOM     "/dev/urandom"
#define RT_DEFAULT_PRI (99)
#define DEFAULT_PRI    (0)

#define USEC_PER_MSEC (1000L)
#define NSEC_PER_USEC (1000L)
#define NSEC_PER_MSEC (USEC_PER_MSEC * NSEC_PER_USEC)

#define MSEC_PER_SEC (1000L)
#define USEC_PER_SEC (USEC_PER_MSEC * MSEC_PER_SEC)
#define NSEC_PER_SEC (NSEC_PER_MSEC * MSEC_PER_SEC)

#define SLEEP_TIME_MS_DEFAULT (1000L)
#define TRY_PRINTMUTEX_MS (1000)

static long sleep_time = (USEC_PER_MSEC * SLEEP_TIME_MS_DEFAULT);

static const char * const queue_full_warning =
"Could not queue trace for printing. It is likely that events happen faster\n"
"than what they can be printed. Probably partly because of random sleeping\n";

static const char * const no_tracer_msg =
"Could not find any tracers! Running this program as root may help!\n";

static const char * const no_latency_tr_msg =
"No latency tracers are supported by your kernel!\n";

struct policy {
	const char *name;
	int policy;
	int default_pri;
};

static const struct policy policies[] = {
	{ "other", SCHED_OTHER, DEFAULT_PRI    },
	{ "batch", SCHED_BATCH, DEFAULT_PRI    },
	{ "idle",  SCHED_IDLE,  DEFAULT_PRI    },
	{ "rr",    SCHED_RR,    RT_DEFAULT_PRI },
	{ "fifo",  SCHED_FIFO,  RT_DEFAULT_PRI },
	{ NULL,    0,           DEFAULT_PRI    }
};

/*
 * The default tracer will be the first on this list that is supported by the
 * currently running Linux kernel.
 */
static const char * const relevant_tracers[] = {
	"preemptirqsoff",
	"preemptoff",
	"irqsoff",
	"wakeup",
	"wakeup_rt",
	"wakeup_dl",
	NULL
};

/* This is the list of tracers for which random sleep makes sense */
static const char * const random_tracers[] = {
	"preemptirqsoff",
	"preemptoff",
	"irqsoff",
	NULL
};

static const char *current_tracer;
static bool force_tracer;

struct ftrace_state {
	char *tracer;
	char *thresh;
	bool opt[OPTIDX_NR];
	bool opt_valid[OPTIDX_NR];
	pthread_mutex_t mutex;
};

struct entry {
	int ticket;
	int ticket_completed_ref;
};

struct print_state {
	int ticket_counter;
	int ticket_completed;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int cnt;
	pthread_mutex_t cnt_mutex;
};

struct short_msg {
	char buf[160];
	int len;
};

static struct print_state printstate;
static struct ftrace_state save_state;
volatile sig_atomic_t signal_flag;

#define PROB_TABLE_MAX_SIZE (1000)

int probabilities[PROB_TABLE_MAX_SIZE];

struct sleep_table {
	int *table;
	int size;
	pthread_mutex_t mutex;
};

static struct sleep_table sleeptable;

#define QUEUE_SIZE (10)

struct queue {
	struct entry entries[QUEUE_SIZE];
	int next_prod_idx;
	int next_cons_idx;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

#define MAX_THREADS (40)

struct queue printqueue;
pthread_t printthread[MAX_THREADS];
pthread_mutex_t print_mtx;
#define PRINT_BUFFER_SIZE (16 * 1024 * 1024)

static void cleanup_exit(int status);
static int set_trace_opt(const char *opt, bool value);

static __always_inline void *malloc_or_die(size_t size)
{
	void *ptr = malloc(size);

	if (unlikely(ptr == NULL)) {
		warn("malloc() failed");
		cleanup_exit(EXIT_FAILURE);
	}
	return ptr;
}

static __always_inline void *malloc_or_die_nocleanup(size_t size)
{
	void *ptr = malloc(size);

	if (unlikely(ptr == NULL))
		err(0, "malloc() failed");
	return ptr;
}

static __always_inline void write_or_die(int fd, const char *buf, size_t count)
{
	ssize_t r;

	do {
		r = write(fd, buf, count);
		if (unlikely(r < 0)) {
			if (errno == EINTR)
				continue;
			warn("write() failed");
			cleanup_exit(EXIT_FAILURE);
		}
		count -= r;
		buf += r;
	} while (count > 0);
}

static __always_inline void clock_gettime_or_die(clockid_t clk_id,
						 struct timespec *tp)
{
	int r = clock_gettime(clk_id, tp);

	if (unlikely(r != 0))
		err(EXIT_FAILURE, "clock_gettime() failed");
}

static __always_inline void sigemptyset_or_die(sigset_t *s)
{
	if (unlikely(sigemptyset(s) != 0)) {
		warn("sigemptyset() failed");
		cleanup_exit(EXIT_FAILURE);
	}
}

static __always_inline void sigaddset_or_die(sigset_t *s, int signum)
{
	if (unlikely(sigaddset(s, signum) != 0)) {
		warn("sigemptyset() failed");
		cleanup_exit(EXIT_FAILURE);
	}
}

static __always_inline void sigaction_or_die(int signum,
					     const struct sigaction *act,
					     struct sigaction *oldact)
{
	if (unlikely(sigaction(signum, act, oldact) != 0)) {
		warn("sigaction() failed");
		cleanup_exit(EXIT_FAILURE);
	}
}

static void open_stdout(void)
{
	if (setvbuf(stdout, NULL, _IONBF, 0) != 0)
		err(EXIT_FAILURE, "setvbuf() failed");
	fd_stdout = fileno(stdout);
	if (fd_stdout < 0)
		err(EXIT_FAILURE, "fileno() failed");
}

/*
 * It's not worth it to call cleanup_exit() from mutex functions because
 * cleanup_exit() uses mutexes.
 */
static __always_inline void mutex_lock(pthread_mutex_t *mtx)
{
	errno = pthread_mutex_lock(mtx);
	if (unlikely(errno))
		err(EXIT_FAILURE, "pthread_mutex_lock() failed");
}


static __always_inline void mutex_unlock(pthread_mutex_t *mtx)
{
	errno = pthread_mutex_unlock(mtx);
	if (unlikely(errno))
		err(EXIT_FAILURE, "pthread_mutex_unlock() failed");
}

static __always_inline void cond_signal(pthread_cond_t *cond)
{
	errno = pthread_cond_signal(cond);
	if (unlikely(errno))
		err(EXIT_FAILURE, "pthread_cond_signal() failed");
}

static __always_inline void cond_wait(pthread_cond_t *restrict cond,
				      pthread_mutex_t *restrict mutex)
{
	errno = pthread_cond_wait(cond, mutex);
	if (unlikely(errno))
		err(EXIT_FAILURE, "pthread_cond_wait() failed");
}

static __always_inline void cond_broadcast(pthread_cond_t *cond)
{
	errno = pthread_cond_broadcast(cond);
	if (unlikely(errno))
		err(EXIT_FAILURE, "pthread_cond_broadcast() failed");
}

static __always_inline void
mutex_init(pthread_mutex_t *mutex,
	   const pthread_mutexattr_t *attr)
{
	errno = pthread_mutex_init(mutex, attr);
	if (errno)
		err(EXIT_FAILURE, "pthread_mutex_init() failed");
}

static __always_inline void mutexattr_init(pthread_mutexattr_t *attr)
{
	errno = pthread_mutexattr_init(attr);
	if (errno)
		err(EXIT_FAILURE, "pthread_mutexattr_init() failed");
}

static __always_inline void mutexattr_destroy(pthread_mutexattr_t *attr)
{
	errno = pthread_mutexattr_destroy(attr);
	if (errno)
		err(EXIT_FAILURE, "pthread_mutexattr_destroy() failed");
}

static __always_inline void mutexattr_settype(pthread_mutexattr_t *attr,
					      int type)
{
	errno = pthread_mutexattr_settype(attr, type);
	if (errno)
		err(EXIT_FAILURE, "pthread_mutexattr_settype() failed");
}

static __always_inline void condattr_init(pthread_condattr_t *attr)
{
	errno = pthread_condattr_init(attr);
	if (errno)
		err(EXIT_FAILURE, "pthread_condattr_init() failed");
}

static __always_inline void condattr_destroy(pthread_condattr_t *attr)
{
	errno = pthread_condattr_destroy(attr);
	if (errno)
		err(EXIT_FAILURE, "pthread_condattr_destroy() failed");
}

static __always_inline void condattr_setclock(pthread_condattr_t *attr,
					      clockid_t clock_id)
{
	errno = pthread_condattr_setclock(attr, clock_id);
	if (unlikely(errno))
		err(EXIT_FAILURE, "pthread_condattr_setclock() failed");
}

static __always_inline void cond_init(pthread_cond_t *cond,
				      const pthread_condattr_t *attr)
{
	errno = pthread_cond_init(cond, attr);
	if (errno)
		err(EXIT_FAILURE, "pthread_cond_init() failed");
}

static __always_inline int
cond_timedwait(pthread_cond_t *restrict cond,
	       pthread_mutex_t *restrict mutex,
	       const struct timespec *restrict abstime)
{
	errno = pthread_cond_timedwait(cond, mutex, abstime);
	if (errno && errno != ETIMEDOUT)
		err(EXIT_FAILURE, "pthread_cond_timedwait() failed");
	return errno;
}

static void init_printstate(void)
{
	pthread_condattr_t cattr;

	printstate.ticket_counter = 0;
	printstate.ticket_completed = 0;
	printstate.cnt = 0;

	mutex_init(&printstate.mutex, NULL);

	condattr_init(&cattr);
	condattr_setclock(&cattr, CLOCK_MONOTONIC);
	cond_init(&printstate.cond, &cattr);
	condattr_destroy(&cattr);
}

static void init_print_mtx(void)
{
	pthread_mutexattr_t mattr;

	mutexattr_init(&mattr);
	mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	mutex_init(&print_mtx, &mattr);
	mutexattr_destroy(&mattr);

}

static void signal_blocking(int how)
{
	sigset_t s;

	sigemptyset_or_die(&s);
	sigaddset_or_die(&s, SIGHUP);
	sigaddset_or_die(&s, SIGTERM);
	sigaddset_or_die(&s, SIGINT);

	errno = pthread_sigmask(how, &s, NULL);
	if (unlikely(errno)) {
		warn("pthread_sigmask() failed");
		cleanup_exit(EXIT_FAILURE);
	}
}

static void signal_handler(int num)
{
	signal_flag = num;
}

static void setup_sig_handler(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_handler;

	sigaction_or_die(SIGHUP, &sa, NULL);
	sigaction_or_die(SIGTERM, &sa, NULL);
	sigaction_or_die(SIGINT, &sa, NULL);
}

static void process_signal(int signal)
{
	char *name;

	name = strsignal(signal);
	if (name == NULL)
		printf("Received signal %d\n", signal);
	else
		printf("Received signal %d (%s)\n", signal, name);
	cleanup_exit(EXIT_SUCCESS);
}

static __always_inline void check_signals(void)
{
	int signal = signal_flag;

	if (unlikely(signal))
		process_signal(signal);
}

static __always_inline void get_time_in_future(struct timespec *future,
					       long time_us)
{
	long nsec;

	clock_gettime_or_die(CLOCK_MONOTONIC, future);
	future->tv_sec += time_us / USEC_PER_SEC;
	nsec = future->tv_nsec + (time_us * NSEC_PER_USEC) % NSEC_PER_SEC;
	if (nsec >= NSEC_PER_SEC) {
		future->tv_nsec = nsec % NSEC_PER_SEC;
		future->tv_sec += 1;
	}
}

static __always_inline bool time_has_passed(const struct timespec *time)
{
	struct timespec now;

	clock_gettime_or_die(CLOCK_MONOTONIC, &now);
	if (now.tv_sec > time->tv_sec)
		return true;
	if (now.tv_sec < time->tv_sec)
		return false;
	return (now.tv_nsec >= time->tv_nsec);
}

static bool mutex_trylock_limit(pthread_mutex_t *mutex, int time_ms)
{
	long time_us = time_ms * USEC_PER_MSEC;
	struct timespec limit;

	get_time_in_future(&limit, time_us);
	do {
		errno =  pthread_mutex_trylock(mutex);
		if (errno && errno != EBUSY)
			err(EXIT_FAILURE, "pthread_mutex_trylock() failed");
	} while (errno && !time_has_passed(&limit));
	return errno == 0;
}

static void restore_trace_opts(const struct ftrace_state *state,
				const bool *cur)
{
	int i;
	int r;

	for (i = 0; i < OPTIDX_NR; i++)
		if (state->opt_valid[i] && state->opt[i] != cur[i]) {
			r = set_trace_opt(optstr[i], state->opt[i]);
			if (r < 0)
				warnx("Failed to restore the %s option to %s",
				      optstr[i], bool2str(state->opt[i]));
			else if (verbose_ftrace())
				printf("Restored the %s option in %s to %s\n",
				       optstr[i], TR_OPTIONS,
				       bool2str(state->opt[i]));
		}
}

static char *read_file(const char *file, enum errhandling h)
{
	int psize;
	char *r;
	static const char *emsg = "Failed to read the %s file";

	r = tracefs_instance_file_read(NULL, file, &psize);
	if (!r) {
		if (h) {
			warn(emsg, file);
			if (h == ERR_CLEANUP)
				cleanup_exit(EXIT_FAILURE);
		} else
			errx(EXIT_FAILURE, emsg, file);
	}

	if (r && r[psize - 1] == '\n')
		r[psize - 1] = '\0';
	return r;
}

static void restore_file(const char *file, char **saved, const char *cur)
{
	if (*saved && was_changed(*saved, cur)) {
		if (tracefs_instance_file_write(NULL, file, *saved) < 0)
			warnx("Failed to restore %s to %s!", file, *saved);
		else if (verbose_ftrace())
			printf("Restored %s to %s\n", file, *saved);
		free(*saved);
		*saved = NULL;
	}
}

static void restore_ftrace(void)
{
	mutex_lock(&save_state.mutex);

	restore_file(TR_CURRENT, &save_state.tracer, current_tracer);
	restore_file(TR_THRESH, &save_state.thresh, threshold);
	restore_trace_opts(&save_state, use_options);

	mutex_unlock(&save_state.mutex);
}

static void cleanup_exit(int status)
{
	char *maxlat;

	if (!setup_ftrace)
		exit(status);

	/*
	 * We try the print_mtx for 1 sec in order to avoid garbled
	 * output if possible, but if it cannot be obtained we proceed anyway.
	 */
	mutex_trylock_limit(&print_mtx, TRY_PRINTMUTEX_MS);

	maxlat = read_file(TR_MAXLAT, ERR_WARN);
	if (maxlat) {
		printf("The maximum detected latency was: %sus\n", maxlat);
		free(maxlat);
	}

	restore_ftrace();
	/*
	 * We do not need to unlock the print_mtx here because we will exit at
	 * the end of this function. Unlocking print_mtx causes problems if a
	 * print thread happens to be waiting for the mutex because we have
	 * just changed the ftrace settings to the original and thus the
	 * print thread would output incorrect data from ftrace.
	 */
	exit(status);
}

static void init_save_state(void)
{
	pthread_mutexattr_t mattr;

	mutexattr_init(&mattr);
	mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	mutex_init(&save_state.mutex, &mattr);
	mutexattr_destroy(&mattr);

	save_state.tracer = NULL;
	save_state.thresh = NULL;
	save_state.opt_valid[OPTIDX_FUNC_TR] = false;
	save_state.opt_valid[OPTIDX_DISP_GR] = false;
}

static int printstate_next_ticket(struct entry *req)
{
	int r;

	r = ++(printstate.ticket_counter);
	req->ticket = r;
	req->ticket_completed_ref = printstate.ticket_completed;
	cond_broadcast(&printstate.cond);
	return r;
}

static __always_inline
void printstate_mark_req_completed(const struct entry *req)
{
	if (req->ticket > printstate.ticket_completed)
		printstate.ticket_completed = req->ticket;
}

static __always_inline
bool printstate_has_new_req_arrived(const struct entry *req)
{
	return (printstate.ticket_counter != req->ticket);
}

static __always_inline int printstate_cnt_inc(void)
{
	int value;

	mutex_lock(&printstate.cnt_mutex);
	value = ++printstate.cnt;
	mutex_unlock(&printstate.cnt_mutex);
	return value;
}

static __always_inline int printstate_cnt_dec(void)
{
	int value;

	mutex_lock(&printstate.cnt_mutex);
	value = --printstate.cnt;
	mutex_unlock(&printstate.cnt_mutex);
	return value;
}

static __always_inline int printstate_cnt_read(void)
{
	int value;

	mutex_lock(&printstate.cnt_mutex);
	value = printstate.cnt;
	mutex_unlock(&printstate.cnt_mutex);
	return value;
}

static __always_inline
bool prev_req_won_race(const struct entry *req)
{
	return (printstate.ticket_completed != req->ticket_completed_ref);
}

static void sleeptable_resize(int size, bool printout, struct short_msg *msg)
{
	int bytes;

	if (printout) {
		msg->len = 0;
		if (unlikely(size > PROB_TABLE_MAX_SIZE))
			bytes = snprintf(msg->buf, sizeof(msg->buf),
"Cannot increase probability table to %d (maximum size reached)\n", size);
		else
			bytes = snprintf(msg->buf, sizeof(msg->buf),
"Increasing probability table to %d\n", size);
		if (bytes < 0)
			warn("snprintf() failed");
		else
			msg->len = bytes;
	}

	if (unlikely(size < 0)) {
		/* Should never happen */
		warnx("Bad program state at %s:%d", __FILE__, __LINE__);
		cleanup_exit(EXIT_FAILURE);
		return;
	}
	sleeptable.size = size;
	sleeptable.table = &probabilities[PROB_TABLE_MAX_SIZE - size];
}

static void init_probabilities(void)
{
	int i;
	int j = 1000;

	for (i = 0; i < PROB_TABLE_MAX_SIZE; i++) {
		probabilities[i] = 1000 / j;
		j--;
	}
	mutex_init(&sleeptable.mutex, NULL);
}

static int table_get_probability(const struct entry *req,
				 struct short_msg *msg)
{
	int diff = req->ticket - req->ticket_completed_ref;
	int rval = 0;

	msg->len = 0;
	diff--;
	/* Should never happen...*/
	if (unlikely(diff < 0)) {
		warnx("Programmer assumption error at %s:%d\n", __FILE__,
		      __LINE__);
		cleanup_exit(EXIT_FAILURE);
	}
	mutex_lock(&sleeptable.mutex);
	if (diff >= (sleeptable.size - 1)) {
		rval = sleeptable.table[sleeptable.size - 1];
		sleeptable_resize(sleeptable.size + 1, verbose_sizechange(),
				  msg);
	} else {
		rval = sleeptable.table[diff];
	}
	mutex_unlock(&sleeptable.mutex);
	return rval;
}

static void init_queue(struct queue *q)
{
	q->next_prod_idx = 0;
	q->next_cons_idx = 0;
	mutex_init(&q->mutex, NULL);
	errno = pthread_cond_init(&q->cond, NULL);
	if (errno)
		err(EXIT_FAILURE, "pthread_cond_init() failed");
}

static __always_inline int queue_len(const struct queue *q)
{
	if (q->next_prod_idx >= q->next_cons_idx)
		return q->next_prod_idx - q->next_cons_idx;
	else
		return QUEUE_SIZE - q->next_cons_idx + q->next_prod_idx;
}

static __always_inline int queue_nr_free(const struct queue *q)
{
	int nr_free = QUEUE_SIZE - queue_len(q);

	/*
	 * If there is only one slot left we will anyway lie and claim that the
	 * queue is full because adding an element will make it appear empty
	 */
	if (nr_free == 1)
		nr_free = 0;
	return nr_free;
}

static __always_inline void queue_idx_inc(int *idx)
{
	*idx = (*idx + 1) % QUEUE_SIZE;
}

static __always_inline void queue_push_to_back(struct queue *q,
					      const struct entry *e)
{
	q->entries[q->next_prod_idx] = *e;
	queue_idx_inc(&q->next_prod_idx);
}

static __always_inline struct entry queue_pop_from_front(struct queue *q)
{
	struct entry e = q->entries[q->next_cons_idx];

	queue_idx_inc(&q->next_cons_idx);
	return e;
}

static __always_inline void queue_cond_signal(struct queue *q)
{
	cond_signal(&q->cond);
}

static __always_inline void queue_cond_wait(struct queue *q)
{
	cond_wait(&q->cond, &q->mutex);
}

static __always_inline int queue_try_to_add_entry(struct queue *q,
						  const struct entry *e)
{
	int r = 0;

	mutex_lock(&q->mutex);
	if (queue_nr_free(q) > 0) {
		queue_push_to_back(q, e);
		cond_signal(&q->cond);
	} else
		r = -1;
	mutex_unlock(&q->mutex);
	return r;
}

static struct entry queue_wait_for_entry(struct queue *q)
{
	struct entry e;

	mutex_lock(&q->mutex);
	while (true) {
		if (queue_len(&printqueue) > 0) {
			e = queue_pop_from_front(q);
			break;
		}
		queue_cond_wait(q);
	}
	mutex_unlock(&q->mutex);

	return e;
}

static const struct policy *policy_from_name(const char *name)
{
	const struct policy *p = &policies[0];

	while (p->name != NULL) {
		if (!strcmp(name, p->name))
			return p;
		p++;
	}
	return NULL;
}

static const char *policy_name(int policy)
{
	const struct policy *p = &policies[0];
	static const char *rval = "unknown";

	while (p->name != NULL) {
		if (p->policy == policy)
			return p->name;
		p++;
	}
	return rval;
}

static bool is_relevant_tracer(const char *name)
{
	unsigned int i;

	for (i = 0; relevant_tracers[i]; i++)
		if (!strcmp(name, relevant_tracers[i]))
			return true;
	return false;
}

static bool random_makes_sense(const char *name)
{
	unsigned int i;

	for (i = 0; random_tracers[i]; i++)
		if (!strcmp(name, random_tracers[i]))
			return true;
	return false;
}

static void show_available(void)
{
	char **tracers;
	int found = 0;
	int i;

	tracers = tracefs_tracers(NULL);
	for (i = 0; tracers && tracers[i]; i++) {
		if (is_relevant_tracer(tracers[i]))
			found++;
	}

	if (!tracers) {
		warnx(no_tracer_msg);
		return;
	}

	if (!found) {
		warnx(no_latency_tr_msg);
		tracefs_list_free(tracers);
		return;
	}

	printf("The following latency tracers are available on your system:\n");
	for (i = 0; tracers[i]; i++) {
		if (is_relevant_tracer(tracers[i]))
			printf("%s\n", tracers[i]);
	}
	tracefs_list_free(tracers);
}

static bool tracer_valid(const char *name, bool *notracer)
{
	char **tracers;
	int i;
	bool rval = false;

	*notracer = false;
	tracers = tracefs_tracers(NULL);
	if (!tracers) {
		*notracer = true;
		return false;
	}
	for (i = 0; tracers[i]; i++)
		if (!strcmp(tracers[i], name)) {
			rval = true;
			break;
		}
	tracefs_list_free(tracers);
	return rval;
}

static const char *find_default_tracer(void)
{
	int i;
	bool notracer;
	bool valid;

	for (i = 0; relevant_tracers[i]; i++) {
		valid = tracer_valid(relevant_tracers[i], &notracer);
		if (notracer)
			errx(EXIT_FAILURE, no_tracer_msg);
		if (valid)
			return relevant_tracers[i];
	}
	return NULL;
}

static bool toss_coin(struct drand48_data *buffer, unsigned int prob)
{
	long r;

	if (unlikely(lrand48_r(buffer, &r))) {
		warnx("lrand48_r() failed");
		cleanup_exit(EXIT_FAILURE);
	}
	r = r % 1000L;
	if (r < prob)
		return true;
	else
		return false;
}


static long go_to_sleep(const struct entry *req)
{
	struct timespec future;
	long delay = sleep_time;

	get_time_in_future(&future, delay);

	mutex_lock(&printstate.mutex);
	while (!printstate_has_new_req_arrived(req)) {
		cond_timedwait(&printstate.cond, &printstate.mutex, &future);
		if (time_has_passed(&future))
			break;
	}

	if (printstate_has_new_req_arrived(req))
		delay = -1;
	mutex_unlock(&printstate.mutex);

	return delay;
}


static void set_priority(void)
{
	int r;
	pid_t pid;
	struct sched_param param;

	memset(&param, 0, sizeof(param));
	param.sched_priority = sched_pri;

	pid = getpid();
	r = sched_setscheduler(pid, sched_policy, &param);

	if (r != 0)
		err(EXIT_FAILURE, "sched_setscheduler() failed");
}

pid_t latency_collector_gettid(void)
{
	return (pid_t) syscall(__NR_gettid);
}

static void print_priority(void)
{
	pid_t tid;
	int policy;
	int r;
	struct sched_param param;

	tid = latency_collector_gettid();
	r = pthread_getschedparam(pthread_self(), &policy, &param);
	if (r != 0) {
		warn("pthread_getschedparam() failed");
		cleanup_exit(EXIT_FAILURE);
	}
	mutex_lock(&print_mtx);
	printf("Thread %d runs with scheduling policy %s and priority %d\n",
	       tid, policy_name(policy), param.sched_priority);
	mutex_unlock(&print_mtx);
}

static __always_inline
void __print_skipmessage(const struct short_msg *resize_msg,
			 const struct timespec *timestamp, char *buffer,
			 size_t bufspace, const struct entry *req, bool excuse,
			 const char *str)
{
	ssize_t bytes = 0;
	char *p = &buffer[0];
	long us, sec;
	int r;

	sec = timestamp->tv_sec;
	us = timestamp->tv_nsec / 1000;

	if (resize_msg != NULL && resize_msg->len > 0) {
		strncpy(p, resize_msg->buf, resize_msg->len);
		bytes += resize_msg->len;
		p += resize_msg->len;
		bufspace -= resize_msg->len;
	}

	if (excuse)
		r = snprintf(p, bufspace,
"%ld.%06ld Latency %d printout skipped due to %s\n",
			     sec, us, req->ticket, str);
	else
		r = snprintf(p, bufspace, "%ld.%06ld Latency %d detected\n",
			    sec, us, req->ticket);

	if (r < 0)
		warn("snprintf() failed");
	else
		bytes += r;

	/* These prints could happen concurrently */
	mutex_lock(&print_mtx);
	write_or_die(fd_stdout, buffer, bytes);
	mutex_unlock(&print_mtx);
}

static void print_skipmessage(const struct short_msg *resize_msg,
			      const struct timespec *timestamp, char *buffer,
			      size_t bufspace, const struct entry *req,
			      bool excuse)
{
	__print_skipmessage(resize_msg, timestamp, buffer, bufspace, req,
			    excuse, "random delay");
}

static void print_lostmessage(const struct timespec *timestamp, char *buffer,
			      size_t bufspace, const struct entry *req,
			      const char *reason)
{
	__print_skipmessage(NULL, timestamp, buffer, bufspace, req, true,
			    reason);
}

static void print_tracefile(const struct short_msg *resize_msg,
			    const struct timespec *timestamp, char *buffer,
			    size_t bufspace, long slept,
			    const struct entry *req)
{
	static const int reserve = 256;
	char *p = &buffer[0];
	ssize_t bytes = 0;
	ssize_t bytes_tot = 0;
	long us, sec;
	long slept_ms;
	int trace_fd;

	/* Save some space for the final string and final null char */
	bufspace = bufspace - reserve - 1;

	if (resize_msg != NULL && resize_msg->len > 0) {
		bytes = resize_msg->len;
		strncpy(p, resize_msg->buf, bytes);
		bytes_tot += bytes;
		p += bytes;
		bufspace -= bytes;
	}

	trace_fd = open(debug_tracefile, O_RDONLY);

	if (trace_fd < 0) {
		warn("open() failed on %s", debug_tracefile);
		return;
	}

	sec = timestamp->tv_sec;
	us = timestamp->tv_nsec / 1000;

	if (slept != 0) {
		slept_ms = slept / 1000;
		bytes = snprintf(p, bufspace,
"%ld.%06ld Latency %d randomly sleep for %ld ms before print\n",
				 sec, us, req->ticket, slept_ms);
	} else {
		bytes = snprintf(p, bufspace,
				 "%ld.%06ld Latency %d immediate print\n", sec,
				 us, req->ticket);
	}

	if (bytes < 0) {
		warn("snprintf() failed");
		return;
	}
	p += bytes;
	bufspace -= bytes;
	bytes_tot += bytes;

	bytes = snprintf(p, bufspace,
">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> BEGIN <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n"
		);

	if (bytes < 0) {
		warn("snprintf() failed");
		return;
	}

	p += bytes;
	bufspace -= bytes;
	bytes_tot += bytes;

	do {
		bytes = read(trace_fd, p, bufspace);
		if (bytes < 0) {
			if (errno == EINTR)
				continue;
			warn("read() failed on %s", debug_tracefile);
			if (unlikely(close(trace_fd) != 0))
				warn("close() failed on %s", debug_tracefile);
			return;
		}
		if (bytes == 0)
			break;
		p += bytes;
		bufspace -= bytes;
		bytes_tot += bytes;
	} while (true);

	if (unlikely(close(trace_fd) != 0))
		warn("close() failed on %s", debug_tracefile);

	printstate_cnt_dec();
	/* Add the reserve space back to the budget for the final string */
	bufspace += reserve;

	bytes = snprintf(p, bufspace,
			 ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> END <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n\n");

	if (bytes < 0) {
		warn("snprintf() failed");
		return;
	}

	bytes_tot += bytes;

	/* These prints could happen concurrently */
	mutex_lock(&print_mtx);
	write_or_die(fd_stdout, buffer, bytes_tot);
	mutex_unlock(&print_mtx);
}

static char *get_no_opt(const char *opt)
{
	char *no_opt;
	int s;

	s = strlen(opt) + strlen(OPT_NO_PREFIX) + 1;
	/* We may be called from cleanup_exit() via set_trace_opt() */
	no_opt = malloc_or_die_nocleanup(s);
	strcpy(no_opt, OPT_NO_PREFIX);
	strcat(no_opt, opt);
	return no_opt;
}

static char *find_next_optstr(const char *allopt, const char **next)
{
	const char *begin;
	const char *end;
	char *r;
	int s = 0;

	if (allopt == NULL)
		return NULL;

	for (begin = allopt; *begin != '\0'; begin++) {
		if (isgraph(*begin))
			break;
	}

	if (*begin == '\0')
		return NULL;

	for (end = begin; *end != '\0' && isgraph(*end); end++)
		s++;

	r = malloc_or_die_nocleanup(s + 1);
	strncpy(r, begin, s);
	r[s] = '\0';
	*next = begin + s;
	return r;
}

static bool get_trace_opt(const char *allopt, const char *opt, bool *found)
{
	*found = false;
	char *no_opt;
	char *str;
	const char *next = allopt;
	bool rval = false;

	no_opt = get_no_opt(opt);

	do {
		str = find_next_optstr(next, &next);
		if (str == NULL)
			break;
		if (!strcmp(str, opt)) {
			*found = true;
			rval = true;
			free(str);
			break;
		}
		if (!strcmp(str, no_opt)) {
			*found = true;
			rval = false;
			free(str);
			break;
		}
		free(str);
	} while (true);
	free(no_opt);

	return rval;
}

static int set_trace_opt(const char *opt, bool value)
{
	char *str;
	int r;

	if (value)
		str = strdup(opt);
	else
		str = get_no_opt(opt);

	r = tracefs_instance_file_write(NULL, TR_OPTIONS, str);
	free(str);
	return r;
}

void save_trace_opts(struct ftrace_state *state)
{
	char *allopt;
	int psize;
	int i;

	allopt = tracefs_instance_file_read(NULL, TR_OPTIONS, &psize);
	if (!allopt)
		errx(EXIT_FAILURE, "Failed to read the %s file\n", TR_OPTIONS);

	for (i = 0; i < OPTIDX_NR; i++)
		state->opt[i] = get_trace_opt(allopt, optstr[i],
					      &state->opt_valid[i]);

	free(allopt);
}

static void write_file(const char *file, const char *cur, const char *new,
		       enum errhandling h)
{
	int r;
	static const char *emsg = "Failed to write to the %s file!";

	/* Do nothing if we now that the current and new value are equal */
	if (cur && !needs_change(cur, new))
		return;

	r = tracefs_instance_file_write(NULL, file, new);
	if (r < 0) {
		if (h) {
			warnx(emsg, file);
			if (h == ERR_CLEANUP)
				cleanup_exit(EXIT_FAILURE);
		} else
			errx(EXIT_FAILURE, emsg, file);
	}
	if (verbose_ftrace()) {
		mutex_lock(&print_mtx);
		printf("%s was set to %s\n", file, new);
		mutex_unlock(&print_mtx);
	}
}

static void reset_max_latency(void)
{
	write_file(TR_MAXLAT, NULL, "0", ERR_CLEANUP);
}

static void save_and_disable_tracer(void)
{
	char *orig_th;
	char *tracer;
	bool need_nop = false;

	mutex_lock(&save_state.mutex);

	save_trace_opts(&save_state);
	tracer = read_file(TR_CURRENT, ERR_EXIT);
	orig_th = read_file(TR_THRESH, ERR_EXIT);

	if (needs_change(tracer, NOP_TRACER)) {
		mutex_lock(&print_mtx);
		if (force_tracer) {
			printf(
				"The %s tracer is already in use but proceeding anyway!\n",
				tracer);
		} else {
			printf(
				"The %s tracer is already in use, cowardly bailing out!\n"
				"This could indicate that another program or instance is tracing.\n"
				"Use the -F [--force] option to disregard the current tracer.\n", tracer);
			exit(0);
		}
		mutex_unlock(&print_mtx);
		need_nop = true;
	}

	save_state.tracer =  tracer;
	save_state.thresh = orig_th;

	if (need_nop)
		write_file(TR_CURRENT, NULL, NOP_TRACER, ERR_EXIT);

	mutex_unlock(&save_state.mutex);
}

void set_trace_opts(struct ftrace_state *state, bool *new)
{
	int i;
	int r;

	/*
	 * We only set options if we earlier detected that the option exists in
	 * the trace_options file and that the wanted setting is different from
	 * the one we saw in save_and_disable_tracer()
	 */
	for (i = 0; i < OPTIDX_NR; i++)
		if (state->opt_valid[i] &&
		    state->opt[i] != new[i]) {
			r = set_trace_opt(optstr[i], new[i]);
			if (r < 0) {
				warnx("Failed to set the %s option to %s",
				      optstr[i], bool2str(new[i]));
				cleanup_exit(EXIT_FAILURE);
			}
			if (verbose_ftrace()) {
				mutex_lock(&print_mtx);
				printf("%s in %s was set to %s\n", optstr[i],
				       TR_OPTIONS, bool2str(new[i]));
				mutex_unlock(&print_mtx);
			}
		}
}

static void enable_tracer(void)
{
	mutex_lock(&save_state.mutex);
	set_trace_opts(&save_state, use_options);

	write_file(TR_THRESH, save_state.thresh, threshold, ERR_CLEANUP);
	write_file(TR_CURRENT, NOP_TRACER, current_tracer, ERR_CLEANUP);

	mutex_unlock(&save_state.mutex);
}

static void tracing_loop(void)
{
	int ifd = inotify_init();
	int wd;
	const ssize_t bufsize = sizeof(inotify_buffer);
	const ssize_t istructsize = sizeof(struct inotify_event);
	char *buf = &inotify_buffer[0];
	ssize_t nr_read;
	char *p;
	int modified;
	struct inotify_event *event;
	struct entry req;
	char *buffer;
	const size_t bufspace = PRINT_BUFFER_SIZE;
	struct timespec timestamp;

	print_priority();

	buffer = malloc_or_die(bufspace);

	if (ifd < 0)
		err(EXIT_FAILURE, "inotify_init() failed!");


	if (setup_ftrace) {
		/*
		 * We must disable the tracer before resetting the max_latency
		 */
		save_and_disable_tracer();
		/*
		 * We must reset the max_latency before the inotify_add_watch()
		 * call.
		 */
		reset_max_latency();
	}

	wd = inotify_add_watch(ifd, debug_maxlat, IN_MODIFY);
	if (wd < 0)
		err(EXIT_FAILURE, "inotify_add_watch() failed!");

	if (setup_ftrace)
		enable_tracer();

	signal_blocking(SIG_UNBLOCK);

	while (true) {
		modified = 0;
		check_signals();
		nr_read = read(ifd, buf, bufsize);
		check_signals();
		if (nr_read < 0) {
			if (errno == EINTR)
				continue;
			warn("read() failed on inotify fd!");
			cleanup_exit(EXIT_FAILURE);
		}
		if (nr_read == bufsize)
			warnx("inotify() buffer filled, skipping events");
		if (nr_read < istructsize) {
			warnx("read() returned too few bytes on inotify fd");
			cleanup_exit(EXIT_FAILURE);
		}

		for (p = buf; p < buf + nr_read;) {
			event = (struct inotify_event *) p;
			if ((event->mask & IN_MODIFY) != 0)
				modified++;
			p += istructsize + event->len;
		}
		while (modified > 0) {
			check_signals();
			mutex_lock(&printstate.mutex);
			check_signals();
			printstate_next_ticket(&req);
			if (printstate_cnt_read() > 0) {
				printstate_mark_req_completed(&req);
				mutex_unlock(&printstate.mutex);
				if (verbose_lostevent()) {
					clock_gettime_or_die(CLOCK_MONOTONIC,
							     &timestamp);
					print_lostmessage(&timestamp, buffer,
							  bufspace, &req,
							  "inotify loop");
				}
				break;
			}
			mutex_unlock(&printstate.mutex);
			if (queue_try_to_add_entry(&printqueue, &req) != 0) {
				/* These prints could happen concurrently */
				check_signals();
				mutex_lock(&print_mtx);
				check_signals();
				write_or_die(fd_stdout, queue_full_warning,
					     strlen(queue_full_warning));
				mutex_unlock(&print_mtx);
			}
			modified--;
		}
	}
}

static void *do_printloop(void *arg)
{
	const size_t bufspace = PRINT_BUFFER_SIZE;
	char *buffer;
	long *rseed = (long *) arg;
	struct drand48_data drandbuf;
	long slept = 0;
	struct entry req;
	int prob = 0;
	struct timespec timestamp;
	struct short_msg resize_msg;

	print_priority();

	if (srand48_r(*rseed, &drandbuf) != 0) {
		warn("srand48_r() failed!\n");
		cleanup_exit(EXIT_FAILURE);
	}

	buffer = malloc_or_die(bufspace);

	while (true) {
		req = queue_wait_for_entry(&printqueue);
		clock_gettime_or_die(CLOCK_MONOTONIC, &timestamp);
		mutex_lock(&printstate.mutex);
		if (prev_req_won_race(&req)) {
			printstate_mark_req_completed(&req);
			mutex_unlock(&printstate.mutex);
			if (verbose_lostevent())
				print_lostmessage(&timestamp, buffer, bufspace,
						  &req, "print loop");
			continue;
		}
		mutex_unlock(&printstate.mutex);

		/*
		 * Toss a coin to decide if we want to sleep before printing
		 * out the backtrace. The reason for this is that opening
		 * /sys/kernel/tracing/trace will cause a blackout of
		 * hundreds of ms, where no latencies will be noted by the
		 * latency tracer. Thus by randomly sleeping we try to avoid
		 * missing traces systematically due to this. With this option
		 * we will sometimes get the first latency, some other times
		 * some of the later ones, in case of closely spaced traces.
		 */
		if (trace_enable && use_random_sleep) {
			slept = 0;
			prob = table_get_probability(&req, &resize_msg);
			if (!toss_coin(&drandbuf, prob))
				slept = go_to_sleep(&req);
			if (slept >= 0) {
				/* A print is ongoing */
				printstate_cnt_inc();
				/*
				 * We will do the printout below so we have to
				 * mark it as completed while we still have the
				 * mutex.
				 */
				mutex_lock(&printstate.mutex);
				printstate_mark_req_completed(&req);
				mutex_unlock(&printstate.mutex);
			}
		}
		if (trace_enable) {
			/*
			 * slept < 0  means that we detected another
			 * notification in go_to_sleep() above
			 */
			if (slept >= 0)
				/*
				 * N.B. printstate_cnt_dec(); will be called
				 * inside print_tracefile()
				 */
				print_tracefile(&resize_msg, &timestamp, buffer,
						bufspace, slept, &req);
			else
				print_skipmessage(&resize_msg, &timestamp,
						  buffer, bufspace, &req, true);
		} else {
			print_skipmessage(&resize_msg, &timestamp, buffer,
					  bufspace, &req, false);
		}
	}
	return NULL;
}

static void start_printthread(void)
{
	unsigned int i;
	long *seed;
	int ufd;

	ufd = open(DEV_URANDOM, O_RDONLY);
	if (nr_threads > MAX_THREADS) {
		warnx(
"Number of requested print threads was %d, max number is %d\n",
		      nr_threads, MAX_THREADS);
		nr_threads = MAX_THREADS;
	}
	for (i = 0; i < nr_threads; i++) {
		seed = malloc_or_die(sizeof(*seed));
		if (ufd <  0 ||
		    read(ufd, seed, sizeof(*seed)) != sizeof(*seed)) {
			printf(
"Warning! Using trivial random number seed, since %s not available\n",
			DEV_URANDOM);
			fflush(stdout);
			*seed = i;
		}
		errno = pthread_create(&printthread[i], NULL, do_printloop,
				       seed);
		if (errno)
			err(EXIT_FAILURE, "pthread_create()");
	}
	if (ufd > 0 && close(ufd) != 0)
		warn("close() failed");
}

static void show_usage(void)
{
	printf(
"Usage: %s [OPTION]...\n\n"
"Collect closely occurring latencies from %s\n"
"with any of the following tracers: preemptirqsoff, preemptoff, irqsoff, "
"wakeup,\nwakeup_dl, or wakeup_rt.\n\n"

"The occurrence of a latency is detected by monitoring the file\n"
"%s with inotify.\n\n"

"The following options are supported:\n\n"

"-l, --list\t\tList the latency tracers that are supported by the\n"
"\t\t\tcurrently running Linux kernel. If you don't see the\n"
"\t\t\ttracer that you want, you will probably need to\n"
"\t\t\tchange your kernel config and build a new kernel.\n\n"

"-t, --tracer TR\t\tUse the tracer TR. The default is to use the first\n"
"\t\t\ttracer that is supported by the kernel in the following\n"
"\t\t\torder of precedence:\n\n"
"\t\t\tpreemptirqsoff\n"
"\t\t\tpreemptoff\n"
"\t\t\tirqsoff\n"
"\t\t\twakeup\n"
"\t\t\twakeup_rt\n"
"\t\t\twakeup_dl\n"
"\n"
"\t\t\tIf TR is not on the list above, then a warning will be\n"
"\t\t\tprinted.\n\n"

"-F, --force\t\tProceed even if another ftrace tracer is active. Without\n"
"\t\t\tthis option, the program will refuse to start tracing if\n"
"\t\t\tany other tracer than the nop tracer is active.\n\n"

"-s, --threshold TH\tConfigure ftrace to use a threshold of TH microseconds\n"
"\t\t\tfor the tracer. The default is 0, which means that\n"
"\t\t\ttracing_max_latency will be used. tracing_max_latency is\n"
"\t\t\tset to 0 when the program is started and contains the\n"
"\t\t\tmaximum of the latencies that have been encountered.\n\n"

"-f, --function\t\tEnable the function-trace option in trace_options. With\n"
"\t\t\tthis option, ftrace will trace the functions that are\n"
"\t\t\texecuted during a latency, without it we only get the\n"
"\t\t\tbeginning, end, and backtrace.\n\n"

"-g, --graph\t\tEnable the display-graph option in trace_option. This\n"
"\t\t\toption causes ftrace to show the graph of how functions\n"
"\t\t\tare calling other functions.\n\n"

"-c, --policy POL\tRun the program with scheduling policy POL. POL can be\n"
"\t\t\tother, batch, idle, rr or fifo. The default is rr. When\n"
"\t\t\tusing rr or fifo, remember that these policies may cause\n"
"\t\t\tother tasks to experience latencies.\n\n"

"-p, --priority PRI\tRun the program with priority PRI. The acceptable range\n"
"\t\t\tof PRI depends on the scheduling policy.\n\n"

"-n, --notrace\t\tIf latency is detected, do not print out the content of\n"
"\t\t\tthe trace file to standard output\n\n"

"-t, --threads NRTHR\tRun NRTHR threads for printing. Default is %d.\n\n"

"-r, --random\t\tArbitrarily sleep a certain amount of time, default\n"
"\t\t\t%ld ms, before reading the trace file. The\n"
"\t\t\tprobabilities for sleep are chosen so that the\n"
"\t\t\tprobability of obtaining any of a cluster of closely\n"
"\t\t\toccurring latencies are equal, i.e. we will randomly\n"
"\t\t\tchoose which one we collect from the trace file.\n\n"
"\t\t\tThis option is probably only useful with the irqsoff,\n"
"\t\t\tpreemptoff, and preemptirqsoff tracers.\n\n"

"-a, --nrlat NRLAT\tFor the purpose of arbitrary delay, assume that there\n"
"\t\t\tare no more than NRLAT clustered latencies. If NRLAT\n"
"\t\t\tlatencies are detected during a run, this value will\n"
"\t\t\tautomatically be increased to NRLAT + 1 and then to\n"
"\t\t\tNRLAT + 2 and so on. The default is %d. This option\n"
"\t\t\timplies -r. We need to know this number in order to\n"
"\t\t\tbe able to calculate the probabilities of sleeping.\n"
"\t\t\tSpecifically, the probabilities of not sleeping, i.e. to\n"
"\t\t\tdo an immediate printout will be:\n\n"
"\t\t\t1/NRLAT  1/(NRLAT - 1) ... 1/3  1/2  1\n\n"
"\t\t\tThe probability of sleeping will be:\n\n"
"\t\t\t1 - P, where P is from the series above\n\n"
"\t\t\tThis descending probability will cause us to choose\n"
"\t\t\tan occurrence at random. Observe that the final\n"
"\t\t\tprobability is 0, it is when we reach this probability\n"
"\t\t\tthat we increase NRLAT automatically. As an example,\n"
"\t\t\twith the default value of 2, the probabilities will be:\n\n"
"\t\t\t1/2  0\n\n"
"\t\t\tThis means, when a latency is detected we will sleep\n"
"\t\t\twith 50%% probability. If we ever detect another latency\n"
"\t\t\tduring the sleep period, then the probability of sleep\n"
"\t\t\twill be 0%% and the table will be expanded to:\n\n"
"\t\t\t1/3  1/2  0\n\n"

"-v, --verbose\t\tIncrease the verbosity. If this option is given once,\n"
"\t\t\tthen print a message every time that the NRLAT value\n"
"\t\t\tis automatically increased. It also causes a message to\n"
"\t\t\tbe printed when the ftrace settings are changed. If this\n"
"\t\t\toption is given at least twice, then also print a\n"
"\t\t\twarning for lost events.\n\n"

"-u, --time TIME\t\tArbitrarily sleep for a specified time TIME ms before\n"
"\t\t\tprinting out the trace from the trace file. The default\n"
"\t\t\tis %ld ms. This option implies -r.\n\n"

"-x, --no-ftrace\t\tDo not configure ftrace. This assume that the user\n"
"\t\t\tconfigures the ftrace files in sysfs such as\n"
"\t\t\t/sys/kernel/tracing/current_tracer or equivalent.\n\n"

"-i, --tracefile FILE\tUse FILE as trace file. The default is\n"
"\t\t\t%s.\n"
"\t\t\tThis options implies -x\n\n"

"-m, --max-lat FILE\tUse FILE as tracing_max_latency file. The default is\n"
"\t\t\t%s.\n"
"\t\t\tThis options implies -x\n\n"
,
prg_name, debug_tracefile_dflt, debug_maxlat_dflt, DEFAULT_NR_PRINTER_THREADS,
SLEEP_TIME_MS_DEFAULT, DEFAULT_TABLE_SIZE, SLEEP_TIME_MS_DEFAULT,
debug_tracefile_dflt, debug_maxlat_dflt);
}

static void find_tracefiles(void)
{
	debug_tracefile_dflt = tracefs_get_tracing_file("trace");
	if (debug_tracefile_dflt == NULL) {
		/* This is needed in show_usage() */
		debug_tracefile_dflt = DEBUG_NOFILE;
	}

	debug_maxlat_dflt = tracefs_get_tracing_file("tracing_max_latency");
	if (debug_maxlat_dflt == NULL) {
		/* This is needed in show_usage() */
		debug_maxlat_dflt = DEBUG_NOFILE;
	}

	debug_tracefile = debug_tracefile_dflt;
	debug_maxlat = debug_maxlat_dflt;
}

bool alldigits(const char *s)
{
	for (; *s != '\0'; s++)
		if (!isdigit(*s))
			return false;
	return true;
}

void check_alldigits(const char *optarg, const char *argname)
{
	if (!alldigits(optarg))
		errx(EXIT_FAILURE,
		     "The %s parameter expects a decimal argument\n", argname);
}

static void scan_arguments(int argc, char *argv[])
{
	int c;
	int i;
	int option_idx = 0;

	static struct option long_options[] = {
		{ "list",       no_argument,            0, 'l' },
		{ "tracer",	required_argument,	0, 't' },
		{ "force",      no_argument,            0, 'F' },
		{ "threshold",  required_argument,      0, 's' },
		{ "function",   no_argument,            0, 'f' },
		{ "graph",      no_argument,            0, 'g' },
		{ "policy",	required_argument,	0, 'c' },
		{ "priority",	required_argument,	0, 'p' },
		{ "help",	no_argument,		0, 'h' },
		{ "notrace",	no_argument,		0, 'n' },
		{ "random",	no_argument,		0, 'r' },
		{ "nrlat",	required_argument,	0, 'a' },
		{ "threads",	required_argument,	0, 'e' },
		{ "time",	required_argument,	0, 'u' },
		{ "verbose",	no_argument,		0, 'v' },
		{ "no-ftrace",  no_argument,            0, 'x' },
		{ "tracefile",	required_argument,	0, 'i' },
		{ "max-lat",	required_argument,	0, 'm' },
		{ 0,		0,			0,  0  }
	};
	const struct policy *p;
	int max, min;
	int value;
	bool notracer, valid;

	/*
	 * We must do this before parsing the arguments because show_usage()
	 * needs to display these.
	 */
	find_tracefiles();

	while (true) {
		c = getopt_long(argc, argv, "lt:Fs:fgc:p:hnra:e:u:vxi:m:",
				long_options, &option_idx);
		if (c == -1)
			break;

		switch (c) {
		case 'l':
			show_available();
			exit(0);
			break;
		case 't':
			current_tracer = strdup(optarg);
			if (!is_relevant_tracer(current_tracer)) {
				warnx("%s is not a known latency tracer!\n",
				      current_tracer);
			}
			valid = tracer_valid(current_tracer, &notracer);
			if (notracer)
				errx(EXIT_FAILURE, no_tracer_msg);
			if (!valid)
				errx(EXIT_FAILURE,
"The tracer %s is not supported by your kernel!\n", current_tracer);
			break;
		case 'F':
			force_tracer = true;
			break;
		case 's':
			check_alldigits(optarg, "-s [--threshold]");
			threshold = strdup(optarg);
			break;
		case 'f':
			use_options[OPTIDX_FUNC_TR] = true;
			break;
		case 'g':
			use_options[OPTIDX_DISP_GR] = true;
			break;
		case 'c':
			p = policy_from_name(optarg);
			if (p != NULL) {
				sched_policy = p->policy;
				sched_policy_set = true;
				if (!sched_pri_set) {
					sched_pri = p->default_pri;
					sched_pri_set = true;
				}
			} else {
				warnx("Unknown scheduling %s\n", optarg);
				show_usage();
				exit(0);
			}
			break;
		case 'p':
			check_alldigits(optarg, "-p [--priority]");
			sched_pri = atoi(optarg);
			sched_pri_set = true;
			break;
		case 'h':
			show_usage();
			exit(0);
			break;
		case 'n':
			trace_enable = false;
			use_random_sleep = false;
			break;
		case 'e':
			check_alldigits(optarg, "-e [--threads]");
			value = atoi(optarg);
			if (value > 0)
				nr_threads = value;
			else {
				warnx("NRTHR must be > 0\n");
				show_usage();
				exit(0);
			}
			break;
		case 'u':
			check_alldigits(optarg, "-u [--time]");
			value = atoi(optarg);
			if (value < 0) {
				warnx("TIME must be >= 0\n");
				show_usage();
				exit(0);
			}
			trace_enable = true;
			use_random_sleep = true;
			sleep_time = value * USEC_PER_MSEC;
			break;
		case 'v':
			verbosity++;
			break;
		case 'r':
			trace_enable = true;
			use_random_sleep = true;
			break;
		case 'a':
			check_alldigits(optarg, "-a [--nrlat]");
			value = atoi(optarg);
			if (value <= 0) {
				warnx("NRLAT must be > 0\n");
				show_usage();
				exit(0);
			}
			trace_enable = true;
			use_random_sleep = true;
			table_startsize = value;
			break;
		case 'x':
			setup_ftrace = false;
			break;
		case 'i':
			setup_ftrace = false;
			debug_tracefile = strdup(optarg);
			break;
		case 'm':
			setup_ftrace = false;
			debug_maxlat = strdup(optarg);
			break;
		default:
			show_usage();
			exit(0);
			break;
		}
	}

	if (setup_ftrace) {
		if (!current_tracer) {
			current_tracer = find_default_tracer();
			if (!current_tracer)
				errx(EXIT_FAILURE,
"No default tracer found and tracer not specified\n");
		}

		if (use_random_sleep && !random_makes_sense(current_tracer)) {
			warnx("WARNING: The tracer is %s and random sleep has",
			      current_tracer);
			fprintf(stderr,
"been enabled. Random sleep is intended for the following tracers:\n");
			for (i = 0; random_tracers[i]; i++)
				fprintf(stderr, "%s\n", random_tracers[i]);
			fprintf(stderr, "\n");
		}
	}

	if (debug_tracefile == DEBUG_NOFILE ||
	    debug_maxlat == DEBUG_NOFILE)
		errx(EXIT_FAILURE,
"Could not find tracing directory e.g. /sys/kernel/tracing\n");

	if (!sched_policy_set) {
		sched_policy = SCHED_RR;
		sched_policy_set = true;
		if (!sched_pri_set) {
			sched_pri = RT_DEFAULT_PRI;
			sched_pri_set = true;
		}
	}

	max = sched_get_priority_max(sched_policy);
	min = sched_get_priority_min(sched_policy);

	if (sched_pri < min) {
		printf(
"ATTENTION: Increasing priority to minimum, which is %d\n", min);
		sched_pri = min;
	}
	if (sched_pri > max) {
		printf(
"ATTENTION: Reducing priority to maximum, which is %d\n", max);
		sched_pri = max;
	}
}

static void show_params(void)
{
	printf(
"\n"
"Running with scheduling policy %s and priority %d. Using %d print threads.\n",
		policy_name(sched_policy), sched_pri, nr_threads);
	if (trace_enable) {
		if (use_random_sleep) {
			printf(
"%s will be printed with random delay\n"
"Start size of the probability table:\t\t\t%d\n"
"Print a message when the prob. table changes size:\t%s\n"
"Print a warning when an event has been lost:\t\t%s\n"
"Sleep time is:\t\t\t\t\t\t%ld ms\n",
debug_tracefile,
table_startsize,
bool2str(verbose_sizechange()),
bool2str(verbose_lostevent()),
sleep_time / USEC_PER_MSEC);
		} else {
			printf("%s will be printed immediately\n",
			       debug_tracefile);
		}
	} else {
		printf("%s will not be printed\n",
		       debug_tracefile);
	}
	if (setup_ftrace) {
		printf("Tracer:\t\t\t\t\t\t\t%s\n"
		       "%s option:\t\t\t\t\t%s\n"
		       "%s option:\t\t\t\t\t%s\n",
		       current_tracer,
		       optstr[OPTIDX_FUNC_TR],
		       bool2str(use_options[OPTIDX_FUNC_TR]),
		       optstr[OPTIDX_DISP_GR],
		       bool2str(use_options[OPTIDX_DISP_GR]));
		if (!strcmp(threshold, "0"))
			printf("Threshold:\t\t\t\t\t\ttracing_max_latency\n");
		else
			printf("Threshold:\t\t\t\t\t\t%s\n", threshold);
	}
	printf("\n");
}

int main(int argc, char *argv[])
{
	init_save_state();
	signal_blocking(SIG_BLOCK);
	setup_sig_handler();
	open_stdout();

	if (argc >= 1)
		prg_name = argv[0];
	else
		prg_name = prg_unknown;

	scan_arguments(argc, argv);
	show_params();

	init_printstate();
	init_print_mtx();
	if (use_random_sleep) {
		init_probabilities();
		if (verbose_sizechange())
			printf("Initializing probability table to %d\n",
			       table_startsize);
		sleeptable_resize(table_startsize, false, NULL);
	}
	set_priority();
	init_queue(&printqueue);
	start_printthread();
	tracing_loop();
	return 0;
}
