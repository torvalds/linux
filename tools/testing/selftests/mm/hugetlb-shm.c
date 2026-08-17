// SPDX-License-Identifier: GPL-2.0
/*
 * hugetlb-shm:
 *
 * Example of using huge page memory in a user application using Sys V shared
 * memory system calls.  In this example the app is requesting 256MB of
 * memory that is backed by huge pages.  The application uses the flag
 * SHM_HUGETLB in the shmget system call to inform the kernel that it is
 * requesting huge pages.
 *
 * Note: The default shared memory limit is quite low on many kernels,
 * you may need to increase it via:
 *
 * echo 268435456 > /proc/sys/kernel/shmmax
 *
 * This will increase the maximum size per shared memory segment to 256MB.
 * The other limit that you will hit eventually is shmall which is the
 * total amount of shared memory in pages. To set it to 16GB on a system
 * with a 4kB pagesize do:
 *
 * echo 4194304 > /proc/sys/kernel/shmall
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>

#include "vm_util.h"
#include "hugepage_settings.h"

#define LENGTH (256UL*1024*1024)

static void prepare(void)
{
	unsigned long length, hugepage_size, nr;

	hugepage_size = default_huge_page_size();
	if (!hugepage_size)
		ksft_exit_skip("Unable to determine huge page size\n");

	length = (LENGTH + hugepage_size - 1) & ~(hugepage_size - 1);
	nr = length / hugepage_size;

	if (!hugetlb_setup_default(nr))
		ksft_exit_skip("Not enough free huge pages\n");

	shm_limits_prepare(length);
}

int main(void)
{
	int shmid;
	unsigned long i;
	char *shmaddr;

	ksft_print_header();
	ksft_set_plan(1);

	prepare();

	shmid = shmget(2, LENGTH, SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
	if (shmid < 0)
		ksft_exit_fail_perror("shmget");

	ksft_print_msg("shmid: 0x%x\n", shmid);

	shmaddr = shmat(shmid, NULL, 0);
	if (shmaddr == (char *)-1) {
		ksft_perror("Shared memory attach failure");
		shmctl(shmid, IPC_RMID, NULL);
		ksft_exit_fail();
	}
	ksft_print_msg("shmaddr: %p\n", shmaddr);

	ksft_print_msg("Starting the writes:\n");
	for (i = 0; i < LENGTH; i++)
		shmaddr[i] = (char)(i);

	ksft_print_msg("Starting the Check...");
	for (i = 0; i < LENGTH; i++)
		if (shmaddr[i] != (char)i)
			ksft_exit_fail_msg("Data mismatch at index %lu\n", i);
	ksft_print_msg("Done.\n");

	if (shmdt((const void *)shmaddr) != 0) {
		ksft_perror("Detach failure");
		shmctl(shmid, IPC_RMID, NULL);
		ksft_exit_fail();
	}

	shmctl(shmid, IPC_RMID, NULL);

	ksft_test_result_pass("hugepage using SysV shmget/shmat\n");
	ksft_finished();
}

SHM_LIMITS_RESTORE()
