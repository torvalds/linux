// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <malloc.h>
#include "vm_util.h"
#include "../kselftest.h"
#include <linux/types.h>
#include <linux/memfd.h>
#include <linux/userfaultfd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <math.h>
#include <asm/unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include <assert.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define PAGEMAP_BITS_ALL		(PAGE_IS_WPALLOWED | PAGE_IS_WRITTEN |	\
					 PAGE_IS_FILE | PAGE_IS_PRESENT |	\
					 PAGE_IS_SWAPPED | PAGE_IS_PFNZERO |	\
					 PAGE_IS_HUGE)
#define PAGEMAP_NON_WRITTEN_BITS	(PAGE_IS_WPALLOWED | PAGE_IS_FILE |	\
					 PAGE_IS_PRESENT | PAGE_IS_SWAPPED |	\
					 PAGE_IS_PFNZERO | PAGE_IS_HUGE)

#define TEST_ITERATIONS 100
#define PAGEMAP "/proc/self/pagemap"
int pagemap_fd;
int uffd;
int page_size;
int hpage_size;
const char *progname;

#define LEN(region)	((region.end - region.start)/page_size)

static long pagemap_ioctl(void *start, int len, void *vec, int vec_len, int flag,
			  int max_pages, long required_mask, long anyof_mask, long excluded_mask,
			  long return_mask)
{
	struct pm_scan_arg arg;

	arg.start = (uintptr_t)start;
	arg.end = (uintptr_t)(start + len);
	arg.vec = (uintptr_t)vec;
	arg.vec_len = vec_len;
	arg.flags = flag;
	arg.size = sizeof(struct pm_scan_arg);
	arg.max_pages = max_pages;
	arg.category_mask = required_mask;
	arg.category_anyof_mask = anyof_mask;
	arg.category_inverted = excluded_mask;
	arg.return_mask = return_mask;

	return ioctl(pagemap_fd, PAGEMAP_SCAN, &arg);
}

static long pagemap_ioc(void *start, int len, void *vec, int vec_len, int flag,
			int max_pages, long required_mask, long anyof_mask, long excluded_mask,
			long return_mask, long *walk_end)
{
	struct pm_scan_arg arg;
	int ret;

	arg.start = (uintptr_t)start;
	arg.end = (uintptr_t)(start + len);
	arg.vec = (uintptr_t)vec;
	arg.vec_len = vec_len;
	arg.flags = flag;
	arg.size = sizeof(struct pm_scan_arg);
	arg.max_pages = max_pages;
	arg.category_mask = required_mask;
	arg.category_anyof_mask = anyof_mask;
	arg.category_inverted = excluded_mask;
	arg.return_mask = return_mask;

	ret = ioctl(pagemap_fd, PAGEMAP_SCAN, &arg);

	if (walk_end)
		*walk_end = arg.walk_end;

	return ret;
}


int init_uffd(void)
{
	struct uffdio_api uffdio_api;

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
	if (uffd == -1)
		return uffd;

	uffdio_api.api = UFFD_API;
	uffdio_api.features = UFFD_FEATURE_WP_UNPOPULATED | UFFD_FEATURE_WP_ASYNC |
			      UFFD_FEATURE_WP_HUGETLBFS_SHMEM;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api))
		return -1;

	if (!(uffdio_api.api & UFFDIO_REGISTER_MODE_WP) ||
	    !(uffdio_api.features & UFFD_FEATURE_WP_UNPOPULATED) ||
	    !(uffdio_api.features & UFFD_FEATURE_WP_ASYNC) ||
	    !(uffdio_api.features & UFFD_FEATURE_WP_HUGETLBFS_SHMEM))
		return -1;

	return 0;
}

int wp_init(void *lpBaseAddress, int dwRegionSize)
{
	struct uffdio_register uffdio_register;
	struct uffdio_writeprotect wp;

	uffdio_register.range.start = (unsigned long)lpBaseAddress;
	uffdio_register.range.len = dwRegionSize;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_WP;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register))
		ksft_exit_fail_msg("ioctl(UFFDIO_REGISTER) %d %s\n", errno, strerror(errno));

	if (!(uffdio_register.ioctls & UFFDIO_WRITEPROTECT))
		ksft_exit_fail_msg("ioctl set is incorrect\n");

	wp.range.start = (unsigned long)lpBaseAddress;
	wp.range.len = dwRegionSize;
	wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;

	if (ioctl(uffd, UFFDIO_WRITEPROTECT, &wp))
		ksft_exit_fail_msg("ioctl(UFFDIO_WRITEPROTECT)\n");

	return 0;
}

int wp_free(void *lpBaseAddress, int dwRegionSize)
{
	struct uffdio_register uffdio_register;

	uffdio_register.range.start = (unsigned long)lpBaseAddress;
	uffdio_register.range.len = dwRegionSize;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_WP;
	if (ioctl(uffd, UFFDIO_UNREGISTER, &uffdio_register.range))
		ksft_exit_fail_msg("ioctl unregister failure\n");
	return 0;
}

int wp_addr_range(void *lpBaseAddress, int dwRegionSize)
{
	if (pagemap_ioctl(lpBaseAddress, dwRegionSize, NULL, 0,
			  PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN) < 0)
		ksft_exit_fail_msg("error %d %d %s\n", 1, errno, strerror(errno));

	return 0;
}

void *gethugetlb_mem(int size, int *shmid)
{
	char *mem;

	if (shmid) {
		*shmid = shmget(2, size, SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
		if (*shmid < 0)
			return NULL;

		mem = shmat(*shmid, 0, 0);
		if (mem == (char *)-1) {
			shmctl(*shmid, IPC_RMID, NULL);
			ksft_exit_fail_msg("Shared memory attach failure\n");
		}
	} else {
		mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
			   MAP_ANONYMOUS | MAP_HUGETLB | MAP_PRIVATE, -1, 0);
		if (mem == MAP_FAILED)
			return NULL;
	}

	return mem;
}

int userfaultfd_tests(void)
{
	int mem_size, vec_size, written, num_pages = 16;
	char *mem, *vec;

	mem_size = num_pages * page_size;
	mem = mmap(NULL, mem_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(mem, mem_size);

	/* Change protection of pages differently */
	mprotect(mem, mem_size/8, PROT_READ|PROT_WRITE);
	mprotect(mem + 1 * mem_size/8, mem_size/8, PROT_READ);
	mprotect(mem + 2 * mem_size/8, mem_size/8, PROT_READ|PROT_WRITE);
	mprotect(mem + 3 * mem_size/8, mem_size/8, PROT_READ);
	mprotect(mem + 4 * mem_size/8, mem_size/8, PROT_READ|PROT_WRITE);
	mprotect(mem + 5 * mem_size/8, mem_size/8, PROT_NONE);
	mprotect(mem + 6 * mem_size/8, mem_size/8, PROT_READ|PROT_WRITE);
	mprotect(mem + 7 * mem_size/8, mem_size/8, PROT_READ);

	wp_addr_range(mem + (mem_size/16), mem_size - 2 * (mem_size/8));
	wp_addr_range(mem, mem_size);

	vec_size = mem_size/page_size;
	vec = malloc(sizeof(struct page_region) * vec_size);

	written = pagemap_ioctl(mem, mem_size, vec, 1, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				vec_size - 2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 0, "%s all new pages must not be written (dirty)\n", __func__);

	wp_free(mem, mem_size);
	munmap(mem, mem_size);
	free(vec);
	return 0;
}

int get_reads(struct page_region *vec, int vec_size)
{
	int i, sum = 0;

	for (i = 0; i < vec_size; i++)
		sum += LEN(vec[i]);

	return sum;
}

int sanity_tests_sd(void)
{
	int mem_size, vec_size, ret, ret2, ret3, i, num_pages = 1000, total_pages = 0;
	int total_writes, total_reads, reads, count;
	struct page_region *vec, *vec2;
	char *mem, *m[2];
	long walk_end;

	vec_size = num_pages/2;
	mem_size = num_pages * page_size;

	vec = malloc(sizeof(struct page_region) * vec_size);
	if (!vec)
		ksft_exit_fail_msg("error nomem\n");

	vec2 = malloc(sizeof(struct page_region) * vec_size);
	if (!vec2)
		ksft_exit_fail_msg("error nomem\n");

	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	/* 1. wrong operation */
	ksft_test_result(pagemap_ioctl(mem, 0, vec, vec_size, 0,
				       0, PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL) == 0,
			 "%s Zero range size is valid\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, NULL, vec_size, 0,
				       0, PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL) < 0,
			 "%s output buffer must be specified with size\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, 0, 0,
				       0, PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL) == 0,
			 "%s output buffer can be 0\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, 0, 0, 0,
				       0, PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL) == 0,
			 "%s output buffer can be 0\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, -1,
				       0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN) < 0,
			 "%s wrong flag specified\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size,
				       PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC | 0xFF,
				       0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN) < 0,
			 "%s flag has extra bits specified\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0,
				       0, 0, 0, 0, PAGE_IS_WRITTEN) >= 0,
			 "%s no selection mask is specified\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0,
				       0, PAGE_IS_WRITTEN, PAGE_IS_WRITTEN, 0, 0) == 0,
			 "%s no return mask is specified\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0,
				       0, PAGE_IS_WRITTEN, 0, 0, 0x1000) < 0,
			 "%s wrong return mask specified\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size,
				       PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				       0, 0xFFF, PAGE_IS_WRITTEN, 0, PAGE_IS_WRITTEN) < 0,
			 "%s mixture of correct and wrong flag\n", __func__);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size,
				       PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				       0, 0, 0, PAGEMAP_BITS_ALL, PAGE_IS_WRITTEN) >= 0,
			 "%s PAGEMAP_BITS_ALL can be specified with PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC\n",
			 __func__);

	/* 2. Clear area with larger vec size */
	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC, 0,
			    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	ksft_test_result(ret >= 0, "%s Clear area with larger vec size\n", __func__);

	/* 3. Repeated pattern of written and non-written pages */
	for (i = 0; i < mem_size; i += 2 * page_size)
		mem[i]++;

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0, PAGE_IS_WRITTEN, 0,
			    0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ksft_test_result(ret == mem_size/(page_size * 2),
			 "%s Repeated pattern of written and non-written pages\n", __func__);

	/* 4. Repeated pattern of written and non-written pages in parts */
	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			    num_pages/2 - 2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ret2 = pagemap_ioctl(mem, mem_size, vec, 2, 0, 0, PAGE_IS_WRITTEN, 0, 0,
			     PAGE_IS_WRITTEN);
	if (ret2 < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret2, errno, strerror(errno));

	ret3 = pagemap_ioctl(mem, mem_size, vec, vec_size,
			     PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			     0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret3 < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret3, errno, strerror(errno));

	ksft_test_result((ret + ret3) == num_pages/2 && ret2 == 2,
			 "%s Repeated pattern of written and non-written pages in parts %d %d %d\n",
			 __func__, ret, ret3, ret2);

	/* 5. Repeated pattern of written and non-written pages max_pages */
	for (i = 0; i < mem_size; i += 2 * page_size)
		mem[i]++;
	mem[(mem_size/page_size - 1) * page_size]++;

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			    num_pages/2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ret2 = pagemap_ioctl(mem, mem_size, vec, vec_size,
			     PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			     0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret2 < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret2, errno, strerror(errno));

	ksft_test_result(ret == num_pages/2 && ret2 == 1,
			 "%s Repeated pattern of written and non-written pages max_pages\n",
			 __func__);

	/* 6. only get 2 dirty pages and clear them as well */
	vec_size = mem_size/page_size;
	memset(mem, -1, mem_size);

	/* get and clear second and third pages */
	ret = pagemap_ioctl(mem + page_size, 2 * page_size, vec, 1,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			    2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ret2 = pagemap_ioctl(mem, mem_size, vec2, vec_size, 0, 0,
			      PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret2 < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret2, errno, strerror(errno));

	ksft_test_result(ret == 1 && LEN(vec[0]) == 2 &&
			 vec[0].start == (uintptr_t)(mem + page_size) &&
			 ret2 == 2 && LEN(vec2[0]) == 1 && vec2[0].start == (uintptr_t)mem &&
			 LEN(vec2[1]) == vec_size - 3 &&
			 vec2[1].start == (uintptr_t)(mem + 3 * page_size),
			 "%s only get 2 written pages and clear them as well\n", __func__);

	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 7. Two regions */
	m[0] = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (m[0] == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	m[1] = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (m[1] == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(m[0], mem_size);
	wp_init(m[1], mem_size);
	wp_addr_range(m[0], mem_size);
	wp_addr_range(m[1], mem_size);

	memset(m[0], 'a', mem_size);
	memset(m[1], 'b', mem_size);

	wp_addr_range(m[0], mem_size);

	ret = pagemap_ioctl(m[1], mem_size, vec, 1, 0, 0, PAGE_IS_WRITTEN, 0, 0,
			    PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ksft_test_result(ret == 1 && LEN(vec[0]) == mem_size/page_size,
			 "%s Two regions\n", __func__);

	wp_free(m[0], mem_size);
	wp_free(m[1], mem_size);
	munmap(m[0], mem_size);
	munmap(m[1], mem_size);

	free(vec);
	free(vec2);

	/* 8. Smaller vec */
	mem_size = 1050 * page_size;
	vec_size = mem_size/(page_size*2);

	vec = malloc(sizeof(struct page_region) * vec_size);
	if (!vec)
		ksft_exit_fail_msg("error nomem\n");

	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC, 0,
			    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	for (i = 0; i < mem_size/page_size; i += 2)
		mem[i * page_size]++;

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			    mem_size/(page_size*5), PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	total_pages += ret;

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			    mem_size/(page_size*5), PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	total_pages += ret;

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			    mem_size/(page_size*5), PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	total_pages += ret;

	ksft_test_result(total_pages == mem_size/(page_size*2), "%s Smaller max_pages\n", __func__);

	free(vec);
	wp_free(mem, mem_size);
	munmap(mem, mem_size);
	total_pages = 0;

	/* 9. Smaller vec */
	mem_size = 10000 * page_size;
	vec_size = 50;

	vec = malloc(sizeof(struct page_region) * vec_size);
	if (!vec)
		ksft_exit_fail_msg("error nomem\n");

	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	for (count = 0; count < TEST_ITERATIONS; count++) {
		total_writes = total_reads = 0;
		walk_end = (long)mem;

		for (i = 0; i < mem_size; i += page_size) {
			if (rand() % 2) {
				mem[i]++;
				total_writes++;
			}
		}

		while (total_reads < total_writes) {
			ret = pagemap_ioc((void *)walk_end, mem_size-(walk_end - (long)mem), vec,
					  vec_size, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
					  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
			if (ret < 0)
				ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

			if (ret > vec_size)
				break;

			reads = get_reads(vec, ret);
			total_reads += reads;
		}

		if (total_reads != total_writes)
			break;
	}

	ksft_test_result(count == TEST_ITERATIONS, "Smaller vec\n");

	free(vec);
	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 10. Walk_end tester */
	vec_size = 1000;
	mem_size = vec_size * page_size;

	vec = malloc(sizeof(struct page_region) * vec_size);
	if (!vec)
		ksft_exit_fail_msg("error nomem\n");

	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	memset(mem, 0, mem_size);

	ret = pagemap_ioc(mem, 0, vec, vec_size, 0,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 0 && walk_end == (long)mem,
			 "Walk_end: Same start and end address\n");

	ret = pagemap_ioc(mem, 0, vec, vec_size, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 0 && walk_end == (long)mem,
			 "Walk_end: Same start and end with WP\n");

	ret = pagemap_ioc(mem, 0, vec, 0, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 0 && walk_end == (long)mem,
			 "Walk_end: Same start and end with 0 output buffer\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + mem_size),
			 "Walk_end: Big vec\n");

	ret = pagemap_ioc(mem, mem_size, vec, 1, 0,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + mem_size),
			 "Walk_end: vec of minimum length\n");

	ret = pagemap_ioc(mem, mem_size, vec, 1, 0,
			  vec_size, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + mem_size),
			 "Walk_end: Max pages specified\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  vec_size/2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + mem_size/2),
			 "Walk_end: Half max pages\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  1, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + page_size),
			 "Walk_end: 1 max page\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  -1, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + mem_size),
			 "Walk_end: max pages\n");

	wp_addr_range(mem, mem_size);
	for (i = 0; i < mem_size; i += 2 * page_size)
		mem[i]++;

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == vec_size/2 && walk_end == (long)(mem + mem_size),
			 "Walk_end sparse: Big vec\n");

	ret = pagemap_ioc(mem, mem_size, vec, 1, 0,
			  0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + page_size * 2),
			 "Walk_end sparse: vec of minimum length\n");

	ret = pagemap_ioc(mem, mem_size, vec, 1, 0,
			  vec_size, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + page_size * 2),
			 "Walk_end sparse: Max pages specified\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size/2, 0,
			  vec_size, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == vec_size/2 && walk_end == (long)(mem + mem_size),
			 "Walk_end sparse: Max pages specified\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  vec_size, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == vec_size/2 && walk_end == (long)(mem + mem_size),
			 "Walk_end sparse: Max pages specified\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  vec_size/2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == vec_size/2 && walk_end == (long)(mem + mem_size),
			 "Walk_endsparse : Half max pages\n");

	ret = pagemap_ioc(mem, mem_size, vec, vec_size, 0,
			  1, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN, &walk_end);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));
	ksft_test_result(ret == 1 && walk_end == (long)(mem + page_size * 2),
			 "Walk_end: 1 max page\n");

	free(vec);
	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	return 0;
}

int base_tests(char *prefix, char *mem, int mem_size, int skip)
{
	int vec_size, written;
	struct page_region *vec, *vec2;

	if (skip) {
		ksft_test_result_skip("%s all new pages must not be written (dirty)\n", prefix);
		ksft_test_result_skip("%s all pages must be written (dirty)\n", prefix);
		ksft_test_result_skip("%s all pages dirty other than first and the last one\n",
				      prefix);
		ksft_test_result_skip("%s PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC\n", prefix);
		ksft_test_result_skip("%s only middle page dirty\n", prefix);
		ksft_test_result_skip("%s only two middle pages dirty\n", prefix);
		return 0;
	}

	vec_size = mem_size/page_size;
	vec = malloc(sizeof(struct page_region) * vec_size);
	vec2 = malloc(sizeof(struct page_region) * vec_size);

	/* 1. all new pages must be not be written (dirty) */
	written = pagemap_ioctl(mem, mem_size, vec, 1, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				vec_size - 2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 0, "%s all new pages must not be written (dirty)\n", prefix);

	/* 2. all pages must be written */
	memset(mem, -1, mem_size);

	written = pagemap_ioctl(mem, mem_size, vec, 1, 0, 0, PAGE_IS_WRITTEN, 0, 0,
			      PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 1 && LEN(vec[0]) == mem_size/page_size,
			 "%s all pages must be written (dirty)\n", prefix);

	/* 3. all pages dirty other than first and the last one */
	written = pagemap_ioctl(mem, mem_size, vec, 1, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	memset(mem + page_size, 0, mem_size - (2 * page_size));

	written = pagemap_ioctl(mem, mem_size, vec, 1, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 1 && LEN(vec[0]) >= vec_size - 2 && LEN(vec[0]) <= vec_size,
			 "%s all pages dirty other than first and the last one\n", prefix);

	written = pagemap_ioctl(mem, mem_size, vec, 1, 0, 0,
				PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 0,
			 "%s PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC\n", prefix);

	/* 4. only middle page dirty */
	written = pagemap_ioctl(mem, mem_size, vec, 1, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	mem[vec_size/2 * page_size]++;

	written = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0, PAGE_IS_WRITTEN,
				0, 0, PAGE_IS_WRITTEN);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 1 && LEN(vec[0]) >= 1,
			 "%s only middle page dirty\n", prefix);

	/* 5. only two middle pages dirty and walk over only middle pages */
	written = pagemap_ioctl(mem, mem_size, vec, 1, PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN | PAGE_IS_HUGE);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	mem[vec_size/2 * page_size]++;
	mem[(vec_size/2 + 1) * page_size]++;

	written = pagemap_ioctl(&mem[vec_size/2 * page_size], 2 * page_size, vec, 1, 0,
				0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN | PAGE_IS_HUGE);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written == 1 && vec[0].start == (uintptr_t)(&mem[vec_size/2 * page_size])
			 && LEN(vec[0]) == 2,
			 "%s only two middle pages dirty\n", prefix);

	free(vec);
	free(vec2);
	return 0;
}

void *gethugepage(int map_size)
{
	int ret;
	char *map;

	map = memalign(hpage_size, map_size);
	if (!map)
		ksft_exit_fail_msg("memalign failed %d %s\n", errno, strerror(errno));

	ret = madvise(map, map_size, MADV_HUGEPAGE);
	if (ret)
		return NULL;

	memset(map, 0, map_size);

	return map;
}

int hpage_unit_tests(void)
{
	char *map;
	int ret, ret2;
	size_t num_pages = 10;
	int map_size = hpage_size * num_pages;
	int vec_size = map_size/page_size;
	struct page_region *vec, *vec2;

	vec = malloc(sizeof(struct page_region) * vec_size);
	vec2 = malloc(sizeof(struct page_region) * vec_size);
	if (!vec || !vec2)
		ksft_exit_fail_msg("malloc failed\n");

	map = gethugepage(map_size);
	if (map) {
		wp_init(map, map_size);
		wp_addr_range(map, map_size);

		/* 1. all new huge page must not be written (dirty) */
		ret = pagemap_ioctl(map, map_size, vec, vec_size,
				    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 0, "%s all new huge page must not be written (dirty)\n",
				 __func__);

		/* 2. all the huge page must not be written */
		ret = pagemap_ioctl(map, map_size, vec, vec_size, 0, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 0, "%s all the huge page must not be written\n", __func__);

		/* 3. all the huge page must be written and clear dirty as well */
		memset(map, -1, map_size);
		ret = pagemap_ioctl(map, map_size, vec, vec_size,
				    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				    0, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 1 && vec[0].start == (uintptr_t)map &&
				 LEN(vec[0]) == vec_size && vec[0].categories == PAGE_IS_WRITTEN,
				 "%s all the huge page must be written and clear\n", __func__);

		/* 4. only middle page written */
		wp_free(map, map_size);
		free(map);
		map = gethugepage(map_size);
		wp_init(map, map_size);
		wp_addr_range(map, map_size);
		map[vec_size/2 * page_size]++;

		ret = pagemap_ioctl(map, map_size, vec, vec_size, 0, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 1 && LEN(vec[0]) > 0,
				 "%s only middle page written\n", __func__);

		wp_free(map, map_size);
		free(map);
	} else {
		ksft_test_result_skip("%s all new huge page must be written\n", __func__);
		ksft_test_result_skip("%s all the huge page must not be written\n", __func__);
		ksft_test_result_skip("%s all the huge page must be written and clear\n", __func__);
		ksft_test_result_skip("%s only middle page written\n", __func__);
	}

	/* 5. clear first half of huge page */
	map = gethugepage(map_size);
	if (map) {
		wp_init(map, map_size);
		wp_addr_range(map, map_size);

		memset(map, 0, map_size);

		wp_addr_range(map, map_size/2);

		ret = pagemap_ioctl(map, map_size, vec, vec_size, 0, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 1 && LEN(vec[0]) == vec_size/2 &&
				 vec[0].start == (uintptr_t)(map + map_size/2),
				 "%s clear first half of huge page\n", __func__);
		wp_free(map, map_size);
		free(map);
	} else {
		ksft_test_result_skip("%s clear first half of huge page\n", __func__);
	}

	/* 6. clear first half of huge page with limited buffer */
	map = gethugepage(map_size);
	if (map) {
		wp_init(map, map_size);
		wp_addr_range(map, map_size);

		memset(map, 0, map_size);

		ret = pagemap_ioctl(map, map_size, vec, vec_size,
				    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				    vec_size/2, PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ret = pagemap_ioctl(map, map_size, vec, vec_size, 0, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 1 && LEN(vec[0]) == vec_size/2 &&
				 vec[0].start == (uintptr_t)(map + map_size/2),
				 "%s clear first half of huge page with limited buffer\n",
				 __func__);
		wp_free(map, map_size);
		free(map);
	} else {
		ksft_test_result_skip("%s clear first half of huge page with limited buffer\n",
				      __func__);
	}

	/* 7. clear second half of huge page */
	map = gethugepage(map_size);
	if (map) {
		wp_init(map, map_size);
		wp_addr_range(map, map_size);

		memset(map, -1, map_size);

		ret = pagemap_ioctl(map + map_size/2, map_size/2, vec, vec_size,
				    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC, vec_size/2,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ret = pagemap_ioctl(map, map_size, vec, vec_size, 0, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 1 && LEN(vec[0]) == vec_size/2,
				 "%s clear second half huge page\n", __func__);
		wp_free(map, map_size);
		free(map);
	} else {
		ksft_test_result_skip("%s clear second half huge page\n", __func__);
	}

	/* 8. get half huge page */
	map = gethugepage(map_size);
	if (map) {
		wp_init(map, map_size);
		wp_addr_range(map, map_size);

		memset(map, -1, map_size);
		usleep(100);

		ret = pagemap_ioctl(map, map_size, vec, 1,
				    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				    hpage_size/(2*page_size), PAGE_IS_WRITTEN, 0, 0,
				    PAGE_IS_WRITTEN);
		if (ret < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

		ksft_test_result(ret == 1 && LEN(vec[0]) == hpage_size/(2*page_size),
				 "%s get half huge page\n", __func__);

		ret2 = pagemap_ioctl(map, map_size, vec, vec_size, 0, 0,
				    PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN);
		if (ret2 < 0)
			ksft_exit_fail_msg("error %d %d %s\n", ret2, errno, strerror(errno));

		ksft_test_result(ret2 == 1 && LEN(vec[0]) == (map_size - hpage_size/2)/page_size,
				 "%s get half huge page\n", __func__);

		wp_free(map, map_size);
		free(map);
	} else {
		ksft_test_result_skip("%s get half huge page\n", __func__);
		ksft_test_result_skip("%s get half huge page\n", __func__);
	}

	free(vec);
	free(vec2);
	return 0;
}

int unmapped_region_tests(void)
{
	void *start = (void *)0x10000000;
	int written, len = 0x00040000;
	int vec_size = len / page_size;
	struct page_region *vec = malloc(sizeof(struct page_region) * vec_size);

	/* 1. Get written pages */
	written = pagemap_ioctl(start, len, vec, vec_size, 0, 0,
				PAGEMAP_NON_WRITTEN_BITS, 0, 0, PAGEMAP_NON_WRITTEN_BITS);
	if (written < 0)
		ksft_exit_fail_msg("error %d %d %s\n", written, errno, strerror(errno));

	ksft_test_result(written >= 0, "%s Get status of pages\n", __func__);

	free(vec);
	return 0;
}

static void test_simple(void)
{
	int i;
	char *map;
	struct page_region vec;

	map = aligned_alloc(page_size, page_size);
	if (!map)
		ksft_exit_fail_msg("aligned_alloc failed\n");

	wp_init(map, page_size);
	wp_addr_range(map, page_size);

	for (i = 0 ; i < TEST_ITERATIONS; i++) {
		if (pagemap_ioctl(map, page_size, &vec, 1, 0, 0,
				  PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN) == 1) {
			ksft_print_msg("written bit was 1, but should be 0 (i=%d)\n", i);
			break;
		}

		wp_addr_range(map, page_size);
		/* Write something to the page to get the written bit enabled on the page */
		map[0]++;

		if (pagemap_ioctl(map, page_size, &vec, 1, 0, 0,
				  PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN) == 0) {
			ksft_print_msg("written bit was 0, but should be 1 (i=%d)\n", i);
			break;
		}

		wp_addr_range(map, page_size);
	}
	wp_free(map, page_size);
	free(map);

	ksft_test_result(i == TEST_ITERATIONS, "Test %s\n", __func__);
}

int sanity_tests(void)
{
	int mem_size, vec_size, ret, fd, i, buf_size;
	struct page_region *vec;
	char *mem, *fmem;
	struct stat sbuf;
	char *tmp_buf;

	/* 1. wrong operation */
	mem_size = 10 * page_size;
	vec_size = mem_size / page_size;

	vec = malloc(sizeof(struct page_region) * vec_size);
	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED || vec == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");

	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size,
				       PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC,
				       0, PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL) >= 0,
			 "%s WP op can be specified with !PAGE_IS_WRITTEN\n", __func__);
	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
				       PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL) >= 0,
			 "%s required_mask specified\n", __func__);
	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
				       0, PAGEMAP_BITS_ALL, 0, PAGEMAP_BITS_ALL) >= 0,
			 "%s anyof_mask specified\n", __func__);
	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
				       0, 0, PAGEMAP_BITS_ALL, PAGEMAP_BITS_ALL) >= 0,
			 "%s excluded_mask specified\n", __func__);
	ksft_test_result(pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
				       PAGEMAP_BITS_ALL, PAGEMAP_BITS_ALL, 0,
				       PAGEMAP_BITS_ALL) >= 0,
			 "%s required_mask and anyof_mask specified\n", __func__);
	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 2. Get sd and present pages with anyof_mask */
	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	memset(mem, 0, mem_size);

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
			    0, PAGEMAP_BITS_ALL, 0, PAGEMAP_BITS_ALL);
	ksft_test_result(ret >= 0 && vec[0].start == (uintptr_t)mem && LEN(vec[0]) == vec_size &&
			 (vec[0].categories & (PAGE_IS_WRITTEN | PAGE_IS_PRESENT)) ==
			 (PAGE_IS_WRITTEN | PAGE_IS_PRESENT),
			 "%s Get sd and present pages with anyof_mask\n", __func__);

	/* 3. Get sd and present pages with required_mask */
	ret = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
			    PAGEMAP_BITS_ALL, 0, 0, PAGEMAP_BITS_ALL);
	ksft_test_result(ret >= 0 && vec[0].start == (uintptr_t)mem && LEN(vec[0]) == vec_size &&
			 (vec[0].categories & (PAGE_IS_WRITTEN | PAGE_IS_PRESENT)) ==
			 (PAGE_IS_WRITTEN | PAGE_IS_PRESENT),
			 "%s Get all the pages with required_mask\n", __func__);

	/* 4. Get sd and present pages with required_mask and anyof_mask */
	ret = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
			    PAGE_IS_WRITTEN, PAGE_IS_PRESENT, 0, PAGEMAP_BITS_ALL);
	ksft_test_result(ret >= 0 && vec[0].start == (uintptr_t)mem && LEN(vec[0]) == vec_size &&
			 (vec[0].categories & (PAGE_IS_WRITTEN | PAGE_IS_PRESENT)) ==
			 (PAGE_IS_WRITTEN | PAGE_IS_PRESENT),
			 "%s Get sd and present pages with required_mask and anyof_mask\n",
			 __func__);

	/* 5. Don't get sd pages */
	ret = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
			    PAGE_IS_WRITTEN, 0, PAGE_IS_WRITTEN, PAGEMAP_BITS_ALL);
	ksft_test_result(ret == 0, "%s Don't get sd pages\n", __func__);

	/* 6. Don't get present pages */
	ret = pagemap_ioctl(mem, mem_size, vec, vec_size, 0, 0,
			    PAGE_IS_PRESENT, 0, PAGE_IS_PRESENT, PAGEMAP_BITS_ALL);
	ksft_test_result(ret == 0, "%s Don't get present pages\n", __func__);

	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 8. Find written present pages with return mask */
	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	memset(mem, 0, mem_size);

	ret = pagemap_ioctl(mem, mem_size, vec, vec_size,
			    PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC, 0,
			    0, PAGEMAP_BITS_ALL, 0, PAGE_IS_WRITTEN);
	ksft_test_result(ret >= 0 && vec[0].start == (uintptr_t)mem && LEN(vec[0]) == vec_size &&
			 vec[0].categories == PAGE_IS_WRITTEN,
			 "%s Find written present pages with return mask\n", __func__);
	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 9. Memory mapped file */
	fd = open(progname, O_RDONLY);
	if (fd < 0)
		ksft_exit_fail_msg("%s Memory mapped file\n", __func__);

	ret = stat(progname, &sbuf);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	fmem = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (fmem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem %d %s\n", errno, strerror(errno));

	tmp_buf = malloc(sbuf.st_size);
	memcpy(tmp_buf, fmem, sbuf.st_size);

	ret = pagemap_ioctl(fmem, sbuf.st_size, vec, vec_size, 0, 0,
			    0, PAGEMAP_NON_WRITTEN_BITS, 0, PAGEMAP_NON_WRITTEN_BITS);

	ksft_test_result(ret >= 0 && vec[0].start == (uintptr_t)fmem &&
			 LEN(vec[0]) == ceilf((float)sbuf.st_size/page_size) &&
			 (vec[0].categories & PAGE_IS_FILE),
			 "%s Memory mapped file\n", __func__);

	munmap(fmem, sbuf.st_size);
	close(fd);

	/* 10. Create and read/write to a memory mapped file */
	buf_size = page_size * 10;

	fd = open(__FILE__".tmp2", O_RDWR | O_CREAT, 0666);
	if (fd < 0)
		ksft_exit_fail_msg("Read/write to memory: %s\n",
				   strerror(errno));

	for (i = 0; i < buf_size; i++)
		if (write(fd, "c", 1) < 0)
			ksft_exit_fail_msg("Create and read/write to a memory mapped file\n");

	fmem = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (fmem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem %d %s\n", errno, strerror(errno));

	wp_init(fmem, buf_size);
	wp_addr_range(fmem, buf_size);

	for (i = 0; i < buf_size; i++)
		fmem[i] = 'z';

	msync(fmem, buf_size, MS_SYNC);

	ret = pagemap_ioctl(fmem, buf_size, vec, vec_size, 0, 0,
			    PAGE_IS_WRITTEN, PAGE_IS_PRESENT | PAGE_IS_SWAPPED | PAGE_IS_FILE, 0,
			    PAGEMAP_BITS_ALL);

	ksft_test_result(ret >= 0 && vec[0].start == (uintptr_t)fmem &&
			 LEN(vec[0]) == (buf_size/page_size) &&
			 (vec[0].categories & PAGE_IS_WRITTEN),
			 "%s Read/write to memory\n", __func__);

	wp_free(fmem, buf_size);
	munmap(fmem, buf_size);
	close(fd);

	free(vec);
	return 0;
}

int mprotect_tests(void)
{
	int ret;
	char *mem, *mem2;
	struct page_region vec;
	int pagemap_fd = open("/proc/self/pagemap", O_RDONLY);

	if (pagemap_fd < 0) {
		fprintf(stderr, "open() failed\n");
		exit(1);
	}

	/* 1. Map two pages */
	mem = mmap(0, 2 * page_size, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	wp_init(mem, 2 * page_size);
	wp_addr_range(mem, 2 * page_size);

	/* Populate both pages. */
	memset(mem, 1, 2 * page_size);

	ret = pagemap_ioctl(mem, 2 * page_size, &vec, 1, 0, 0, PAGE_IS_WRITTEN,
			    0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ksft_test_result(ret == 1 && LEN(vec) == 2, "%s Both pages written\n", __func__);

	/* 2. Start tracking */
	wp_addr_range(mem, 2 * page_size);

	ksft_test_result(pagemap_ioctl(mem, 2 * page_size, &vec, 1, 0, 0,
				       PAGE_IS_WRITTEN, 0, 0, PAGE_IS_WRITTEN) == 0,
			 "%s Both pages are not written (dirty)\n", __func__);

	/* 3. Remap the second page */
	mem2 = mmap(mem + page_size, page_size, PROT_READ|PROT_WRITE,
		    MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0);
	if (mem2 == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	wp_init(mem2, page_size);
	wp_addr_range(mem2, page_size);

	/* Protect + unprotect. */
	mprotect(mem, page_size, PROT_NONE);
	mprotect(mem, 2 * page_size, PROT_READ);
	mprotect(mem, 2 * page_size, PROT_READ|PROT_WRITE);

	/* Modify both pages. */
	memset(mem, 2, 2 * page_size);

	/* Protect + unprotect. */
	mprotect(mem, page_size, PROT_NONE);
	mprotect(mem, page_size, PROT_READ);
	mprotect(mem, page_size, PROT_READ|PROT_WRITE);

	ret = pagemap_ioctl(mem, 2 * page_size, &vec, 1, 0, 0, PAGE_IS_WRITTEN,
			    0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ksft_test_result(ret == 1 && LEN(vec) == 2,
			 "%s Both pages written after remap and mprotect\n", __func__);

	/* 4. Clear and make the pages written */
	wp_addr_range(mem, 2 * page_size);

	memset(mem, 'A', 2 * page_size);

	ret = pagemap_ioctl(mem, 2 * page_size, &vec, 1, 0, 0, PAGE_IS_WRITTEN,
			    0, 0, PAGE_IS_WRITTEN);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	ksft_test_result(ret == 1 && LEN(vec) == 2,
			 "%s Clear and make the pages written\n", __func__);

	wp_free(mem, 2 * page_size);
	munmap(mem, 2 * page_size);
	return 0;
}

/* transact test */
static const unsigned int nthreads = 6, pages_per_thread = 32, access_per_thread = 8;
static pthread_barrier_t start_barrier, end_barrier;
static unsigned int extra_thread_faults;
static unsigned int iter_count = 1000;
static volatile int finish;

static ssize_t get_dirty_pages_reset(char *mem, unsigned int count,
				     int reset, int page_size)
{
	struct pm_scan_arg arg = {0};
	struct page_region rgns[256];
	int i, j, cnt, ret;

	arg.size = sizeof(struct pm_scan_arg);
	arg.start = (uintptr_t)mem;
	arg.max_pages = count;
	arg.end = (uintptr_t)(mem + count * page_size);
	arg.vec = (uintptr_t)rgns;
	arg.vec_len = sizeof(rgns) / sizeof(*rgns);
	if (reset)
		arg.flags |= PM_SCAN_WP_MATCHING | PM_SCAN_CHECK_WPASYNC;
	arg.category_mask = PAGE_IS_WRITTEN;
	arg.return_mask = PAGE_IS_WRITTEN;

	ret = ioctl(pagemap_fd, PAGEMAP_SCAN, &arg);
	if (ret < 0)
		ksft_exit_fail_msg("ioctl failed\n");

	cnt = 0;
	for (i = 0; i < ret; ++i) {
		if (rgns[i].categories != PAGE_IS_WRITTEN)
			ksft_exit_fail_msg("wrong flags\n");

		for (j = 0; j < LEN(rgns[i]); ++j)
			cnt++;
	}

	return cnt;
}

void *thread_proc(void *mem)
{
	int *m = mem;
	long curr_faults, faults;
	struct rusage r;
	unsigned int i;
	int ret;

	if (getrusage(RUSAGE_THREAD, &r))
		ksft_exit_fail_msg("getrusage\n");

	curr_faults = r.ru_minflt;

	while (!finish) {
		ret = pthread_barrier_wait(&start_barrier);
		if (ret && ret != PTHREAD_BARRIER_SERIAL_THREAD)
			ksft_exit_fail_msg("pthread_barrier_wait\n");

		for (i = 0; i < access_per_thread; ++i)
			__atomic_add_fetch(m + i * (0x1000 / sizeof(*m)), 1, __ATOMIC_SEQ_CST);

		ret = pthread_barrier_wait(&end_barrier);
		if (ret && ret != PTHREAD_BARRIER_SERIAL_THREAD)
			ksft_exit_fail_msg("pthread_barrier_wait\n");

		if (getrusage(RUSAGE_THREAD, &r))
			ksft_exit_fail_msg("getrusage\n");

		faults = r.ru_minflt - curr_faults;
		if (faults < access_per_thread)
			ksft_exit_fail_msg("faults < access_per_thread");

		__atomic_add_fetch(&extra_thread_faults, faults - access_per_thread,
				   __ATOMIC_SEQ_CST);
		curr_faults = r.ru_minflt;
	}

	return NULL;
}

static void transact_test(int page_size)
{
	unsigned int i, count, extra_pages;
	pthread_t th;
	char *mem;
	int ret, c;

	if (pthread_barrier_init(&start_barrier, NULL, nthreads + 1))
		ksft_exit_fail_msg("pthread_barrier_init\n");

	if (pthread_barrier_init(&end_barrier, NULL, nthreads + 1))
		ksft_exit_fail_msg("pthread_barrier_init\n");

	mem = mmap(NULL, 0x1000 * nthreads * pages_per_thread, PROT_READ | PROT_WRITE,
		   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("Error mmap %s.\n", strerror(errno));

	wp_init(mem, 0x1000 * nthreads * pages_per_thread);
	wp_addr_range(mem, 0x1000 * nthreads * pages_per_thread);

	memset(mem, 0, 0x1000 * nthreads * pages_per_thread);

	count = get_dirty_pages_reset(mem, nthreads * pages_per_thread, 1, page_size);
	ksft_test_result(count > 0, "%s count %d\n", __func__, count);
	count = get_dirty_pages_reset(mem, nthreads * pages_per_thread, 1, page_size);
	ksft_test_result(count == 0, "%s count %d\n", __func__, count);

	finish = 0;
	for (i = 0; i < nthreads; ++i)
		pthread_create(&th, NULL, thread_proc, mem + 0x1000 * i * pages_per_thread);

	extra_pages = 0;
	for (i = 0; i < iter_count; ++i) {
		count = 0;

		ret = pthread_barrier_wait(&start_barrier);
		if (ret && ret != PTHREAD_BARRIER_SERIAL_THREAD)
			ksft_exit_fail_msg("pthread_barrier_wait\n");

		count = get_dirty_pages_reset(mem, nthreads * pages_per_thread, 1,
					      page_size);

		ret = pthread_barrier_wait(&end_barrier);
		if (ret && ret != PTHREAD_BARRIER_SERIAL_THREAD)
			ksft_exit_fail_msg("pthread_barrier_wait\n");

		if (count > nthreads * access_per_thread)
			ksft_exit_fail_msg("Too big count %d expected %d, iter %d\n",
					   count, nthreads * access_per_thread, i);

		c = get_dirty_pages_reset(mem, nthreads * pages_per_thread, 1, page_size);
		count += c;

		if (c > nthreads * access_per_thread) {
			ksft_test_result_fail(" %s count > nthreads\n", __func__);
			return;
		}

		if (count != nthreads * access_per_thread) {
			/*
			 * The purpose of the test is to make sure that no page updates are lost
			 * when the page updates and read-resetting soft dirty flags are performed
			 * in parallel. However, it is possible that the application will get the
			 * soft dirty flags twice on the two consecutive read-resets. This seems
			 * unavoidable as soft dirty flag is handled in software through page faults
			 * in kernel. While the updating the flags is supposed to be synchronized
			 * between page fault handling and read-reset, it is possible that
			 * read-reset happens after page fault PTE update but before the application
			 * re-executes write instruction. So read-reset gets the flag, clears write
			 * access and application gets page fault again for the same write.
			 */
			if (count < nthreads * access_per_thread) {
				ksft_test_result_fail("Lost update, iter %d, %d vs %d.\n", i, count,
						      nthreads * access_per_thread);
				return;
			}

			extra_pages += count - nthreads * access_per_thread;
		}
	}

	pthread_barrier_wait(&start_barrier);
	finish = 1;
	pthread_barrier_wait(&end_barrier);

	ksft_test_result_pass("%s Extra pages %u (%.1lf%%), extra thread faults %d.\n", __func__,
			      extra_pages,
			      100.0 * extra_pages / (iter_count * nthreads * access_per_thread),
			      extra_thread_faults);
}

int main(int argc, char *argv[])
{
	int mem_size, shmid, buf_size, fd, i, ret;
	char *mem, *map, *fmem;
	struct stat sbuf;

	progname = argv[0];

	ksft_print_header();

	if (init_uffd())
		ksft_exit_pass();

	ksft_set_plan(115);

	page_size = getpagesize();
	hpage_size = read_pmd_pagesize();

	pagemap_fd = open(PAGEMAP, O_RDONLY);
	if (pagemap_fd < 0)
		return -EINVAL;

	/* 1. Sanity testing */
	sanity_tests_sd();

	/* 2. Normal page testing */
	mem_size = 10 * page_size;
	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	base_tests("Page testing:", mem, mem_size, 0);

	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 3. Large page testing */
	mem_size = 512 * 10 * page_size;
	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (mem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem\n");
	wp_init(mem, mem_size);
	wp_addr_range(mem, mem_size);

	base_tests("Large Page testing:", mem, mem_size, 0);

	wp_free(mem, mem_size);
	munmap(mem, mem_size);

	/* 4. Huge page testing */
	map = gethugepage(hpage_size);
	if (map) {
		wp_init(map, hpage_size);
		wp_addr_range(map, hpage_size);
		base_tests("Huge page testing:", map, hpage_size, 0);
		wp_free(map, hpage_size);
		free(map);
	} else {
		base_tests("Huge page testing:", NULL, 0, 1);
	}

	/* 5. SHM Hugetlb page testing */
	mem_size = 2*1024*1024;
	mem = gethugetlb_mem(mem_size, &shmid);
	if (mem) {
		wp_init(mem, mem_size);
		wp_addr_range(mem, mem_size);

		base_tests("Hugetlb shmem testing:", mem, mem_size, 0);

		wp_free(mem, mem_size);
		shmctl(shmid, IPC_RMID, NULL);
	} else {
		base_tests("Hugetlb shmem testing:", NULL, 0, 1);
	}

	/* 6. Hugetlb page testing */
	mem = gethugetlb_mem(mem_size, NULL);
	if (mem) {
		wp_init(mem, mem_size);
		wp_addr_range(mem, mem_size);

		base_tests("Hugetlb mem testing:", mem, mem_size, 0);

		wp_free(mem, mem_size);
	} else {
		base_tests("Hugetlb mem testing:", NULL, 0, 1);
	}

	/* 7. File Hugetlb testing */
	mem_size = 2*1024*1024;
	fd = memfd_create("uffd-test", MFD_HUGETLB | MFD_NOEXEC_SEAL);
	mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mem) {
		wp_init(mem, mem_size);
		wp_addr_range(mem, mem_size);

		base_tests("Hugetlb shmem testing:", mem, mem_size, 0);

		wp_free(mem, mem_size);
		shmctl(shmid, IPC_RMID, NULL);
	} else {
		base_tests("Hugetlb shmem testing:", NULL, 0, 1);
	}
	close(fd);

	/* 8. File memory testing */
	buf_size = page_size * 10;

	fd = open(__FILE__".tmp0", O_RDWR | O_CREAT, 0777);
	if (fd < 0)
		ksft_exit_fail_msg("Create and read/write to a memory mapped file: %s\n",
				   strerror(errno));

	for (i = 0; i < buf_size; i++)
		if (write(fd, "c", 1) < 0)
			ksft_exit_fail_msg("Create and read/write to a memory mapped file\n");

	ret = stat(__FILE__".tmp0", &sbuf);
	if (ret < 0)
		ksft_exit_fail_msg("error %d %d %s\n", ret, errno, strerror(errno));

	fmem = mmap(NULL, sbuf.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (fmem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem %d %s\n", errno, strerror(errno));

	wp_init(fmem, sbuf.st_size);
	wp_addr_range(fmem, sbuf.st_size);

	base_tests("File memory testing:", fmem, sbuf.st_size, 0);

	wp_free(fmem, sbuf.st_size);
	munmap(fmem, sbuf.st_size);
	close(fd);

	/* 9. File memory testing */
	buf_size = page_size * 10;

	fd = memfd_create(__FILE__".tmp00", MFD_NOEXEC_SEAL);
	if (fd < 0)
		ksft_exit_fail_msg("Create and read/write to a memory mapped file: %s\n",
				   strerror(errno));

	if (ftruncate(fd, buf_size))
		ksft_exit_fail_msg("Error ftruncate\n");

	for (i = 0; i < buf_size; i++)
		if (write(fd, "c", 1) < 0)
			ksft_exit_fail_msg("Create and read/write to a memory mapped file\n");

	fmem = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (fmem == MAP_FAILED)
		ksft_exit_fail_msg("error nomem %d %s\n", errno, strerror(errno));

	wp_init(fmem, buf_size);
	wp_addr_range(fmem, buf_size);

	base_tests("File anonymous memory testing:", fmem, buf_size, 0);

	wp_free(fmem, buf_size);
	munmap(fmem, buf_size);
	close(fd);

	/* 10. Huge page tests */
	hpage_unit_tests();

	/* 11. Iterative test */
	test_simple();

	/* 12. Mprotect test */
	mprotect_tests();

	/* 13. Transact test */
	transact_test(page_size);

	/* 14. Sanity testing */
	sanity_tests();

	/*15. Unmapped address test */
	unmapped_region_tests();

	/* 16. Userfaultfd tests */
	userfaultfd_tests();

	close(pagemap_fd);
	ksft_exit_pass();
}
