// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC.
 * Author: Suren Baghdasaryan <surenb@google.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Fork a child that concurrently modifies address space while the main
 * process is reading /proc/$PID/maps and verifying the results. Address
 * space modifications include:
 *     VMA splitting and merging
 *
 */
#define _GNU_SOURCE
#include "../kselftest_harness.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

/* /proc/pid/maps parsing routines */
struct page_content {
	char *data;
	ssize_t size;
};

#define LINE_MAX_SIZE		256

struct line_content {
	char text[LINE_MAX_SIZE];
	unsigned long start_addr;
	unsigned long end_addr;
};

enum test_state {
	INIT,
	CHILD_READY,
	PARENT_READY,
	SETUP_READY,
	SETUP_MODIFY_MAPS,
	SETUP_MAPS_MODIFIED,
	SETUP_RESTORE_MAPS,
	SETUP_MAPS_RESTORED,
	TEST_READY,
	TEST_DONE,
};

struct vma_modifier_info;

FIXTURE(proc_maps_race)
{
	struct vma_modifier_info *mod_info;
	struct page_content page1;
	struct page_content page2;
	struct line_content last_line;
	struct line_content first_line;
	unsigned long duration_sec;
	int shared_mem_size;
	int page_size;
	int vma_count;
	bool verbose;
	int maps_fd;
	pid_t pid;
};

typedef bool (*vma_modifier_op)(FIXTURE_DATA(proc_maps_race) *self);
typedef bool (*vma_mod_result_check_op)(struct line_content *mod_last_line,
					struct line_content *mod_first_line,
					struct line_content *restored_last_line,
					struct line_content *restored_first_line);

struct vma_modifier_info {
	int vma_count;
	void *addr;
	int prot;
	void *next_addr;
	vma_modifier_op vma_modify;
	vma_modifier_op vma_restore;
	vma_mod_result_check_op vma_mod_check;
	pthread_mutex_t sync_lock;
	pthread_cond_t sync_cond;
	enum test_state curr_state;
	bool exit;
	void *child_mapped_addr[];
};


static bool read_two_pages(FIXTURE_DATA(proc_maps_race) *self)
{
	ssize_t  bytes_read;

	if (lseek(self->maps_fd, 0, SEEK_SET) < 0)
		return false;

	bytes_read = read(self->maps_fd, self->page1.data, self->page_size);
	if (bytes_read <= 0)
		return false;

	self->page1.size = bytes_read;

	bytes_read = read(self->maps_fd, self->page2.data, self->page_size);
	if (bytes_read <= 0)
		return false;

	self->page2.size = bytes_read;

	return true;
}

static void copy_first_line(struct page_content *page, char *first_line)
{
	char *pos = strchr(page->data, '\n');

	strncpy(first_line, page->data, pos - page->data);
	first_line[pos - page->data] = '\0';
}

static void copy_last_line(struct page_content *page, char *last_line)
{
	/* Get the last line in the first page */
	const char *end = page->data + page->size - 1;
	/* skip last newline */
	const char *pos = end - 1;

	/* search previous newline */
	while (pos[-1] != '\n')
		pos--;
	strncpy(last_line, pos, end - pos);
	last_line[end - pos] = '\0';
}

/* Read the last line of the first page and the first line of the second page */
static bool read_boundary_lines(FIXTURE_DATA(proc_maps_race) *self,
				struct line_content *last_line,
				struct line_content *first_line)
{
	if (!read_two_pages(self))
		return false;

	copy_last_line(&self->page1, last_line->text);
	copy_first_line(&self->page2, first_line->text);

	return sscanf(last_line->text, "%lx-%lx", &last_line->start_addr,
		      &last_line->end_addr) == 2 &&
	       sscanf(first_line->text, "%lx-%lx", &first_line->start_addr,
		      &first_line->end_addr) == 2;
}

/* Thread synchronization routines */
static void wait_for_state(struct vma_modifier_info *mod_info, enum test_state state)
{
	pthread_mutex_lock(&mod_info->sync_lock);
	while (mod_info->curr_state != state)
		pthread_cond_wait(&mod_info->sync_cond, &mod_info->sync_lock);
	pthread_mutex_unlock(&mod_info->sync_lock);
}

static void signal_state(struct vma_modifier_info *mod_info, enum test_state state)
{
	pthread_mutex_lock(&mod_info->sync_lock);
	mod_info->curr_state = state;
	pthread_cond_signal(&mod_info->sync_cond);
	pthread_mutex_unlock(&mod_info->sync_lock);
}

static void stop_vma_modifier(struct vma_modifier_info *mod_info)
{
	wait_for_state(mod_info, SETUP_READY);
	mod_info->exit = true;
	signal_state(mod_info, SETUP_MODIFY_MAPS);
}

static void print_first_lines(char *text, int nr)
{
	const char *end = text;

	while (nr && (end = strchr(end, '\n')) != NULL) {
		nr--;
		end++;
	}

	if (end) {
		int offs = end - text;

		text[offs] = '\0';
		printf("%s", text);
		text[offs] = '\n';
		printf("\n");
	} else {
		printf("%s", text);
	}
}

static void print_last_lines(char *text, int nr)
{
	const char *start = text + strlen(text);

	nr++; /* to ignore the last newline */
	while (nr) {
		while (start > text && *start != '\n')
			start--;
		nr--;
		start--;
	}
	printf("%s", start);
}

static void print_boundaries(const char *title, FIXTURE_DATA(proc_maps_race) *self)
{
	if (!self->verbose)
		return;

	printf("%s", title);
	/* Print 3 boundary lines from each page */
	print_last_lines(self->page1.data, 3);
	printf("-----------------page boundary-----------------\n");
	print_first_lines(self->page2.data, 3);
}

static bool print_boundaries_on(bool condition, const char *title,
				FIXTURE_DATA(proc_maps_race) *self)
{
	if (self->verbose && condition)
		print_boundaries(title, self);

	return condition;
}

static void report_test_start(const char *name, bool verbose)
{
	if (verbose)
		printf("==== %s ====\n", name);
}

static struct timespec print_ts;

static void start_test_loop(struct timespec *ts, bool verbose)
{
	if (verbose)
		print_ts.tv_sec = ts->tv_sec;
}

static void end_test_iteration(struct timespec *ts, bool verbose)
{
	if (!verbose)
		return;

	/* Update every second */
	if (print_ts.tv_sec == ts->tv_sec)
		return;

	printf(".");
	fflush(stdout);
	print_ts.tv_sec = ts->tv_sec;
}

static void end_test_loop(bool verbose)
{
	if (verbose)
		printf("\n");
}

static bool capture_mod_pattern(FIXTURE_DATA(proc_maps_race) *self,
				struct line_content *mod_last_line,
				struct line_content *mod_first_line,
				struct line_content *restored_last_line,
				struct line_content *restored_first_line)
{
	print_boundaries("Before modification", self);

	signal_state(self->mod_info, SETUP_MODIFY_MAPS);
	wait_for_state(self->mod_info, SETUP_MAPS_MODIFIED);

	/* Copy last line of the first page and first line of the last page */
	if (!read_boundary_lines(self, mod_last_line, mod_first_line))
		return false;

	print_boundaries("After modification", self);

	signal_state(self->mod_info, SETUP_RESTORE_MAPS);
	wait_for_state(self->mod_info, SETUP_MAPS_RESTORED);

	/* Copy last line of the first page and first line of the last page */
	if (!read_boundary_lines(self, restored_last_line, restored_first_line))
		return false;

	print_boundaries("After restore", self);

	if (!self->mod_info->vma_mod_check(mod_last_line, mod_first_line,
					   restored_last_line, restored_first_line))
		return false;

	/*
	 * The content of these lines after modify+resore should be the same
	 * as the original.
	 */
	return strcmp(restored_last_line->text, self->last_line.text) == 0 &&
	       strcmp(restored_first_line->text, self->first_line.text) == 0;
}

static bool query_addr_at(int maps_fd, void *addr,
			  unsigned long *vma_start, unsigned long *vma_end)
{
	struct procmap_query q;

	memset(&q, 0, sizeof(q));
	q.size = sizeof(q);
	/* Find the VMA at the split address */
	q.query_addr = (unsigned long long)addr;
	q.query_flags = 0;
	if (ioctl(maps_fd, PROCMAP_QUERY, &q))
		return false;

	*vma_start = q.vma_start;
	*vma_end = q.vma_end;

	return true;
}

static inline bool split_vma(FIXTURE_DATA(proc_maps_race) *self)
{
	return mmap(self->mod_info->addr, self->page_size, self->mod_info->prot | PROT_EXEC,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
}

static inline bool merge_vma(FIXTURE_DATA(proc_maps_race) *self)
{
	return mmap(self->mod_info->addr, self->page_size, self->mod_info->prot,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
}

static inline bool check_split_result(struct line_content *mod_last_line,
				      struct line_content *mod_first_line,
				      struct line_content *restored_last_line,
				      struct line_content *restored_first_line)
{
	/* Make sure vmas at the boundaries are changing */
	return strcmp(mod_last_line->text, restored_last_line->text) != 0 &&
	       strcmp(mod_first_line->text, restored_first_line->text) != 0;
}

static inline bool shrink_vma(FIXTURE_DATA(proc_maps_race) *self)
{
	return mremap(self->mod_info->addr, self->page_size * 3,
		      self->page_size, 0) != MAP_FAILED;
}

static inline bool expand_vma(FIXTURE_DATA(proc_maps_race) *self)
{
	return mremap(self->mod_info->addr, self->page_size,
		      self->page_size * 3, 0) != MAP_FAILED;
}

static inline bool check_shrink_result(struct line_content *mod_last_line,
				       struct line_content *mod_first_line,
				       struct line_content *restored_last_line,
				       struct line_content *restored_first_line)
{
	/* Make sure only the last vma of the first page is changing */
	return strcmp(mod_last_line->text, restored_last_line->text) != 0 &&
	       strcmp(mod_first_line->text, restored_first_line->text) == 0;
}

static inline bool remap_vma(FIXTURE_DATA(proc_maps_race) *self)
{
	/*
	 * Remap the last page of the next vma into the middle of the vma.
	 * This splits the current vma and the first and middle parts (the
	 * parts at lower addresses) become the last vma objserved in the
	 * first page and the first vma observed in the last page.
	 */
	return mremap(self->mod_info->next_addr + self->page_size * 2, self->page_size,
		      self->page_size, MREMAP_FIXED | MREMAP_MAYMOVE | MREMAP_DONTUNMAP,
		      self->mod_info->addr + self->page_size) != MAP_FAILED;
}

static inline bool patch_vma(FIXTURE_DATA(proc_maps_race) *self)
{
	return mprotect(self->mod_info->addr + self->page_size, self->page_size,
			self->mod_info->prot) == 0;
}

static inline bool check_remap_result(struct line_content *mod_last_line,
				      struct line_content *mod_first_line,
				      struct line_content *restored_last_line,
				      struct line_content *restored_first_line)
{
	/* Make sure vmas at the boundaries are changing */
	return strcmp(mod_last_line->text, restored_last_line->text) != 0 &&
	       strcmp(mod_first_line->text, restored_first_line->text) != 0;
}

FIXTURE_SETUP(proc_maps_race)
{
	const char *verbose = getenv("VERBOSE");
	const char *duration = getenv("DURATION");
	struct vma_modifier_info *mod_info;
	pthread_mutexattr_t mutex_attr;
	pthread_condattr_t cond_attr;
	unsigned long duration_sec;
	char fname[32];

	self->page_size = (unsigned long)sysconf(_SC_PAGESIZE);
	self->verbose = verbose && !strncmp(verbose, "1", 1);
	duration_sec = duration ? atol(duration) : 0;
	self->duration_sec = duration_sec ? duration_sec : 5UL;

	/*
	 * Have to map enough vmas for /proc/pid/maps to contain more than one
	 * page worth of vmas. Assume at least 32 bytes per line in maps output
	 */
	self->vma_count = self->page_size / 32 + 1;
	self->shared_mem_size = sizeof(struct vma_modifier_info) + self->vma_count * sizeof(void *);

	/* map shared memory for communication with the child process */
	self->mod_info = (struct vma_modifier_info *)mmap(NULL, self->shared_mem_size,
				PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	ASSERT_NE(self->mod_info, MAP_FAILED);
	mod_info = self->mod_info;

	/* Initialize shared members */
	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
	ASSERT_EQ(pthread_mutex_init(&mod_info->sync_lock, &mutex_attr), 0);
	pthread_condattr_init(&cond_attr);
	pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
	ASSERT_EQ(pthread_cond_init(&mod_info->sync_cond, &cond_attr), 0);
	mod_info->vma_count = self->vma_count;
	mod_info->curr_state = INIT;
	mod_info->exit = false;

	self->pid = fork();
	if (!self->pid) {
		/* Child process modifying the address space */
		int prot = PROT_READ | PROT_WRITE;
		int i;

		for (i = 0; i < mod_info->vma_count; i++) {
			mod_info->child_mapped_addr[i] = mmap(NULL, self->page_size * 3, prot,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			ASSERT_NE(mod_info->child_mapped_addr[i], MAP_FAILED);
			/* change protection in adjacent maps to prevent merging */
			prot ^= PROT_WRITE;
		}
		signal_state(mod_info, CHILD_READY);
		wait_for_state(mod_info, PARENT_READY);
		while (true) {
			signal_state(mod_info, SETUP_READY);
			wait_for_state(mod_info, SETUP_MODIFY_MAPS);
			if (mod_info->exit)
				break;

			ASSERT_TRUE(mod_info->vma_modify(self));
			signal_state(mod_info, SETUP_MAPS_MODIFIED);
			wait_for_state(mod_info, SETUP_RESTORE_MAPS);
			ASSERT_TRUE(mod_info->vma_restore(self));
			signal_state(mod_info, SETUP_MAPS_RESTORED);

			wait_for_state(mod_info, TEST_READY);
			while (mod_info->curr_state != TEST_DONE) {
				ASSERT_TRUE(mod_info->vma_modify(self));
				ASSERT_TRUE(mod_info->vma_restore(self));
			}
		}
		for (i = 0; i < mod_info->vma_count; i++)
			munmap(mod_info->child_mapped_addr[i], self->page_size * 3);

		exit(0);
	}

	sprintf(fname, "/proc/%d/maps", self->pid);
	self->maps_fd = open(fname, O_RDONLY);
	ASSERT_NE(self->maps_fd, -1);

	/* Wait for the child to map the VMAs */
	wait_for_state(mod_info, CHILD_READY);

	/* Read first two pages */
	self->page1.data = malloc(self->page_size);
	ASSERT_NE(self->page1.data, NULL);
	self->page2.data = malloc(self->page_size);
	ASSERT_NE(self->page2.data, NULL);

	ASSERT_TRUE(read_boundary_lines(self, &self->last_line, &self->first_line));

	/*
	 * Find the addresses corresponding to the last line in the first page
	 * and the first line in the last page.
	 */
	mod_info->addr = NULL;
	mod_info->next_addr = NULL;
	for (int i = 0; i < mod_info->vma_count; i++) {
		if (mod_info->child_mapped_addr[i] == (void *)self->last_line.start_addr) {
			mod_info->addr = mod_info->child_mapped_addr[i];
			mod_info->prot = PROT_READ;
			/* Even VMAs have write permission */
			if ((i % 2) == 0)
				mod_info->prot |= PROT_WRITE;
		} else if (mod_info->child_mapped_addr[i] == (void *)self->first_line.start_addr) {
			mod_info->next_addr = mod_info->child_mapped_addr[i];
		}

		if (mod_info->addr && mod_info->next_addr)
			break;
	}
	ASSERT_TRUE(mod_info->addr && mod_info->next_addr);

	signal_state(mod_info, PARENT_READY);

}

FIXTURE_TEARDOWN(proc_maps_race)
{
	int status;

	stop_vma_modifier(self->mod_info);

	free(self->page2.data);
	free(self->page1.data);

	for (int i = 0; i < self->vma_count; i++)
		munmap(self->mod_info->child_mapped_addr[i], self->page_size);
	close(self->maps_fd);
	waitpid(self->pid, &status, 0);
	munmap(self->mod_info, self->shared_mem_size);
}

TEST_F(proc_maps_race, test_maps_tearing_from_split)
{
	struct vma_modifier_info *mod_info = self->mod_info;

	struct line_content split_last_line;
	struct line_content split_first_line;
	struct line_content restored_last_line;
	struct line_content restored_first_line;

	wait_for_state(mod_info, SETUP_READY);

	/* re-read the file to avoid using stale data from previous test */
	ASSERT_TRUE(read_boundary_lines(self, &self->last_line, &self->first_line));

	mod_info->vma_modify = split_vma;
	mod_info->vma_restore = merge_vma;
	mod_info->vma_mod_check = check_split_result;

	report_test_start("Tearing from split", self->verbose);
	ASSERT_TRUE(capture_mod_pattern(self, &split_last_line, &split_first_line,
					&restored_last_line, &restored_first_line));

	/* Now start concurrent modifications for self->duration_sec */
	signal_state(mod_info, TEST_READY);

	struct line_content new_last_line;
	struct line_content new_first_line;
	struct timespec start_ts, end_ts;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &start_ts);
	start_test_loop(&start_ts, self->verbose);
	do {
		bool last_line_changed;
		bool first_line_changed;
		unsigned long vma_start;
		unsigned long vma_end;

		ASSERT_TRUE(read_boundary_lines(self, &new_last_line, &new_first_line));

		/* Check if we read vmas after split */
		if (!strcmp(new_last_line.text, split_last_line.text)) {
			/*
			 * The vmas should be consistent with split results,
			 * however if vma was concurrently restored after a
			 * split, it can be reported twice (first the original
			 * split one, then the same vma but extended after the
			 * merge) because we found it as the next vma again.
			 * In that case new first line will be the same as the
			 * last restored line.
			 */
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_first_line.text, split_first_line.text) &&
					strcmp(new_first_line.text, restored_last_line.text),
					"Split result invalid", self));
		} else {
			/* The vmas should be consistent with merge results */
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_last_line.text, restored_last_line.text),
					"Merge result invalid", self));
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_first_line.text, restored_first_line.text),
					"Merge result invalid", self));
		}
		/*
		 * First and last lines should change in unison. If the last
		 * line changed then the first line should change as well and
		 * vice versa.
		 */
		last_line_changed = strcmp(new_last_line.text, self->last_line.text) != 0;
		first_line_changed = strcmp(new_first_line.text, self->first_line.text) != 0;
		ASSERT_EQ(last_line_changed, first_line_changed);

		/* Check if PROCMAP_QUERY ioclt() finds the right VMA */
		ASSERT_TRUE(query_addr_at(self->maps_fd, mod_info->addr + self->page_size,
					  &vma_start, &vma_end));
		/*
		 * The vma at the split address can be either the same as
		 * original one (if read before the split) or the same as the
		 * first line in the second page (if read after the split).
		 */
		ASSERT_TRUE((vma_start == self->last_line.start_addr &&
			     vma_end == self->last_line.end_addr) ||
			    (vma_start == split_first_line.start_addr &&
			     vma_end == split_first_line.end_addr));

		clock_gettime(CLOCK_MONOTONIC_COARSE, &end_ts);
		end_test_iteration(&end_ts, self->verbose);
	} while (end_ts.tv_sec - start_ts.tv_sec < self->duration_sec);
	end_test_loop(self->verbose);

	/* Signal the modifyer thread to stop and wait until it exits */
	signal_state(mod_info, TEST_DONE);
}

TEST_F(proc_maps_race, test_maps_tearing_from_resize)
{
	struct vma_modifier_info *mod_info = self->mod_info;

	struct line_content shrunk_last_line;
	struct line_content shrunk_first_line;
	struct line_content restored_last_line;
	struct line_content restored_first_line;

	wait_for_state(mod_info, SETUP_READY);

	/* re-read the file to avoid using stale data from previous test */
	ASSERT_TRUE(read_boundary_lines(self, &self->last_line, &self->first_line));

	mod_info->vma_modify = shrink_vma;
	mod_info->vma_restore = expand_vma;
	mod_info->vma_mod_check = check_shrink_result;

	report_test_start("Tearing from resize", self->verbose);
	ASSERT_TRUE(capture_mod_pattern(self, &shrunk_last_line, &shrunk_first_line,
					&restored_last_line, &restored_first_line));

	/* Now start concurrent modifications for self->duration_sec */
	signal_state(mod_info, TEST_READY);

	struct line_content new_last_line;
	struct line_content new_first_line;
	struct timespec start_ts, end_ts;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &start_ts);
	start_test_loop(&start_ts, self->verbose);
	do {
		unsigned long vma_start;
		unsigned long vma_end;

		ASSERT_TRUE(read_boundary_lines(self, &new_last_line, &new_first_line));

		/* Check if we read vmas after shrinking it */
		if (!strcmp(new_last_line.text, shrunk_last_line.text)) {
			/*
			 * The vmas should be consistent with shrunk results,
			 * however if the vma was concurrently restored, it
			 * can be reported twice (first as shrunk one, then
			 * as restored one) because we found it as the next vma
			 * again. In that case new first line will be the same
			 * as the last restored line.
			 */
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_first_line.text, shrunk_first_line.text) &&
					strcmp(new_first_line.text, restored_last_line.text),
					"Shrink result invalid", self));
		} else {
			/* The vmas should be consistent with the original/resored state */
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_last_line.text, restored_last_line.text),
					"Expand result invalid", self));
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_first_line.text, restored_first_line.text),
					"Expand result invalid", self));
		}

		/* Check if PROCMAP_QUERY ioclt() finds the right VMA */
		ASSERT_TRUE(query_addr_at(self->maps_fd, mod_info->addr, &vma_start, &vma_end));
		/*
		 * The vma should stay at the same address and have either the
		 * original size of 3 pages or 1 page if read after shrinking.
		 */
		ASSERT_TRUE(vma_start == self->last_line.start_addr &&
			    (vma_end - vma_start == self->page_size * 3 ||
			     vma_end - vma_start == self->page_size));

		clock_gettime(CLOCK_MONOTONIC_COARSE, &end_ts);
		end_test_iteration(&end_ts, self->verbose);
	} while (end_ts.tv_sec - start_ts.tv_sec < self->duration_sec);
	end_test_loop(self->verbose);

	/* Signal the modifyer thread to stop and wait until it exits */
	signal_state(mod_info, TEST_DONE);
}

TEST_F(proc_maps_race, test_maps_tearing_from_remap)
{
	struct vma_modifier_info *mod_info = self->mod_info;

	struct line_content remapped_last_line;
	struct line_content remapped_first_line;
	struct line_content restored_last_line;
	struct line_content restored_first_line;

	wait_for_state(mod_info, SETUP_READY);

	/* re-read the file to avoid using stale data from previous test */
	ASSERT_TRUE(read_boundary_lines(self, &self->last_line, &self->first_line));

	mod_info->vma_modify = remap_vma;
	mod_info->vma_restore = patch_vma;
	mod_info->vma_mod_check = check_remap_result;

	report_test_start("Tearing from remap", self->verbose);
	ASSERT_TRUE(capture_mod_pattern(self, &remapped_last_line, &remapped_first_line,
					&restored_last_line, &restored_first_line));

	/* Now start concurrent modifications for self->duration_sec */
	signal_state(mod_info, TEST_READY);

	struct line_content new_last_line;
	struct line_content new_first_line;
	struct timespec start_ts, end_ts;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &start_ts);
	start_test_loop(&start_ts, self->verbose);
	do {
		unsigned long vma_start;
		unsigned long vma_end;

		ASSERT_TRUE(read_boundary_lines(self, &new_last_line, &new_first_line));

		/* Check if we read vmas after remapping it */
		if (!strcmp(new_last_line.text, remapped_last_line.text)) {
			/*
			 * The vmas should be consistent with remap results,
			 * however if the vma was concurrently restored, it
			 * can be reported twice (first as split one, then
			 * as restored one) because we found it as the next vma
			 * again. In that case new first line will be the same
			 * as the last restored line.
			 */
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_first_line.text, remapped_first_line.text) &&
					strcmp(new_first_line.text, restored_last_line.text),
					"Remap result invalid", self));
		} else {
			/* The vmas should be consistent with the original/resored state */
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_last_line.text, restored_last_line.text),
					"Remap restore result invalid", self));
			ASSERT_FALSE(print_boundaries_on(
					strcmp(new_first_line.text, restored_first_line.text),
					"Remap restore result invalid", self));
		}

		/* Check if PROCMAP_QUERY ioclt() finds the right VMA */
		ASSERT_TRUE(query_addr_at(self->maps_fd, mod_info->addr + self->page_size,
					  &vma_start, &vma_end));
		/*
		 * The vma should either stay at the same address and have the
		 * original size of 3 pages or we should find the remapped vma
		 * at the remap destination address with size of 1 page.
		 */
		ASSERT_TRUE((vma_start == self->last_line.start_addr &&
			     vma_end - vma_start == self->page_size * 3) ||
			    (vma_start == self->last_line.start_addr + self->page_size &&
			     vma_end - vma_start == self->page_size));

		clock_gettime(CLOCK_MONOTONIC_COARSE, &end_ts);
		end_test_iteration(&end_ts, self->verbose);
	} while (end_ts.tv_sec - start_ts.tv_sec < self->duration_sec);
	end_test_loop(self->verbose);

	/* Signal the modifyer thread to stop and wait until it exits */
	signal_state(mod_info, TEST_DONE);
}

TEST_HARNESS_MAIN
