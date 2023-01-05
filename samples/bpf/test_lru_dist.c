// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 Facebook
 */
#define _GNU_SOURCE
#include <linux/types.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>

#include <bpf/bpf.h>
#include "bpf_util.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
#ifndef offsetof
# define offsetof(TYPE, MEMBER)	((size_t)&((TYPE *)0)->MEMBER)
#endif
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

static int nr_cpus;
static unsigned long long *dist_keys;
static unsigned int dist_key_counts;

struct list_head {
	struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void __list_del_entry(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
	__list_del_entry(list);
	list_add(list, head);
}

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

struct pfect_lru_node {
	struct list_head list;
	unsigned long long key;
};

struct pfect_lru {
	struct list_head list;
	struct pfect_lru_node *free_nodes;
	unsigned int cur_size;
	unsigned int lru_size;
	unsigned int nr_unique;
	unsigned int nr_misses;
	unsigned int total;
	int map_fd;
};

static void pfect_lru_init(struct pfect_lru *lru, unsigned int lru_size,
			   unsigned int nr_possible_elems)
{
	lru->map_fd = bpf_map_create(BPF_MAP_TYPE_HASH, NULL,
				     sizeof(unsigned long long),
				     sizeof(struct pfect_lru_node *),
				     nr_possible_elems, NULL);
	assert(lru->map_fd != -1);

	lru->free_nodes = malloc(lru_size * sizeof(struct pfect_lru_node));
	assert(lru->free_nodes);

	INIT_LIST_HEAD(&lru->list);
	lru->cur_size = 0;
	lru->lru_size = lru_size;
	lru->nr_unique = lru->nr_misses = lru->total = 0;
}

static void pfect_lru_destroy(struct pfect_lru *lru)
{
	close(lru->map_fd);
	free(lru->free_nodes);
}

static int pfect_lru_lookup_or_insert(struct pfect_lru *lru,
				      unsigned long long key)
{
	struct pfect_lru_node *node = NULL;
	int seen = 0;

	lru->total++;
	if (!bpf_map_lookup_elem(lru->map_fd, &key, &node)) {
		if (node) {
			list_move(&node->list, &lru->list);
			return 1;
		}
		seen = 1;
	}

	if (lru->cur_size < lru->lru_size) {
		node =  &lru->free_nodes[lru->cur_size++];
		INIT_LIST_HEAD(&node->list);
	} else {
		struct pfect_lru_node *null_node = NULL;

		node = list_last_entry(&lru->list,
				       struct pfect_lru_node,
				       list);
		bpf_map_update_elem(lru->map_fd, &node->key, &null_node, BPF_EXIST);
	}

	node->key = key;
	list_move(&node->list, &lru->list);

	lru->nr_misses++;
	if (seen) {
		assert(!bpf_map_update_elem(lru->map_fd, &key, &node, BPF_EXIST));
	} else {
		lru->nr_unique++;
		assert(!bpf_map_update_elem(lru->map_fd, &key, &node, BPF_NOEXIST));
	}

	return seen;
}

static unsigned int read_keys(const char *dist_file,
			      unsigned long long **keys)
{
	struct stat fst;
	unsigned long long *retkeys;
	unsigned int counts = 0;
	int dist_fd;
	char *b, *l;
	int i;

	dist_fd = open(dist_file, 0);
	assert(dist_fd != -1);

	assert(fstat(dist_fd, &fst) == 0);
	b = malloc(fst.st_size);
	assert(b);

	assert(read(dist_fd, b, fst.st_size) == fst.st_size);
	close(dist_fd);
	for (i = 0; i < fst.st_size; i++) {
		if (b[i] == '\n')
			counts++;
	}
	counts++; /* in case the last line has no \n */

	retkeys = malloc(counts * sizeof(unsigned long long));
	assert(retkeys);

	counts = 0;
	for (l = strtok(b, "\n"); l; l = strtok(NULL, "\n"))
		retkeys[counts++] = strtoull(l, NULL, 10);
	free(b);

	*keys = retkeys;

	return counts;
}

static int create_map(int map_type, int map_flags, unsigned int size)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts,
		.map_flags = map_flags,
	);
	int map_fd;

	map_fd = bpf_map_create(map_type, NULL, sizeof(unsigned long long),
				sizeof(unsigned long long), size, &opts);

	if (map_fd == -1)
		perror("bpf_create_map");

	return map_fd;
}

static int sched_next_online(int pid, int next_to_try)
{
	cpu_set_t cpuset;

	if (next_to_try == nr_cpus)
		return -1;

	while (next_to_try < nr_cpus) {
		CPU_ZERO(&cpuset);
		CPU_SET(next_to_try++, &cpuset);
		if (!sched_setaffinity(pid, sizeof(cpuset), &cpuset))
			break;
	}

	return next_to_try;
}

static void run_parallel(unsigned int tasks, void (*fn)(int i, void *data),
			 void *data)
{
	int next_sched_cpu = 0;
	pid_t pid[tasks];
	int i;

	for (i = 0; i < tasks; i++) {
		pid[i] = fork();
		if (pid[i] == 0) {
			next_sched_cpu = sched_next_online(0, next_sched_cpu);
			fn(i, data);
			exit(0);
		} else if (pid[i] == -1) {
			printf("couldn't spawn #%d process\n", i);
			exit(1);
		}
		/* It is mostly redundant and just allow the parent
		 * process to update next_shced_cpu for the next child
		 * process
		 */
		next_sched_cpu = sched_next_online(pid[i], next_sched_cpu);
	}
	for (i = 0; i < tasks; i++) {
		int status;

		assert(waitpid(pid[i], &status, 0) == pid[i]);
		assert(status == 0);
	}
}

static void do_test_lru_dist(int task, void *data)
{
	unsigned int nr_misses = 0;
	struct pfect_lru pfect_lru;
	unsigned long long key, value = 1234;
	unsigned int i;

	unsigned int lru_map_fd = ((unsigned int *)data)[0];
	unsigned int lru_size = ((unsigned int *)data)[1];
	unsigned long long key_offset = task * dist_key_counts;

	pfect_lru_init(&pfect_lru, lru_size, dist_key_counts);

	for (i = 0; i < dist_key_counts; i++) {
		key = dist_keys[i] + key_offset;

		pfect_lru_lookup_or_insert(&pfect_lru, key);

		if (!bpf_map_lookup_elem(lru_map_fd, &key, &value))
			continue;

		if (bpf_map_update_elem(lru_map_fd, &key, &value, BPF_NOEXIST)) {
			printf("bpf_map_update_elem(lru_map_fd, %llu): errno:%d\n",
			       key, errno);
			assert(0);
		}

		nr_misses++;
	}

	printf("    task:%d BPF LRU: nr_unique:%u(/%u) nr_misses:%u(/%u)\n",
	       task, pfect_lru.nr_unique, dist_key_counts, nr_misses,
	       dist_key_counts);
	printf("    task:%d Perfect LRU: nr_unique:%u(/%u) nr_misses:%u(/%u)\n",
	       task, pfect_lru.nr_unique, pfect_lru.total,
	       pfect_lru.nr_misses, pfect_lru.total);

	pfect_lru_destroy(&pfect_lru);
	close(lru_map_fd);
}

static void test_parallel_lru_dist(int map_type, int map_flags,
				   int nr_tasks, unsigned int lru_size)
{
	int child_data[2];
	int lru_map_fd;

	printf("%s (map_type:%d map_flags:0x%X):\n", __func__, map_type,
	       map_flags);

	if (map_flags & BPF_F_NO_COMMON_LRU)
		lru_map_fd = create_map(map_type, map_flags,
					nr_cpus * lru_size);
	else
		lru_map_fd = create_map(map_type, map_flags,
					nr_tasks * lru_size);
	assert(lru_map_fd != -1);

	child_data[0] = lru_map_fd;
	child_data[1] = lru_size;

	run_parallel(nr_tasks, do_test_lru_dist, child_data);

	close(lru_map_fd);
}

static void test_lru_loss0(int map_type, int map_flags)
{
	unsigned long long key, value[nr_cpus];
	unsigned int old_unused_losses = 0;
	unsigned int new_unused_losses = 0;
	unsigned int used_losses = 0;
	int map_fd;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, 0) != -1);

	if (map_flags & BPF_F_NO_COMMON_LRU)
		map_fd = create_map(map_type, map_flags, 900 * nr_cpus);
	else
		map_fd = create_map(map_type, map_flags, 900);

	assert(map_fd != -1);

	value[0] = 1234;

	for (key = 1; key <= 1000; key++) {
		int start_key, end_key;

		assert(bpf_map_update_elem(map_fd, &key, value, BPF_NOEXIST) == 0);

		start_key = 101;
		end_key = min(key, 900);

		while (start_key <= end_key) {
			bpf_map_lookup_elem(map_fd, &start_key, value);
			start_key++;
		}
	}

	for (key = 1; key <= 1000; key++) {
		if (bpf_map_lookup_elem(map_fd, &key, value)) {
			if (key <= 100)
				old_unused_losses++;
			else if (key <= 900)
				used_losses++;
			else
				new_unused_losses++;
		}
	}

	close(map_fd);

	printf("older-elem-losses:%d(/100) active-elem-losses:%d(/800) "
	       "newer-elem-losses:%d(/100)\n",
	       old_unused_losses, used_losses, new_unused_losses);
}

static void test_lru_loss1(int map_type, int map_flags)
{
	unsigned long long key, value[nr_cpus];
	int map_fd;
	unsigned int nr_losses = 0;

	printf("%s (map_type:%d map_flags:0x%X): ", __func__, map_type,
	       map_flags);

	assert(sched_next_online(0, 0) != -1);

	if (map_flags & BPF_F_NO_COMMON_LRU)
		map_fd = create_map(map_type, map_flags, 1000 * nr_cpus);
	else
		map_fd = create_map(map_type, map_flags, 1000);

	assert(map_fd != -1);

	value[0] = 1234;

	for (key = 1; key <= 1000; key++)
		assert(!bpf_map_update_elem(map_fd, &key, value, BPF_NOEXIST));

	for (key = 1; key <= 1000; key++) {
		if (bpf_map_lookup_elem(map_fd, &key, value))
			nr_losses++;
	}

	close(map_fd);

	printf("nr_losses:%d(/1000)\n", nr_losses);
}

static void do_test_parallel_lru_loss(int task, void *data)
{
	const unsigned int nr_stable_elems = 1000;
	const unsigned int nr_repeats = 100000;

	int map_fd = *(int *)data;
	unsigned long long stable_base;
	unsigned long long key, value[nr_cpus];
	unsigned long long next_ins_key;
	unsigned int nr_losses = 0;
	unsigned int i;

	stable_base = task * nr_repeats * 2 + 1;
	next_ins_key = stable_base;
	value[0] = 1234;
	for (i = 0; i < nr_stable_elems; i++) {
		assert(bpf_map_update_elem(map_fd, &next_ins_key, value,
				       BPF_NOEXIST) == 0);
		next_ins_key++;
	}

	for (i = 0; i < nr_repeats; i++) {
		int rn;

		rn = rand();

		if (rn % 10) {
			key = rn % nr_stable_elems + stable_base;
			bpf_map_lookup_elem(map_fd, &key, value);
		} else {
			bpf_map_update_elem(map_fd, &next_ins_key, value,
					BPF_NOEXIST);
			next_ins_key++;
		}
	}

	key = stable_base;
	for (i = 0; i < nr_stable_elems; i++) {
		if (bpf_map_lookup_elem(map_fd, &key, value))
			nr_losses++;
		key++;
	}

	printf("    task:%d nr_losses:%u\n", task, nr_losses);
}

static void test_parallel_lru_loss(int map_type, int map_flags, int nr_tasks)
{
	int map_fd;

	printf("%s (map_type:%d map_flags:0x%X):\n", __func__, map_type,
	       map_flags);

	/* Give 20% more than the active working set */
	if (map_flags & BPF_F_NO_COMMON_LRU)
		map_fd = create_map(map_type, map_flags,
				    nr_cpus * (1000 + 200));
	else
		map_fd = create_map(map_type, map_flags,
				    nr_tasks * (1000 + 200));

	assert(map_fd != -1);

	run_parallel(nr_tasks, do_test_parallel_lru_loss, &map_fd);

	close(map_fd);
}

int main(int argc, char **argv)
{
	int map_flags[] = {0, BPF_F_NO_COMMON_LRU};
	const char *dist_file;
	int nr_tasks = 1;
	int lru_size;
	int f;

	if (argc < 4) {
		printf("Usage: %s <dist-file> <lru-size> <nr-tasks>\n",
		       argv[0]);
		return -1;
	}

	dist_file = argv[1];
	lru_size = atoi(argv[2]);
	nr_tasks = atoi(argv[3]);

	setbuf(stdout, NULL);

	srand(time(NULL));

	nr_cpus = bpf_num_possible_cpus();
	assert(nr_cpus != -1);
	printf("nr_cpus:%d\n\n", nr_cpus);

	nr_tasks = min(nr_tasks, nr_cpus);

	dist_key_counts = read_keys(dist_file, &dist_keys);
	if (!dist_key_counts) {
		printf("%s has no key\n", dist_file);
		return -1;
	}

	for (f = 0; f < ARRAY_SIZE(map_flags); f++) {
		test_lru_loss0(BPF_MAP_TYPE_LRU_HASH, map_flags[f]);
		test_lru_loss1(BPF_MAP_TYPE_LRU_HASH, map_flags[f]);
		test_parallel_lru_loss(BPF_MAP_TYPE_LRU_HASH, map_flags[f],
				       nr_tasks);
		test_parallel_lru_dist(BPF_MAP_TYPE_LRU_HASH, map_flags[f],
				       nr_tasks, lru_size);
		printf("\n");
	}

	free(dist_keys);

	return 0;
}
