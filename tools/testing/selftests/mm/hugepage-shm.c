// SPDX-License-Identifier: GPL-2.0
/*
 * hugepage-shm:
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

#define LENGTH (256UL*1024*1024)

#define dprintf(x)  printf(x)

int main(void)
{
	int shmid;
	unsigned long i;
	char *shmaddr;

	shmid = shmget(2, LENGTH, SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
	if (shmid < 0) {
		perror("shmget");
		exit(1);
	}
	printf("shmid: 0x%x\n", shmid);

	shmaddr = shmat(shmid, NULL, 0);
	if (shmaddr == (char *)-1) {
		perror("Shared memory attach failure");
		shmctl(shmid, IPC_RMID, NULL);
		exit(2);
	}
	printf("shmaddr: %p\n", shmaddr);

	dprintf("Starting the writes:\n");
	for (i = 0; i < LENGTH; i++) {
		shmaddr[i] = (char)(i);
		if (!(i % (1024 * 1024)))
			dprintf(".");
	}
	dprintf("\n");

	dprintf("Starting the Check...");
	for (i = 0; i < LENGTH; i++)
		if (shmaddr[i] != (char)i) {
			printf("\nIndex %lu mismatched\n", i);
			exit(3);
		}
	dprintf("Done.\n");

	if (shmdt((const void *)shmaddr) != 0) {
		perror("Detach failure");
		shmctl(shmid, IPC_RMID, NULL);
		exit(4);
	}

	shmctl(shmid, IPC_RMID, NULL);

	return 0;
}
