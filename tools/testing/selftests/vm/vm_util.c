// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <fcntl.h>
#include "../kselftest.h"
#include "vm_util.h"

#define PMD_SIZE_FILE_PATH "/sys/kernel/mm/transparent_hugepage/hpage_pmd_size"
#define SMAP_FILE_PATH "/proc/self/smaps"
#define MAX_LINE_LENGTH 500

uint64_t pagemap_get_entry(int fd, char *start)
{
	const unsigned long pfn = (unsigned long)start / getpagesize();
	uint64_t entry;
	int ret;

	ret = pread(fd, &entry, sizeof(entry), pfn * sizeof(entry));
	if (ret != sizeof(entry))
		ksft_exit_fail_msg("reading pagemap failed\n");
	return entry;
}

bool pagemap_is_softdirty(int fd, char *start)
{
	uint64_t entry = pagemap_get_entry(fd, start);

	// Check if dirty bit (55th bit) is set
	return entry & 0x0080000000000000ull;
}

void clear_softdirty(void)
{
	int ret;
	const char *ctrl = "4";
	int fd = open("/proc/self/clear_refs", O_WRONLY);

	if (fd < 0)
		ksft_exit_fail_msg("opening clear_refs failed\n");
	ret = write(fd, ctrl, strlen(ctrl));
	close(fd);
	if (ret != strlen(ctrl))
		ksft_exit_fail_msg("writing clear_refs failed\n");
}

bool check_for_pattern(FILE *fp, const char *pattern, char *buf, size_t len)
{
	while (fgets(buf, len, fp)) {
		if (!strncmp(buf, pattern, strlen(pattern)))
			return true;
	}
	return false;
}

uint64_t read_pmd_pagesize(void)
{
	int fd;
	char buf[20];
	ssize_t num_read;

	fd = open(PMD_SIZE_FILE_PATH, O_RDONLY);
	if (fd == -1)
		ksft_exit_fail_msg("Open hpage_pmd_size failed\n");

	num_read = read(fd, buf, 19);
	if (num_read < 1) {
		close(fd);
		ksft_exit_fail_msg("Read hpage_pmd_size failed\n");
	}
	buf[num_read] = '\0';
	close(fd);

	return strtoul(buf, NULL, 10);
}

bool __check_huge(void *addr, char *pattern, int nr_hpages,
		  uint64_t hpage_size)
{
	uint64_t thp = -1;
	int ret;
	FILE *fp;
	char buffer[MAX_LINE_LENGTH];
	char addr_pattern[MAX_LINE_LENGTH];

	ret = snprintf(addr_pattern, MAX_LINE_LENGTH, "%08lx-",
		       (unsigned long) addr);
	if (ret >= MAX_LINE_LENGTH)
		ksft_exit_fail_msg("%s: Pattern is too long\n", __func__);

	fp = fopen(SMAP_FILE_PATH, "r");
	if (!fp)
		ksft_exit_fail_msg("%s: Failed to open file %s\n", __func__, SMAP_FILE_PATH);

	if (!check_for_pattern(fp, addr_pattern, buffer, sizeof(buffer)))
		goto err_out;

	/*
	 * Fetch the pattern in the same block and check the number of
	 * hugepages.
	 */
	if (!check_for_pattern(fp, pattern, buffer, sizeof(buffer)))
		goto err_out;

	snprintf(addr_pattern, MAX_LINE_LENGTH, "%s%%9ld kB", pattern);

	if (sscanf(buffer, addr_pattern, &thp) != 1)
		ksft_exit_fail_msg("Reading smap error\n");

err_out:
	fclose(fp);
	return thp == (nr_hpages * (hpage_size >> 10));
}

bool check_huge_anon(void *addr, int nr_hpages, uint64_t hpage_size)
{
	return __check_huge(addr, "AnonHugePages: ", nr_hpages, hpage_size);
}

bool check_huge_file(void *addr, int nr_hpages, uint64_t hpage_size)
{
	return __check_huge(addr, "FilePmdMapped:", nr_hpages, hpage_size);
}

bool check_huge_shmem(void *addr, int nr_hpages, uint64_t hpage_size)
{
	return __check_huge(addr, "ShmemPmdMapped:", nr_hpages, hpage_size);
}
