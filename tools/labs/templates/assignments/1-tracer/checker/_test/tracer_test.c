/*
 * SO2 Kprobe based tracer - test suite
 *
 * Authors:
 *	Daniel Baluta <daniel.baluta@gmail.com>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>


#include "test.h"
#include "debug.h"
#include "util.h"

#include "tracer.h"
#include "tracer_test.h"
#include "helper.h"

/* use this to enable stats debugging */
#if 0
#define DEBUG
#endif

#define MSECS  1000

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct tracer_stats {
	pid_t tr_pid;
	int tr_alloc;
	int tr_free;
	int tr_mem;
	int tr_mem_free;
	int tr_sched;
	int tr_up;
	int tr_down;
	int tr_lock;
	int tr_unlock;
};

struct test_case {
	char test_name[NAMESIZE];
	int score;
	struct test_params test_params;
};

struct tracer_stats ts[MCOUNT];

struct test_case tc[] = {
	/* 0 */
	{
		.test_name = "test_simple_kmalloc",
		.test_params = {
			.thread_name	= "xthread-0",
			.kcalls		= 1,
			.alloc		= {1024, },
			.idx		= 0,
		},
		.score = 5,
	},
	/* 1 */
	{
		.test_name = "test_simple_kfree",
		.test_params = {
			.thread_name	= "xthread-1",
			.kcalls		= 1,
			.alloc		= {4096, },
			.free		= {1, },
			.idx		= 1,
		},
		.score = 5,
	},
	/* 2 */
	{
		.test_name = "test_simple_sched",
		.test_params = {
			.thread_name	= "xthread-2",
			.sched		= 1,
			.idx		= 2,
		},
		.score = 4,
	},
	/* 3 */
	{
		.test_name = "test_simple_up_down",
		.test_params = {
			.thread_name	= "xthread-3",
			.up		= 1,
			.down		= 1,
			.idx		= 3,
		},
		.score = 4,
	},
	/* 4 */
	{
		.test_name = "test_simple_lock_unlock",
		.test_params = {
			.thread_name	= "xthread-4",
			.lock		= 1,
			.unlock		= 1,
			.idx		= 4,
		},
		.score = 4,
	},

	/* 5 */
	{
		.test_name = "test_medium_kmalloc",
		.test_params = {
			.thread_name	= "xthread-5",
			.kcalls		= 16,
			.alloc		= {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
			.idx		= 5,
		},
		.score = 5,
	},

	/* 6 */
	{
		.test_name = "test_medium_free",
		.test_params = {
			.thread_name	= "xthread-6",
			.kcalls		= 12,
			.alloc		= {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048},
			.free		= {0, 0, 1, 0, 0,  1,  0,  0,   1,   0,   0,    1},
			.idx		= 6,
		},
		.score = 5,
	},

	/* 7 */
	{
		.test_name = "test_medium_sched",
		.test_params = {
			.thread_name	= "xthread-7",
			.sched		= 30,
			.idx		= 7,
		},
		.score = 5,
	},

	/* 8 */
	{
		.test_name = "test_medium_up_down",
		.test_params = {
			.thread_name	= "xthread-8",
			.up		= 32,
			.down		= 32,
			.idx		= 8,
		},
		.score = 4,
	},
	/* 9 */
	{
		.test_name = "test_medium_lock_unlock",
		.test_params = {
			.thread_name	= "xthread-9",
			.lock		= 32,
			.unlock		= 32,
			.idx		= 9,
		},
		.score = 4,
	},
	/* 10 */
	{
		.test_name = "test_medium_combined",
		.test_params = {
			.thread_name	= "xthread-9",
			.kcalls		= 9,
			.alloc		= {1024, 512, 128, 64, 32, 64, 128, 512, 1024},
			.free		= {1,    1,   1,   1,  1,  1,  1,   1,   1},
			.lock		= 8,
			.unlock		= 8,
			.up		= 12,
			.down		= 12,
			.idx		= 10,
		},
		.score = 5,
	},
};

/* declared in test.h; used for printing information in test macro */
int max_points = 100;

/*
 * Do initialization for tracer test functions.
 */

static void init_test(void)
{
	int rc;

	rc = system("insmod " MODULE_FILENAME);
	DIE(rc != 0, "init_test");
}

static void init_test2(int *fd)
{
	int rc;

	system("insmod " MODULE_FILENAME);

	rc = open("/dev/tracer", O_RDONLY);
	DIE(rc < 0, "init_test2");

	*fd = rc;
}

/*
 * Do cleanup for tracer test functions.
 */

static void cleanup_test(void)
{
	system("rmmod " MODULE_NAME);
}

static void cleanup_test2(int fd)
{
	close(fd);

	system("rmmod " MODULE_NAME);
}

/*
 * Do initialization for tracer helper test module
 */
static void init_helper(int *fd)
{
	int rc;

	system("insmod " HELPER_MODULE_FILENAME);

	rc = open("/dev/helper", O_RDONLY);
	DIE(rc < 0, "init helper");

	*fd = rc;
}

/*
 * Do cleanup for tracer helper test module
 */

static void cleanup_helper(int fd)
{
	close(fd);

	system("rmmod " HELPER_MODULE_NAME);
}


/*
 * Check for successful module insertion and removal from the kernel.
 */

static void test_insmod_rmmod(void)
{
	int rc;

	rc = system("insmod " MODULE_FILENAME);
	test("test_insmod", rc == 0, 1);

	rc = system("rmmod " MODULE_NAME);
	test("test_rmmod", rc == 0, 1);

	rc = system("insmod " MODULE_FILENAME);
	test(__func__, rc == 0, 1);

	system("rmmod " MODULE_NAME);
}

static void test_open_dev_tracer(void)
{
	int rc;
	char dev_name[64];

	init_test();
	snprintf(dev_name, 63, "/dev/%s", TRACER_DEV_NAME);

	rc = open(dev_name, O_RDONLY);
	test(__func__, rc >= 0, 1);
	close(rc);

	cleanup_test();
}

static void test_dev_minor_major(void)
{
	int rc;
	struct stat buf;

	init_test();

	rc = lstat("/dev/tracer", &buf);
	if (rc < 0) {
		perror("lstat");
		exit(-1);
	}
	test(__func__, major(buf.st_rdev) == 10 &&
		minor(buf.st_rdev) == 42, 1);

	cleanup_test();
}

/*
 * Check for proc entry for kprobe stats
 */

static void test_proc_entry_exists_after_insmod(void)
{
	int rc;

	init_test();

	rc = system("ls /proc/tracer > /dev/null 2>&1");
	test(__func__, rc == 0, 2);

	cleanup_test();
}

static void test_proc_entry_inexistent_after_rmmod(void)
{
	int rc;

	init_test();
	cleanup_test();

	rc = system("ls /proc/tracer > /dev/null 2>&1");
	test(__func__, rc != 0, 2);
}

int tracer_proc_check_values(struct tracer_stats *st,
	struct test_case *tc, int no)
{
	int idx, idz, idk;/* really? */
	int a, b, c, d, e, f, g, h, i, j;/* no, no */
	int total_mem = 0;
	int total_free = 0;
	int no_free = 0;
	int ok = 0;
	/* this is embarassing - O(n^2) - stats are not sorted by pid */

	for (idx = 0; idx < no; idx++) {
		ok = 0;
		for (idk = 0; idk < no; idk++) {
			if (st[idk].tr_pid != tc[idx].test_params.pid)
				continue;
			ok = 1;
			total_mem = 0;
			total_free = 0;
			no_free = 0;

			for (idz = 0; idz < tc[idx].test_params.kcalls; idz++) {
				total_mem += tc[idx].test_params.alloc[idz];
				total_free += tc[idx].test_params.free[idz] *
					tc[idx].test_params.alloc[idz];
				if (tc[idx].test_params.free[idz])
					no_free++;
			}

			a = (st[idk].tr_pid == tc[idx].test_params.pid);
			b = (st[idk].tr_alloc == tc[idx].test_params.kcalls);
			dprintf("tr_alloc (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_alloc,
				tc[idx].test_params.kcalls);

			c = (st[idk].tr_free == no_free);
			dprintf("tr_free (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_free, no_free);

			d = (st[idk].tr_mem == total_mem);
			dprintf("tr_mem (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_mem, total_mem);

			e = (st[idk].tr_mem_free == total_free);
			dprintf("tr_free (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_mem_free,
				total_free);

			f = (st[idk].tr_sched >= tc[idx].test_params.sched);
			dprintf("tr_sched (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_sched,
				tc[idx].test_params.sched);

			g = (st[idk].tr_up == tc[idx].test_params.up);
			dprintf("tr_up (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_up,
				tc[idx].test_params.up);

			h = (st[idk].tr_down == tc[idx].test_params.down);
			dprintf("tr_down (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_down,
				tc[idx].test_params.down);

			i = (st[idk].tr_lock == tc[idx].test_params.lock);
			dprintf("tr_lock (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_lock,
				tc[idx].test_params.lock);

			j = (st[idk].tr_unlock == tc[idx].test_params.unlock);
			dprintf("tr_unlock (%d): got %d, expected %d\n",
				st[idk].tr_pid, st[idk].tr_unlock,
				tc[idx].test_params.unlock);

			if (!a || !b || !c || !d ||
					!e || !f || !g || !h || !i || !j)
				return 0;
		}
	}
	return ok;
}

static void tracer_proc_read_values(struct tracer_stats *st, int no)
{
	char buffer[256];
	FILE *f;
	int i;

	f = fopen("/proc/tracer", "rt");
	DIE(f == NULL, "tracer_proc_read_value");

	/* skip header line */
	fgets(buffer, 256, f);

	for (i = 0; i < no; i++) {
		fscanf(f, "%d %d %d %d %d %d %d %d %d %d",
			&st[i].tr_pid, &st[i].tr_alloc, &st[i].tr_free,
			&st[i].tr_mem, &st[i].tr_mem_free, &st[i].tr_sched,
			&st[i].tr_up, &st[i].tr_down, &st[i].tr_lock,
			&st[i].tr_unlock);
	}
	fclose(f);
}

/*
 * creates a process prepared to run with @tp params
 * returns the pid of the newly created process
 */
void prepare_helper(int fd, struct test_params *tp, pid_t *pid)
{
	int rc;

	rc = ioctl(fd, PREPARE_TEST, tp);
	DIE(rc < 0, "prepare helper");
	*pid = rc;
}

void start_helper(int fd, int idx)
{
	int rc;

	rc = ioctl(fd, START_TEST, idx);
	DIE(rc < 0, "start helper");
}

void stop_helper(int fd, int idx)
{
	int rc;

	rc = ioctl(fd, STOP_TEST, idx);
	DIE(rc < 0, "stop helper");
}

/* XXX: we should really check the return codes */
void trace_process(int fd, pid_t pid)
{
	int rc;

	rc = ioctl(fd, TRACER_ADD_PROCESS, pid);
	DIE(rc < 0, "trace_process");
}

void untrace_process(int fd, pid_t pid)
{
	int rc;

	rc = ioctl(fd, TRACER_REMOVE_PROCESS, pid);
	DIE(rc < 0, "untrace process");
}
static void test_single(void)
{
	int fd, fdh, i, rc;

	init_test2(&fd);
	init_helper(&fdh);

	for (i = 0; i < 11; i++) {

		prepare_helper(fdh, &tc[i].test_params, &tc[i].test_params.pid);
		usleep(400 * MSECS);

		trace_process(fd, tc[i].test_params.pid);
		usleep(400 * MSECS);

		start_helper(fdh, tc[i].test_params.idx);
		usleep(400 * MSECS);

		/* check proc for schedule stats */
		tracer_proc_read_values(&ts[0], 1);
		rc = tracer_proc_check_values(&ts[0], &tc[i], 1);

		memset(&ts[0], 0, sizeof(struct tracer_stats));

		untrace_process(fd, tc[i].test_params.pid);
		usleep(400 * MSECS);
		stop_helper(fdh, tc[i].test_params.idx);

		usleep(400 * MSECS);
		test(tc[i].test_name, rc == 1, tc[i].score);
	}

	cleanup_helper(fdh);
	cleanup_test2(fd);
}


static void test_multiple_zero_stats(void)
{
	int fd, fdh, i, rc;
	struct test_case mz[16];
	struct tracer_stats zstats[16]; /* zstats, mz, wtf? */

	for (i = 0; i < 16; i++) {
		memset(&mz[i], 0, sizeof(struct test_case));
		snprintf(mz[i].test_params.thread_name, 16, "xthread-%d", i);
		mz[i].test_params.idx = i;
	}

	init_test2(&fd);
	init_helper(&fdh);

	for (i = 0; i < 16; i++) {
		prepare_helper(fdh, &mz[i].test_params, &mz[i].test_params.pid);

		trace_process(fd, mz[i].test_params.pid);
	}

	usleep(400 * MSECS);
	for (i = 0; i < 16; i++)
		start_helper(fdh, mz[i].test_params.idx);

	usleep(400 * MSECS);

	/* check proc for schedule stats */

	tracer_proc_read_values(&zstats[0], 16);
	rc = tracer_proc_check_values(&zstats[0], &mz[0], 16);

	for (i = 0; i < 16; i++)
		untrace_process(fd, mz[i].test_params.pid);

	for (i = 0; i < 16; i++)
		stop_helper(fdh, mz[i].test_params.idx);
	usleep(400 * MSECS);

	test("test_multiple_zero_stats", rc == 1, 5);

	cleanup_helper(fdh);
	cleanup_test2(fd);
}

/*
 * FIXME: duplicate code
 */
static void test_multiple_nonzero_stats(void)
{
	int fd, fdh, i, rc;
	struct test_case mz[16];
	struct tracer_stats zstats[16]; /* zstats, mz, wtf? */

	for (i = 0; i < 16; i++) {
		memset(&mz[i], 0, sizeof(struct test_case));
		snprintf(mz[i].test_params.thread_name, 16, "xthread-%d", i);
		mz[i].test_params.up = i;
		mz[i].test_params.down = i;
		mz[i].test_params.sched = i;
		mz[i].test_params.lock = i;
		mz[i].test_params.unlock = i;
		mz[i].test_params.idx = i;
	}

	init_test2(&fd);
	init_helper(&fdh);

	for (i = 0; i < 16; i++) {
		prepare_helper(fdh, &mz[i].test_params, &mz[i].test_params.pid);

		trace_process(fd, mz[i].test_params.pid);
	}

	usleep(400 * MSECS);
	for (i = 0; i < 16; i++)
		start_helper(fdh, mz[i].test_params.idx);

	usleep(400 * MSECS);

	/* check proc for schedule stats */

	tracer_proc_read_values(&zstats[0], 16);
	rc = tracer_proc_check_values(&zstats[0], &mz[0], 16);

	for (i = 0; i < 16; i++)
		untrace_process(fd, mz[i].test_params.pid);

	for (i = 0; i < 16; i++)
		stop_helper(fdh, mz[i].test_params.idx);
	usleep(400 * MSECS);

	test("test_multiple_nonzero_stats", rc == 1, 12);

	cleanup_helper(fdh);
	cleanup_test2(fd);
}

/*
 * FIXME: duplicate code
 */
static void test_decent_alloc_free(void)
{
	int fd, fdh, i, rc, j;
	struct test_case mz[32];
	struct tracer_stats zstats[32]; /* zstats, mz, wtf? */

	for (i = 0; i < 32; i++) {
		memset(&mz[i], 0, sizeof(struct test_case));
		snprintf(mz[i].test_params.thread_name, 16, "xthread-%d", i);
		mz[i].test_params.kcalls = 32;
		for (j = 0; j < 32; j++) {
			mz[i].test_params.alloc[j] = 8 * j * (i+1);
			mz[i].test_params.free[j] = 1;
		}

		mz[i].test_params.idx = i;
	}

	init_test2(&fd);
	init_helper(&fdh);

	for (i = 0; i < 32; i++) {
		prepare_helper(fdh, &mz[i].test_params, &mz[i].test_params.pid);

		trace_process(fd, mz[i].test_params.pid);
	}

	usleep(800 * MSECS);
	for (i = 0; i < 32; i++)
		start_helper(fdh, mz[i].test_params.idx);

	usleep(800 * MSECS);

	/* check proc for schedule stats */

	tracer_proc_read_values(&zstats[0], 32);
	rc = tracer_proc_check_values(&zstats[0], &mz[0], 32);

	for (i = 0; i < 32; i++)
		untrace_process(fd, mz[i].test_params.pid);

	for (i = 0; i < 32; i++)
		stop_helper(fdh, mz[i].test_params.idx);
	usleep(800 * MSECS);

	test("test_decent_alloc_free", rc == 1, 12);

	cleanup_helper(fdh);
	cleanup_test2(fd);
}

/*
 * FIXME: duplicate code
 */
static void test_mini_stress(void)
{
	int fd, fdh, i, rc;
	struct test_case mz[32];
	struct tracer_stats zstats[32]; /* zstats, mz, wtf? */

	for (i = 0; i < 32; i++) {
		memset(&mz[i], 0, sizeof(struct test_case));
		snprintf(mz[i].test_params.thread_name, 16, "xthread-%d", i);
		mz[i].test_params.up = 512 + i;
		mz[i].test_params.down = 512 + i;
		mz[i].test_params.sched = i;
		mz[i].test_params.lock = 128 + i;
		mz[i].test_params.unlock = 128 + i;
		mz[i].test_params.idx = i;
	}

	init_test2(&fd);
	init_helper(&fdh);

	for (i = 0; i < 32; i++) {
		prepare_helper(fdh, &mz[i].test_params, &mz[i].test_params.pid);

		trace_process(fd, mz[i].test_params.pid);
	}

	usleep(800 * MSECS);
	for (i = 0; i < 32; i++)
		start_helper(fdh, mz[i].test_params.idx);

	usleep(800 * MSECS);

	/* check proc for schedule stats */

	tracer_proc_read_values(&zstats[0], 32);
	rc = tracer_proc_check_values(&zstats[0], &mz[0], 32);

	for (i = 0; i < 32; i++)
		untrace_process(fd, mz[i].test_params.pid);

	for (i = 0; i < 32; i++)
		stop_helper(fdh, mz[i].test_params.idx);
	usleep(800 * MSECS);

	test("test_mini_stress", rc == 1, 12);

	cleanup_helper(fdh);
	cleanup_test2(fd);
}




static void (*test_fun_array[])(void) = {
	NULL,
	test_insmod_rmmod,
	test_open_dev_tracer,
	test_dev_minor_major,
	test_proc_entry_exists_after_insmod,
	test_proc_entry_inexistent_after_rmmod,
	test_single,
	test_multiple_zero_stats,
	test_multiple_nonzero_stats,
	test_decent_alloc_free,
	test_mini_stress,
};

/*
 * Usage message for invalid executable call.
 */

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s test_no\n\n", argv0);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	int test_idx;

	if (argc != 2)
		usage(argv[0]);

	test_idx = atoi(argv[1]);

	if (test_idx < 1 ||
		test_idx >= ARRAY_SIZE(test_fun_array)) {
		fprintf(stderr, "Error: test index %d is out of bounds\n",
				test_idx);
		exit(EXIT_FAILURE);
	}

	srand(time(NULL));
	srand48(time(NULL));
	test_fun_array[test_idx]();

	return 0;
}
