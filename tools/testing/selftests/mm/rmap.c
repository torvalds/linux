// SPDX-License-Identifier: GPL-2.0
/*
 * RMAP functional tests
 *
 * Author(s): Wei Yang <richard.weiyang@gmail.com>
 */

#include "../kselftest_harness.h"
#include <strings.h>
#include <pthread.h>
#include <numa.h>
#include <numaif.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <sys/sem.h>
#include <unistd.h>
#include <fcntl.h>

#include "vm_util.h"

#define TOTAL_LEVEL 5
#define MAX_CHILDREN 3

#define FAIL_ON_CHECK	(1 << 0)
#define FAIL_ON_WORK	(1 << 1)

struct sembuf sem_wait = {0, -1, 0};
struct sembuf sem_signal = {0, 1, 0};

enum backend_type {
	ANON,
	SHM,
	NORM_FILE,
};

#define PREFIX "kst_rmap"
#define MAX_FILENAME_LEN 256
const char *suffixes[] = {
	"",
	"_shm",
	"_file",
};

struct global_data;
typedef int (*work_fn)(struct global_data *data);
typedef int (*check_fn)(struct global_data *data);
typedef void (*prepare_fn)(struct global_data *data);

struct global_data {
	int worker_level;

	int semid;
	int pipefd[2];

	unsigned int mapsize;
	unsigned int rand_seed;
	char *region;

	prepare_fn do_prepare;
	work_fn do_work;
	check_fn do_check;

	enum backend_type backend;
	char filename[MAX_FILENAME_LEN];

	unsigned long *expected_pfn;
};

/*
 * Create a process tree with TOTAL_LEVEL height and at most MAX_CHILDREN
 * children for each.
 *
 * It will randomly select one process as 'worker' process which will
 * 'do_work' until all processes are created. And all other processes will
 * wait until 'worker' finish its work.
 */
void propagate_children(struct __test_metadata *_metadata, struct global_data *data)
{
	pid_t root_pid, pid;
	unsigned int num_child;
	int status;
	int ret = 0;
	int curr_child, worker_child;
	int curr_level = 1;
	bool is_worker = true;

	root_pid = getpid();
repeat:
	num_child = rand_r(&data->rand_seed) % MAX_CHILDREN + 1;
	worker_child = is_worker ? rand_r(&data->rand_seed) % num_child : -1;

	for (curr_child = 0; curr_child < num_child; curr_child++) {
		pid = fork();

		if (pid < 0) {
			perror("Error: fork\n");
		} else if (pid == 0) {
			curr_level++;

			if (curr_child != worker_child)
				is_worker = false;

			if (curr_level == TOTAL_LEVEL)
				break;

			data->rand_seed += curr_child;
			goto repeat;
		}
	}

	if (data->do_prepare)
		data->do_prepare(data);

	close(data->pipefd[1]);

	if (is_worker && curr_level == data->worker_level) {
		/* This is the worker process, first wait last process created */
		char buf;

		while (read(data->pipefd[0], &buf, 1) > 0)
			;

		if (data->do_work)
			ret = data->do_work(data);

		/* Kick others */
		semctl(data->semid, 0, IPC_RMID);
	} else {
		/* Wait worker finish */
		semop(data->semid, &sem_wait, 1);
		if (data->do_check)
			ret = data->do_check(data);
	}

	/* Wait all child to quit */
	while (wait(&status) > 0) {
		if (WIFEXITED(status))
			ret |= WEXITSTATUS(status);
	}

	if (getpid() == root_pid) {
		if (ret & FAIL_ON_WORK)
			SKIP(return, "Failed in worker");

		ASSERT_EQ(ret, 0);
	} else {
		exit(ret);
	}
}

FIXTURE(migrate)
{
	struct global_data data;
};

FIXTURE_SETUP(migrate)
{
	struct global_data *data = &self->data;

	if (numa_available() < 0)
		SKIP(return, "NUMA not available");
	if (numa_bitmask_weight(numa_all_nodes_ptr) <= 1)
		SKIP(return, "Not enough NUMA nodes available");

	data->mapsize = getpagesize();

	data->expected_pfn = mmap(0, sizeof(unsigned long),
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(data->expected_pfn, MAP_FAILED);

	/* Prepare semaphore */
	data->semid = semget(IPC_PRIVATE, 1, 0666 | IPC_CREAT);
	ASSERT_NE(data->semid, -1);
	ASSERT_NE(semctl(data->semid, 0, SETVAL, 0), -1);

	/* Prepare pipe */
	ASSERT_NE(pipe(data->pipefd), -1);

	data->rand_seed = time(NULL);
	srand(data->rand_seed);

	data->worker_level = rand() % TOTAL_LEVEL + 1;

	data->do_prepare = NULL;
	data->do_work = NULL;
	data->do_check = NULL;

	data->backend = ANON;
};

FIXTURE_TEARDOWN(migrate)
{
	struct global_data *data = &self->data;

	if (data->region != MAP_FAILED)
		munmap(data->region, data->mapsize);
	data->region = MAP_FAILED;
	if (data->expected_pfn != MAP_FAILED)
		munmap(data->expected_pfn, sizeof(unsigned long));
	data->expected_pfn = MAP_FAILED;
	semctl(data->semid, 0, IPC_RMID);
	data->semid = -1;

	close(data->pipefd[0]);

	switch (data->backend) {
	case ANON:
		break;
	case SHM:
		shm_unlink(data->filename);
		break;
	case NORM_FILE:
		unlink(data->filename);
		break;
	}
}

void access_region(struct global_data *data)
{
	/*
	 * Force read "region" to make sure page fault in.
	 */
	FORCE_READ(*data->region);
}

int try_to_move_page(char *region)
{
	int ret;
	int node;
	int status = 0;
	int failures = 0;

	ret = move_pages(0, 1, (void **)&region, NULL, &status, MPOL_MF_MOVE_ALL);
	if (ret != 0) {
		perror("Failed to get original numa");
		return FAIL_ON_WORK;
	}

	/* Pick up a different target node */
	for (node = 0; node <= numa_max_node(); node++) {
		if (numa_bitmask_isbitset(numa_all_nodes_ptr, node) && node != status)
			break;
	}

	if (node > numa_max_node()) {
		ksft_print_msg("Couldn't find available numa node for testing\n");
		return FAIL_ON_WORK;
	}

	while (1) {
		ret = move_pages(0, 1, (void **)&region, &node, &status, MPOL_MF_MOVE_ALL);

		/* migrate successfully */
		if (!ret)
			break;

		/* error happened */
		if (ret < 0) {
			ksft_perror("Failed to move pages");
			return FAIL_ON_WORK;
		}

		/* migration is best effort; try again */
		if (++failures >= 100)
			return FAIL_ON_WORK;
	}

	return 0;
}

int move_region(struct global_data *data)
{
	int ret;
	int pagemap_fd;

	ret = try_to_move_page(data->region);
	if (ret != 0)
		return ret;

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd == -1)
		return FAIL_ON_WORK;
	*data->expected_pfn = pagemap_get_pfn(pagemap_fd, data->region);

	return 0;
}

int has_same_pfn(struct global_data *data)
{
	unsigned long pfn;
	int pagemap_fd;

	if (data->region == MAP_FAILED)
		return 0;

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd == -1)
		return FAIL_ON_CHECK;

	pfn = pagemap_get_pfn(pagemap_fd, data->region);
	if (pfn != *data->expected_pfn)
		return FAIL_ON_CHECK;

	return 0;
}

TEST_F(migrate, anon)
{
	struct global_data *data = &self->data;

	/* Map an area and fault in */
	data->region = mmap(0, data->mapsize, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(data->region, MAP_FAILED);
	memset(data->region, 0xcf, data->mapsize);

	data->do_prepare = access_region;
	data->do_work = move_region;
	data->do_check = has_same_pfn;

	propagate_children(_metadata, data);
}

TEST_F(migrate, shm)
{
	int shm_fd;
	struct global_data *data = &self->data;

	snprintf(data->filename, MAX_FILENAME_LEN, "%s%s", PREFIX, suffixes[SHM]);
	shm_fd = shm_open(data->filename, O_CREAT | O_RDWR, 0666);
	ASSERT_NE(shm_fd, -1);
	ftruncate(shm_fd, data->mapsize);
	data->backend = SHM;

	/* Map a shared area and fault in */
	data->region = mmap(0, data->mapsize, PROT_READ | PROT_WRITE,
				MAP_SHARED, shm_fd, 0);
	ASSERT_NE(data->region, MAP_FAILED);
	memset(data->region, 0xcf, data->mapsize);
	close(shm_fd);

	data->do_prepare = access_region;
	data->do_work = move_region;
	data->do_check = has_same_pfn;

	propagate_children(_metadata, data);
}

TEST_F(migrate, file)
{
	int fd;
	struct global_data *data = &self->data;

	snprintf(data->filename, MAX_FILENAME_LEN, "%s%s", PREFIX, suffixes[NORM_FILE]);
	fd = open(data->filename, O_CREAT | O_RDWR | O_EXCL, 0666);
	ASSERT_NE(fd, -1);
	ftruncate(fd, data->mapsize);
	data->backend = NORM_FILE;

	/* Map a shared area and fault in */
	data->region = mmap(0, data->mapsize, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
	ASSERT_NE(data->region, MAP_FAILED);
	memset(data->region, 0xcf, data->mapsize);
	close(fd);

	data->do_prepare = access_region;
	data->do_work = move_region;
	data->do_check = has_same_pfn;

	propagate_children(_metadata, data);
}

void prepare_local_region(struct global_data *data)
{
	/* Allocate range and set the same data */
	data->region = mmap(NULL, data->mapsize, PROT_READ|PROT_WRITE,
			   MAP_PRIVATE|MAP_ANON, -1, 0);
	if (data->region == MAP_FAILED)
		return;

	memset(data->region, 0xcf, data->mapsize);
}

int merge_and_migrate(struct global_data *data)
{
	int pagemap_fd;
	int ret = 0;

	if (data->region == MAP_FAILED)
		return FAIL_ON_WORK;

	if (ksm_start() < 0)
		return FAIL_ON_WORK;

	ret = try_to_move_page(data->region);

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd == -1)
		return FAIL_ON_WORK;
	*data->expected_pfn = pagemap_get_pfn(pagemap_fd, data->region);

	return ret;
}

TEST_F(migrate, ksm)
{
	int ret;
	struct global_data *data = &self->data;

	if (ksm_stop() < 0)
		SKIP(return, "accessing \"/sys/kernel/mm/ksm/run\") failed");
	if (ksm_get_full_scans() < 0)
		SKIP(return, "accessing \"/sys/kernel/mm/ksm/full_scan\") failed");

	ret = prctl(PR_SET_MEMORY_MERGE, 1, 0, 0, 0);
	if (ret < 0 && errno == EINVAL)
		SKIP(return, "PR_SET_MEMORY_MERGE not supported");
	else if (ret)
		ksft_exit_fail_perror("PR_SET_MEMORY_MERGE=1 failed");

	data->do_prepare = prepare_local_region;
	data->do_work = merge_and_migrate;
	data->do_check = has_same_pfn;

	propagate_children(_metadata, data);
}

TEST_HARNESS_MAIN
