/*
 * SO2 - Block device driver (#8)
 * Test suite for exercise #3 (RAM Disk)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#define NR_SECTORS	128
#define SECTOR_SIZE	512

#define DEVICE_NAME	"/dev/myblock"
#define MODULE_NAME	"ram-disk"
#define MY_BLOCK_MAJOR	"240"
#define MY_BLOCK_MINOR	"0"


#define max_elem_value(elem)	\
	(1 << 8*sizeof(elem))

static unsigned char buffer[SECTOR_SIZE];
static unsigned char buffer_copy[SECTOR_SIZE];

static void test_sector(int fd, size_t sector)
{
	int i;

	for (i = 0; i < sizeof(buffer) / sizeof(buffer[0]); i++)
		buffer[i] = rand() % max_elem_value(buffer[0]);

	lseek(fd, sector * SECTOR_SIZE, SEEK_SET);
	write(fd, buffer, sizeof(buffer));

	fsync(fd);

	lseek(fd, sector * SECTOR_SIZE, SEEK_SET);
	read(fd, buffer_copy, sizeof(buffer_copy));

	printf("test sector %3d ... ", sector);
	if (memcmp(buffer, buffer_copy, sizeof(buffer_copy)) == 0)
		printf("passed\n");
	else
		printf("failed\n");
}

int main(void)
{
	int fd;
	size_t i;
	int back_errno;

	printf("insmod ../kernel/" MODULE_NAME ".ko\n");
	if (system("insmod ../kernel/" MODULE_NAME ".ko\n")) {
		fprintf(stderr, "insmod failed\n");
		exit(EXIT_FAILURE);
	}

	sleep(1);

	printf("mknod " DEVICE_NAME " b " MY_BLOCK_MAJOR " " MY_BLOCK_MINOR "\n");
	system("mknod " DEVICE_NAME " b " MY_BLOCK_MAJOR " " MY_BLOCK_MINOR "\n");
	sleep(1);

	fd = open(DEVICE_NAME, O_RDWR);
	if (fd < 0) {
		back_errno = errno;
		perror("open");
		fprintf(stderr, "errno is %d\n", back_errno);
		exit(EXIT_FAILURE);
	}

	srand(time(NULL));
	for (i = 0; i < NR_SECTORS; i++)
		test_sector(fd, i);

	close(fd);

	sleep(1);
	printf("rmmod " MODULE_NAME "\n");
	system("rmmod " MODULE_NAME "\n");

	return 0;
}
