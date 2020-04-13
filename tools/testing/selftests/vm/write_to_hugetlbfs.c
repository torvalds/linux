// SPDX-License-Identifier: GPL-2.0
/*
 * This program reserves and uses hugetlb memory, supporting a bunch of
 * scenarios needed by the charged_reserved_hugetlb.sh test.
 */

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* Global definitions. */
enum method {
	HUGETLBFS,
	MMAP_MAP_HUGETLB,
	SHM,
	MAX_METHOD
};


/* Global variables. */
static const char *self;
static char *shmaddr;
static int shmid;

/*
 * Show usage and exit.
 */
static void exit_usage(void)
{
	printf("Usage: %s -p <path to hugetlbfs file> -s <size to map> "
	       "[-m <0=hugetlbfs | 1=mmap(MAP_HUGETLB)>] [-l] [-r] "
	       "[-o] [-w] [-n]\n",
	       self);
	exit(EXIT_FAILURE);
}

void sig_handler(int signo)
{
	printf("Received %d.\n", signo);
	if (signo == SIGINT) {
		printf("Deleting the memory\n");
		if (shmdt((const void *)shmaddr) != 0) {
			perror("Detach failure");
			shmctl(shmid, IPC_RMID, NULL);
			exit(4);
		}

		shmctl(shmid, IPC_RMID, NULL);
		printf("Done deleting the memory\n");
	}
	exit(2);
}

int main(int argc, char **argv)
{
	int fd = 0;
	int key = 0;
	int *ptr = NULL;
	int c = 0;
	int size = 0;
	char path[256] = "";
	enum method method = MAX_METHOD;
	int want_sleep = 0, private = 0;
	int populate = 0;
	int write = 0;
	int reserve = 1;

	unsigned long i;

	if (signal(SIGINT, sig_handler) == SIG_ERR)
		err(1, "\ncan't catch SIGINT\n");

	/* Parse command-line arguments. */
	setvbuf(stdout, NULL, _IONBF, 0);
	self = argv[0];

	while ((c = getopt(argc, argv, "s:p:m:owlrn")) != -1) {
		switch (c) {
		case 's':
			size = atoi(optarg);
			break;
		case 'p':
			strncpy(path, optarg, sizeof(path));
			break;
		case 'm':
			if (atoi(optarg) >= MAX_METHOD) {
				errno = EINVAL;
				perror("Invalid -m.");
				exit_usage();
			}
			method = atoi(optarg);
			break;
		case 'o':
			populate = 1;
			break;
		case 'w':
			write = 1;
			break;
		case 'l':
			want_sleep = 1;
			break;
		case 'r':
		    private
			= 1;
			break;
		case 'n':
			reserve = 0;
			break;
		default:
			errno = EINVAL;
			perror("Invalid arg");
			exit_usage();
		}
	}

	if (strncmp(path, "", sizeof(path)) != 0) {
		printf("Writing to this path: %s\n", path);
	} else {
		errno = EINVAL;
		perror("path not found");
		exit_usage();
	}

	if (size != 0) {
		printf("Writing this size: %d\n", size);
	} else {
		errno = EINVAL;
		perror("size not found");
		exit_usage();
	}

	if (!populate)
		printf("Not populating.\n");
	else
		printf("Populating.\n");

	if (!write)
		printf("Not writing to memory.\n");

	if (method == MAX_METHOD) {
		errno = EINVAL;
		perror("-m Invalid");
		exit_usage();
	} else
		printf("Using method=%d\n", method);

	if (!private)
		printf("Shared mapping.\n");
	else
		printf("Private mapping.\n");

	if (!reserve)
		printf("NO_RESERVE mapping.\n");
	else
		printf("RESERVE mapping.\n");

	switch (method) {
	case HUGETLBFS:
		printf("Allocating using HUGETLBFS.\n");
		fd = open(path, O_CREAT | O_RDWR, 0777);
		if (fd == -1)
			err(1, "Failed to open file.");

		ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
			   (private ? MAP_PRIVATE : MAP_SHARED) |
				   (populate ? MAP_POPULATE : 0) |
				   (reserve ? 0 : MAP_NORESERVE),
			   fd, 0);

		if (ptr == MAP_FAILED) {
			close(fd);
			err(1, "Error mapping the file");
		}
		break;
	case MMAP_MAP_HUGETLB:
		printf("Allocating using MAP_HUGETLB.\n");
		ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
			   (private ? (MAP_PRIVATE | MAP_ANONYMOUS) :
				      MAP_SHARED) |
				   MAP_HUGETLB | (populate ? MAP_POPULATE : 0) |
				   (reserve ? 0 : MAP_NORESERVE),
			   -1, 0);

		if (ptr == MAP_FAILED)
			err(1, "mmap");

		printf("Returned address is %p\n", ptr);
		break;
	case SHM:
		printf("Allocating using SHM.\n");
		shmid = shmget(key, size,
			       SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
		if (shmid < 0) {
			shmid = shmget(++key, size,
				       SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
			if (shmid < 0)
				err(1, "shmget");
		}
		printf("shmid: 0x%x, shmget key:%d\n", shmid, key);

		ptr = shmat(shmid, NULL, 0);
		if (ptr == (int *)-1) {
			perror("Shared memory attach failure");
			shmctl(shmid, IPC_RMID, NULL);
			exit(2);
		}
		printf("shmaddr: %p\n", ptr);

		break;
	default:
		errno = EINVAL;
		err(1, "Invalid method.");
	}

	if (write) {
		printf("Writing to memory.\n");
		memset(ptr, 1, size);
	}

	if (want_sleep) {
		/* Signal to caller that we're done. */
		printf("DONE\n");

		/* Hold memory until external kill signal is delivered. */
		while (1)
			sleep(100);
	}

	if (method == HUGETLBFS)
		close(fd);

	return 0;
}
