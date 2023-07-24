// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "mlock2.h"

#include "../kselftest.h"

struct vm_boundaries {
	unsigned long start;
	unsigned long end;
};

static int get_vm_area(unsigned long addr, struct vm_boundaries *area)
{
	FILE *file;
	int ret = 1;
	char line[1024] = {0};
	char *end_addr;
	char *stop;
	unsigned long start;
	unsigned long end;

	if (!area)
		return ret;

	file = fopen("/proc/self/maps", "r");
	if (!file) {
		perror("fopen");
		return ret;
	}

	memset(area, 0, sizeof(struct vm_boundaries));

	while(fgets(line, 1024, file)) {
		end_addr = strchr(line, '-');
		if (!end_addr) {
			printf("cannot parse /proc/self/maps\n");
			goto out;
		}
		*end_addr = '\0';
		end_addr++;
		stop = strchr(end_addr, ' ');
		if (!stop) {
			printf("cannot parse /proc/self/maps\n");
			goto out;
		}

		sscanf(line, "%lx", &start);
		sscanf(end_addr, "%lx", &end);

		if (start <= addr && end > addr) {
			area->start = start;
			area->end = end;
			ret = 0;
			goto out;
		}
	}
out:
	fclose(file);
	return ret;
}

#define VMFLAGS "VmFlags:"

static bool is_vmflag_set(unsigned long addr, const char *vmflag)
{
	char *line = NULL;
	char *flags;
	size_t size = 0;
	bool ret = false;
	FILE *smaps;

	smaps = seek_to_smaps_entry(addr);
	if (!smaps) {
		printf("Unable to parse /proc/self/smaps\n");
		goto out;
	}

	while (getline(&line, &size, smaps) > 0) {
		if (!strstr(line, VMFLAGS)) {
			free(line);
			line = NULL;
			size = 0;
			continue;
		}

		flags = line + strlen(VMFLAGS);
		ret = (strstr(flags, vmflag) != NULL);
		goto out;
	}

out:
	free(line);
	fclose(smaps);
	return ret;
}

#define SIZE "Size:"
#define RSS  "Rss:"
#define LOCKED "lo"

static unsigned long get_value_for_name(unsigned long addr, const char *name)
{
	char *line = NULL;
	size_t size = 0;
	char *value_ptr;
	FILE *smaps = NULL;
	unsigned long value = -1UL;

	smaps = seek_to_smaps_entry(addr);
	if (!smaps) {
		printf("Unable to parse /proc/self/smaps\n");
		goto out;
	}

	while (getline(&line, &size, smaps) > 0) {
		if (!strstr(line, name)) {
			free(line);
			line = NULL;
			size = 0;
			continue;
		}

		value_ptr = line + strlen(name);
		if (sscanf(value_ptr, "%lu kB", &value) < 1) {
			printf("Unable to parse smaps entry for Size\n");
			goto out;
		}
		break;
	}

out:
	if (smaps)
		fclose(smaps);
	free(line);
	return value;
}

static bool is_vma_lock_on_fault(unsigned long addr)
{
	bool locked;
	unsigned long vma_size, vma_rss;

	locked = is_vmflag_set(addr, LOCKED);
	if (!locked)
		return false;

	vma_size = get_value_for_name(addr, SIZE);
	vma_rss = get_value_for_name(addr, RSS);

	/* only one page is faulted in */
	return (vma_rss < vma_size);
}

#define PRESENT_BIT     0x8000000000000000ULL
#define PFN_MASK        0x007FFFFFFFFFFFFFULL
#define UNEVICTABLE_BIT (1UL << 18)

static int lock_check(unsigned long addr)
{
	bool locked;
	unsigned long vma_size, vma_rss;

	locked = is_vmflag_set(addr, LOCKED);
	if (!locked)
		return false;

	vma_size = get_value_for_name(addr, SIZE);
	vma_rss = get_value_for_name(addr, RSS);

	return (vma_rss == vma_size);
}

static int unlock_lock_check(char *map)
{
	if (is_vmflag_set((unsigned long)map, LOCKED)) {
		printf("VMA flag %s is present on page 1 after unlock\n", LOCKED);
		return 1;
	}

	return 0;
}

static int test_mlock_lock()
{
	char *map;
	int ret = 1;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED) {
		perror("test_mlock_locked mmap");
		goto out;
	}

	if (mlock2_(map, 2 * page_size, 0)) {
		if (errno == ENOSYS) {
			printf("Cannot call new mlock family, skipping test\n");
			_exit(KSFT_SKIP);
		}
		perror("mlock2(0)");
		goto unmap;
	}

	if (!lock_check((unsigned long)map))
		goto unmap;

	/* Now unlock and recheck attributes */
	if (munlock(map, 2 * page_size)) {
		perror("munlock()");
		goto unmap;
	}

	ret = unlock_lock_check(map);

unmap:
	munmap(map, 2 * page_size);
out:
	return ret;
}

static int onfault_check(char *map)
{
	*map = 'a';
	if (!is_vma_lock_on_fault((unsigned long)map)) {
		printf("VMA is not marked for lock on fault\n");
		return 1;
	}

	return 0;
}

static int unlock_onfault_check(char *map)
{
	unsigned long page_size = getpagesize();

	if (is_vma_lock_on_fault((unsigned long)map) ||
	    is_vma_lock_on_fault((unsigned long)map + page_size)) {
		printf("VMA is still lock on fault after unlock\n");
		return 1;
	}

	return 0;
}

static int test_mlock_onfault()
{
	char *map;
	int ret = 1;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED) {
		perror("test_mlock_locked mmap");
		goto out;
	}

	if (mlock2_(map, 2 * page_size, MLOCK_ONFAULT)) {
		if (errno == ENOSYS) {
			printf("Cannot call new mlock family, skipping test\n");
			_exit(KSFT_SKIP);
		}
		perror("mlock2(MLOCK_ONFAULT)");
		goto unmap;
	}

	if (onfault_check(map))
		goto unmap;

	/* Now unlock and recheck attributes */
	if (munlock(map, 2 * page_size)) {
		if (errno == ENOSYS) {
			printf("Cannot call new mlock family, skipping test\n");
			_exit(KSFT_SKIP);
		}
		perror("munlock()");
		goto unmap;
	}

	ret = unlock_onfault_check(map);
unmap:
	munmap(map, 2 * page_size);
out:
	return ret;
}

static int test_lock_onfault_of_present()
{
	char *map;
	int ret = 1;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED) {
		perror("test_mlock_locked mmap");
		goto out;
	}

	*map = 'a';

	if (mlock2_(map, 2 * page_size, MLOCK_ONFAULT)) {
		if (errno == ENOSYS) {
			printf("Cannot call new mlock family, skipping test\n");
			_exit(KSFT_SKIP);
		}
		perror("mlock2(MLOCK_ONFAULT)");
		goto unmap;
	}

	if (!is_vma_lock_on_fault((unsigned long)map) ||
	    !is_vma_lock_on_fault((unsigned long)map + page_size)) {
		printf("VMA with present pages is not marked lock on fault\n");
		goto unmap;
	}
	ret = 0;
unmap:
	munmap(map, 2 * page_size);
out:
	return ret;
}

static int test_munlockall()
{
	char *map;
	int ret = 1;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (map == MAP_FAILED) {
		perror("test_munlockall mmap");
		goto out;
	}

	if (mlockall(MCL_CURRENT)) {
		perror("mlockall(MCL_CURRENT)");
		goto out;
	}

	if (!lock_check((unsigned long)map))
		goto unmap;

	if (munlockall()) {
		perror("munlockall()");
		goto unmap;
	}

	if (unlock_lock_check(map))
		goto unmap;

	munmap(map, 2 * page_size);

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

	if (map == MAP_FAILED) {
		perror("test_munlockall second mmap");
		goto out;
	}

	if (mlockall(MCL_CURRENT | MCL_ONFAULT)) {
		perror("mlockall(MCL_CURRENT | MCL_ONFAULT)");
		goto unmap;
	}

	if (onfault_check(map))
		goto unmap;

	if (munlockall()) {
		perror("munlockall()");
		goto unmap;
	}

	if (unlock_onfault_check(map))
		goto unmap;

	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		perror("mlockall(MCL_CURRENT | MCL_FUTURE)");
		goto out;
	}

	if (!lock_check((unsigned long)map))
		goto unmap;

	if (munlockall()) {
		perror("munlockall()");
		goto unmap;
	}

	ret = unlock_lock_check(map);

unmap:
	munmap(map, 2 * page_size);
out:
	munlockall();
	return ret;
}

static int test_vma_management(bool call_mlock)
{
	int ret = 1;
	void *map;
	unsigned long page_size = getpagesize();
	struct vm_boundaries page1;
	struct vm_boundaries page2;
	struct vm_boundaries page3;

	map = mmap(NULL, 3 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED) {
		perror("mmap()");
		return ret;
	}

	if (call_mlock && mlock2_(map, 3 * page_size, MLOCK_ONFAULT)) {
		if (errno == ENOSYS) {
			printf("Cannot call new mlock family, skipping test\n");
			_exit(KSFT_SKIP);
		}
		perror("mlock(ONFAULT)\n");
		goto out;
	}

	if (get_vm_area((unsigned long)map, &page1) ||
	    get_vm_area((unsigned long)map + page_size, &page2) ||
	    get_vm_area((unsigned long)map + page_size * 2, &page3)) {
		printf("couldn't find mapping in /proc/self/maps\n");
		goto out;
	}

	/*
	 * Before we unlock a portion, we need to that all three pages are in
	 * the same VMA.  If they are not we abort this test (Note that this is
	 * not a failure)
	 */
	if (page1.start != page2.start || page2.start != page3.start) {
		printf("VMAs are not merged to start, aborting test\n");
		ret = 0;
		goto out;
	}

	if (munlock(map + page_size, page_size)) {
		perror("munlock()");
		goto out;
	}

	if (get_vm_area((unsigned long)map, &page1) ||
	    get_vm_area((unsigned long)map + page_size, &page2) ||
	    get_vm_area((unsigned long)map + page_size * 2, &page3)) {
		printf("couldn't find mapping in /proc/self/maps\n");
		goto out;
	}

	/* All three VMAs should be different */
	if (page1.start == page2.start || page2.start == page3.start) {
		printf("failed to split VMA for munlock\n");
		goto out;
	}

	/* Now unlock the first and third page and check the VMAs again */
	if (munlock(map, page_size * 3)) {
		perror("munlock()");
		goto out;
	}

	if (get_vm_area((unsigned long)map, &page1) ||
	    get_vm_area((unsigned long)map + page_size, &page2) ||
	    get_vm_area((unsigned long)map + page_size * 2, &page3)) {
		printf("couldn't find mapping in /proc/self/maps\n");
		goto out;
	}

	/* Now all three VMAs should be the same */
	if (page1.start != page2.start || page2.start != page3.start) {
		printf("failed to merge VMAs after munlock\n");
		goto out;
	}

	ret = 0;
out:
	munmap(map, 3 * page_size);
	return ret;
}

static int test_mlockall(int (test_function)(bool call_mlock))
{
	int ret = 1;

	if (mlockall(MCL_CURRENT | MCL_ONFAULT | MCL_FUTURE)) {
		perror("mlockall");
		return ret;
	}

	ret = test_function(false);
	munlockall();
	return ret;
}

int main(int argc, char **argv)
{
	int ret = 0;
	ret += test_mlock_lock();
	ret += test_mlock_onfault();
	ret += test_munlockall();
	ret += test_lock_onfault_of_present();
	ret += test_vma_management(true);
	ret += test_mlockall(test_vma_management);
	return ret;
}
