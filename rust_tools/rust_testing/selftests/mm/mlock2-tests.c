// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "../kselftest.h"
#include "mlock2.h"

struct vm_boundaries {
	unsigned long start;
	unsigned long end;
};

static int get_vm_area(unsigned long addr, struct vm_boundaries *area)
{
	FILE *file;
	int ret = 1;
	char line[1024] = {0};
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
		if (sscanf(line, "%lx-%lx", &start, &end) != 2) {
			ksft_print_msg("cannot parse /proc/self/maps\n");
			goto out;
		}

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
		ksft_print_msg("Unable to parse /proc/self/smaps\n");
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
		ksft_print_msg("Unable to parse /proc/self/smaps\n");
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
			ksft_print_msg("Unable to parse smaps entry for Size\n");
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
		ksft_print_msg("VMA flag %s is present on page 1 after unlock\n", LOCKED);
		return 1;
	}

	return 0;
}

static void test_mlock_lock(void)
{
	char *map;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s", strerror(errno));

	if (mlock2_(map, 2 * page_size, 0)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("mlock2(0): %s\n", strerror(errno));
	}

	ksft_test_result(lock_check((unsigned long)map), "%s: Locked\n", __func__);

	/* Now unlock and recheck attributes */
	if (munlock(map, 2 * page_size)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("munlock(): %s\n", strerror(errno));
	}

	ksft_test_result(!unlock_lock_check(map), "%s: Unlocked\n", __func__);
	munmap(map, 2 * page_size);
}

static int onfault_check(char *map)
{
	*map = 'a';
	if (!is_vma_lock_on_fault((unsigned long)map)) {
		ksft_print_msg("VMA is not marked for lock on fault\n");
		return 1;
	}

	return 0;
}

static int unlock_onfault_check(char *map)
{
	unsigned long page_size = getpagesize();

	if (is_vma_lock_on_fault((unsigned long)map) ||
	    is_vma_lock_on_fault((unsigned long)map + page_size)) {
		ksft_print_msg("VMA is still lock on fault after unlock\n");
		return 1;
	}

	return 0;
}

static void test_mlock_onfault(void)
{
	char *map;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s", strerror(errno));

	if (mlock2_(map, 2 * page_size, MLOCK_ONFAULT)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("mlock2(MLOCK_ONFAULT): %s\n", strerror(errno));
	}

	ksft_test_result(!onfault_check(map), "%s: VMA marked for lock on fault\n", __func__);

	/* Now unlock and recheck attributes */
	if (munlock(map, 2 * page_size)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("munlock(): %s\n", strerror(errno));
	}

	ksft_test_result(!unlock_onfault_check(map), "VMA open lock after fault\n");
	munmap(map, 2 * page_size);
}

static void test_lock_onfault_of_present(void)
{
	char *map;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s", strerror(errno));

	*map = 'a';

	if (mlock2_(map, 2 * page_size, MLOCK_ONFAULT)) {
		munmap(map, 2 * page_size);
		ksft_test_result_fail("mlock2(MLOCK_ONFAULT) error: %s", strerror(errno));
	}

	ksft_test_result(is_vma_lock_on_fault((unsigned long)map) ||
			 is_vma_lock_on_fault((unsigned long)map + page_size),
			 "VMA with present pages is not marked lock on fault\n");
	munmap(map, 2 * page_size);
}

static void test_munlockall0(void)
{
	char *map;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s\n", strerror(errno));

	if (mlockall(MCL_CURRENT)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("mlockall(MCL_CURRENT): %s\n", strerror(errno));
	}

	ksft_test_result(lock_check((unsigned long)map), "%s: Locked memory area\n", __func__);

	if (munlockall()) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("munlockall(): %s\n", strerror(errno));
	}

	ksft_test_result(!unlock_lock_check(map), "%s: No locked memory\n", __func__);
	munmap(map, 2 * page_size);
}

static void test_munlockall1(void)
{
	char *map;
	unsigned long page_size = getpagesize();

	map = mmap(NULL, 2 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s", strerror(errno));

	if (mlockall(MCL_CURRENT | MCL_ONFAULT)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("mlockall(MCL_CURRENT | MCL_ONFAULT): %s\n", strerror(errno));
	}

	ksft_test_result(!onfault_check(map), "%s: VMA marked for lock on fault\n", __func__);

	if (munlockall()) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("munlockall(): %s\n", strerror(errno));
	}

	ksft_test_result(!unlock_onfault_check(map), "%s: Unlocked\n", __func__);

	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("mlockall(MCL_CURRENT | MCL_FUTURE): %s\n", strerror(errno));
	}

	ksft_test_result(lock_check((unsigned long)map), "%s: Locked\n", __func__);

	if (munlockall()) {
		munmap(map, 2 * page_size);
		ksft_exit_fail_msg("munlockall() %s\n", strerror(errno));
	}

	ksft_test_result(!unlock_lock_check(map), "%s: No locked memory\n", __func__);
	munmap(map, 2 * page_size);
}

static void test_vma_management(bool call_mlock)
{
	void *map;
	unsigned long page_size = getpagesize();
	struct vm_boundaries page1;
	struct vm_boundaries page2;
	struct vm_boundaries page3;

	map = mmap(NULL, 3 * page_size, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s", strerror(errno));

	if (call_mlock && mlock2_(map, 3 * page_size, MLOCK_ONFAULT)) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("mlock error: %s", strerror(errno));
	}

	if (get_vm_area((unsigned long)map, &page1) ||
	    get_vm_area((unsigned long)map + page_size, &page2) ||
	    get_vm_area((unsigned long)map + page_size * 2, &page3)) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("couldn't find mapping in /proc/self/maps");
	}

	/*
	 * Before we unlock a portion, we need to that all three pages are in
	 * the same VMA.  If they are not we abort this test (Note that this is
	 * not a failure)
	 */
	if (page1.start != page2.start || page2.start != page3.start) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("VMAs are not merged to start, aborting test");
	}

	if (munlock(map + page_size, page_size)) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("munlock(): %s", strerror(errno));
	}

	if (get_vm_area((unsigned long)map, &page1) ||
	    get_vm_area((unsigned long)map + page_size, &page2) ||
	    get_vm_area((unsigned long)map + page_size * 2, &page3)) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("couldn't find mapping in /proc/self/maps");
	}

	/* All three VMAs should be different */
	if (page1.start == page2.start || page2.start == page3.start) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("failed to split VMA for munlock");
	}

	/* Now unlock the first and third page and check the VMAs again */
	if (munlock(map, page_size * 3)) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("munlock(): %s", strerror(errno));
	}

	if (get_vm_area((unsigned long)map, &page1) ||
	    get_vm_area((unsigned long)map + page_size, &page2) ||
	    get_vm_area((unsigned long)map + page_size * 2, &page3)) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("couldn't find mapping in /proc/self/maps");
	}

	/* Now all three VMAs should be the same */
	if (page1.start != page2.start || page2.start != page3.start) {
		munmap(map, 3 * page_size);
		ksft_test_result_fail("failed to merge VMAs after munlock");
	}

	ksft_test_result_pass("%s call_mlock %d\n", __func__, call_mlock);
	munmap(map, 3 * page_size);
}

static void test_mlockall(void)
{
	if (mlockall(MCL_CURRENT | MCL_ONFAULT | MCL_FUTURE))
		ksft_exit_fail_msg("mlockall failed: %s\n", strerror(errno));

	test_vma_management(false);
	munlockall();
}

int main(int argc, char **argv)
{
	int ret, size = 3 * getpagesize();
	void *map;

	ksft_print_header();

	map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (map == MAP_FAILED)
		ksft_exit_fail_msg("mmap error: %s", strerror(errno));

	ret = mlock2_(map, size, MLOCK_ONFAULT);
	if (ret && errno == ENOSYS)
		ksft_finished();

	munmap(map, size);

	ksft_set_plan(13);

	test_mlock_lock();
	test_mlock_onfault();
	test_munlockall0();
	test_munlockall1();
	test_lock_onfault_of_present();
	test_vma_management(true);
	test_mlockall();

	ksft_finished();
}
