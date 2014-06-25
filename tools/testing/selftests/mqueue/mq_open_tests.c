/*
 * This application is Copyright 2012 Red Hat, Inc.
 *	Doug Ledford <dledford@redhat.com>
 *
 * mq_open_tests is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * mq_open_tests is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * For the full text of the license, see <http://www.gnu.org/licenses/>.
 *
 * mq_open_tests.c
 *   Tests the various situations that should either succeed or fail to
 *   open a posix message queue and then reports whether or not they
 *   did as they were supposed to.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <mqueue.h>

static char *usage =
"Usage:\n"
"  %s path\n"
"\n"
"	path	Path name of the message queue to create\n"
"\n"
"	Note: this program must be run as root in order to enable all tests\n"
"\n";

char *DEF_MSGS = "/proc/sys/fs/mqueue/msg_default";
char *DEF_MSGSIZE = "/proc/sys/fs/mqueue/msgsize_default";
char *MAX_MSGS = "/proc/sys/fs/mqueue/msg_max";
char *MAX_MSGSIZE = "/proc/sys/fs/mqueue/msgsize_max";

int default_settings;
struct rlimit saved_limits, cur_limits;
int saved_def_msgs, saved_def_msgsize, saved_max_msgs, saved_max_msgsize;
int cur_def_msgs, cur_def_msgsize, cur_max_msgs, cur_max_msgsize;
FILE *def_msgs, *def_msgsize, *max_msgs, *max_msgsize;
char *queue_path;
mqd_t queue = -1;

static inline void __set(FILE *stream, int value, char *err_msg);
void shutdown(int exit_val, char *err_cause, int line_no);
static inline int get(FILE *stream);
static inline void set(FILE *stream, int value);
static inline void getr(int type, struct rlimit *rlim);
static inline void setr(int type, struct rlimit *rlim);
void validate_current_settings();
static inline void test_queue(struct mq_attr *attr, struct mq_attr *result);
static inline int test_queue_fail(struct mq_attr *attr, struct mq_attr *result);

static inline void __set(FILE *stream, int value, char *err_msg)
{
	rewind(stream);
	if (fprintf(stream, "%d", value) < 0)
		perror(err_msg);
}


void shutdown(int exit_val, char *err_cause, int line_no)
{
	static int in_shutdown = 0;

	/* In case we get called recursively by a set() call below */
	if (in_shutdown++)
		return;

	if (seteuid(0) == -1)
		perror("seteuid() failed");

	if (queue != -1)
		if (mq_close(queue))
			perror("mq_close() during shutdown");
	if (queue_path)
		/*
		 * Be silent if this fails, if we cleaned up already it's
		 * expected to fail
		 */
		mq_unlink(queue_path);
	if (default_settings) {
		if (saved_def_msgs)
			__set(def_msgs, saved_def_msgs,
			      "failed to restore saved_def_msgs");
		if (saved_def_msgsize)
			__set(def_msgsize, saved_def_msgsize,
			      "failed to restore saved_def_msgsize");
	}
	if (saved_max_msgs)
		__set(max_msgs, saved_max_msgs,
		      "failed to restore saved_max_msgs");
	if (saved_max_msgsize)
		__set(max_msgsize, saved_max_msgsize,
		      "failed to restore saved_max_msgsize");
	if (exit_val)
		error(exit_val, errno, "%s at %d", err_cause, line_no);
	exit(0);
}

static inline int get(FILE *stream)
{
	int value;
	rewind(stream);
	if (fscanf(stream, "%d", &value) != 1)
		shutdown(4, "Error reading /proc entry", __LINE__ - 1);
	return value;
}

static inline void set(FILE *stream, int value)
{
	int new_value;

	rewind(stream);
	if (fprintf(stream, "%d", value) < 0)
		return shutdown(5, "Failed writing to /proc file",
				__LINE__ - 1);
	new_value = get(stream);
	if (new_value != value)
		return shutdown(5, "We didn't get what we wrote to /proc back",
				__LINE__ - 1);
}

static inline void getr(int type, struct rlimit *rlim)
{
	if (getrlimit(type, rlim))
		shutdown(6, "getrlimit()", __LINE__ - 1);
}

static inline void setr(int type, struct rlimit *rlim)
{
	if (setrlimit(type, rlim))
		shutdown(7, "setrlimit()", __LINE__ - 1);
}

void validate_current_settings()
{
	int rlim_needed;

	if (cur_limits.rlim_cur < 4096) {
		printf("Current rlimit value for POSIX message queue bytes is "
		       "unreasonably low,\nincreasing.\n\n");
		cur_limits.rlim_cur = 8192;
		cur_limits.rlim_max = 16384;
		setr(RLIMIT_MSGQUEUE, &cur_limits);
	}

	if (default_settings) {
		rlim_needed = (cur_def_msgs + 1) * (cur_def_msgsize + 1 +
						    2 * sizeof(void *));
		if (rlim_needed > cur_limits.rlim_cur) {
			printf("Temporarily lowering default queue parameters "
			       "to something that will work\n"
			       "with the current rlimit values.\n\n");
			set(def_msgs, 10);
			cur_def_msgs = 10;
			set(def_msgsize, 128);
			cur_def_msgsize = 128;
		}
	} else {
		rlim_needed = (cur_max_msgs + 1) * (cur_max_msgsize + 1 +
						    2 * sizeof(void *));
		if (rlim_needed > cur_limits.rlim_cur) {
			printf("Temporarily lowering maximum queue parameters "
			       "to something that will work\n"
			       "with the current rlimit values in case this is "
			       "a kernel that ties the default\n"
			       "queue parameters to the maximum queue "
			       "parameters.\n\n");
			set(max_msgs, 10);
			cur_max_msgs = 10;
			set(max_msgsize, 128);
			cur_max_msgsize = 128;
		}
	}
}

/*
 * test_queue - Test opening a queue, shutdown if we fail.  This should
 * only be called in situations that should never fail.  We clean up
 * after ourselves and return the queue attributes in *result.
 */
static inline void test_queue(struct mq_attr *attr, struct mq_attr *result)
{
	int flags = O_RDWR | O_EXCL | O_CREAT;
	int perms = DEFFILEMODE;

	if ((queue = mq_open(queue_path, flags, perms, attr)) == -1)
		shutdown(1, "mq_open()", __LINE__);
	if (mq_getattr(queue, result))
		shutdown(1, "mq_getattr()", __LINE__);
	if (mq_close(queue))
		shutdown(1, "mq_close()", __LINE__);
	queue = -1;
	if (mq_unlink(queue_path))
		shutdown(1, "mq_unlink()", __LINE__);
}

/*
 * Same as test_queue above, but failure is not fatal.
 * Returns:
 * 0 - Failed to create a queue
 * 1 - Created a queue, attributes in *result
 */
static inline int test_queue_fail(struct mq_attr *attr, struct mq_attr *result)
{
	int flags = O_RDWR | O_EXCL | O_CREAT;
	int perms = DEFFILEMODE;

	if ((queue = mq_open(queue_path, flags, perms, attr)) == -1)
		return 0;
	if (mq_getattr(queue, result))
		shutdown(1, "mq_getattr()", __LINE__);
	if (mq_close(queue))
		shutdown(1, "mq_close()", __LINE__);
	queue = -1;
	if (mq_unlink(queue_path))
		shutdown(1, "mq_unlink()", __LINE__);
	return 1;
}

int main(int argc, char *argv[])
{
	struct mq_attr attr, result;

	if (argc != 2) {
		fprintf(stderr, "Must pass a valid queue name\n\n");
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}

	/*
	 * Although we can create a msg queue with a non-absolute path name,
	 * unlink will fail.  So, if the name doesn't start with a /, add one
	 * when we save it.
	 */
	if (*argv[1] == '/')
		queue_path = strdup(argv[1]);
	else {
		queue_path = malloc(strlen(argv[1]) + 2);
		if (!queue_path) {
			perror("malloc()");
			exit(1);
		}
		queue_path[0] = '/';
		queue_path[1] = 0;
		strcat(queue_path, argv[1]);
	}

	if (getuid() != 0) {
		fprintf(stderr, "Not running as root, but almost all tests "
			"require root in order to modify\nsystem settings.  "
			"Exiting.\n");
		exit(1);
	}

	/* Find out what files there are for us to make tweaks in */
	def_msgs = fopen(DEF_MSGS, "r+");
	def_msgsize = fopen(DEF_MSGSIZE, "r+");
	max_msgs = fopen(MAX_MSGS, "r+");
	max_msgsize = fopen(MAX_MSGSIZE, "r+");

	if (!max_msgs)
		shutdown(2, "Failed to open msg_max", __LINE__);
	if (!max_msgsize)
		shutdown(2, "Failed to open msgsize_max", __LINE__);
	if (def_msgs || def_msgsize)
		default_settings = 1;

	/* Load up the current system values for everything we can */
	getr(RLIMIT_MSGQUEUE, &saved_limits);
	cur_limits = saved_limits;
	if (default_settings) {
		saved_def_msgs = cur_def_msgs = get(def_msgs);
		saved_def_msgsize = cur_def_msgsize = get(def_msgsize);
	}
	saved_max_msgs = cur_max_msgs = get(max_msgs);
	saved_max_msgsize = cur_max_msgsize = get(max_msgsize);

	/* Tell the user our initial state */
	printf("\nInitial system state:\n");
	printf("\tUsing queue path:\t\t%s\n", queue_path);
	printf("\tRLIMIT_MSGQUEUE(soft):\t\t%ld\n",
		(long) saved_limits.rlim_cur);
	printf("\tRLIMIT_MSGQUEUE(hard):\t\t%ld\n",
		(long) saved_limits.rlim_max);
	printf("\tMaximum Message Size:\t\t%d\n", saved_max_msgsize);
	printf("\tMaximum Queue Size:\t\t%d\n", saved_max_msgs);
	if (default_settings) {
		printf("\tDefault Message Size:\t\t%d\n", saved_def_msgsize);
		printf("\tDefault Queue Size:\t\t%d\n", saved_def_msgs);
	} else {
		printf("\tDefault Message Size:\t\tNot Supported\n");
		printf("\tDefault Queue Size:\t\tNot Supported\n");
	}
	printf("\n");

	validate_current_settings();

	printf("Adjusted system state for testing:\n");
	printf("\tRLIMIT_MSGQUEUE(soft):\t\t%ld\n", (long) cur_limits.rlim_cur);
	printf("\tRLIMIT_MSGQUEUE(hard):\t\t%ld\n", (long) cur_limits.rlim_max);
	printf("\tMaximum Message Size:\t\t%d\n", cur_max_msgsize);
	printf("\tMaximum Queue Size:\t\t%d\n", cur_max_msgs);
	if (default_settings) {
		printf("\tDefault Message Size:\t\t%d\n", cur_def_msgsize);
		printf("\tDefault Queue Size:\t\t%d\n", cur_def_msgs);
	}

	printf("\n\nTest series 1, behavior when no attr struct "
	       "passed to mq_open:\n");
	if (!default_settings) {
		test_queue(NULL, &result);
		printf("Given sane system settings, mq_open without an attr "
		       "struct succeeds:\tPASS\n");
		if (result.mq_maxmsg != cur_max_msgs ||
		    result.mq_msgsize != cur_max_msgsize) {
			printf("Kernel does not support setting the default "
			       "mq attributes,\nbut also doesn't tie the "
			       "defaults to the maximums:\t\t\tPASS\n");
		} else {
			set(max_msgs, ++cur_max_msgs);
			set(max_msgsize, ++cur_max_msgsize);
			test_queue(NULL, &result);
			if (result.mq_maxmsg == cur_max_msgs &&
			    result.mq_msgsize == cur_max_msgsize)
				printf("Kernel does not support setting the "
				       "default mq attributes and\n"
				       "also ties system wide defaults to "
				       "the system wide maximums:\t\t"
				       "FAIL\n");
			else
				printf("Kernel does not support setting the "
				       "default mq attributes,\n"
				       "but also doesn't tie the defaults to "
				       "the maximums:\t\t\tPASS\n");
		}
	} else {
		printf("Kernel supports setting defaults separately from "
		       "maximums:\t\tPASS\n");
		/*
		 * While we are here, go ahead and test that the kernel
		 * properly follows the default settings
		 */
		test_queue(NULL, &result);
		printf("Given sane values, mq_open without an attr struct "
		       "succeeds:\t\tPASS\n");
		if (result.mq_maxmsg != cur_def_msgs ||
		    result.mq_msgsize != cur_def_msgsize)
			printf("Kernel supports setting defaults, but does "
			       "not actually honor them:\tFAIL\n\n");
		else {
			set(def_msgs, ++cur_def_msgs);
			set(def_msgsize, ++cur_def_msgsize);
			/* In case max was the same as the default */
			set(max_msgs, ++cur_max_msgs);
			set(max_msgsize, ++cur_max_msgsize);
			test_queue(NULL, &result);
			if (result.mq_maxmsg != cur_def_msgs ||
			    result.mq_msgsize != cur_def_msgsize)
				printf("Kernel supports setting defaults, but "
				       "does not actually honor them:\t"
				       "FAIL\n");
			else
				printf("Kernel properly honors default setting "
				       "knobs:\t\t\t\tPASS\n");
		}
		set(def_msgs, cur_max_msgs + 1);
		cur_def_msgs = cur_max_msgs + 1;
		set(def_msgsize, cur_max_msgsize + 1);
		cur_def_msgsize = cur_max_msgsize + 1;
		if (cur_def_msgs * (cur_def_msgsize + 2 * sizeof(void *)) >=
		    cur_limits.rlim_cur) {
			cur_limits.rlim_cur = (cur_def_msgs + 2) *
				(cur_def_msgsize + 2 * sizeof(void *));
			cur_limits.rlim_max = 2 * cur_limits.rlim_cur;
			setr(RLIMIT_MSGQUEUE, &cur_limits);
		}
		if (test_queue_fail(NULL, &result)) {
			if (result.mq_maxmsg == cur_max_msgs &&
			    result.mq_msgsize == cur_max_msgsize)
				printf("Kernel properly limits default values "
				       "to lesser of default/max:\t\tPASS\n");
			else
				printf("Kernel does not properly set default "
				       "queue parameters when\ndefaults > "
				       "max:\t\t\t\t\t\t\t\tFAIL\n");
		} else
			printf("Kernel fails to open mq because defaults are "
			       "greater than maximums:\tFAIL\n");
		set(def_msgs, --cur_def_msgs);
		set(def_msgsize, --cur_def_msgsize);
		cur_limits.rlim_cur = cur_limits.rlim_max = cur_def_msgs *
			cur_def_msgsize;
		setr(RLIMIT_MSGQUEUE, &cur_limits);
		if (test_queue_fail(NULL, &result))
			printf("Kernel creates queue even though defaults "
			       "would exceed\nrlimit setting:"
			       "\t\t\t\t\t\t\t\tFAIL\n");
		else
			printf("Kernel properly fails to create queue when "
			       "defaults would\nexceed rlimit:"
			       "\t\t\t\t\t\t\t\tPASS\n");
	}

	/*
	 * Test #2 - open with an attr struct that exceeds rlimit
	 */
	printf("\n\nTest series 2, behavior when attr struct is "
	       "passed to mq_open:\n");
	cur_max_msgs = 32;
	cur_max_msgsize = cur_limits.rlim_max >> 4;
	set(max_msgs, cur_max_msgs);
	set(max_msgsize, cur_max_msgsize);
	attr.mq_maxmsg = cur_max_msgs;
	attr.mq_msgsize = cur_max_msgsize;
	if (test_queue_fail(&attr, &result))
		printf("Queue open in excess of rlimit max when euid = 0 "
		       "succeeded:\t\tFAIL\n");
	else
		printf("Queue open in excess of rlimit max when euid = 0 "
		       "failed:\t\tPASS\n");
	attr.mq_maxmsg = cur_max_msgs + 1;
	attr.mq_msgsize = 10;
	if (test_queue_fail(&attr, &result))
		printf("Queue open with mq_maxmsg > limit when euid = 0 "
		       "succeeded:\t\tPASS\n");
	else
		printf("Queue open with mq_maxmsg > limit when euid = 0 "
		       "failed:\t\tFAIL\n");
	attr.mq_maxmsg = 1;
	attr.mq_msgsize = cur_max_msgsize + 1;
	if (test_queue_fail(&attr, &result))
		printf("Queue open with mq_msgsize > limit when euid = 0 "
		       "succeeded:\t\tPASS\n");
	else
		printf("Queue open with mq_msgsize > limit when euid = 0 "
		       "failed:\t\tFAIL\n");
	attr.mq_maxmsg = 65536;
	attr.mq_msgsize = 65536;
	if (test_queue_fail(&attr, &result))
		printf("Queue open with total size > 2GB when euid = 0 "
		       "succeeded:\t\tFAIL\n");
	else
		printf("Queue open with total size > 2GB when euid = 0 "
		       "failed:\t\t\tPASS\n");

	if (seteuid(99) == -1) {
		perror("seteuid() failed");
		exit(1);
	}

	attr.mq_maxmsg = cur_max_msgs;
	attr.mq_msgsize = cur_max_msgsize;
	if (test_queue_fail(&attr, &result))
		printf("Queue open in excess of rlimit max when euid = 99 "
		       "succeeded:\t\tFAIL\n");
	else
		printf("Queue open in excess of rlimit max when euid = 99 "
		       "failed:\t\tPASS\n");
	attr.mq_maxmsg = cur_max_msgs + 1;
	attr.mq_msgsize = 10;
	if (test_queue_fail(&attr, &result))
		printf("Queue open with mq_maxmsg > limit when euid = 99 "
		       "succeeded:\t\tFAIL\n");
	else
		printf("Queue open with mq_maxmsg > limit when euid = 99 "
		       "failed:\t\tPASS\n");
	attr.mq_maxmsg = 1;
	attr.mq_msgsize = cur_max_msgsize + 1;
	if (test_queue_fail(&attr, &result))
		printf("Queue open with mq_msgsize > limit when euid = 99 "
		       "succeeded:\t\tFAIL\n");
	else
		printf("Queue open with mq_msgsize > limit when euid = 99 "
		       "failed:\t\tPASS\n");
	attr.mq_maxmsg = 65536;
	attr.mq_msgsize = 65536;
	if (test_queue_fail(&attr, &result))
		printf("Queue open with total size > 2GB when euid = 99 "
		       "succeeded:\t\tFAIL\n");
	else
		printf("Queue open with total size > 2GB when euid = 99 "
		       "failed:\t\t\tPASS\n");

	shutdown(0,"",0);
}
