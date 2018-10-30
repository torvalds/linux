/*
 * This application is Copyright 2012 Red Hat, Inc.
 *	Doug Ledford <dledford@redhat.com>
 *
 * mq_perf_tests is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * mq_perf_tests is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * For the full text of the license, see <http://www.gnu.org/licenses/>.
 *
 * mq_perf_tests.c
 *   Tests various types of message queue workloads, concentrating on those
 *   situations that invole large message sizes, large message queue depths,
 *   or both, and reports back useful metrics about kernel message queue
 *   performance.
 *
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <popt.h>
#include <error.h>

#include "../kselftest.h"

static char *usage =
"Usage:\n"
"  %s [-c #[,#..] -f] path\n"
"\n"
"	-c #	Skip most tests and go straight to a high queue depth test\n"
"		and then run that test continuously (useful for running at\n"
"		the same time as some other workload to see how much the\n"
"		cache thrashing caused by adding messages to a very deep\n"
"		queue impacts the performance of other programs).  The number\n"
"		indicates which CPU core we should bind the process to during\n"
"		the run.  If you have more than one physical CPU, then you\n"
"		will need one copy per physical CPU package, and you should\n"
"		specify the CPU cores to pin ourself to via a comma separated\n"
"		list of CPU values.\n"
"	-f	Only usable with continuous mode.  Pin ourself to the CPUs\n"
"		as requested, then instead of looping doing a high mq\n"
"		workload, just busy loop.  This will allow us to lock up a\n"
"		single CPU just like we normally would, but without actually\n"
"		thrashing the CPU cache.  This is to make it easier to get\n"
"		comparable numbers from some other workload running on the\n"
"		other CPUs.  One set of numbers with # CPUs locked up running\n"
"		an mq workload, and another set of numbers with those same\n"
"		CPUs locked away from the test workload, but not doing\n"
"		anything to trash the cache like the mq workload might.\n"
"	path	Path name of the message queue to create\n"
"\n"
"	Note: this program must be run as root in order to enable all tests\n"
"\n";

char *MAX_MSGS = "/proc/sys/fs/mqueue/msg_max";
char *MAX_MSGSIZE = "/proc/sys/fs/mqueue/msgsize_max";

#define min(a, b) ((a) < (b) ? (a) : (b))
#define MAX_CPUS 64
char *cpu_option_string;
int cpus_to_pin[MAX_CPUS];
int num_cpus_to_pin;
pthread_t cpu_threads[MAX_CPUS];
pthread_t main_thread;
cpu_set_t *cpu_set;
int cpu_set_size;
int cpus_online;

#define MSG_SIZE 16
#define TEST1_LOOPS 10000000
#define TEST2_LOOPS 100000
int continuous_mode;
int continuous_mode_fake;

struct rlimit saved_limits, cur_limits;
int saved_max_msgs, saved_max_msgsize;
int cur_max_msgs, cur_max_msgsize;
FILE *max_msgs, *max_msgsize;
int cur_nice;
char *queue_path = "/mq_perf_tests";
mqd_t queue = -1;
struct mq_attr result;
int mq_prio_max;

const struct poptOption options[] = {
	{
		.longName = "continuous",
		.shortName = 'c',
		.argInfo = POPT_ARG_STRING,
		.arg = &cpu_option_string,
		.val = 'c',
		.descrip = "Run continuous tests at a high queue depth in "
			"order to test the effects of cache thrashing on "
			"other tasks on the system.  This test is intended "
			"to be run on one core of each physical CPU while "
			"some other CPU intensive task is run on all the other "
			"cores of that same physical CPU and the other task "
			"is timed.  It is assumed that the process of adding "
			"messages to the message queue in a tight loop will "
			"impact that other task to some degree.  Once the "
			"tests are performed in this way, you should then "
			"re-run the tests using fake mode in order to check "
			"the difference in time required to perform the CPU "
			"intensive task",
		.argDescrip = "cpu[,cpu]",
	},
	{
		.longName = "fake",
		.shortName = 'f',
		.argInfo = POPT_ARG_NONE,
		.arg = &continuous_mode_fake,
		.val = 0,
		.descrip = "Tie up the CPUs that we would normally tie up in"
			"continuous mode, but don't actually do any mq stuff, "
			"just keep the CPU busy so it can't be used to process "
			"system level tasks as this would free up resources on "
			"the other CPU cores and skew the comparison between "
			"the no-mqueue work and mqueue work tests",
		.argDescrip = NULL,
	},
	{
		.longName = "path",
		.shortName = 'p',
		.argInfo = POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,
		.arg = &queue_path,
		.val = 'p',
		.descrip = "The name of the path to use in the mqueue "
			"filesystem for our tests",
		.argDescrip = "pathname",
	},
	POPT_AUTOHELP
	POPT_TABLEEND
};

static inline void __set(FILE *stream, int value, char *err_msg);
void shutdown(int exit_val, char *err_cause, int line_no);
void sig_action_SIGUSR1(int signum, siginfo_t *info, void *context);
void sig_action(int signum, siginfo_t *info, void *context);
static inline int get(FILE *stream);
static inline void set(FILE *stream, int value);
static inline int try_set(FILE *stream, int value);
static inline void getr(int type, struct rlimit *rlim);
static inline void setr(int type, struct rlimit *rlim);
static inline void open_queue(struct mq_attr *attr);
void increase_limits(void);

static inline void __set(FILE *stream, int value, char *err_msg)
{
	rewind(stream);
	if (fprintf(stream, "%d", value) < 0)
		perror(err_msg);
}


void shutdown(int exit_val, char *err_cause, int line_no)
{
	static int in_shutdown = 0;
	int errno_at_shutdown = errno;
	int i;

	/* In case we get called by multiple threads or from an sighandler */
	if (in_shutdown++)
		return;

	for (i = 0; i < num_cpus_to_pin; i++)
		if (cpu_threads[i]) {
			pthread_kill(cpu_threads[i], SIGUSR1);
			pthread_join(cpu_threads[i], NULL);
		}

	if (queue != -1)
		if (mq_close(queue))
			perror("mq_close() during shutdown");
	if (queue_path)
		/*
		 * Be silent if this fails, if we cleaned up already it's
		 * expected to fail
		 */
		mq_unlink(queue_path);
	if (saved_max_msgs)
		__set(max_msgs, saved_max_msgs,
		      "failed to restore saved_max_msgs");
	if (saved_max_msgsize)
		__set(max_msgsize, saved_max_msgsize,
		      "failed to restore saved_max_msgsize");
	if (exit_val)
		error(exit_val, errno_at_shutdown, "%s at %d",
		      err_cause, line_no);
	exit(0);
}

void sig_action_SIGUSR1(int signum, siginfo_t *info, void *context)
{
	if (pthread_self() != main_thread)
		pthread_exit(0);
	else {
		fprintf(stderr, "Caught signal %d in SIGUSR1 handler, "
				"exiting\n", signum);
		shutdown(0, "", 0);
		fprintf(stderr, "\n\nReturned from shutdown?!?!\n\n");
		exit(0);
	}
}

void sig_action(int signum, siginfo_t *info, void *context)
{
	if (pthread_self() != main_thread)
		pthread_kill(main_thread, signum);
	else {
		fprintf(stderr, "Caught signal %d, exiting\n", signum);
		shutdown(0, "", 0);
		fprintf(stderr, "\n\nReturned from shutdown?!?!\n\n");
		exit(0);
	}
}

static inline int get(FILE *stream)
{
	int value;
	rewind(stream);
	if (fscanf(stream, "%d", &value) != 1)
		shutdown(4, "Error reading /proc entry", __LINE__);
	return value;
}

static inline void set(FILE *stream, int value)
{
	int new_value;

	rewind(stream);
	if (fprintf(stream, "%d", value) < 0)
		return shutdown(5, "Failed writing to /proc file", __LINE__);
	new_value = get(stream);
	if (new_value != value)
		return shutdown(5, "We didn't get what we wrote to /proc back",
				__LINE__);
}

static inline int try_set(FILE *stream, int value)
{
	int new_value;

	rewind(stream);
	fprintf(stream, "%d", value);
	new_value = get(stream);
	return new_value == value;
}

static inline void getr(int type, struct rlimit *rlim)
{
	if (getrlimit(type, rlim))
		shutdown(6, "getrlimit()", __LINE__);
}

static inline void setr(int type, struct rlimit *rlim)
{
	if (setrlimit(type, rlim))
		shutdown(7, "setrlimit()", __LINE__);
}

/**
 * open_queue - open the global queue for testing
 * @attr - An attr struct specifying the desired queue traits
 * @result - An attr struct that lists the actual traits the queue has
 *
 * This open is not allowed to fail, failure will result in an orderly
 * shutdown of the program.  The global queue_path is used to set what
 * queue to open, the queue descriptor is saved in the global queue
 * variable.
 */
static inline void open_queue(struct mq_attr *attr)
{
	int flags = O_RDWR | O_EXCL | O_CREAT | O_NONBLOCK;
	int perms = DEFFILEMODE;

	queue = mq_open(queue_path, flags, perms, attr);
	if (queue == -1)
		shutdown(1, "mq_open()", __LINE__);
	if (mq_getattr(queue, &result))
		shutdown(1, "mq_getattr()", __LINE__);
	printf("\n\tQueue %s created:\n", queue_path);
	printf("\t\tmq_flags:\t\t\t%s\n", result.mq_flags & O_NONBLOCK ?
	       "O_NONBLOCK" : "(null)");
	printf("\t\tmq_maxmsg:\t\t\t%lu\n", result.mq_maxmsg);
	printf("\t\tmq_msgsize:\t\t\t%lu\n", result.mq_msgsize);
	printf("\t\tmq_curmsgs:\t\t\t%lu\n", result.mq_curmsgs);
}

void *fake_cont_thread(void *arg)
{
	int i;

	for (i = 0; i < num_cpus_to_pin; i++)
		if (cpu_threads[i] == pthread_self())
			break;
	printf("\tStarted fake continuous mode thread %d on CPU %d\n", i,
	       cpus_to_pin[i]);
	while (1)
		;
}

void *cont_thread(void *arg)
{
	char buff[MSG_SIZE];
	int i, priority;

	for (i = 0; i < num_cpus_to_pin; i++)
		if (cpu_threads[i] == pthread_self())
			break;
	printf("\tStarted continuous mode thread %d on CPU %d\n", i,
	       cpus_to_pin[i]);
	while (1) {
		while (mq_send(queue, buff, sizeof(buff), 0) == 0)
			;
		mq_receive(queue, buff, sizeof(buff), &priority);
	}
}

#define drain_queue() \
	while (mq_receive(queue, buff, MSG_SIZE, &prio_in) == MSG_SIZE)

#define do_untimed_send() \
	do { \
		if (mq_send(queue, buff, MSG_SIZE, prio_out)) \
			shutdown(3, "Test send failure", __LINE__); \
	} while (0)

#define do_send_recv() \
	do { \
		clock_gettime(clock, &start); \
		if (mq_send(queue, buff, MSG_SIZE, prio_out)) \
			shutdown(3, "Test send failure", __LINE__); \
		clock_gettime(clock, &middle); \
		if (mq_receive(queue, buff, MSG_SIZE, &prio_in) != MSG_SIZE) \
			shutdown(3, "Test receive failure", __LINE__); \
		clock_gettime(clock, &end); \
		nsec = ((middle.tv_sec - start.tv_sec) * 1000000000) + \
			(middle.tv_nsec - start.tv_nsec); \
		send_total.tv_nsec += nsec; \
		if (send_total.tv_nsec >= 1000000000) { \
			send_total.tv_sec++; \
			send_total.tv_nsec -= 1000000000; \
		} \
		nsec = ((end.tv_sec - middle.tv_sec) * 1000000000) + \
			(end.tv_nsec - middle.tv_nsec); \
		recv_total.tv_nsec += nsec; \
		if (recv_total.tv_nsec >= 1000000000) { \
			recv_total.tv_sec++; \
			recv_total.tv_nsec -= 1000000000; \
		} \
	} while (0)

struct test {
	char *desc;
	void (*func)(int *);
};

void const_prio(int *prio)
{
	return;
}

void inc_prio(int *prio)
{
	if (++*prio == mq_prio_max)
		*prio = 0;
}

void dec_prio(int *prio)
{
	if (--*prio < 0)
		*prio = mq_prio_max - 1;
}

void random_prio(int *prio)
{
	*prio = random() % mq_prio_max;
}

struct test test2[] = {
	{"\n\tTest #2a: Time send/recv message, queue full, constant prio\n",
		const_prio},
	{"\n\tTest #2b: Time send/recv message, queue full, increasing prio\n",
		inc_prio},
	{"\n\tTest #2c: Time send/recv message, queue full, decreasing prio\n",
		dec_prio},
	{"\n\tTest #2d: Time send/recv message, queue full, random prio\n",
		random_prio},
	{NULL, NULL}
};

/**
 * Tests to perform (all done with MSG_SIZE messages):
 *
 * 1) Time to add/remove message with 0 messages on queue
 * 1a) with constant prio
 * 2) Time to add/remove message when queue close to capacity:
 * 2a) with constant prio
 * 2b) with increasing prio
 * 2c) with decreasing prio
 * 2d) with random prio
 * 3) Test limits of priorities honored (double check _SC_MQ_PRIO_MAX)
 */
void *perf_test_thread(void *arg)
{
	char buff[MSG_SIZE];
	int prio_out, prio_in;
	int i;
	clockid_t clock;
	pthread_t *t;
	struct timespec res, start, middle, end, send_total, recv_total;
	unsigned long long nsec;
	struct test *cur_test;

	t = &cpu_threads[0];
	printf("\n\tStarted mqueue performance test thread on CPU %d\n",
	       cpus_to_pin[0]);
	mq_prio_max = sysconf(_SC_MQ_PRIO_MAX);
	if (mq_prio_max == -1)
		shutdown(2, "sysconf(_SC_MQ_PRIO_MAX)", __LINE__);
	if (pthread_getcpuclockid(cpu_threads[0], &clock) != 0)
		shutdown(2, "pthread_getcpuclockid", __LINE__);

	if (clock_getres(clock, &res))
		shutdown(2, "clock_getres()", __LINE__);

	printf("\t\tMax priorities:\t\t\t%d\n", mq_prio_max);
	printf("\t\tClock resolution:\t\t%lu nsec%s\n", res.tv_nsec,
	       res.tv_nsec > 1 ? "s" : "");



	printf("\n\tTest #1: Time send/recv message, queue empty\n");
	printf("\t\t(%d iterations)\n", TEST1_LOOPS);
	prio_out = 0;
	send_total.tv_sec = 0;
	send_total.tv_nsec = 0;
	recv_total.tv_sec = 0;
	recv_total.tv_nsec = 0;
	for (i = 0; i < TEST1_LOOPS; i++)
		do_send_recv();
	printf("\t\tSend msg:\t\t\t%ld.%lus total time\n",
	       send_total.tv_sec, send_total.tv_nsec);
	nsec = ((unsigned long long)send_total.tv_sec * 1000000000 +
		 send_total.tv_nsec) / TEST1_LOOPS;
	printf("\t\t\t\t\t\t%lld nsec/msg\n", nsec);
	printf("\t\tRecv msg:\t\t\t%ld.%lus total time\n",
	       recv_total.tv_sec, recv_total.tv_nsec);
	nsec = ((unsigned long long)recv_total.tv_sec * 1000000000 +
		recv_total.tv_nsec) / TEST1_LOOPS;
	printf("\t\t\t\t\t\t%lld nsec/msg\n", nsec);


	for (cur_test = test2; cur_test->desc != NULL; cur_test++) {
		printf("%s:\n", cur_test->desc);
		printf("\t\t(%d iterations)\n", TEST2_LOOPS);
		prio_out = 0;
		send_total.tv_sec = 0;
		send_total.tv_nsec = 0;
		recv_total.tv_sec = 0;
		recv_total.tv_nsec = 0;
		printf("\t\tFilling queue...");
		fflush(stdout);
		clock_gettime(clock, &start);
		for (i = 0; i < result.mq_maxmsg - 1; i++) {
			do_untimed_send();
			cur_test->func(&prio_out);
		}
		clock_gettime(clock, &end);
		nsec = ((unsigned long long)(end.tv_sec - start.tv_sec) *
			1000000000) + (end.tv_nsec - start.tv_nsec);
		printf("done.\t\t%lld.%llds\n", nsec / 1000000000,
		       nsec % 1000000000);
		printf("\t\tTesting...");
		fflush(stdout);
		for (i = 0; i < TEST2_LOOPS; i++) {
			do_send_recv();
			cur_test->func(&prio_out);
		}
		printf("done.\n");
		printf("\t\tSend msg:\t\t\t%ld.%lus total time\n",
		       send_total.tv_sec, send_total.tv_nsec);
		nsec = ((unsigned long long)send_total.tv_sec * 1000000000 +
			 send_total.tv_nsec) / TEST2_LOOPS;
		printf("\t\t\t\t\t\t%lld nsec/msg\n", nsec);
		printf("\t\tRecv msg:\t\t\t%ld.%lus total time\n",
		       recv_total.tv_sec, recv_total.tv_nsec);
		nsec = ((unsigned long long)recv_total.tv_sec * 1000000000 +
			recv_total.tv_nsec) / TEST2_LOOPS;
		printf("\t\t\t\t\t\t%lld nsec/msg\n", nsec);
		printf("\t\tDraining queue...");
		fflush(stdout);
		clock_gettime(clock, &start);
		drain_queue();
		clock_gettime(clock, &end);
		nsec = ((unsigned long long)(end.tv_sec - start.tv_sec) *
			1000000000) + (end.tv_nsec - start.tv_nsec);
		printf("done.\t\t%lld.%llds\n", nsec / 1000000000,
		       nsec % 1000000000);
	}
	return 0;
}

void increase_limits(void)
{
	cur_limits.rlim_cur = RLIM_INFINITY;
	cur_limits.rlim_max = RLIM_INFINITY;
	setr(RLIMIT_MSGQUEUE, &cur_limits);
	while (try_set(max_msgs, cur_max_msgs += 10))
		;
	cur_max_msgs = get(max_msgs);
	while (try_set(max_msgsize, cur_max_msgsize += 1024))
		;
	cur_max_msgsize = get(max_msgsize);
	if (setpriority(PRIO_PROCESS, 0, -20) != 0)
		shutdown(2, "setpriority()", __LINE__);
	cur_nice = -20;
}

int main(int argc, char *argv[])
{
	struct mq_attr attr;
	char *option, *next_option;
	int i, cpu, rc;
	struct sigaction sa;
	poptContext popt_context;
	void *retval;

	main_thread = pthread_self();
	num_cpus_to_pin = 0;

	if (sysconf(_SC_NPROCESSORS_ONLN) == -1) {
		perror("sysconf(_SC_NPROCESSORS_ONLN)");
		exit(1);
	}
	cpus_online = min(MAX_CPUS, sysconf(_SC_NPROCESSORS_ONLN));
	cpu_set = CPU_ALLOC(cpus_online);
	if (cpu_set == NULL) {
		perror("CPU_ALLOC()");
		exit(1);
	}
	cpu_set_size = CPU_ALLOC_SIZE(cpus_online);
	CPU_ZERO_S(cpu_set_size, cpu_set);

	popt_context = poptGetContext(NULL, argc, (const char **)argv,
				      options, 0);

	while ((rc = poptGetNextOpt(popt_context)) > 0) {
		switch (rc) {
		case 'c':
			continuous_mode = 1;
			option = cpu_option_string;
			do {
				next_option = strchr(option, ',');
				if (next_option)
					*next_option = '\0';
				cpu = atoi(option);
				if (cpu >= cpus_online)
					fprintf(stderr, "CPU %d exceeds "
						"cpus online, ignoring.\n",
						cpu);
				else
					cpus_to_pin[num_cpus_to_pin++] = cpu;
				if (next_option)
					option = ++next_option;
			} while (next_option && num_cpus_to_pin < MAX_CPUS);
			/* Double check that they didn't give us the same CPU
			 * more than once */
			for (cpu = 0; cpu < num_cpus_to_pin; cpu++) {
				if (CPU_ISSET_S(cpus_to_pin[cpu], cpu_set_size,
						cpu_set)) {
					fprintf(stderr, "Any given CPU may "
						"only be given once.\n");
					exit(1);
				} else
					CPU_SET_S(cpus_to_pin[cpu],
						  cpu_set_size, cpu_set);
			}
			break;
		case 'p':
			/*
			 * Although we can create a msg queue with a
			 * non-absolute path name, unlink will fail.  So,
			 * if the name doesn't start with a /, add one
			 * when we save it.
			 */
			option = queue_path;
			if (*option != '/') {
				queue_path = malloc(strlen(option) + 2);
				if (!queue_path) {
					perror("malloc()");
					exit(1);
				}
				queue_path[0] = '/';
				queue_path[1] = 0;
				strcat(queue_path, option);
				free(option);
			}
			break;
		}
	}

	if (continuous_mode && num_cpus_to_pin == 0) {
		fprintf(stderr, "Must pass at least one CPU to continuous "
			"mode.\n");
		poptPrintUsage(popt_context, stderr, 0);
		exit(1);
	} else if (!continuous_mode) {
		num_cpus_to_pin = 1;
		cpus_to_pin[0] = cpus_online - 1;
	}

	if (getuid() != 0)
		ksft_exit_skip("Not running as root, but almost all tests "
			"require root in order to modify\nsystem settings.  "
			"Exiting.\n");

	max_msgs = fopen(MAX_MSGS, "r+");
	max_msgsize = fopen(MAX_MSGSIZE, "r+");
	if (!max_msgs)
		shutdown(2, "Failed to open msg_max", __LINE__);
	if (!max_msgsize)
		shutdown(2, "Failed to open msgsize_max", __LINE__);

	/* Load up the current system values for everything we can */
	getr(RLIMIT_MSGQUEUE, &saved_limits);
	cur_limits = saved_limits;
	saved_max_msgs = cur_max_msgs = get(max_msgs);
	saved_max_msgsize = cur_max_msgsize = get(max_msgsize);
	errno = 0;
	cur_nice = getpriority(PRIO_PROCESS, 0);
	if (errno)
		shutdown(2, "getpriority()", __LINE__);

	/* Tell the user our initial state */
	printf("\nInitial system state:\n");
	printf("\tUsing queue path:\t\t\t%s\n", queue_path);
	printf("\tRLIMIT_MSGQUEUE(soft):\t\t\t%ld\n",
		(long) saved_limits.rlim_cur);
	printf("\tRLIMIT_MSGQUEUE(hard):\t\t\t%ld\n",
		(long) saved_limits.rlim_max);
	printf("\tMaximum Message Size:\t\t\t%d\n", saved_max_msgsize);
	printf("\tMaximum Queue Size:\t\t\t%d\n", saved_max_msgs);
	printf("\tNice value:\t\t\t\t%d\n", cur_nice);
	printf("\n");

	increase_limits();

	printf("Adjusted system state for testing:\n");
	if (cur_limits.rlim_cur == RLIM_INFINITY) {
		printf("\tRLIMIT_MSGQUEUE(soft):\t\t\t(unlimited)\n");
		printf("\tRLIMIT_MSGQUEUE(hard):\t\t\t(unlimited)\n");
	} else {
		printf("\tRLIMIT_MSGQUEUE(soft):\t\t\t%ld\n",
		       (long) cur_limits.rlim_cur);
		printf("\tRLIMIT_MSGQUEUE(hard):\t\t\t%ld\n",
		       (long) cur_limits.rlim_max);
	}
	printf("\tMaximum Message Size:\t\t\t%d\n", cur_max_msgsize);
	printf("\tMaximum Queue Size:\t\t\t%d\n", cur_max_msgs);
	printf("\tNice value:\t\t\t\t%d\n", cur_nice);
	printf("\tContinuous mode:\t\t\t(%s)\n", continuous_mode ?
	       (continuous_mode_fake ? "fake mode" : "enabled") :
	       "disabled");
	printf("\tCPUs to pin:\t\t\t\t%d", cpus_to_pin[0]);
	for (cpu = 1; cpu < num_cpus_to_pin; cpu++)
			printf(",%d", cpus_to_pin[cpu]);
	printf("\n");

	sa.sa_sigaction = sig_action_SIGUSR1;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGHUP);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGQUIT);
	sigaddset(&sa.sa_mask, SIGTERM);
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGUSR1, &sa, NULL) == -1)
		shutdown(1, "sigaction(SIGUSR1)", __LINE__);
	sa.sa_sigaction = sig_action;
	if (sigaction(SIGHUP, &sa, NULL) == -1)
		shutdown(1, "sigaction(SIGHUP)", __LINE__);
	if (sigaction(SIGINT, &sa, NULL) == -1)
		shutdown(1, "sigaction(SIGINT)", __LINE__);
	if (sigaction(SIGQUIT, &sa, NULL) == -1)
		shutdown(1, "sigaction(SIGQUIT)", __LINE__);
	if (sigaction(SIGTERM, &sa, NULL) == -1)
		shutdown(1, "sigaction(SIGTERM)", __LINE__);

	if (!continuous_mode_fake) {
		attr.mq_flags = O_NONBLOCK;
		attr.mq_maxmsg = cur_max_msgs;
		attr.mq_msgsize = MSG_SIZE;
		open_queue(&attr);
	}
	for (i = 0; i < num_cpus_to_pin; i++) {
		pthread_attr_t thread_attr;
		void *thread_func;

		if (continuous_mode_fake)
			thread_func = &fake_cont_thread;
		else if (continuous_mode)
			thread_func = &cont_thread;
		else
			thread_func = &perf_test_thread;

		CPU_ZERO_S(cpu_set_size, cpu_set);
		CPU_SET_S(cpus_to_pin[i], cpu_set_size, cpu_set);
		pthread_attr_init(&thread_attr);
		pthread_attr_setaffinity_np(&thread_attr, cpu_set_size,
					    cpu_set);
		if (pthread_create(&cpu_threads[i], &thread_attr, thread_func,
				   NULL))
			shutdown(1, "pthread_create()", __LINE__);
		pthread_attr_destroy(&thread_attr);
	}

	if (!continuous_mode) {
		pthread_join(cpu_threads[0], &retval);
		shutdown((long)retval, "perf_test_thread()", __LINE__);
	} else {
		while (1)
			sleep(1);
	}
	shutdown(0, "", 0);
}
