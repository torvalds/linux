#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>

#include "run-test.h"
#include "ssr.h"

#define SSR_BASE_NAME		"ssr"
#define SSR_LIN_EXT		".ko"
#define SSR_MOD_NAME		SSR_BASE_NAME SSR_LIN_EXT

#define CRC_SIZE		4

#define ONE_SECTOR		KERNEL_SECTOR_SIZE
#define ONE_PAGE		4096
#define TWO_PAGES		8192
#define TEN_PAGES		40960
#define ONE_MEG			1048576

/* Read/write buffers. */
static unsigned char *log_rd_buf, *log_wr_buf;
static unsigned char *phys1_rd_buf, *phys1_wr_buf;
static unsigned char *phys2_rd_buf, *phys2_wr_buf;
static unsigned char *log_rd_crc, *log_wr_crc;
static unsigned char *phys1_rd_crc, *phys1_wr_crc;
static unsigned char *phys2_rd_crc, *phys2_wr_crc;

/* File descriptors. */
static int log_fd, phys1_fd, phys2_fd;

enum {
	START = 0,
	MIDDLE,
	END
};

enum {
	PHYS_FILL_DATA = 'P',
	LOG_FILL_DATA = 'L',
	CORRUPT_DATA = 'C',
	PHYS1_DISK_DIRTY_DATA = 'a',
	PHYS1_BUF_DIRTY_DATA = 'A',
	PHYS2_DISK_DIRTY_DATA = 'b',
	PHYS2_BUF_DIRTY_DATA = 'B',
	LOG_DISK_DIRTY_DATA = 'd',
	LOG_BUF_DIRTY_DATA = 'D',
};

/*
 * "upgraded" read routine
 */

static ssize_t xread(int fd, void *buffer, size_t len)
{
	ssize_t ret;
	ssize_t n;

	n = 0;
	while (n < (ssize_t) len) {
		ret = read(fd, (char *) buffer + n, len - n);
		if (ret < 0)
			return -1;
		if (ret == 0)
			break;
		n += ret;
	}

	return n;
}

/*
 * "upgraded" write routine
 */

static ssize_t xwrite(int fd, const void *buffer, size_t len)
{
	ssize_t ret;
	ssize_t n;

	n = 0;
	while (n < (ssize_t) len) {
		ret = write(fd, (const char *) buffer + n, len - n);
		if (ret < 0)
			return -1;
		if (ret == 0)
			break;
		n += ret;
	}

	return n;
}

/*
 * Compute CRC32.
 */

static unsigned int crc32(unsigned int seed,
		const unsigned char *p, unsigned int len)
{
	size_t i;
	unsigned int crc = seed;

	while (len--) {
		crc ^= *p++;
		for (i = 0; i < 8; i++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
	}

	return crc;
}

static void compute_crc(const void *data_buffer, void *crc_buffer, size_t len)
{
	size_t i;
	unsigned int crc;

	for (i = 0; i < len; i += ONE_SECTOR) {
		crc = crc32(0, (const unsigned char *) data_buffer + i, ONE_SECTOR);
		memcpy((char *) crc_buffer + i / ONE_SECTOR * CRC_SIZE,
				&crc, CRC_SIZE);
	}
}

static off_t data_offset_from_whence(int whence, size_t len)
{
	switch (whence) {
	case START:
		return 0;
	case MIDDLE:
		return LOGICAL_DISK_SIZE / 2 - len;
	case END:
		return LOGICAL_DISK_SIZE - len;
	default:
		return -1;
	}
}

static off_t crc_offset_from_whence(int whence, size_t len)
{
	off_t data_offset = data_offset_from_whence(whence, len);

	return LOGICAL_DISK_SIZE + data_offset / ONE_SECTOR * CRC_SIZE;
}

static void fill_buffer(void *buffer, int c, size_t len)
{
	memset(buffer, c, len);
}

static void log_fill_buffer(size_t len)
{
	fill_buffer(log_wr_buf, LOG_FILL_DATA, len);
}

static void phys_fill_buffer(size_t len)
{
	fill_buffer(phys1_wr_buf, PHYS_FILL_DATA, len);
	fill_buffer(phys2_wr_buf, PHYS_FILL_DATA, len);
}

static ssize_t read_whence_data(int fd, void *buffer, size_t len, int whence)
{
	off_t offset = data_offset_from_whence(whence, len);

	lseek(fd, offset, SEEK_SET);
	return xread(fd, buffer, len);
}

static ssize_t read_whence_crc(int fd, void *crc_buffer, size_t data_len,
		int whence)
{
	off_t offset = crc_offset_from_whence(whence, data_len);

	lseek(fd, offset, SEEK_SET);
	return xread(fd, crc_buffer, data_len / ONE_SECTOR * CRC_SIZE);
}

static ssize_t write_whence_data(int fd, const void *buffer,
		size_t len, int whence)
{
	off_t offset = data_offset_from_whence(whence, len);

	lseek(fd, offset, SEEK_SET);
	return xwrite(fd, buffer, len);
}

static ssize_t write_whence_crc(int fd, void *crc_buffer, size_t data_len,
		int whence)
{
	off_t offset = crc_offset_from_whence(whence, data_len);

	lseek(fd, offset, SEEK_SET);
	return xwrite(fd, crc_buffer, data_len / ONE_SECTOR * CRC_SIZE);
}

static ssize_t log_read_whence(size_t len, int whence)
{
	ssize_t n;

	n = read_whence_data(log_fd, log_rd_buf, len, whence);
	if (n < 0)
		return -1;
	compute_crc(log_rd_buf, log_rd_crc, len);
	return n;
}

static ssize_t log_write_whence(size_t len, int whence)
{
	compute_crc(log_wr_buf, log_wr_crc, len);
	return write_whence_data(log_fd, log_wr_buf, len, whence);
}

static ssize_t phys_read_whence(size_t id, size_t len, int whence)
{
	ssize_t n_data, n_crc;
	int fd = ((id == 1) ? phys1_fd : phys2_fd);

	unsigned char *data_buf = ((id == 1) ? phys1_rd_buf : phys2_rd_buf);
	unsigned char *crc_buf = ((id == 1) ? phys1_rd_crc : phys2_rd_crc);

	n_data = read_whence_data(fd, data_buf, len, whence);
	if (n_data < 0)
		return -1;
	n_crc = read_whence_crc(fd, crc_buf, len, whence);
	if (n_crc < 0)
		return -1;
	return n_data;
}

static ssize_t phys_write_whence(size_t id, size_t len, int whence)
{
	ssize_t n_data, n_crc;
	int fd = ((id == 1) ? phys1_fd : phys2_fd);
	unsigned char *data_buf = ((id == 1) ? phys1_wr_buf : phys2_wr_buf);
	unsigned char *crc_buf = ((id == 1) ? phys1_wr_crc : phys2_wr_crc);

	compute_crc(data_buf, crc_buf, len);
	n_data = write_whence_data(fd, data_buf, len, whence);
	if (n_data < 0)
		return -1;
	n_crc = write_whence_crc(fd, crc_buf, len, whence);
	if (n_crc < 0)
		return -1;
	return n_data;
}

static void corrupt_buffer(void *buffer, size_t sectors)
{
	size_t i;

	for (i = 0; i < sectors; i++)
		((unsigned char *) buffer)[i * ONE_SECTOR] = CORRUPT_DATA;
}

static ssize_t phys_corrupt_and_write_whence(size_t id, size_t len,
		size_t sectors, int whence)
{
	ssize_t n_data, n_crc;
	int fd = ((id == 1) ? phys1_fd : phys2_fd);
	unsigned char *data_buf = ((id == 1) ? phys1_wr_buf : phys2_wr_buf);
	unsigned char *crc_buf = ((id == 1) ? phys1_wr_crc : phys2_wr_crc);

	compute_crc(data_buf, crc_buf, len);
	corrupt_buffer(data_buf, sectors);
	n_data = write_whence_data(fd, data_buf, len, whence);
	if (n_data < 0)
		return -1;
	n_crc = write_whence_crc(fd, crc_buf, len, whence);
	if (n_crc < 0)
		return -1;
	return n_data;
}

static ssize_t log_read_start(size_t len)
{
	return log_read_whence(len, START);
}

static ssize_t log_read_middle(size_t len)
{
	return log_read_whence(len, MIDDLE);
}

static ssize_t log_read_end(size_t len)
{
	return log_read_whence(len, END);
}

static ssize_t log_write_start(size_t len)
{
	return log_write_whence(len, START);
}

static ssize_t log_write_middle(size_t len)
{
	return log_write_whence(len, MIDDLE);
}

static ssize_t log_write_end(size_t len)
{
	return log_write_whence(len, END);
}

static ssize_t phys1_read_start(size_t len)
{
	return phys_read_whence(1, len, START);
}

#if 0
static ssize_t phys1_read_middle(size_t len)
{
	return phys_read_whence(1, len, MIDDLE);
}

static ssize_t phys1_read_end(size_t len)
{
	return phys_read_whence(1, len, END);
}
#endif

static ssize_t phys1_write_start(size_t len)
{
	return phys_write_whence(1, len, START);
}

static ssize_t phys1_corrupt_and_write_start(size_t len, size_t sectors)
{
	return phys_corrupt_and_write_whence(1, len, sectors, START);
}

#if 0
static ssize_t phys1_write_middle(size_t len)
{
	return phys_write_whence(1, len, MIDDLE);
}

static ssize_t phys1_write_end(size_t len)
{
	return phys_write_whence(1, len, END);
}
#endif

static ssize_t phys2_read_start(size_t len)
{
	return phys_read_whence(2, len, START);
}

#if 0
static ssize_t phys2_read_middle(size_t len)
{
	return phys_read_whence(2, len, MIDDLE);
}

static ssize_t phys2_read_end(size_t len)
{
	return phys_read_whence(2, len, END);
}
#endif

static ssize_t phys2_write_start(size_t len)
{
	return phys_write_whence(2, len, START);
}

static ssize_t phys2_corrupt_and_write_start(size_t len, size_t sectors)
{
	return phys_corrupt_and_write_whence(2, len, sectors, START);
}

#if 0
static ssize_t phys2_write_middle(size_t len)
{
	return phys_write_whence(2, len, MIDDLE);
}

static ssize_t phys2_write_end(size_t len)
{
	return phys_write_whence(2, len, END);
}
#endif

static int cmp_data_log_rd_phys1_wr(size_t len)
{
	return memcmp(log_rd_buf, phys1_wr_buf, len);
}

static int cmp_data_log_rd_phys2_wr(size_t len)
{
	return memcmp(log_rd_buf, phys2_wr_buf, len);
}

static int cmp_data_log_rd_phys1_rd(size_t len)
{
	return memcmp(log_rd_buf, phys1_rd_buf, len);
}

static int cmp_data_log_rd_phys2_rd(size_t len)
{
	return memcmp(log_rd_buf, phys2_rd_buf, len);
}

static int cmp_data_log_wr_phys1_rd(size_t len)
{
	return memcmp(log_wr_buf, phys1_rd_buf, len);
}

static int cmp_data_log_wr_phys2_rd(size_t len)
{
	return memcmp(log_wr_buf, phys2_rd_buf, len);
}

static int cmp_crc_log_rd_phys1_wr(size_t data_len)
{
	return memcmp(log_rd_crc, phys1_wr_crc, data_len / ONE_SECTOR * CRC_SIZE);
}

static int cmp_crc_log_rd_phys2_wr(size_t data_len)
{
	return memcmp(log_rd_crc, phys2_wr_crc, data_len / ONE_SECTOR * CRC_SIZE);
}

static int cmp_crc_log_rd_phys1_rd(size_t data_len)
{
	return memcmp(log_rd_crc, phys1_rd_crc, data_len / ONE_SECTOR * CRC_SIZE);
}

static int cmp_crc_log_rd_phys2_rd(size_t data_len)
{
	return memcmp(log_rd_crc, phys2_rd_crc, data_len / ONE_SECTOR * CRC_SIZE);
}

static int cmp_crc_log_wr_phys1_rd(size_t data_len)
{
	return memcmp(log_wr_crc, phys1_rd_crc, data_len / ONE_SECTOR * CRC_SIZE);
}

static int cmp_crc_log_wr_phys2_rd(size_t data_len)
{
	return memcmp(log_wr_crc, phys2_rd_crc, data_len / ONE_SECTOR * CRC_SIZE);
}

static void drop_caches(void)
{
	int fd;
	char buf[] = "1\n";

	fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
	assert(fd >= 0);
	write(fd, buf, strlen(buf));
	close(fd);
}

static void flush_disk_buffers(void)
{
	sync();
	//system("/bin/echo 1 > /proc/sys/vm/drop_caches");
	drop_caches();
}

static void dump_data(const void *buf, size_t len, const char *header)
{
	size_t i;

	printf("%s:", header);
	for (i = 0; i < len / sizeof(unsigned int); i++) {
		if (i % 4 == 0)
			printf("\n\t");
		printf(" %08x", ((unsigned int *) buf)[i]);
	}
	printf("\n\n");
}

void init_world(void)
{
	/* Cleanup if required. */
	flush_disk_buffers();
	system("/sbin/rmmod " SSR_BASE_NAME " > /dev/null 2>&1");
	system("/bin/cat /proc/devices | /bin/grep " SSR_BASE_NAME " > /dev/null");
	system("/bin/rm -f " LOGICAL_DISK_NAME);

	assert(system("/sbin/insmod " SSR_MOD_NAME) == 0);
	assert(system("/bin/cat /proc/devices | /bin/grep " SSR_BASE_NAME
				" > /dev/null") == 0);
	assert(access(PHYSICAL_DISK1_NAME, F_OK) == 0);
	assert(access(PHYSICAL_DISK2_NAME, F_OK) == 0);
	assert(access(LOGICAL_DISK_NAME, F_OK) == 0);

	log_rd_buf = calloc(1024 * 1024, 1);
	assert(log_rd_buf != NULL);
	log_wr_buf = calloc(1024 * 1024, 1);
	assert(log_rd_buf != NULL);
	phys1_rd_buf = calloc(1024 * 1024, 1);
	assert(phys1_rd_buf != NULL);
	phys1_wr_buf = calloc(1024 * 1024, 1);
	assert(phys1_wr_buf != NULL);
	phys2_rd_buf = calloc(1024 * 1024, 1);
	assert(phys2_rd_buf != NULL);
	phys2_wr_buf = calloc(1024 * 1024, 1);
	assert(phys2_wr_buf != NULL);
	log_rd_crc = calloc(8 * 1024, 1);
	assert(log_rd_crc != NULL);
	log_wr_crc = calloc(8 * 1024, 1);
	assert(log_rd_crc != NULL);
	phys1_rd_crc = calloc(8 * 1024, 1);
	assert(phys1_rd_crc != NULL);
	phys1_wr_crc = calloc(8 * 1024, 1);
	assert(phys1_wr_crc != NULL);
	phys2_rd_crc = calloc(8 * 1024, 1);
	assert(phys2_rd_crc != NULL);
	phys2_wr_crc = calloc(8 * 1024, 1);
	assert(phys2_wr_crc != NULL);
}

void cleanup_world(void)
{
	flush_disk_buffers();
	system("/sbin/rmmod " SSR_BASE_NAME);
	system("/bin/cat /proc/devices | /bin/grep " SSR_BASE_NAME " > /dev/null");
	system("/bin/rm -f " LOGICAL_DISK_NAME);
	free(log_rd_buf); free(log_wr_buf);
	free(phys1_rd_buf); free(phys1_wr_buf);
	free(phys2_rd_buf); free(phys2_wr_buf);
	free(log_rd_crc); free(log_wr_crc);
	free(phys1_rd_crc); free(phys1_wr_crc);
	free(phys2_rd_crc); free(phys2_wr_crc);
}

static void make_disks_dirty(void)
{
	fill_buffer(phys1_wr_buf, PHYS1_DISK_DIRTY_DATA, ONE_MEG);
	fill_buffer(phys1_wr_crc, PHYS1_DISK_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	fill_buffer(phys2_wr_buf, PHYS2_DISK_DIRTY_DATA, ONE_MEG);
	fill_buffer(phys2_wr_crc, PHYS2_DISK_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	phys1_write_start(ONE_MEG);
	phys2_write_start(ONE_MEG);
}

static void make_buffers_dirty(void)
{
	fill_buffer(phys1_wr_buf, PHYS1_BUF_DIRTY_DATA, ONE_MEG);
	fill_buffer(phys1_wr_crc, PHYS1_BUF_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	fill_buffer(phys1_rd_buf, PHYS1_BUF_DIRTY_DATA, ONE_MEG);
	fill_buffer(phys1_rd_crc, PHYS1_BUF_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	fill_buffer(phys2_wr_buf, PHYS2_BUF_DIRTY_DATA, ONE_MEG);
	fill_buffer(phys2_wr_crc, PHYS2_BUF_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	fill_buffer(phys2_rd_buf, PHYS2_BUF_DIRTY_DATA, ONE_MEG);
	fill_buffer(phys2_rd_crc, PHYS2_BUF_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	fill_buffer(log_wr_buf, LOG_BUF_DIRTY_DATA, ONE_MEG);
	fill_buffer(log_wr_crc, LOG_BUF_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
	fill_buffer(log_rd_buf, LOG_BUF_DIRTY_DATA, ONE_MEG);
	fill_buffer(log_rd_crc, LOG_BUF_DIRTY_DATA, ONE_MEG / ONE_SECTOR * CRC_SIZE);
}

static void init_test(void)
{
	flush_disk_buffers();
	log_fd = open(LOGICAL_DISK_NAME, O_RDWR);
	assert(log_fd >= 0);
	phys1_fd = open(PHYSICAL_DISK1_NAME, O_RDWR);
	assert(phys1_fd >= 0);
	phys2_fd = open(PHYSICAL_DISK2_NAME, O_RDWR);
	assert(phys2_fd >= 0);
	make_disks_dirty();
	make_buffers_dirty();
	flush_disk_buffers();
}

static void cleanup_test(void)
{
	close(log_fd);
	close(phys1_fd);
	close(phys2_fd);
}

static void open_logical(void)
{
	int fd;

	fd = open(LOGICAL_DISK_NAME, O_RDWR);
	basic_test(fd >= 0);
	close(fd);
}

static void close_logical(void)
{
	int fd, rc;

	fd = open(LOGICAL_DISK_NAME, O_RDWR);
	rc = close(fd);
	basic_test(rc == 0);
}

static void use_after_close_invalid(void)
{
	int fd, val;
	ssize_t n;

	fd = open(LOGICAL_DISK_NAME, O_RDWR);
	close(fd);
	n = read(fd, &val, sizeof(val));
	basic_test(n < 0);
}

static void lseek_logical(void)
{
	off_t offset;

	init_test();
	offset = lseek(log_fd, LOGICAL_DISK_SIZE / 2, SEEK_SET);
	basic_test(offset == LOGICAL_DISK_SIZE / 2);
	cleanup_test();
}

static void read_one_sector_start(void)
{
	ssize_t n;

	init_test();
	n = log_read_start(ONE_SECTOR);
	basic_test(n == ONE_SECTOR);
	cleanup_test();
}

static void read_one_sector_middle(void)
{
	ssize_t n;

	init_test();
	n = log_read_middle(ONE_SECTOR);
	basic_test(n == ONE_SECTOR);
	cleanup_test();
}

static void read_one_sector_end(void)
{
	ssize_t n;

	init_test();
	n = log_read_end(ONE_SECTOR);
	basic_test(n == ONE_SECTOR);
	cleanup_test();
}

static void write_one_sector_start(void)
{
	ssize_t n;

	init_test();
	n = log_write_start(ONE_SECTOR);
	basic_test(n == ONE_SECTOR);
	cleanup_test();
}

static void write_one_sector_middle(void)
{
	ssize_t n;

	init_test();
	n = log_write_middle(ONE_SECTOR);
	basic_test(n == ONE_SECTOR);
	cleanup_test();
}

static void write_one_sector_end(void)
{
	ssize_t n;

	init_test();
	n = log_write_end(ONE_SECTOR);
	basic_test(n == ONE_SECTOR);
	cleanup_test();
}

static void read_one_page_start(void)
{
	ssize_t n;

	init_test();
	n = log_read_start(ONE_PAGE);
	basic_test(n == ONE_PAGE);
	cleanup_test();
}

static void read_one_page_middle(void)
{
	ssize_t n;

	init_test();
	n = log_read_middle(ONE_PAGE);
	basic_test(n == ONE_PAGE);
	cleanup_test();
}

static void read_one_page_end(void)
{
	ssize_t n;

	init_test();
	n = log_read_end(ONE_PAGE);
	basic_test(n == ONE_PAGE);
	cleanup_test();
}

static void write_one_page_start(void)
{
	ssize_t n;

	init_test();
	n = log_write_start(ONE_PAGE);
	basic_test(n == ONE_PAGE);
	cleanup_test();
}

static void write_one_page_middle(void)
{
	ssize_t n;

	init_test();
	n = log_write_middle(ONE_PAGE);
	basic_test(n == ONE_PAGE);
	cleanup_test();
}

static void write_one_page_end(void)
{
	ssize_t n;

	init_test();
	n = log_write_end(ONE_PAGE);
	basic_test(n == ONE_PAGE);
	cleanup_test();
}

static void read_two_pages_start(void)
{
	ssize_t n;

	init_test();
	n = log_read_start(TWO_PAGES);
	basic_test(n == TWO_PAGES);
	cleanup_test();
}

static void read_two_pages_middle(void)
{
	ssize_t n;

	init_test();
	n = log_read_middle(TWO_PAGES);
	basic_test(n == TWO_PAGES);
	cleanup_test();
}

static void read_two_pages_end(void)
{
	ssize_t n;

	init_test();
	n = log_read_end(TWO_PAGES);
	basic_test(n == TWO_PAGES);
	cleanup_test();
}

static void write_two_pages_start(void)
{
	ssize_t n;

	init_test();
	n = log_write_start(TWO_PAGES);
	basic_test(n == TWO_PAGES);
	cleanup_test();
}

static void write_two_pages_middle(void)
{
	ssize_t n;

	init_test();
	n = log_write_middle(TWO_PAGES);
	basic_test(n == TWO_PAGES);
	cleanup_test();
}

static void write_two_pages_end(void)
{
	ssize_t n;

	init_test();
	n = log_write_end(TWO_PAGES);
	basic_test(n == TWO_PAGES);
	cleanup_test();
}

static void read_one_meg_start(void)
{
	ssize_t n;

	init_test();
	n = log_read_start(ONE_MEG);
	basic_test(n == ONE_MEG);
	cleanup_test();
}

static void read_one_meg_middle(void)
{
	ssize_t n;

	init_test();
	n = log_read_middle(ONE_MEG);
	basic_test(n == ONE_MEG);
	cleanup_test();
}

static void read_one_meg_end(void)
{
	ssize_t n;

	init_test();
	n = log_read_end(ONE_MEG);
	basic_test(n == ONE_MEG);
	cleanup_test();
}

static void write_one_meg_start(void)
{
	ssize_t n;

	init_test();
	n = log_write_start(ONE_MEG);
	basic_test(n == ONE_MEG);
	cleanup_test();
}

static void write_one_meg_middle(void)
{
	ssize_t n;

	init_test();
	n = log_write_middle(ONE_MEG);
	basic_test(n == ONE_MEG);
	cleanup_test();
}

static void write_one_meg_end(void)
{
	ssize_t n;

	init_test();
	n = log_write_end(ONE_MEG);
	basic_test(n == ONE_MEG);
	cleanup_test();
}

static void read_boundary_one_sector(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xread(log_fd, log_rd_buf, ONE_SECTOR);
	basic_test(n == 0);
	cleanup_test();
}

static void read_boundary_one_page(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xread(log_fd, log_rd_buf, ONE_PAGE);
	basic_test(n == 0);
	cleanup_test();
}

static void read_boundary_two_pages(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xread(log_fd, log_rd_buf, TWO_PAGES);
	basic_test(n == 0);
	cleanup_test();
}

static void read_boundary_one_meg(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xread(log_fd, log_rd_buf, ONE_MEG);
	basic_test(n == 0);
	cleanup_test();
}

static void write_boundary_one_sector(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xwrite(log_fd, log_rd_buf, ONE_SECTOR);
	basic_test(n < 0);
	cleanup_test();
}

static void write_boundary_one_page(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xwrite(log_fd, log_wr_buf, ONE_PAGE);
	basic_test(n < 0);
	cleanup_test();
}

static void write_boundary_two_pages(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xwrite(log_fd, log_wr_buf, TWO_PAGES);
	basic_test(n < 0);
	cleanup_test();
}

static void write_boundary_one_meg(void)
{
	ssize_t n;

	init_test();
	lseek(log_fd, LOGICAL_DISK_SIZE, SEEK_SET);
	n = xwrite(log_fd, log_wr_buf, ONE_MEG);
	basic_test(n < 0);
	cleanup_test();
}

static size_t get_free_memory(void)
{
	FILE *f;
	size_t i;
	char buf[256];
	char *p;

	f = fopen("/proc/meminfo", "rt");
	assert(f != NULL);
	/* Second line is 'MemFree: ...' */
	fgets(buf, 256, f);
	fgets(buf, 256, f);
	fclose(f);

	p = NULL;
	for (i = 0; i < 256; i++)
		if (buf[i] == ':') {
			p = buf+i+1;
			break;
		}

	return strtoul(p, NULL, 10);
}

static void memory_is_freed(void)
{
	size_t mem_used_before, mem_used_after;
	size_t i;

	init_test();
	mem_used_before = get_free_memory();
	for (i = 0; i < 5; i++)
		log_write_start(ONE_MEG);
	mem_used_after = get_free_memory();

	/* We assume 3MB (3072KB) is a reasonable memory usage in writes. */
	basic_test(mem_used_after < mem_used_before + 3072 &&
			mem_used_before < mem_used_after + 3072);
	cleanup_test();
}

static void write_one_sector_check_phys1(void)
{
	int rc;
	size_t len = ONE_SECTOR;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_data_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_page_check_phys1(void)
{
	int rc;
	size_t len = ONE_PAGE;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_data_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_two_pages_check_phys1(void)
{
	int rc;
	size_t len = TWO_PAGES;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_data_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_meg_check_phys1(void)
{
	int rc;
	size_t len = ONE_MEG;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_data_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_sector_check_phys(void)
{
	int rc1, rc2;
	size_t len = ONE_SECTOR;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_data_log_wr_phys1_rd(len);
	rc2 = cmp_data_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void write_one_page_check_phys(void)
{
	int rc1, rc2;
	size_t len = ONE_PAGE;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_data_log_wr_phys1_rd(len);
	rc2 = cmp_data_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void write_two_pages_check_phys(void)
{
	int rc1, rc2;
	size_t len = TWO_PAGES;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_data_log_wr_phys1_rd(len);
	rc2 = cmp_data_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void write_one_meg_check_phys(void)
{
	int rc1, rc2;
	size_t len = ONE_MEG;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_data_log_wr_phys1_rd(len);
	rc2 = cmp_data_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void read_one_sector_after_write(void)
{
	int rc;
	size_t len = ONE_SECTOR;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc = cmp_data_log_rd_phys1_wr(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void read_one_page_after_write(void)
{
	int rc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc = cmp_data_log_rd_phys1_wr(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void read_two_pages_after_write(void)
{
	int rc;
	size_t len = TWO_PAGES;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc = cmp_data_log_rd_phys1_wr(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void read_one_meg_after_write(void)
{
	int rc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc = cmp_data_log_rd_phys1_wr(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_sector_check_phys1_crc(void)
{
	int rc;
	size_t len = ONE_SECTOR;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_crc_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_page_check_phys1_crc(void)
{
	int rc;
	size_t len = ONE_PAGE;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_crc_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_two_pages_check_phys1_crc(void)
{
	int rc;
	size_t len = TWO_PAGES;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_crc_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_meg_check_phys1_crc(void)
{
	int rc;
	size_t len = ONE_MEG;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc = cmp_crc_log_wr_phys1_rd(len);
	basic_test(rc == 0);
	cleanup_test();
}

static void write_one_sector_check_phys_crc(void)
{
	int rc1, rc2;
	size_t len = ONE_SECTOR;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_crc_log_wr_phys1_rd(len);
	rc2 = cmp_crc_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void write_one_page_check_phys_crc(void)
{
	int rc1, rc2;
	size_t len = ONE_PAGE;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_crc_log_wr_phys1_rd(len);
	rc2 = cmp_crc_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void write_two_pages_check_phys_crc(void)
{
	int rc1, rc2;
	size_t len = TWO_PAGES;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_crc_log_wr_phys1_rd(len);
	rc2 = cmp_crc_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void write_one_meg_check_phys_crc(void)
{
	int rc1, rc2;
	size_t len = ONE_MEG;

	init_test();
	log_fill_buffer(len);
	log_write_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	phys2_read_start(len);
	rc1 = cmp_crc_log_wr_phys1_rd(len);
	rc2 = cmp_crc_log_wr_phys2_rd(len);
	basic_test(rc1 == 0 && rc2 == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_sector_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_SECTOR;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, 1);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys2_wr(len);
	rc_crc = cmp_crc_log_rd_phys2_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_sector_in_page_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, 1);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys2_wr(len);
	rc_crc = cmp_crc_log_rd_phys2_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_page_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, ONE_PAGE / ONE_SECTOR);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys2_wr(len);
	rc_crc = cmp_crc_log_rd_phys2_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_ten_pages_in_one_meg_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, TEN_PAGES / ONE_SECTOR);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys2_wr(len);
	rc_crc = cmp_crc_log_rd_phys2_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_meg_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, ONE_MEG / ONE_SECTOR);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys2_wr(len);
	rc_crc = cmp_crc_log_rd_phys2_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_sector_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_SECTOR;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, 1);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc_data = cmp_data_log_rd_phys1_rd(len);
	rc_crc = cmp_crc_log_rd_phys1_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_sector_in_page_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, 1);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc_data = cmp_data_log_rd_phys1_rd(len);
	rc_crc = cmp_crc_log_rd_phys1_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_page_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, ONE_PAGE / ONE_SECTOR);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc_data = cmp_data_log_rd_phys1_rd(len);
	rc_crc = cmp_crc_log_rd_phys1_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_ten_page_in_one_meg_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, TEN_PAGES / ONE_SECTOR);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc_data = cmp_data_log_rd_phys1_rd(len);
	rc_crc = cmp_crc_log_rd_phys1_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_meg_disk1(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, ONE_MEG / ONE_SECTOR);
	phys2_write_start(len);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys1_read_start(len);
	rc_data = cmp_data_log_rd_phys1_rd(len);
	rc_crc = cmp_crc_log_rd_phys1_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_sector_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_SECTOR;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, 1);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys1_wr(len);
	rc_crc = cmp_crc_log_rd_phys1_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_sector_in_page_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, 1);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys1_wr(len);
	rc_crc = cmp_crc_log_rd_phys1_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_one_page_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, ONE_PAGE / ONE_SECTOR);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys1_wr(len);
	rc_crc = cmp_crc_log_rd_phys1_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void corrupt_read_correct_ten_pages_in_one_meg_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, TEN_PAGES / ONE_SECTOR);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys1_wr(len);
	rc_crc = cmp_crc_log_rd_phys1_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	flush_disk_buffers();
	cleanup_test();
}

static void corrupt_read_correct_one_meg_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, ONE_MEG / ONE_SECTOR);
	flush_disk_buffers();
	log_read_start(len);
	rc_data = cmp_data_log_rd_phys1_wr(len);
	rc_crc = cmp_crc_log_rd_phys1_wr(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_sector_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_SECTOR;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, 1);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys2_read_start(len);
	rc_data = cmp_data_log_rd_phys2_rd(len);
	rc_crc = cmp_crc_log_rd_phys2_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_sector_in_page_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, 1);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys2_read_start(len);
	rc_data = cmp_data_log_rd_phys2_rd(len);
	rc_crc = cmp_crc_log_rd_phys2_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_page_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_PAGE;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, ONE_PAGE / ONE_SECTOR);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys2_read_start(len);
	rc_data = cmp_data_log_rd_phys2_rd(len);
	rc_crc = cmp_crc_log_rd_phys2_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_ten_page_in_one_meg_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, TEN_PAGES / ONE_SECTOR);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys2_read_start(len);
	rc_data = cmp_data_log_rd_phys2_rd(len);
	rc_crc = cmp_crc_log_rd_phys2_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void recover_one_meg_disk2(void)
{
	int rc_data, rc_crc;
	size_t len = ONE_MEG;

	init_test();
	phys_fill_buffer(len);
	phys1_write_start(len);
	phys2_corrupt_and_write_start(len, ONE_MEG / ONE_SECTOR);
	flush_disk_buffers();
	log_read_start(len);
	flush_disk_buffers();
	phys2_read_start(len);
	rc_data = cmp_data_log_rd_phys2_rd(len);
	rc_crc = cmp_crc_log_rd_phys2_rd(len);
	basic_test(rc_data == 0 && rc_crc == 0);
	cleanup_test();
}

static void dual_error(void)
{
	ssize_t n;
	size_t len = ONE_SECTOR;

	init_test();
	phys_fill_buffer(len);
	phys1_corrupt_and_write_start(len, 1);
	phys2_corrupt_and_write_start(len, 1);
	flush_disk_buffers();
	n = log_read_start(len);
	basic_test(n <= 0);
	cleanup_test();
}

struct run_test_t test_array[] = {
	{ open_logical, "open(" LOGICAL_DISK_NAME ")", 4 },
	{ close_logical, "close(" LOGICAL_DISK_NAME ")", 4 },
	{ use_after_close_invalid, "use after close is invalid", 4 },
	{ lseek_logical, "lseek(" LOGICAL_DISK_NAME ")", 4 },
	{ read_one_sector_start, "read one sector from the start", 5 },
	{ read_one_sector_middle, "read one sector from the middle", 5 },
	{ read_one_sector_end, "read one sector from the end", 5 },
	{ write_one_sector_start, "write one sector from the start", 5 },
	{ write_one_sector_middle, "write one sector from the middle", 5 },
	{ write_one_sector_end, "write one sector from the end", 5 },
	{ read_one_page_start, "read one page from the start", 5 },
	{ read_one_page_middle, "read one page from the middle", 5 },
	{ read_one_page_end, "read one page from the end", 5 },
	{ write_one_page_start, "write one page from the start", 5 },
	{ write_one_page_middle, "write one page from the middle", 5 },
	{ write_one_page_end, "write one page from the end", 5 },
	{ read_two_pages_start, "read two pages from the start", 5 },
	{ read_two_pages_middle, "read two pages from the middle", 5 },
	{ read_two_pages_end, "read two pages from the end", 5 },
	{ write_two_pages_start, "write two pages from the start", 5 },
	{ write_two_pages_middle, "write two pages from the middle", 5 },
	{ write_two_pages_end, "write two pages from the end", 5 },
	{ read_one_meg_start, "read 1MB from the start", 5 },
	{ read_one_meg_middle, "read 1MB from the middle", 5 },
	{ read_one_meg_end, "read 1MB from the end", 5 },
	{ write_one_meg_start, "write 1MB from the start", 5 },
	{ write_one_meg_middle, "write 1MB from the middle", 5 },
	{ write_one_meg_end, "write 1MB from the end", 5 },
	{ read_boundary_one_sector, "read one sector outside boundary", 7 },
	{ read_boundary_one_page, "read one page with contents outside boundary", 7 },
	{ read_boundary_two_pages, "read two pages with contents outside boundary", 7 },
	{ read_boundary_one_meg, "read 1MB with contents outside boundary", 7 },
	{ write_boundary_one_sector, "write one sector outside boundary", 7 },
	{ write_boundary_one_page, "write one page with contents outside boundary", 7 },
	{ write_boundary_two_pages, "write two pages with contents outside boundary", 7 },
	{ write_boundary_one_meg, "write 1MB with contents outside boundary", 7 },
	{ memory_is_freed, "check memory is freed", 24 },
	{ write_one_sector_check_phys1, "write one sector and check disk1 (no CRC check)", 15 },
	{ write_one_page_check_phys1, "write one page and check disk1 (no CRC check)", 15 },
	{ write_two_pages_check_phys1, "write two pages and check disk1 (no CRC check)", 15 },
	{ write_one_meg_check_phys1, "write 1MB and check disk1 (no CRC check)", 15 },
	{ write_one_sector_check_phys, "write one sector and check disks (no CRC check)", 15 },
	{ write_one_page_check_phys, "write one page and check disks (no CRC check)", 15 },
	{ write_two_pages_check_phys, "write two pages and check disks (no CRC check)", 15 },
	{ write_one_meg_check_phys, "write 1MB and check disks (no CRC check)", 15 },
	{ read_one_sector_after_write, "read one sector after physical write (correct CRC)", 16 },
	{ read_one_page_after_write, "read one page after physical write (correct CRC)", 16 },
	{ read_two_pages_after_write, "read two pages after physical write (correct CRC)", 16 },
	{ read_one_meg_after_write, "read 1MB after physical write (correct CRC)", 16 },
	{ write_one_sector_check_phys1_crc, "write one sector and check disk1 (do CRC check)", 16 },
	{ write_one_page_check_phys1_crc, "write one page and check disk1 (do CRC check)", 16 },
	{ write_two_pages_check_phys1_crc, "write two pages and check disk1 (do CRC check)", 16 },
	{ write_one_meg_check_phys1_crc, "write 1MB and check disk1 (do CRC check)", 16 },
	{ write_one_sector_check_phys_crc, "write one sector and check disks (do CRC check)", 16 },
	{ write_one_page_check_phys_crc, "write one page and check disks (do CRC check)", 16 },
	{ write_two_pages_check_phys_crc, "write two pages and check disks (do CRC check)", 16 },
	{ write_one_meg_check_phys_crc, "write 1MB and check disks (do CRC check)", 16 },
	{ corrupt_read_correct_one_sector_disk1, "read corrected one sector error from disk1", 18 },
	{ corrupt_read_correct_one_sector_in_page_disk1, "read corrected one sector in page error from disk1", 18 },
	{ corrupt_read_correct_one_page_disk1, "read corrected one page error from disk1", 18 },
	{ corrupt_read_correct_ten_pages_in_one_meg_disk1, "read corrected ten pages error in one meg from disk1", 18 },
	{ corrupt_read_correct_one_meg_disk1, "read corrected one meg error from disk1", 18 },
	{ recover_one_sector_disk1, "recover one sector error from disk1", 18 },
	{ recover_one_sector_in_page_disk1, "recover one sector error in one page from disk1", 18 },
	{ recover_one_page_disk1, "recover one page filled with errors from disk1", 18 },
	{ recover_ten_page_in_one_meg_disk1, "recover ten pages error in 1MB from disk1", 18 },
	{ recover_one_meg_disk1, "recover 1MB filled with errors from disk1", 18 },
	{ corrupt_read_correct_one_sector_disk2, "read corrected one sector error from disk2", 18 },
	{ corrupt_read_correct_one_sector_in_page_disk2, "read corrected one sector in page error from disk2", 18 },
	{ corrupt_read_correct_one_page_disk2, "read corrected one page error from disk2", 18 },
	{ corrupt_read_correct_ten_pages_in_one_meg_disk2, "read corrected ten pages error in one meg from disk2", 18 },
	{ corrupt_read_correct_one_meg_disk2, "read corrected one meg error from disk2", 18 },
	{ recover_one_sector_disk2, "recover one sector error from disk2", 18 },
	{ recover_one_sector_in_page_disk2, "recover one sector error in one page from disk2", 18 },
	{ recover_one_page_disk2, "recover one page filled with errors from disk2", 18 },
	{ recover_ten_page_in_one_meg_disk2, "recover ten pages error in 1MB from disk2", 18 },
	{ recover_one_meg_disk2, "recover 1MB filled with errors from disk2", 18 },
	{ dual_error, "signal error when both physical disks are corrupted", 12 },
};
size_t max_points = 900;

/* Return number of tests in test_array. */
size_t get_num_tests(void)
{
	return sizeof(test_array) / sizeof(test_array[0]);
}
