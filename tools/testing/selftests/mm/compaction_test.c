// SPDX-License-Identifier: GPL-2.0
/*
 *
 * A test for the patch "Allow compaction of unevictable pages".
 * With this patch we should be able to allocate at least 1/4
 * of RAM in huge pages. Without the patch much less is
 * allocated.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "kselftest.h"
#include "hugepage_settings.h"

#define MAP_SIZE_MB	100
#define MAP_SIZE	(MAP_SIZE_MB * 1024 * 1024)

struct map_list {
	void *map;
	struct map_list *next;
};

int read_memory_info(unsigned long *memfree, unsigned long *hugepagesize)
{
	char  buffer[256] = {0};
	char *cmd = "cat /proc/meminfo | grep -i memfree | grep -o '[0-9]*'";
	FILE *cmdfile = popen(cmd, "r");

	if (!(fgets(buffer, sizeof(buffer), cmdfile))) {
		ksft_print_msg("Failed to read meminfo: %s\n", strerror(errno));
		return -1;
	}

	pclose(cmdfile);

	*memfree = atoll(buffer);
	cmd = "cat /proc/meminfo | grep -i hugepagesize | grep -o '[0-9]*'";
	cmdfile = popen(cmd, "r");

	if (!(fgets(buffer, sizeof(buffer), cmdfile))) {
		ksft_print_msg("Failed to read meminfo: %s\n", strerror(errno));
		return -1;
	}

	pclose(cmdfile);
	*hugepagesize = atoll(buffer);

	return 0;
}

int prereq(void)
{
	char allowed;
	int fd;

	fd = open("/proc/sys/vm/compact_unevictable_allowed",
		  O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		ksft_print_msg("Failed to open /proc/sys/vm/compact_unevictable_allowed: %s\n",
			       strerror(errno));
		return -1;
	}

	if (read(fd, &allowed, sizeof(char)) != sizeof(char)) {
		ksft_print_msg("Failed to read from /proc/sys/vm/compact_unevictable_allowed: %s\n",
			       strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	if (allowed == '1')
		return 0;

	ksft_print_msg("Compaction isn't allowed\n");
	return -1;
}

int check_compaction(unsigned long mem_free, unsigned long hugepage_size)
{
	unsigned long nr_hugepages;
	int compaction_index = 0;
	int ret = -1;

	/* We want to test with 80% of available memory. Else, OOM killer comes
	   in to play */
	mem_free = mem_free * 0.8;

	/*
	 * Request huge pages for about half of the free memory. The Kernel
	 * will allocate as much as it can, and we expect it will get at least 1/3
	 */
	nr_hugepages = mem_free / hugepage_size / 2;
	hugetlb_set_nr_default_pages(nr_hugepages);

	/* We should have been able to request at least 1/3 rd of the memory in
	   huge pages */
	nr_hugepages = hugetlb_nr_default_pages();
	if (!nr_hugepages) {
		ksft_print_msg("ERROR: No memory is available as huge pages\n");
		goto out;
	}
	compaction_index = mem_free/(nr_hugepages * hugepage_size);

	ksft_print_msg("Number of huge pages allocated = %lu\n", nr_hugepages);

	if (compaction_index > 3) {
		ksft_print_msg("ERROR: Less than 1/%d of memory is available\n"
			       "as huge pages\n", compaction_index);
		goto out;
	}

	ret = 0;

 out:
	ksft_test_result(ret == 0, "check_compaction\n");
	return ret;
}

int main(int argc, char **argv)
{
	struct rlimit lim;
	struct map_list *list = NULL, *entry;
	size_t page_size, i;
	void *map = NULL;
	unsigned long mem_free = 0;
	unsigned long hugepage_size = 0;
	long mem_fragmentable_MB = 0;

	ksft_print_header();

	if (prereq() || geteuid())
		ksft_exit_skip("Prerequisites unsatisfied\n");

	/* Start the test without hugepages reducing mem_free */
	if (!hugetlb_setup_default_exact(0))
		ksft_exit_skip("Could not reset nr_hugepages\n");

	ksft_set_plan(1);

	lim.rlim_cur = RLIM_INFINITY;
	lim.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_MEMLOCK, &lim))
		ksft_exit_fail_msg("Failed to set rlimit: %s\n", strerror(errno));

	page_size = getpagesize();

	if (read_memory_info(&mem_free, &hugepage_size) != 0)
		ksft_exit_fail_msg("Failed to get meminfo\n");

	mem_fragmentable_MB = mem_free * 0.8 / 1024;

	while (mem_fragmentable_MB > 0) {
		map = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE,
			   MAP_ANONYMOUS | MAP_PRIVATE | MAP_LOCKED, -1, 0);
		if (map == MAP_FAILED)
			break;

		entry = malloc(sizeof(struct map_list));
		if (!entry) {
			munmap(map, MAP_SIZE);
			break;
		}
		entry->map = map;
		entry->next = list;
		list = entry;

		/* Write something (in this case the address of the map) to
		 * ensure that KSM can't merge the mapped pages
		 */
		for (i = 0; i < MAP_SIZE; i += page_size)
			*(unsigned long *)(map + i) = (unsigned long)map + i;

		mem_fragmentable_MB -= MAP_SIZE_MB;
	}

	/* Unmap every other entry in the list to create fragmentation with
	 * locked pages before invoking check_compaction().
	 */
	for (entry = list; entry != NULL; entry = entry->next) {
		munmap(entry->map, MAP_SIZE);
		if (!entry->next)
			break;
		entry = entry->next;
	}

	if (check_compaction(mem_free, hugepage_size) == 0)
		ksft_exit_pass();

	ksft_exit_fail();
}
