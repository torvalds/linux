// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <alloca.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lz4.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <zstd.h>

#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>

#include <linux/random.h>
#include <linux/unistd.h>

#include <kselftest.h>

#include "utils.h"

#define TEST_FAILURE 1
#define TEST_SUCCESS 0

#define INCFS_ROOT_INODE 0

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define le16_to_cpu(x)          (x)
#define le32_to_cpu(x)          (x)
#define le64_to_cpu(x)          (x)
#else
#error Big endian not supported!
#endif

struct {
	int file;
	int test;
	bool verbose;
} options;

#define TESTCOND(condition)						\
	do {								\
		if (!(condition)) {					\
			ksft_print_msg("%s failed %d\n",		\
				       __func__, __LINE__);		\
			goto out;					\
		} else if (options.verbose)				\
			ksft_print_msg("%s succeeded %d\n",		\
				       __func__, __LINE__);		\
	} while (false)

#define TEST(statement, condition)					\
	do {								\
		statement;						\
		TESTCOND(condition);					\
	} while (false)

#define TESTEQUAL(statement, res)					\
	TESTCOND((statement) == (res))

#define TESTNE(statement, res)					\
	TESTCOND((statement) != (res))

struct hash_block {
	char data[INCFS_DATA_FILE_BLOCK_SIZE];
};

struct test_signature {
	void *data;
	size_t size;

	char add_data[100];
	size_t add_data_size;
};

struct test_file {
	int index;
	incfs_uuid_t id;
	char *name;
	off_t size;
	char root_hash[INCFS_MAX_HASH_SIZE];
	struct hash_block *mtree;
	int mtree_block_count;
	struct test_signature sig;
};

struct test_files_set {
	struct test_file *files;
	int files_count;
};

struct linux_dirent64 {
	uint64_t       d_ino;
	int64_t        d_off;
	unsigned short d_reclen;
	unsigned char  d_type;
	char	       d_name[0];
} __packed;

struct test_files_set get_test_files_set(void)
{
	static struct test_file files[] = {
		{ .index = 0, .name = "file_one_byte", .size = 1 },
		{ .index = 1,
		  .name = "file_one_block",
		  .size = INCFS_DATA_FILE_BLOCK_SIZE },
		{ .index = 2,
		  .name = "file_one_and_a_half_blocks",
		  .size = INCFS_DATA_FILE_BLOCK_SIZE +
			  INCFS_DATA_FILE_BLOCK_SIZE / 2 },
		{ .index = 3,
		  .name = "file_three",
		  .size = 300 * INCFS_DATA_FILE_BLOCK_SIZE + 3 },
		{ .index = 4,
		  .name = "file_four",
		  .size = 400 * INCFS_DATA_FILE_BLOCK_SIZE + 7 },
		{ .index = 5,
		  .name = "file_five",
		  .size = 500 * INCFS_DATA_FILE_BLOCK_SIZE + 7 },
		{ .index = 6,
		  .name = "file_six",
		  .size = 600 * INCFS_DATA_FILE_BLOCK_SIZE + 7 },
		{ .index = 7,
		  .name = "file_seven",
		  .size = 700 * INCFS_DATA_FILE_BLOCK_SIZE + 7 },
		{ .index = 8,
		  .name = "file_eight",
		  .size = 800 * INCFS_DATA_FILE_BLOCK_SIZE + 7 },
		{ .index = 9,
		  .name = "file_nine",
		  .size = 900 * INCFS_DATA_FILE_BLOCK_SIZE + 7 },
		{ .index = 10, .name = "file_big", .size = 500 * 1024 * 1024 }
	};

	if (options.file)
		return (struct test_files_set) {
			.files = files + options.file - 1,
			.files_count = 1,
		};

	return (struct test_files_set){ .files = files,
					.files_count = ARRAY_SIZE(files) };
}

struct test_files_set get_small_test_files_set(void)
{
	static struct test_file files[] = {
		{ .index = 0, .name = "file_one_byte", .size = 1 },
		{ .index = 1,
		  .name = "file_one_block",
		  .size = INCFS_DATA_FILE_BLOCK_SIZE },
		{ .index = 2,
		  .name = "file_one_and_a_half_blocks",
		  .size = INCFS_DATA_FILE_BLOCK_SIZE +
			  INCFS_DATA_FILE_BLOCK_SIZE / 2 },
		{ .index = 3,
		  .name = "file_three",
		  .size = 300 * INCFS_DATA_FILE_BLOCK_SIZE + 3 },
		{ .index = 4,
		  .name = "file_four",
		  .size = 400 * INCFS_DATA_FILE_BLOCK_SIZE + 7 }
	};
	return (struct test_files_set){ .files = files,
					.files_count = ARRAY_SIZE(files) };
}

static int get_file_block_seed(int file, int block)
{
	return 7919 * file + block;
}

static loff_t min(loff_t a, loff_t b)
{
	return a < b ? a : b;
}

static pid_t flush_and_fork(void)
{
	fflush(stdout);
	return fork();
}

static void print_error(char *msg)
{
	ksft_print_msg("%s: %s\n", msg, strerror(errno));
}

static int wait_for_process(pid_t pid)
{
	int status;
	int wait_res;

	wait_res = waitpid(pid, &status, 0);
	if (wait_res <= 0) {
		print_error("Can't wait for the child");
		return -EINVAL;
	}
	if (!WIFEXITED(status)) {
		ksft_print_msg("Unexpected child status pid=%d\n", pid);
		return -EINVAL;
	}
	status = WEXITSTATUS(status);
	if (status != 0)
		return status;
	return 0;
}

static void rnd_buf(uint8_t *data, size_t len, unsigned int seed)
{
	int i;

	for (i = 0; i < len; i++) {
		seed = 1103515245 * seed + 12345;
		data[i] = (uint8_t)(seed >> (i % 13));
	}
}

char *bin2hex(char *dst, const void *src, size_t count)
{
	const unsigned char *_src = src;
	static const char hex_asc[] = "0123456789abcdef";

	while (count--) {
		unsigned char x = *_src++;

		*dst++ = hex_asc[(x & 0xf0) >> 4];
		*dst++ = hex_asc[(x & 0x0f)];
	}
	*dst = 0;
	return dst;
}

static char *get_index_filename(const char *mnt_dir, incfs_uuid_t id)
{
	char path[FILENAME_MAX];
	char str_id[1 + 2 * sizeof(id)];

	bin2hex(str_id, id.bytes, sizeof(id.bytes));
	snprintf(path, ARRAY_SIZE(path), "%s/.index/%s", mnt_dir, str_id);

	return strdup(path);
}

static char *get_incomplete_filename(const char *mnt_dir, incfs_uuid_t id)
{
	char path[FILENAME_MAX];
	char str_id[1 + 2 * sizeof(id)];

	bin2hex(str_id, id.bytes, sizeof(id.bytes));
	snprintf(path, ARRAY_SIZE(path), "%s/.incomplete/%s", mnt_dir, str_id);

	return strdup(path);
}

int open_file_by_id(const char *mnt_dir, incfs_uuid_t id, bool use_ioctl)
{
	char *path = get_index_filename(mnt_dir, id);
	int cmd_fd = open_commands_file(mnt_dir);
	int fd = open(path, O_RDWR | O_CLOEXEC);
	struct incfs_permit_fill permit_fill = {
		.file_descriptor = fd,
	};
	int error = 0;

	if (fd < 0) {
		print_error("Can't open file by id.");
		error = -errno;
		goto out;
	}

	if (use_ioctl && ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill)) {
		print_error("Failed to call PERMIT_FILL");
		error = -errno;
		goto out;
	}

	if (ioctl(fd, INCFS_IOC_PERMIT_FILL, &permit_fill) != -1) {
		print_error(
			"Successfully called PERMIT_FILL on non pending_read file");
		return -errno;
		goto out;
	}

out:
	free(path);
	close(cmd_fd);

	if (error) {
		close(fd);
		return error;
	}

	return fd;
}

int get_file_attr(const char *mnt_dir, incfs_uuid_t id, char *value, int size)
{
	char *path = get_index_filename(mnt_dir, id);
	int res;

	res = getxattr(path, INCFS_XATTR_METADATA_NAME, value, size);
	if (res < 0)
		res = -errno;

	free(path);
	return res;
}

static bool same_id(incfs_uuid_t *id1, incfs_uuid_t *id2)
{
	return !memcmp(id1->bytes, id2->bytes, sizeof(id1->bytes));
}

ssize_t ZSTD_compress_default(char *data, char *comp_data, size_t data_size,
					size_t comp_size)
{
	return ZSTD_compress(comp_data, comp_size, data, data_size, 1);
}

static int emit_test_blocks(const char *mnt_dir, struct test_file *file,
			int blocks[], int count)
{
	uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];
	uint8_t comp_data[2 * INCFS_DATA_FILE_BLOCK_SIZE];
	int block_count = (count > 32) ? 32 : count;
	int data_buf_size = 2 * INCFS_DATA_FILE_BLOCK_SIZE * block_count;
	uint8_t *data_buf = malloc(data_buf_size);
	uint8_t *current_data = data_buf;
	uint8_t *data_end = data_buf + data_buf_size;
	struct incfs_fill_block *block_buf =
		calloc(block_count, sizeof(struct incfs_fill_block));
	struct incfs_fill_blocks fill_blocks = {
		.count = block_count,
		.fill_blocks = ptr_to_u64(block_buf),
	};
	ssize_t write_res = 0;
	int fd = -1;
	int error = 0;
	int i = 0;
	int blocks_written = 0;

	for (i = 0; i < block_count; i++) {
		int block_index = blocks[i];
		bool compress_zstd = (file->index + block_index) % 4 == 2;
		bool compress_lz4 = (file->index + block_index) % 4 == 0;
		int seed = get_file_block_seed(file->index, block_index);
		off_t block_offset =
			((off_t)block_index) * INCFS_DATA_FILE_BLOCK_SIZE;
		size_t block_size = 0;

		if (block_offset > file->size) {
			error = -EINVAL;
			break;
		}
		if (file->size - block_offset >
			INCFS_DATA_FILE_BLOCK_SIZE)
			block_size = INCFS_DATA_FILE_BLOCK_SIZE;
		else
			block_size = file->size - block_offset;

		rnd_buf(data, block_size, seed);
		if (compress_lz4) {
			size_t comp_size = LZ4_compress_default((char *)data,
					(char *)comp_data, block_size,
					ARRAY_SIZE(comp_data));

			if (comp_size <= 0) {
				error = -EBADMSG;
				break;
			}
			if (current_data + comp_size > data_end) {
				error = -ENOMEM;
				break;
			}
			memcpy(current_data, comp_data, comp_size);
			block_size = comp_size;
			block_buf[i].compression = COMPRESSION_LZ4;
		} else if (compress_zstd) {
			size_t comp_size = ZSTD_compress(comp_data,
					ARRAY_SIZE(comp_data), data, block_size,
					1);

			if (comp_size <= 0) {
				error = -EBADMSG;
				break;
			}
			if (current_data + comp_size > data_end) {
				error = -ENOMEM;
				break;
			}
			memcpy(current_data, comp_data, comp_size);
			block_size = comp_size;
			block_buf[i].compression = COMPRESSION_ZSTD;
		} else {
			if (current_data + block_size > data_end) {
				error = -ENOMEM;
				break;
			}
			memcpy(current_data, data, block_size);
			block_buf[i].compression = COMPRESSION_NONE;
		}

		block_buf[i].block_index = block_index;
		block_buf[i].data_len = block_size;
		block_buf[i].data = ptr_to_u64(current_data);
		current_data += block_size;
	}

	if (!error) {
		fd = open_file_by_id(mnt_dir, file->id, false);
		if (fd < 0) {
			error = -errno;
			goto out;
		}
		write_res = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);
		if (write_res >= 0) {
			ksft_print_msg("Wrote to file via normal fd error\n");
			error = -EPERM;
			goto out;
		}

		close(fd);
		fd = open_file_by_id(mnt_dir, file->id, true);
		if (fd < 0) {
			error = -errno;
			goto out;
		}
		write_res = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);
		if (write_res < 0)
			error = -errno;
		else
			blocks_written = write_res;
	}
	if (error) {
		ksft_print_msg(
			"Writing data block error. Write returned: %d. Error:%s\n",
			write_res, strerror(-error));
	}

out:
	free(block_buf);
	free(data_buf);
	close(fd);
	return (error < 0) ? error : blocks_written;
}

static int emit_test_block(const char *mnt_dir, struct test_file *file,
				int block_index)
{
	int res = emit_test_blocks(mnt_dir, file, &block_index, 1);

	if (res == 0)
		return -EINVAL;
	if (res == 1)
		return 0;
	return res;
}

static void shuffle(int array[], int count, unsigned int seed)
{
	int i;

	for (i = 0; i < count - 1; i++) {
		int items_left = count - i;
		int shuffle_index;
		int v;

		seed = 1103515245 * seed + 12345;
		shuffle_index = i + seed % items_left;

		v = array[shuffle_index];
		array[shuffle_index] = array[i];
		array[i] = v;
	}
}

static int emit_test_file_data(const char *mount_dir, struct test_file *file)
{
	int i;
	int block_cnt = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	int *block_indexes = NULL;
	int result = 0;
	int blocks_written = 0;

	if (file->size == 0)
		return 0;

	block_indexes = calloc(block_cnt, sizeof(*block_indexes));
	for (i = 0; i < block_cnt; i++)
		block_indexes[i] = i;
	shuffle(block_indexes, block_cnt, file->index);

	for (i = 0; i < block_cnt; i += blocks_written) {
		blocks_written = emit_test_blocks(mount_dir, file,
					block_indexes + i, block_cnt - i);
		if (blocks_written < 0) {
			result = blocks_written;
			goto out;
		}
		if (blocks_written == 0) {
			result = -EIO;
			goto out;
		}
	}
out:
	free(block_indexes);
	return result;
}

static loff_t read_whole_file(const char *filename)
{
	int fd = -1;
	loff_t result;
	loff_t bytes_read = 0;
	uint8_t buff[16 * 1024];

	fd = open(filename, O_RDONLY | O_CLOEXEC);
	if (fd <= 0)
		return fd;

	while (1) {
		int read_result = read(fd, buff, ARRAY_SIZE(buff));

		if (read_result < 0) {
			print_error("Error during reading from a file.");
			result = -errno;
			goto cleanup;
		} else if (read_result == 0)
			break;

		bytes_read += read_result;
	}
	result = bytes_read;

cleanup:
	close(fd);
	return result;
}

static int read_test_file(uint8_t *buf, size_t len, char *filename,
			  int block_idx)
{
	int fd = -1;
	int result;
	int bytes_read = 0;
	size_t bytes_to_read = len;
	off_t offset = ((off_t)block_idx) * INCFS_DATA_FILE_BLOCK_SIZE;

	fd = open(filename, O_RDONLY | O_CLOEXEC);
	if (fd <= 0)
		return fd;

	if (lseek(fd, offset, SEEK_SET) != offset) {
		print_error("Seek error");
		return -errno;
	}

	while (bytes_read < bytes_to_read) {
		int read_result =
			read(fd, buf + bytes_read, bytes_to_read - bytes_read);
		if (read_result < 0) {
			result = -errno;
			goto cleanup;
		} else if (read_result == 0)
			break;

		bytes_read += read_result;
	}
	result = bytes_read;

cleanup:
	close(fd);
	return result;
}

static char *create_backing_dir(const char *mount_dir)
{
	struct stat st;
	char backing_dir_name[255];

	snprintf(backing_dir_name, ARRAY_SIZE(backing_dir_name), "%s-src",
		 mount_dir);

	if (stat(backing_dir_name, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			int error = delete_dir_tree(backing_dir_name);

			if (error) {
				ksft_print_msg(
				      "Can't delete existing backing dir. %d\n",
				      error);
				return NULL;
			}
		} else {
			if (unlink(backing_dir_name)) {
				print_error("Can't clear backing dir");
				return NULL;
			}
		}
	}

	if (mkdir(backing_dir_name, 0777)) {
		if (errno != EEXIST) {
			print_error("Can't open/create backing dir");
			return NULL;
		}
	}

	return strdup(backing_dir_name);
}

static int validate_test_file_content_with_seed(const char *mount_dir,
						struct test_file *file,
						unsigned int shuffle_seed)
{
	int error = -1;
	char *filename = concat_file_name(mount_dir, file->name);
	off_t size = file->size;
	loff_t actual_size = get_file_size(filename);
	int block_cnt = 1 + (size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	int *block_indexes = NULL;
	int i;

	block_indexes = alloca(sizeof(int) * block_cnt);
	for (i = 0; i < block_cnt; i++)
		block_indexes[i] = i;

	if (shuffle_seed != 0)
		shuffle(block_indexes, block_cnt, shuffle_seed);

	if (actual_size != size) {
		ksft_print_msg(
			"File size doesn't match. name: %s expected size:%ld actual size:%ld\n",
			filename, size, actual_size);
		error = -1;
		goto failure;
	}

	for (i = 0; i < block_cnt; i++) {
		int block_idx = block_indexes[i];
		uint8_t expected_block[INCFS_DATA_FILE_BLOCK_SIZE];
		uint8_t actual_block[INCFS_DATA_FILE_BLOCK_SIZE];
		int seed = get_file_block_seed(file->index, block_idx);
		size_t bytes_to_compare = min(
			(off_t)INCFS_DATA_FILE_BLOCK_SIZE,
			size - ((off_t)block_idx) * INCFS_DATA_FILE_BLOCK_SIZE);
		int read_result =
			read_test_file(actual_block, INCFS_DATA_FILE_BLOCK_SIZE,
				       filename, block_idx);
		if (read_result < 0) {
			ksft_print_msg(
				"Error reading block %d from file %s. Error: %s\n",
				block_idx, filename, strerror(-read_result));
			error = read_result;
			goto failure;
		}
		rnd_buf(expected_block, INCFS_DATA_FILE_BLOCK_SIZE, seed);
		if (memcmp(expected_block, actual_block, bytes_to_compare)) {
			ksft_print_msg(
				"File contents don't match. name: %s block:%d\n",
				file->name, block_idx);
			error = -2;
			goto failure;
		}
	}
	free(filename);
	return 0;

failure:
	free(filename);
	return error;
}

static int validate_test_file_content(const char *mount_dir,
				      struct test_file *file)
{
	return validate_test_file_content_with_seed(mount_dir, file, 0);
}

static int data_producer(const char *mount_dir, struct test_files_set *test_set)
{
	int ret = 0;
	int timeout_ms = 1000;
	struct incfs_pending_read_info prs[100] = {};
	int prs_size = ARRAY_SIZE(prs);
	int fd = open_commands_file(mount_dir);

	if (fd < 0)
		return -errno;

	while ((ret = wait_for_pending_reads(fd, timeout_ms, prs, prs_size)) >
	       0) {
		int read_count = ret;
		int i;

		for (i = 0; i < read_count; i++) {
			int j = 0;
			struct test_file *file = NULL;

			for (j = 0; j < test_set->files_count; j++) {
				bool same = same_id(&(test_set->files[j].id),
					&(prs[i].file_id));

				if (same) {
					file = &test_set->files[j];
					break;
				}
			}
			if (!file) {
				ksft_print_msg(
					"Unknown file in pending reads.\n");
				break;
			}

			ret = emit_test_block(mount_dir, file,
				prs[i].block_index);
			if (ret < 0) {
				ksft_print_msg("Emitting test data error: %s\n",
						strerror(-ret));
				break;
			}
		}
	}
	close(fd);
	return ret;
}

static int data_producer2(const char *mount_dir,
			  struct test_files_set *test_set)
{
	int ret = 0;
	int timeout_ms = 1000;
	struct incfs_pending_read_info2 prs[100] = {};
	int prs_size = ARRAY_SIZE(prs);
	int fd = open_commands_file(mount_dir);

	if (fd < 0)
		return -errno;

	while ((ret = wait_for_pending_reads2(fd, timeout_ms, prs, prs_size)) >
	       0) {
		int read_count = ret;
		int i;

		for (i = 0; i < read_count; i++) {
			int j = 0;
			struct test_file *file = NULL;

			for (j = 0; j < test_set->files_count; j++) {
				bool same = same_id(&(test_set->files[j].id),
					&(prs[i].file_id));

				if (same) {
					file = &test_set->files[j];
					break;
				}
			}
			if (!file) {
				ksft_print_msg(
					"Unknown file in pending reads.\n");
				break;
			}

			ret = emit_test_block(mount_dir, file,
				prs[i].block_index);
			if (ret < 0) {
				ksft_print_msg("Emitting test data error: %s\n",
						strerror(-ret));
				break;
			}
		}
	}
	close(fd);
	return ret;
}

static int build_mtree(struct test_file *file)
{
	char data[INCFS_DATA_FILE_BLOCK_SIZE] = {};
	const int digest_size = SHA256_DIGEST_SIZE;
	const int hash_per_block = INCFS_DATA_FILE_BLOCK_SIZE / digest_size;
	int block_count = 0;
	int hash_block_count = 0;
	int total_tree_block_count = 0;
	int tree_lvl_index[INCFS_MAX_MTREE_LEVELS] = {};
	int tree_lvl_count[INCFS_MAX_MTREE_LEVELS] = {};
	int levels_count = 0;
	int i, level;

	if (file->size == 0)
		return 0;

	block_count = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	hash_block_count = block_count;
	for (i = 0; hash_block_count > 1; i++) {
		hash_block_count = (hash_block_count + hash_per_block - 1)
			/ hash_per_block;
		tree_lvl_count[i] = hash_block_count;
		total_tree_block_count += hash_block_count;
	}
	levels_count = i;

	for (i = 0; i < levels_count; i++) {
		int prev_lvl_base = (i == 0) ? total_tree_block_count :
			tree_lvl_index[i - 1];

		tree_lvl_index[i] = prev_lvl_base - tree_lvl_count[i];
	}

	file->mtree_block_count = total_tree_block_count;
	if (block_count == 1) {
		int seed = get_file_block_seed(file->index, 0);

		memset(data, 0, INCFS_DATA_FILE_BLOCK_SIZE);
		rnd_buf((uint8_t *)data, file->size, seed);
		sha256(data, INCFS_DATA_FILE_BLOCK_SIZE, file->root_hash);
		return 0;
	}

	file->mtree = calloc(total_tree_block_count, sizeof(*file->mtree));
	/* Build level 0 hashes. */
	for (i = 0; i < block_count; i++) {
		off_t offset = i * INCFS_DATA_FILE_BLOCK_SIZE;
		size_t block_size = INCFS_DATA_FILE_BLOCK_SIZE;
		int block_index = tree_lvl_index[0] +
					i / hash_per_block;
		int block_off = (i % hash_per_block) * digest_size;
		int seed = get_file_block_seed(file->index, i);
		char *hash_ptr = file->mtree[block_index].data + block_off;

		if (file->size - offset < block_size) {
			block_size = file->size - offset;
			memset(data, 0, INCFS_DATA_FILE_BLOCK_SIZE);
		}

		rnd_buf((uint8_t *)data, block_size, seed);
		sha256(data, INCFS_DATA_FILE_BLOCK_SIZE, hash_ptr);
	}

	/* Build higher levels of hash tree. */
	for (level = 1; level < levels_count; level++) {
		int prev_lvl_base = tree_lvl_index[level - 1];
		int prev_lvl_count = tree_lvl_count[level - 1];

		for (i = 0; i < prev_lvl_count; i++) {
			int block_index =
				i / hash_per_block + tree_lvl_index[level];
			int block_off = (i % hash_per_block) * digest_size;
			char *hash_ptr =
				file->mtree[block_index].data + block_off;

			sha256(file->mtree[i + prev_lvl_base].data,
			       INCFS_DATA_FILE_BLOCK_SIZE, hash_ptr);
		}
	}

	/* Calculate root hash from the top block */
	sha256(file->mtree[0].data,
		INCFS_DATA_FILE_BLOCK_SIZE, file->root_hash);

	return 0;
}

static int load_hash_tree(const char *mount_dir, struct test_file *file)
{
	int err;
	int i;
	int fd;
	struct incfs_fill_blocks fill_blocks = {
		.count = file->mtree_block_count,
	};
	struct incfs_fill_block *fill_block_array =
		calloc(fill_blocks.count, sizeof(struct incfs_fill_block));

	if (fill_blocks.count == 0)
		return 0;

	if (!fill_block_array)
		return -ENOMEM;
	fill_blocks.fill_blocks = ptr_to_u64(fill_block_array);

	for (i = 0; i < fill_blocks.count; i++) {
		fill_block_array[i] = (struct incfs_fill_block){
			.block_index = i,
			.data_len = INCFS_DATA_FILE_BLOCK_SIZE,
			.data = ptr_to_u64(file->mtree[i].data),
			.flags = INCFS_BLOCK_FLAGS_HASH
		};
	}

	fd = open_file_by_id(mount_dir, file->id, false);
	if (fd < 0) {
		err = errno;
		goto failure;
	}

	err = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);
	close(fd);
	if (err >= 0) {
		err = -EPERM;
		goto failure;
	}

	fd = open_file_by_id(mount_dir, file->id, true);
	if (fd < 0) {
		err = errno;
		goto failure;
	}

	err = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);
	close(fd);
	if (err < fill_blocks.count)
		err = errno;
	else {
		err = 0;
		free(file->mtree);
	}

failure:
	free(fill_block_array);
	return err;
}

static int cant_touch_index_test(const char *mount_dir)
{
	char *file_name = "test_file";
	int file_size = 123;
	incfs_uuid_t file_id;
	char *index_path = concat_file_name(mount_dir, ".index");
	char *subdir = concat_file_name(index_path, "subdir");
	char *dst_name = concat_file_name(mount_dir, "something");
	char *filename_in_index = NULL;
	char *file_path = concat_file_name(mount_dir, file_name);
	char *backing_dir;
	int cmd_fd = -1;
	int err;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;
	free(backing_dir);

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;


	err = mkdir(subdir, 0777);
	if (err == 0 || errno != EBUSY) {
		print_error("Shouldn't be able to crate subdir in index\n");
		goto failure;
	}

	err = rmdir(index_path);
	if (err == 0 || errno != EBUSY) {
		print_error(".index directory should not be removed\n");
		goto failure;
	}

	err = emit_file(cmd_fd, ".index", file_name, &file_id,
				file_size, NULL);
	if (err != -EBUSY) {
		print_error("Shouldn't be able to crate a file in index\n");
		goto failure;
	}

	err = emit_file(cmd_fd, NULL, file_name, &file_id,
				file_size, NULL);
	if (err < 0)
		goto failure;
	filename_in_index = get_index_filename(mount_dir, file_id);

	err = unlink(filename_in_index);
	if (err == 0 || errno != EBUSY) {
		print_error("Shouldn't be delete from index\n");
		goto failure;
	}


	err = rename(filename_in_index, dst_name);
	if (err == 0 || errno != EBUSY) {
		print_error("Shouldn't be able to move from index\n");
		goto failure;
	}

	free(filename_in_index);
	filename_in_index = concat_file_name(index_path, "abc");
	err = link(file_path, filename_in_index);
	if (err == 0 || errno != EBUSY) {
		print_error("Shouldn't be able to link inside index\n");
		goto failure;
	}

	err = rename(index_path, dst_name);
	if (err == 0 || errno != EBUSY) {
		print_error("Shouldn't rename .index directory\n");
		goto failure;
	}

	close(cmd_fd);
	free(subdir);
	free(index_path);
	free(dst_name);
	free(filename_in_index);
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	free(subdir);
	free(dst_name);
	free(index_path);
	free(filename_in_index);
	close(cmd_fd);
	umount(mount_dir);
	return TEST_FAILURE;
}

static bool iterate_directory(const char *dir_to_iterate, bool root,
			      int file_count)
{
	struct expected_name {
		const char *name;
		bool root_only;
		bool found;
	} names[] = {
		{INCFS_LOG_FILENAME, true, false},
		{INCFS_PENDING_READS_FILENAME, true, false},
		{INCFS_BLOCKS_WRITTEN_FILENAME, true, false},
		{".index", true, false},
		{".incomplete", true, false},
		{"..", false, false},
		{".", false, false},
	};

	bool pass = true, found;
	int i;

	/* Test directory iteration */
	int fd = open(dir_to_iterate, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (fd < 0) {
		print_error("Can't open directory\n");
		return false;
	}

	for (;;) {
		/* Enough space for one dirent - no name over 30 */
		char buf[sizeof(struct linux_dirent64) + NAME_MAX];
		struct linux_dirent64 *dirent = (struct linux_dirent64 *) buf;
		int nread;
		int i;

		for (i = 0; i < NAME_MAX; ++i) {
			nread = syscall(__NR_getdents64, fd, buf,
					 sizeof(struct linux_dirent64) + i);

			if (nread >= 0)
				break;
			if (errno != EINVAL)
				break;
		}

		if (nread == 0)
			break;
		if (nread < 0) {
			print_error("Error iterating directory\n");
			pass = false;
			goto failure;
		}

		/* Expected size is rounded up to 8 byte boundary. Not sure if
		 * this is universal truth or just happenstance, but useful test
		 * for the moment
		 */
		if (nread != (((sizeof(struct linux_dirent64)
				+ strlen(dirent->d_name) + 1) + 7) & ~7)) {
			print_error("Wrong dirent size");
			pass = false;
			goto failure;
		}

		found = false;
		for (i = 0; i < sizeof(names) / sizeof(*names); ++i)
			if (!strcmp(dirent->d_name, names[i].name)) {
				if (names[i].root_only && !root) {
					print_error("Root file error");
					pass = false;
					goto failure;
				}

				if (names[i].found) {
					print_error("File appears twice");
					pass = false;
					goto failure;
				}

				names[i].found = true;
				found = true;
				break;
			}

		if (!found)
			--file_count;
	}

	for (i = 0; i < sizeof(names) / sizeof(*names); ++i) {
		if (!names[i].found)
			if (root || !names[i].root_only) {
				print_error("Expected file not present");
				pass = false;
				goto failure;
			}
	}

	if (file_count) {
		print_error("Wrong number of files\n");
		pass = false;
		goto failure;
	}

failure:
	close(fd);
	return pass;
}

static int basic_file_ops_test(const char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	char *subdir1 = concat_file_name(mount_dir, "subdir1");
	char *subdir2 = concat_file_name(mount_dir, "subdir2");
	char *backing_dir;
	int cmd_fd = -1;
	int i, err;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;
	free(backing_dir);

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	err = mkdir(subdir1, 0777);
	if (err < 0 && errno != EEXIST) {
		print_error("Can't create subdir1\n");
		goto failure;
	}

	err = mkdir(subdir2, 0777);
	if (err < 0 && errno != EEXIST) {
		print_error("Can't create subdir2\n");
		goto failure;
	}

	/* Create all test files in subdir1 directory */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		loff_t size;
		char *file_path = concat_file_name(subdir1, file->name);

		err = emit_file(cmd_fd, "subdir1", file->name, &file->id,
				     file->size, NULL);
		if (err < 0)
			goto failure;

		size = get_file_size(file_path);
		free(file_path);
		if (size != file->size) {
			ksft_print_msg("Wrong size %lld of %s.\n",
				size, file->name);
			goto failure;
		}
	}

	if (!iterate_directory(subdir1, false, file_num))
		goto failure;

	/* Link the files to subdir2 */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *src_name = concat_file_name(subdir1, file->name);
		char *dst_name = concat_file_name(subdir2, file->name);
		loff_t size;

		err = link(src_name, dst_name);
		if (err < 0) {
			print_error("Can't move file\n");
			goto failure;
		}

		size = get_file_size(dst_name);
		if (size != file->size) {
			ksft_print_msg("Wrong size %lld of %s.\n",
				size, file->name);
			goto failure;
		}
		free(src_name);
		free(dst_name);
	}

	/* Move the files from subdir2 to the mount dir */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *src_name = concat_file_name(subdir2, file->name);
		char *dst_name = concat_file_name(mount_dir, file->name);
		loff_t size;

		err = rename(src_name, dst_name);
		if (err < 0) {
			print_error("Can't move file\n");
			goto failure;
		}

		size = get_file_size(dst_name);
		if (size != file->size) {
			ksft_print_msg("Wrong size %lld of %s.\n",
				size, file->name);
			goto failure;
		}
		free(src_name);
		free(dst_name);
	}

	/* +2 because there are 2 subdirs */
	if (!iterate_directory(mount_dir, true, file_num + 2))
		goto failure;

	/* Open and close all files from the mount dir */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *path = concat_file_name(mount_dir, file->name);
		int fd;

		fd = open(path, O_RDWR | O_CLOEXEC);
		free(path);
		if (fd <= 0) {
			print_error("Can't open file");
			goto failure;
		}
		if (close(fd)) {
			print_error("Can't close file");
			goto failure;
		}
	}

	/* Delete all files from the mount dir */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *path = concat_file_name(mount_dir, file->name);

		err = unlink(path);
		free(path);
		if (err < 0) {
			print_error("Can't unlink file");
			goto failure;
		}
	}

	err = delete_dir_tree(subdir1);
	if (err) {
		ksft_print_msg("Error deleting subdir1 %d", err);
		goto failure;
	}

	err = rmdir(subdir2);
	if (err) {
		print_error("Error deleting subdir2");
		goto failure;
	}

	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int dynamic_files_and_data_test(const char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	const int missing_file_idx = 5;
	int cmd_fd = -1;
	char *backing_dir;
	int i;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;
	free(backing_dir);

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Check that test files don't exist in the filesystem. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *filename = concat_file_name(mount_dir, file->name);

		if (access(filename, F_OK) != -1) {
			ksft_print_msg(
				"File %s somehow already exists in a clean FS.\n",
				filename);
			goto failure;
		}
		free(filename);
	}

	/* Write test data into the command file. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		int res;

		build_mtree(file);
		res = emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL);
		if (res < 0) {
			ksft_print_msg("Error %s emiting file %s.\n",
				       strerror(-res), file->name);
			goto failure;
		}

		/* Skip writing data to one file so we can check */
		/* that it's missing later. */
		if (i == missing_file_idx)
			continue;

		res = emit_test_file_data(mount_dir, file);
		if (res) {
			ksft_print_msg("Error %s emiting data for %s.\n",
				       strerror(-res), file->name);
			goto failure;
		}
	}

	/* Validate contents of the FS */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (i == missing_file_idx) {
			/* No data has been written to this file. */
			/* Check for read error; */
			uint8_t buf;
			char *filename =
				concat_file_name(mount_dir, file->name);
			int res = read_test_file(&buf, 1, filename, 0);

			free(filename);
			if (res > 0) {
				ksft_print_msg(
					"Data present, even though never writtern.\n");
				goto failure;
			}
			if (res != -ETIME) {
				ksft_print_msg("Wrong error code: %d.\n", res);
				goto failure;
			}
		} else {
			if (validate_test_file_content(mount_dir, file) < 0)
				goto failure;
		}
	}

	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int concurrent_reads_and_writes_test(const char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	/* Validate each file from that many child processes. */
	const int child_multiplier = 3;
	int cmd_fd = -1;
	char *backing_dir;
	int status;
	int i;
	pid_t producer_pid;
	pid_t *child_pids = alloca(child_multiplier * file_num * sizeof(pid_t));

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;
	free(backing_dir);

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Tell FS about the files, without actually providing the data. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		int res;

		res = emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL);
		if (res)
			goto failure;
	}

	/* Start child processes acessing data in the files */
	for (i = 0; i < file_num * child_multiplier; i++) {
		struct test_file *file = &test.files[i / child_multiplier];
		pid_t child_pid = flush_and_fork();

		if (child_pid == 0) {
			/* This is a child process, do the data validation. */
			int ret = validate_test_file_content_with_seed(
				mount_dir, file, i);
			if (ret >= 0) {
				/* Zero exit status if data is valid. */
				exit(0);
			}

			/* Positive status if validation error found. */
			exit(-ret);
		} else if (child_pid > 0) {
			child_pids[i] = child_pid;
		} else {
			print_error("Fork error");
			goto failure;
		}
	}

	producer_pid = flush_and_fork();
	if (producer_pid == 0) {
		int ret;
		/*
		 * This is a child that should provide data to
		 * pending reads.
		 */

		ret = data_producer(mount_dir, &test);
		exit(-ret);
	} else {
		status = wait_for_process(producer_pid);
		if (status != 0) {
			ksft_print_msg("Data produces failed. %d(%s) ", status,
				       strerror(status));
			goto failure;
		}
	}

	/* Check that all children has finished with 0 exit status */
	for (i = 0; i < file_num * child_multiplier; i++) {
		struct test_file *file = &test.files[i / child_multiplier];

		status = wait_for_process(child_pids[i]);
		if (status != 0) {
			ksft_print_msg(
				"Validation for the file %s failed with code %d (%s)\n",
				file->name, status, strerror(status));
			goto failure;
		}
	}

	/* Check that there are no pending reads left */
	{
		struct incfs_pending_read_info prs[1] = {};
		int timeout = 0;
		int read_count = wait_for_pending_reads(cmd_fd, timeout, prs,
							ARRAY_SIZE(prs));

		if (read_count) {
			ksft_print_msg(
				"Pending reads pending when all data written\n");
			goto failure;
		}
	}

	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int work_after_remount_test(const char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	const int file_num_stage1 = file_num / 2;
	const int file_num_stage2 = file_num;
	char *backing_dir = NULL;
	int i = 0;
	int cmd_fd = -1;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Write first half of the data into the command file. (stage 1) */
	for (i = 0; i < file_num_stage1; i++) {
		struct test_file *file = &test.files[i];

		build_mtree(file);
		if (emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL))
			goto failure;

		if (emit_test_file_data(mount_dir, file))
			goto failure;
	}

	/* Unmount and mount again, to see that data is persistent. */
	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Write the second half of the data into the command file. (stage 2) */
	for (; i < file_num_stage2; i++) {
		struct test_file *file = &test.files[i];
		int res = emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL);

		if (res)
			goto failure;

		if (emit_test_file_data(mount_dir, file))
			goto failure;
	}

	/* Validate contents of the FS */
	for (i = 0; i < file_num_stage2; i++) {
		struct test_file *file = &test.files[i];

		if (validate_test_file_content(mount_dir, file) < 0)
			goto failure;
	}

	/* Delete all files */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *filename = concat_file_name(mount_dir, file->name);
		char *filename_in_index = get_index_filename(mount_dir,
							file->id);

		if (access(filename, F_OK) != 0) {
			ksft_print_msg("File %s is not visible.\n", filename);
			goto failure;
		}

		if (access(filename_in_index, F_OK) != 0) {
			ksft_print_msg("File %s is not visible.\n",
				filename_in_index);
			goto failure;
		}

		unlink(filename);

		if (access(filename, F_OK) != -1) {
			ksft_print_msg("File %s is still present.\n", filename);
			goto failure;
		}

		if (access(filename_in_index, F_OK) != -1) {
			ksft_print_msg("File %s is still present.\n",
				filename_in_index);
			goto failure;
		}
		free(filename);
		free(filename_in_index);
	}

	/* Unmount and mount again, to see that deleted files stay deleted. */
	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Validate all deleted files are still deleted. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *filename = concat_file_name(mount_dir, file->name);

		if (access(filename, F_OK) != -1) {
			ksft_print_msg("File %s is still visible.\n", filename);
			goto failure;
		}
		free(filename);
	}

	/* Final unmount */
	close(cmd_fd);
	free(backing_dir);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	free(backing_dir);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int attribute_test(const char *mount_dir)
{
	char file_attr[] = "metadata123123";
	char attr_buf[INCFS_MAX_FILE_ATTR_SIZE] = {};
	int cmd_fd = -1;
	incfs_uuid_t file_id;
	int attr_res = 0;
	char *backing_dir;


	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;


	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	if (emit_file(cmd_fd, NULL, "file", &file_id, 12, file_attr))
		goto failure;

	/* Test attribute values */
	attr_res = get_file_attr(mount_dir, file_id, attr_buf,
		ARRAY_SIZE(attr_buf));
	if (attr_res != strlen(file_attr)) {
		ksft_print_msg("Get file attr error: %d\n", attr_res);
		goto failure;
	}
	if (strcmp(attr_buf, file_attr) != 0) {
		ksft_print_msg("Incorrect file attr value: '%s'", attr_buf);
		goto failure;
	}

	/* Unmount and mount again, to see that attributes are persistent. */
	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Test attribute values again after remount*/
	attr_res = get_file_attr(mount_dir, file_id, attr_buf,
		ARRAY_SIZE(attr_buf));
	if (attr_res != strlen(file_attr)) {
		ksft_print_msg("Get dir attr error: %d\n", attr_res);
		goto failure;
	}
	if (strcmp(attr_buf, file_attr) != 0) {
		ksft_print_msg("Incorrect file attr value: '%s'", attr_buf);
		goto failure;
	}

	/* Final unmount */
	close(cmd_fd);
	free(backing_dir);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	free(backing_dir);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int child_procs_waiting_for_data_test(const char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	int cmd_fd = -1;
	int i;
	pid_t *child_pids = alloca(file_num * sizeof(pid_t));
	char *backing_dir;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file.  (10s wait time) */
	if (mount_fs(mount_dir, backing_dir, 10000) != 0)
		goto failure;


	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Tell FS about the files, without actually providing the data. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL);
	}

	/* Start child processes acessing data in the files */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		pid_t child_pid = flush_and_fork();

		if (child_pid == 0) {
			/* This is a child process, do the data validation. */
			int ret = validate_test_file_content(mount_dir, file);

			if (ret >= 0) {
				/* Zero exit status if data is valid. */
				exit(0);
			}

			/* Positive status if validation error found. */
			exit(-ret);
		} else if (child_pid > 0) {
			child_pids[i] = child_pid;
		} else {
			print_error("Fork error");
			goto failure;
		}
	}

	/* Write test data into the command file. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (emit_test_file_data(mount_dir, file))
			goto failure;
	}

	/* Check that all children has finished with 0 exit status */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		int status = wait_for_process(child_pids[i]);

		if (status != 0) {
			ksft_print_msg(
				"Validation for the file %s failed with code %d (%s)\n",
				file->name, status, strerror(status));
			goto failure;
		}
	}

	close(cmd_fd);
	free(backing_dir);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	free(backing_dir);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int multiple_providers_test(const char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	const int producer_count = 5;
	int cmd_fd = -1;
	int status;
	int i;
	pid_t *producer_pids = alloca(producer_count * sizeof(pid_t));
	char *backing_dir;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file.  (10s wait time) */
	if (mount_fs_opt(mount_dir, backing_dir,
			 "read_timeout_ms=10000,report_uid", false) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Tell FS about the files, without actually providing the data. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL) < 0)
			goto failure;
	}

	/* Start producer processes */
	for (i = 0; i < producer_count; i++) {
		pid_t producer_pid = flush_and_fork();

		if (producer_pid == 0) {
			int ret;
			/*
			 * This is a child that should provide data to
			 * pending reads.
			 */

			ret = data_producer2(mount_dir, &test);
			exit(-ret);
		} else if (producer_pid > 0) {
			producer_pids[i] = producer_pid;
		} else {
			print_error("Fork error");
			goto failure;
		}
	}

	/* Validate FS content */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		char *filename = concat_file_name(mount_dir, file->name);
		loff_t read_result = read_whole_file(filename);

		free(filename);
		if (read_result != file->size) {
			ksft_print_msg(
				"Error validating file %s. Result: %ld\n",
				file->name, read_result);
			goto failure;
		}
	}

	/* Check that all producers has finished with 0 exit status */
	for (i = 0; i < producer_count; i++) {
		status = wait_for_process(producer_pids[i]);
		if (status != 0) {
			ksft_print_msg("Producer %d failed with code (%s)\n", i,
				       strerror(status));
			goto failure;
		}
	}

	close(cmd_fd);
	free(backing_dir);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	free(backing_dir);
	umount(mount_dir);
	return TEST_FAILURE;
}

static int hash_tree_test(const char *mount_dir)
{
	char *backing_dir;
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	const int corrupted_file_idx = 5;
	int i = 0;
	int cmd_fd = -1;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file. */
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Write hashes and data. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];
		int res;

		build_mtree(file);
		res = crypto_emit_file(cmd_fd, NULL, file->name, &file->id,
				       file->size, file->root_hash,
				       file->sig.add_data);

		if (i == corrupted_file_idx) {
			/* Corrupt third blocks hash */
			file->mtree[0].data[2 * SHA256_DIGEST_SIZE] ^= 0xff;
		}
		if (emit_test_file_data(mount_dir, file))
			goto failure;

		res = load_hash_tree(mount_dir, file);
		if (res) {
			ksft_print_msg("Can't load hashes for %s. error: %s\n",
				file->name, strerror(-res));
			goto failure;
		}
	}

	/* Validate data */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (i == corrupted_file_idx) {
			uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];
			char *filename =
				concat_file_name(mount_dir, file->name);
			int res;

			res = read_test_file(data, INCFS_DATA_FILE_BLOCK_SIZE,
					     filename, 2);
			free(filename);
			if (res != -EBADMSG) {
				ksft_print_msg("Hash violation missed1. %d\n",
					       res);
				goto failure;
			}
		} else if (validate_test_file_content(mount_dir, file) < 0)
			goto failure;
	}

	/* Unmount and mount again, to that hashes are persistent. */
	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}
	if (mount_fs(mount_dir, backing_dir, 50) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Validate data again */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (i == corrupted_file_idx) {
			uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];
			char *filename =
				concat_file_name(mount_dir, file->name);
			int res;

			res = read_test_file(data, INCFS_DATA_FILE_BLOCK_SIZE,
					     filename, 2);
			free(filename);
			if (res != -EBADMSG) {
				ksft_print_msg("Hash violation missed2. %d\n",
					       res);
				goto failure;
			}
		} else if (validate_test_file_content(mount_dir, file) < 0)
			goto failure;
	}

	/* Final unmount */
	close(cmd_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}
	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	free(backing_dir);
	umount(mount_dir);
	return TEST_FAILURE;
}

enum expected_log { FULL_LOG, NO_LOG, PARTIAL_LOG };

static int validate_logs(const char *mount_dir, int log_fd,
			 struct test_file *file,
			 enum expected_log expected_log,
			 bool report_uid, bool expect_data)
{
	int result = TEST_FAILURE;
	uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];
	struct incfs_pending_read_info prs[2048] = {};
	struct incfs_pending_read_info2 prs2[2048] = {};
	struct incfs_pending_read_info *previous_record = NULL;
	int prs_size = ARRAY_SIZE(prs);
	int block_count = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	int expected_read_count, read_count, block_index, read_index;
	char *filename = NULL;
	int fd = -1;

	TEST(filename = concat_file_name(mount_dir, file->name), filename);
	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);

	if (block_count > prs_size)
		block_count = prs_size;
	expected_read_count = block_count;

	for (block_index = 0; block_index < block_count; block_index++) {
		int result = pread(fd, data, sizeof(data),
			    INCFS_DATA_FILE_BLOCK_SIZE * block_index);

		/* Make some read logs of type SAME_FILE_NEXT_BLOCK */
		if (block_index % 100 == 10)
			usleep(20000);

		/* Skip some blocks to make logs of type SAME_FILE */
		if (block_index % 10 == 5) {
			++block_index;
			--expected_read_count;
		}

		if (expect_data)
			TESTCOND(result > 0);

		if (!expect_data)
			TESTEQUAL(result, -1);
	}

	if (report_uid)
		read_count = wait_for_pending_reads2(log_fd,
				expected_log == NO_LOG ? 10 : 0,
				prs2, prs_size);
	else
		read_count = wait_for_pending_reads(log_fd,
				expected_log == NO_LOG ? 10 : 0,
				prs, prs_size);

	if (expected_log == NO_LOG)
		TESTEQUAL(read_count, 0);

	if (expected_log == PARTIAL_LOG)
		TESTCOND(read_count > 0 &&
			 read_count <= expected_read_count);

	if (expected_log == FULL_LOG)
		TESTEQUAL(read_count, expected_read_count);

	/* If read less than expected, advance block_index appropriately */
	for (block_index = 0, read_index = 0;
	     read_index < expected_read_count - read_count;
	     block_index++, read_index++)
		if (block_index % 10 == 5)
			++block_index;

	for (read_index = 0; read_index < read_count;
	     block_index++, read_index++) {
		struct incfs_pending_read_info *record = report_uid ?
			(struct incfs_pending_read_info *) &prs2[read_index] :
			&prs[read_index];

		TESTCOND(same_id(&record->file_id, &file->id));
		TESTEQUAL(record->block_index, block_index);
		TESTNE(record->timestamp_us, 0);
		if (previous_record)
			TESTEQUAL(record->serial_number,
				  previous_record->serial_number + 1);

		previous_record = record;
		if (block_index % 10 == 5)
			++block_index;
	}

	result = TEST_SUCCESS;
out:
	close(fd);
	free(filename);
	return result;
}

static int read_log_test(const char *mount_dir)
{
	int result = TEST_FAILURE;
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	int i = 0;
	int cmd_fd = -1, log_fd = -1;
	char *backing_dir = NULL;

	/* Create files */
	TEST(backing_dir = create_backing_dir(mount_dir), backing_dir);
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir,
			       "readahead=0,report_uid,read_timeout_ms=0",
				 false), 0);
	TEST(cmd_fd = open_commands_file(mount_dir), cmd_fd != -1);
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		TESTEQUAL(emit_file(cmd_fd, NULL, file->name, &file->id,
				    file->size, NULL), 0);
	}
	close(cmd_fd);
	cmd_fd = -1;

	/* Validate logs */
	TEST(log_fd = open_log_file(mount_dir), log_fd != -1);
	for (i = 0; i < file_num; i++)
		TESTEQUAL(validate_logs(mount_dir, log_fd, &test.files[i],
					FULL_LOG, true, false), 0);

	/* Unmount and mount again without report_uid */
	close(log_fd);
	log_fd = -1;
	TESTEQUAL(umount(mount_dir), 0);
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir,
			       "readahead=0,read_timeout_ms=0", false), 0);

	TEST(log_fd = open_log_file(mount_dir), log_fd != -1);
	for (i = 0; i < file_num; i++)
		TESTEQUAL(validate_logs(mount_dir, log_fd, &test.files[i],
					FULL_LOG, false, false), 0);

	/* No read log to make sure poll doesn't crash */
	close(log_fd);
	log_fd = -1;
	TESTEQUAL(umount(mount_dir), 0);
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir,
			       "readahead=0,rlog_pages=0,read_timeout_ms=0",
			       false), 0);

	TEST(log_fd = open_log_file(mount_dir), log_fd != -1);
	for (i = 0; i < file_num; i++)
		TESTEQUAL(validate_logs(mount_dir, log_fd, &test.files[i],
					NO_LOG, false, false), 0);

	/* Remount and check that logs start working again */
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir,
			       "readahead=0,rlog_pages=1,read_timeout_ms=0",
			       true), 0);
	for (i = 0; i < file_num; i++)
		TESTEQUAL(validate_logs(mount_dir, log_fd, &test.files[i],
					PARTIAL_LOG, false, false), 0);

	/* Remount and check that logs continue working */
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir,
			       "readahead=0,rlog_pages=4,read_timeout_ms=0",
			       true), 0);
	for (i = 0; i < file_num; i++)
		TESTEQUAL(validate_logs(mount_dir, log_fd, &test.files[i],
					FULL_LOG, false, false), 0);

	/* Check logs work with data */
	for (i = 0; i < file_num; i++) {
		TESTEQUAL(emit_test_file_data(mount_dir, &test.files[i]), 0);
		TESTEQUAL(validate_logs(mount_dir, log_fd, &test.files[i],
					FULL_LOG, false, true), 0);
	}

	/* Final unmount */
	close(log_fd);
	log_fd = -1;
	TESTEQUAL(umount(mount_dir), 0);

	result = TEST_SUCCESS;
out:
	close(cmd_fd);
	close(log_fd);
	free(backing_dir);
	umount(mount_dir);
	return result;
}

static int emit_partial_test_file_data(const char *mount_dir,
				       struct test_file *file)
{
	int i, j;
	int block_cnt = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	int *block_indexes = NULL;
	int result = 0;
	int blocks_written = 0;
	int bw_fd = -1;
	char buffer[20];
	struct pollfd pollfd;
	long blocks_written_total, blocks_written_new_total;

	if (file->size == 0)
		return 0;

	bw_fd = open_blocks_written_file(mount_dir);
	if (bw_fd == -1)
		return -errno;

	result = read(bw_fd, buffer, sizeof(buffer));
	if (result <= 0) {
		result = -EIO;
		goto out;
	}

	buffer[result] = 0;
	blocks_written_total = strtol(buffer, NULL, 10);
	result = 0;

	pollfd = (struct pollfd) {
		.fd = bw_fd,
		.events = POLLIN,
	};

	result = poll(&pollfd, 1, 0);
	if (result) {
		result = -EIO;
		goto out;
	}

	/* Emit 2 blocks, skip 2 blocks etc*/
	block_indexes = calloc(block_cnt, sizeof(*block_indexes));
	for (i = 0, j = 0; i < block_cnt; ++i)
		if ((i & 2) == 0) {
			block_indexes[j] = i;
			++j;
		}

	for (i = 0; i < j; i += blocks_written) {
		blocks_written = emit_test_blocks(mount_dir, file,
						  block_indexes + i, j - i);
		if (blocks_written < 0) {
			result = blocks_written;
			goto out;
		}
		if (blocks_written == 0) {
			result = -EIO;
			goto out;
		}

		result = poll(&pollfd, 1, 0);
		if (result != 1 || pollfd.revents != POLLIN) {
			result = -EIO;
			goto out;
		}

		result = read(bw_fd, buffer, sizeof(buffer));
		buffer[result] = 0;
		blocks_written_new_total = strtol(buffer, NULL, 10);

		if (blocks_written_new_total - blocks_written_total
		    != blocks_written) {
			result = -EIO;
			goto out;
		}

		blocks_written_total = blocks_written_new_total;
		result = 0;
	}
out:
	free(block_indexes);
	close(bw_fd);
	return result;
}

static int validate_ranges(const char *mount_dir, struct test_file *file)
{
	int block_cnt = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	char *filename = concat_file_name(mount_dir, file->name);
	int fd;
	struct incfs_filled_range ranges[128];
	struct incfs_get_filled_blocks_args fba = {
		.range_buffer = ptr_to_u64(ranges),
		.range_buffer_size = sizeof(ranges),
	};
	int error = TEST_SUCCESS;
	int i;
	int range_cnt;
	int cmd_fd = -1;
	struct incfs_permit_fill permit_fill;

	fd = open(filename, O_RDONLY | O_CLOEXEC);
	free(filename);
	if (fd <= 0)
		return TEST_FAILURE;

	error = ioctl(fd, INCFS_IOC_GET_FILLED_BLOCKS, &fba);
	if (error != -1 || errno != EPERM) {
		ksft_print_msg("INCFS_IOC_GET_FILLED_BLOCKS not blocked\n");
		error = -EPERM;
		goto out;
	}

	cmd_fd = open_commands_file(mount_dir);
	permit_fill.file_descriptor = fd;
	if (ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill)) {
		print_error("INCFS_IOC_PERMIT_FILL failed");
		return -EPERM;
		goto out;
	}

	error = ioctl(fd, INCFS_IOC_GET_FILLED_BLOCKS, &fba);
	if (error && errno != ERANGE)
		goto out;

	if (error && errno == ERANGE && block_cnt < 509)
		goto out;

	if (!error && block_cnt >= 509) {
		error = -ERANGE;
		goto out;
	}

	if (fba.total_blocks_out != block_cnt) {
		error = -EINVAL;
		goto out;
	}

	if (fba.data_blocks_out != block_cnt) {
		error = -EINVAL;
		goto out;
	}

	range_cnt = (block_cnt + 3) / 4;
	if (range_cnt > 128)
		range_cnt = 128;
	if (range_cnt != fba.range_buffer_size_out / sizeof(*ranges)) {
		error = -ERANGE;
		goto out;
	}

	error = TEST_SUCCESS;
	for (i = 0; i < fba.range_buffer_size_out / sizeof(*ranges) - 1; ++i)
		if (ranges[i].begin != i * 4 || ranges[i].end != i * 4 + 2) {
			error = -EINVAL;
			goto out;
		}

	if (ranges[i].begin != i * 4 ||
	    (ranges[i].end != i * 4 + 1 && ranges[i].end != i * 4 + 2)) {
		error = -EINVAL;
		goto out;
	}

	for (i = 0; i < 64; ++i) {
		fba.start_index = i * 2;
		fba.end_index = i * 2 + 2;
		error = ioctl(fd, INCFS_IOC_GET_FILLED_BLOCKS, &fba);
		if (error)
			goto out;

		if (fba.total_blocks_out != block_cnt) {
			error = -EINVAL;
			goto out;
		}

		if (fba.start_index >= block_cnt) {
			if (fba.index_out != fba.start_index) {
				error = -EINVAL;
				goto out;
			}

			break;
		}

		if (i % 2) {
			if (fba.range_buffer_size_out != 0) {
				error = -EINVAL;
				goto out;
			}
		} else {
			if (fba.range_buffer_size_out != sizeof(*ranges)) {
				error = -EINVAL;
				goto out;
			}

			if (ranges[0].begin != i * 2) {
				error = -EINVAL;
				goto out;
			}

			if (ranges[0].end != i * 2 + 1 &&
			    ranges[0].end != i * 2 + 2) {
				error = -EINVAL;
				goto out;
			}
		}
	}

out:
	close(fd);
	close(cmd_fd);
	return error;
}

static int get_blocks_test(const char *mount_dir)
{
	char *backing_dir;
	int cmd_fd = -1;
	int i;
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	if (mount_fs_opt(mount_dir, backing_dir, "readahead=0", false) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	/* Write data. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (emit_file(cmd_fd, NULL, file->name, &file->id, file->size,
			      NULL))
			goto failure;

		if (emit_partial_test_file_data(mount_dir, file))
			goto failure;
	}

	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (validate_ranges(mount_dir, file))
			goto failure;

		/*
		 * The smallest files are filled completely, so this checks that
		 * the fast get_filled_blocks path is not causing issues
		 */
		if (validate_ranges(mount_dir, file))
			goto failure;
	}

	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return TEST_FAILURE;
}

static int emit_partial_test_file_hash(const char *mount_dir,
				       struct test_file *file)
{
	int err;
	int fd;
	struct incfs_fill_blocks fill_blocks = {
		.count = 1,
	};
	struct incfs_fill_block *fill_block_array =
		calloc(fill_blocks.count, sizeof(struct incfs_fill_block));
	uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];

	if (file->size <= 4096 / 32 * 4096)
		return 0;

	if (!fill_block_array)
		return -ENOMEM;
	fill_blocks.fill_blocks = ptr_to_u64(fill_block_array);

	rnd_buf(data, sizeof(data), 0);

	fill_block_array[0] =
		(struct incfs_fill_block){ .block_index = 1,
					   .data_len =
						   INCFS_DATA_FILE_BLOCK_SIZE,
					   .data = ptr_to_u64(data),
					   .flags = INCFS_BLOCK_FLAGS_HASH };

	fd = open_file_by_id(mount_dir, file->id, true);
	if (fd < 0) {
		err = errno;
		goto failure;
	}

	err = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);
	close(fd);
	if (err < fill_blocks.count)
		err = errno;
	else
		err = 0;

failure:
	free(fill_block_array);
	return err;
}

static int validate_hash_ranges(const char *mount_dir, struct test_file *file)
{
	int block_cnt = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	char *filename = concat_file_name(mount_dir, file->name);
	int fd;
	struct incfs_filled_range ranges[128];
	struct incfs_get_filled_blocks_args fba = {
		.range_buffer = ptr_to_u64(ranges),
		.range_buffer_size = sizeof(ranges),
	};
	int error = TEST_SUCCESS;
	int file_blocks = (file->size + INCFS_DATA_FILE_BLOCK_SIZE - 1) /
			  INCFS_DATA_FILE_BLOCK_SIZE;
	int cmd_fd = -1;
	struct incfs_permit_fill permit_fill;

	if (file->size <= 4096 / 32 * 4096)
		return 0;

	fd = open(filename, O_RDONLY | O_CLOEXEC);
	free(filename);
	if (fd <= 0)
		return TEST_FAILURE;

	error = ioctl(fd, INCFS_IOC_GET_FILLED_BLOCKS, &fba);
	if (error != -1 || errno != EPERM) {
		ksft_print_msg("INCFS_IOC_GET_FILLED_BLOCKS not blocked\n");
		error = -EPERM;
		goto out;
	}

	cmd_fd = open_commands_file(mount_dir);
	permit_fill.file_descriptor = fd;
	if (ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill)) {
		print_error("INCFS_IOC_PERMIT_FILL failed");
		return -EPERM;
		goto out;
	}

	error = ioctl(fd, INCFS_IOC_GET_FILLED_BLOCKS, &fba);
	if (error)
		goto out;

	if (fba.total_blocks_out <= block_cnt) {
		error = -EINVAL;
		goto out;
	}

	if (fba.data_blocks_out != block_cnt) {
		error = -EINVAL;
		goto out;
	}

	if (fba.range_buffer_size_out != sizeof(struct incfs_filled_range)) {
		error = -EINVAL;
		goto out;
	}

	if (ranges[0].begin != file_blocks + 1 ||
	    ranges[0].end != file_blocks + 2) {
		error = -EINVAL;
		goto out;
	}

out:
	close(cmd_fd);
	close(fd);
	return error;
}

static int get_hash_blocks_test(const char *mount_dir)
{
	char *backing_dir;
	int cmd_fd = -1;
	int i;
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	if (mount_fs_opt(mount_dir, backing_dir, "readahead=0", false) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (crypto_emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, file->root_hash,
				     file->sig.add_data))
			goto failure;

		if (emit_partial_test_file_hash(mount_dir, file))
			goto failure;
	}

	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (validate_hash_ranges(mount_dir, file))
			goto failure;
	}

	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return TEST_FAILURE;
}

static int large_file_test(const char *mount_dir)
{
	char *backing_dir;
	int cmd_fd = -1;
	int i;
	int result = TEST_FAILURE;
	uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE] = {};
	int block_count = 3LL * 1024 * 1024 * 1024 / INCFS_DATA_FILE_BLOCK_SIZE;
	struct incfs_fill_block *block_buf =
		calloc(block_count, sizeof(struct incfs_fill_block));
	struct incfs_fill_blocks fill_blocks = {
		.count = block_count,
		.fill_blocks = ptr_to_u64(block_buf),
	};
	incfs_uuid_t id;
	int fd = -1;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	if (mount_fs_opt(mount_dir, backing_dir, "readahead=0", false) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	if (emit_file(cmd_fd, NULL, "very_large_file", &id,
		      (uint64_t)block_count * INCFS_DATA_FILE_BLOCK_SIZE,
		      NULL) < 0)
		goto failure;

	for (i = 0; i < block_count; i++) {
		block_buf[i].compression = COMPRESSION_NONE;
		block_buf[i].block_index = i;
		block_buf[i].data_len = INCFS_DATA_FILE_BLOCK_SIZE;
		block_buf[i].data = ptr_to_u64(data);
	}

	fd = open_file_by_id(mount_dir, id, true);
	if (fd < 0)
		goto failure;

	if (ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks) != block_count)
		goto failure;

	if (emit_file(cmd_fd, NULL, "very_very_large_file", &id, 1LL << 40,
		      NULL) < 0)
		goto failure;

	result = TEST_SUCCESS;

failure:
	close(fd);
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return result;
}

static int validate_mapped_file(const char *orig_name, const char *name,
				size_t size, size_t offset)
{
	struct stat st;
	int orig_fd = -1, fd = -1;
	size_t block;
	int result = TEST_FAILURE;

	if (stat(name, &st)) {
		ksft_print_msg("Failed to stat %s with error %s\n",
			       name, strerror(errno));
		goto failure;
	}

	if (size != st.st_size) {
		ksft_print_msg("Mismatched file sizes for file %s - expected %llu, got %llu\n",
				   name, size, st.st_size);
		goto failure;
	}

	fd = open(name, O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		ksft_print_msg("Failed to open %s with error %s\n", name,
			       strerror(errno));
		goto failure;
	}

	orig_fd = open(orig_name, O_RDONLY | O_CLOEXEC);
	if (orig_fd == -1) {
		ksft_print_msg("Failed to open %s with error %s\n", orig_name,
			       strerror(errno));
		goto failure;
	}

	for (block = 0; block < size; block += INCFS_DATA_FILE_BLOCK_SIZE) {
		uint8_t orig_data[INCFS_DATA_FILE_BLOCK_SIZE];
		uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];
		ssize_t orig_read, mapped_read;

		orig_read = pread(orig_fd, orig_data,
				 INCFS_DATA_FILE_BLOCK_SIZE, block + offset);
		mapped_read = pread(fd, data, INCFS_DATA_FILE_BLOCK_SIZE,
				    block);

		if (orig_read < mapped_read ||
		    mapped_read != min(size - block,
				       INCFS_DATA_FILE_BLOCK_SIZE)) {
			ksft_print_msg("Failed to read enough data: %llu %llu %llu %lld %lld\n",
				       block, size, offset, orig_read,
				       mapped_read);
			goto failure;
		}

		if (memcmp(orig_data, data, mapped_read)) {
			ksft_print_msg("Data doesn't match: %llu %llu %llu %lld %lld\n",
				       block, size, offset, orig_read,
				       mapped_read);
			goto failure;
		}
	}

	result = TEST_SUCCESS;

failure:
	close(orig_fd);
	close(fd);
	return result;
}

static int mapped_file_test(const char *mount_dir)
{
	char *backing_dir;
	int result = TEST_FAILURE;
	int cmd_fd = -1;
	int i;
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	if (mount_fs_opt(mount_dir, backing_dir, "readahead=0", false) != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	for (i = 0; i < file_num; ++i) {
		struct test_file *file = &test.files[i];
		size_t blocks = file->size / INCFS_DATA_FILE_BLOCK_SIZE;
		size_t mapped_offset = blocks / 4 *
			INCFS_DATA_FILE_BLOCK_SIZE;
		size_t mapped_size = file->size / 4 * 3 - mapped_offset;
		struct incfs_create_mapped_file_args mfa;
		char mapped_file_name[FILENAME_MAX];
		char orig_file_path[PATH_MAX];
		char mapped_file_path[PATH_MAX];

		if (emit_file(cmd_fd, NULL, file->name, &file->id, file->size,
					NULL) < 0)
			goto failure;

		if (emit_test_file_data(mount_dir, file))
			goto failure;

		if (snprintf(mapped_file_name, ARRAY_SIZE(mapped_file_name),
					"%s.mapped", file->name) < 0)
			goto failure;

		mfa = (struct incfs_create_mapped_file_args) {
			.size = mapped_size,
			.mode = 0664,
			.file_name = ptr_to_u64(mapped_file_name),
			.source_file_id = file->id,
			.source_offset = mapped_offset,
		};

		result = ioctl(cmd_fd, INCFS_IOC_CREATE_MAPPED_FILE, &mfa);
		if (result) {
			ksft_print_msg(
				"Failed to create mapped file with error %d\n",
				result);
			goto failure;
		}

		result = snprintf(orig_file_path,
				  ARRAY_SIZE(orig_file_path), "%s/%s",
				  mount_dir, file->name);

		if (result < 0 || result >= ARRAY_SIZE(mapped_file_path)) {
			result = TEST_FAILURE;
			goto failure;
		}

		result = snprintf(mapped_file_path,
				  ARRAY_SIZE(mapped_file_path), "%s/%s",
				  mount_dir, mapped_file_name);

		if (result < 0 || result >= ARRAY_SIZE(mapped_file_path)) {
			result = TEST_FAILURE;
			goto failure;
		}

		result = validate_mapped_file(orig_file_path, mapped_file_path,
					      mapped_size, mapped_offset);
		if (result)
			goto failure;
	}

failure:
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return result;
}

static const char v1_file[] = {
	/* Header */
	/* 0x00: Magic number */
	0x49, 0x4e, 0x43, 0x46, 0x53, 0x00, 0x00, 0x00,
	/* 0x08: Version */
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x10: Header size */
	0x38, 0x00,
	/* 0x12: Block size */
	0x00, 0x10,
	/* 0x14: Flags */
	0x00, 0x00, 0x00, 0x00,
	/* 0x18: First md offset */
	0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20: File size */
	0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x28: UUID */
	0x8c, 0x7d, 0xd9, 0x22, 0xad, 0x47, 0x49, 0x4f,
	0xc0, 0x2c, 0x38, 0x8e, 0x12, 0xc0, 0x0e, 0xac,

	/* 0x38: Attribute */
	0x6d, 0x65, 0x74, 0x61, 0x64, 0x61, 0x74, 0x61,
	0x31, 0x32, 0x33, 0x31, 0x32, 0x33,

	/* Attribute md record */
	/* 0x46: Type */
	0x02,
	/* 0x47: Size */
	0x25, 0x00,
	/* 0x49: CRC */
	0x9a, 0xef, 0xef, 0x72,
	/* 0x4d: Next md offset */
	0x75, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x55: Prev md offset */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x5d: fa_offset */
	0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x65: fa_size */
	0x0e, 0x00,
	/* 0x67: fa_crc */
	0xfb, 0x5e, 0x72, 0x89,

	/* Blockmap table */
	/* 0x6b: First 10-byte entry */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* Blockmap md record */
	/* 0x75: Type */
	0x01,
	/* 0x76: Size */
	0x23, 0x00,
	/* 0x78: CRC */
	0x74, 0x45, 0xd3, 0xb9,
	/* 0x7c: Next md offset */
	0x00, 0x00, 0x00, 0x00,	0x00, 0x00, 0x00, 0x00,
	/* 0x84: Prev md offset */
	0x46, 0x00, 0x00, 0x00,	0x00, 0x00, 0x00, 0x00,
	/* 0x8c: blockmap offset */
	0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x94: blockmap count */
	0x01, 0x00, 0x00, 0x00,
};

static int compatibility_test(const char *mount_dir)
{
	static const char *name = "file";
	int result = TEST_FAILURE;
	char *backing_dir = NULL;
	char *filename = NULL;
	int fd = -1;
	uint64_t size = 0x0c;

	TEST(backing_dir = create_backing_dir(mount_dir), backing_dir);
	TEST(filename = concat_file_name(backing_dir, name), filename);
	TEST(fd = open(filename, O_CREAT | O_WRONLY | O_CLOEXEC, 0777),
	     fd != -1);
	TESTEQUAL(write(fd, v1_file, sizeof(v1_file)), sizeof(v1_file));
	TESTEQUAL(fsetxattr(fd, INCFS_XATTR_SIZE_NAME, &size, sizeof(size), 0),
		  0);
	TESTEQUAL(mount_fs(mount_dir, backing_dir, 50), 0);
	free(filename);
	TEST(filename = concat_file_name(mount_dir, name), filename);
	close(fd);
	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);

	result = TEST_SUCCESS;
out:
	close(fd);
	umount(mount_dir);
	free(backing_dir);
	free(filename);
	return result;
}

static int zero_blocks_written_count(int fd, uint32_t data_blocks_written,
				     uint32_t hash_blocks_written)
{
	int test_result = TEST_FAILURE;
	uint64_t offset;
	uint8_t type;
	uint32_t bw;

	/* Get first md record */
	TESTEQUAL(pread(fd, &offset, sizeof(offset), 24), sizeof(offset));

	/* Find status md record */
	for (;;) {
		TESTNE(offset, 0);
		TESTEQUAL(pread(fd, &type, sizeof(type), le64_to_cpu(offset)),
			  sizeof(type));
		if (type == 4)
			break;
		TESTEQUAL(pread(fd, &offset, sizeof(offset),
				le64_to_cpu(offset) + 7),
			  sizeof(offset));
	}

	/* Read blocks_written */
	offset = le64_to_cpu(offset);
	TESTEQUAL(pread(fd, &bw, sizeof(bw), offset + 23), sizeof(bw));
	TESTEQUAL(le32_to_cpu(bw), data_blocks_written);
	TESTEQUAL(pread(fd, &bw, sizeof(bw), offset + 27), sizeof(bw));
	TESTEQUAL(le32_to_cpu(bw), hash_blocks_written);

	/* Write out zero */
	bw = 0;
	TESTEQUAL(pwrite(fd, &bw, sizeof(bw), offset + 23), sizeof(bw));
	TESTEQUAL(pwrite(fd, &bw, sizeof(bw), offset + 27), sizeof(bw));

	test_result = TEST_SUCCESS;
out:
	return test_result;
}

static int validate_block_count(const char *mount_dir, const char *backing_dir,
				struct test_file *file,
				int total_data_blocks, int filled_data_blocks,
				int total_hash_blocks, int filled_hash_blocks)
{
	char *filename = NULL;
	char *backing_filename = NULL;
	int fd = -1;
	struct incfs_get_block_count_args bca = {};
	int test_result = TEST_FAILURE;
	struct incfs_filled_range ranges[128];
	struct incfs_get_filled_blocks_args fba = {
		.range_buffer = ptr_to_u64(ranges),
		.range_buffer_size = sizeof(ranges),
	};
	int cmd_fd = -1;
	struct incfs_permit_fill permit_fill;

	TEST(filename = concat_file_name(mount_dir, file->name), filename);
	TEST(backing_filename = concat_file_name(backing_dir, file->name),
	     backing_filename);
	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);

	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, filled_data_blocks);
	TESTEQUAL(bca.total_hash_blocks_out, total_hash_blocks);
	TESTEQUAL(bca.filled_hash_blocks_out, filled_hash_blocks);

	close(fd);
	TESTEQUAL(umount(mount_dir), 0);
	TEST(fd = open(backing_filename, O_RDWR | O_CLOEXEC), fd != -1);
	TESTEQUAL(zero_blocks_written_count(fd, filled_data_blocks,
					    filled_hash_blocks),
		  TEST_SUCCESS);
	close(fd);
	fd = -1;
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir, "readahead=0", false),
		  0);
	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);

	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, 0);
	TESTEQUAL(bca.total_hash_blocks_out, total_hash_blocks);
	TESTEQUAL(bca.filled_hash_blocks_out, 0);

	TEST(cmd_fd = open_commands_file(mount_dir), cmd_fd != -1);
	permit_fill.file_descriptor = fd;
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill), 0);
	do {
		ioctl(fd, INCFS_IOC_GET_FILLED_BLOCKS, &fba);
		fba.start_index = fba.index_out + 1;
	} while (fba.index_out < fba.total_blocks_out);

	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, filled_data_blocks);
	TESTEQUAL(bca.total_hash_blocks_out, total_hash_blocks);
	TESTEQUAL(bca.filled_hash_blocks_out, filled_hash_blocks);

	test_result = TEST_SUCCESS;
out:
	close(cmd_fd);
	close(fd);
	free(filename);
	free(backing_filename);
	return test_result;
}



static int validate_data_block_count(const char *mount_dir,
				     const char *backing_dir,
				     struct test_file *file)
{
	const int total_data_blocks = 1 + (file->size - 1) /
						INCFS_DATA_FILE_BLOCK_SIZE;
	const int filled_data_blocks = (total_data_blocks + 1) / 2;

	int test_result = TEST_FAILURE;
	char *filename = NULL;
	char *incomplete_filename = NULL;
	struct stat stat_buf_incomplete, stat_buf_file;
	int fd = -1;
	struct incfs_get_block_count_args bca = {};
	int i;

	TEST(filename = concat_file_name(mount_dir, file->name), filename);
	TEST(incomplete_filename = get_incomplete_filename(mount_dir, file->id),
	     incomplete_filename);

	TESTEQUAL(stat(filename, &stat_buf_file), 0);
	TESTEQUAL(stat(incomplete_filename, &stat_buf_incomplete), 0);
	TESTEQUAL(stat_buf_file.st_ino, stat_buf_incomplete.st_ino);
	TESTEQUAL(stat_buf_file.st_nlink, 3);

	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, 0);
	TESTEQUAL(bca.total_hash_blocks_out, 0);
	TESTEQUAL(bca.filled_hash_blocks_out, 0);

	for (i = 0; i < total_data_blocks; i += 2)
		TESTEQUAL(emit_test_block(mount_dir, file, i), 0);

	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, filled_data_blocks);
	TESTEQUAL(bca.total_hash_blocks_out, 0);
	TESTEQUAL(bca.filled_hash_blocks_out, 0);
	close(fd);
	fd = -1;

	TESTEQUAL(validate_block_count(mount_dir, backing_dir, file,
				       total_data_blocks, filled_data_blocks,
				       0, 0),
		  0);

	for (i = 1; i < total_data_blocks; i += 2)
		TESTEQUAL(emit_test_block(mount_dir, file, i), 0);

	TESTEQUAL(stat(incomplete_filename, &stat_buf_incomplete), -1);
	TESTEQUAL(errno, ENOENT);

	test_result = TEST_SUCCESS;
out:
	close(fd);
	free(incomplete_filename);
	free(filename);
	return test_result;
}

static int data_block_count_test(const char *mount_dir)
{
	int result = TEST_FAILURE;
	char *backing_dir;
	int cmd_fd = -1;
	int i;
	struct test_files_set test = get_test_files_set();

	TEST(backing_dir = create_backing_dir(mount_dir), backing_dir);
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir, "readahead=0", false),
		  0);

	for (i = 0; i < test.files_count; ++i) {
		struct test_file *file = &test.files[i];

		TEST(cmd_fd = open_commands_file(mount_dir), cmd_fd != -1);
		TESTEQUAL(emit_file(cmd_fd, NULL, file->name, &file->id,
				    file->size,	NULL),
			  0);
		close(cmd_fd);
		cmd_fd = -1;

		TESTEQUAL(validate_data_block_count(mount_dir, backing_dir,
						    file),
			  0);
	}

	result = TEST_SUCCESS;
out:
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return result;
}

static int validate_hash_block_count(const char *mount_dir,
				     const char *backing_dir,
				     struct test_file *file)
{
	const int digest_size = SHA256_DIGEST_SIZE;
	const int hash_per_block = INCFS_DATA_FILE_BLOCK_SIZE / digest_size;
	const int total_data_blocks = 1 + (file->size - 1) /
					INCFS_DATA_FILE_BLOCK_SIZE;

	int result = TEST_FAILURE;
	int hash_layer = total_data_blocks;
	int total_hash_blocks = 0;
	int filled_hash_blocks;
	char *filename = NULL;
	int fd = -1;
	struct incfs_get_block_count_args bca = {};

	while (hash_layer > 1) {
		hash_layer = (hash_layer + hash_per_block - 1) / hash_per_block;
		total_hash_blocks += hash_layer;
	}
	filled_hash_blocks = total_hash_blocks > 1 ? 1 : 0;

	TEST(filename = concat_file_name(mount_dir, file->name), filename);
	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);

	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, 0);
	TESTEQUAL(bca.total_hash_blocks_out, total_hash_blocks);
	TESTEQUAL(bca.filled_hash_blocks_out, 0);

	TESTEQUAL(emit_partial_test_file_hash(mount_dir, file), 0);

	TESTEQUAL(ioctl(fd, INCFS_IOC_GET_BLOCK_COUNT, &bca), 0);
	TESTEQUAL(bca.total_data_blocks_out, total_data_blocks);
	TESTEQUAL(bca.filled_data_blocks_out, 0);
	TESTEQUAL(bca.total_hash_blocks_out, total_hash_blocks);
	TESTEQUAL(bca.filled_hash_blocks_out, filled_hash_blocks);
	close(fd);
	fd = -1;

	if (filled_hash_blocks)
		TESTEQUAL(validate_block_count(mount_dir, backing_dir, file,
				       total_data_blocks, 0,
				       total_hash_blocks, filled_hash_blocks),
		  0);

	result = TEST_SUCCESS;
out:
	close(fd);
	free(filename);
	return result;
}

static int hash_block_count_test(const char *mount_dir)
{
	int result = TEST_FAILURE;
	char *backing_dir;
	int cmd_fd = -1;
	int i;
	struct test_files_set test = get_test_files_set();

	TEST(backing_dir = create_backing_dir(mount_dir), backing_dir);
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir, "readahead=0", false),
		  0);

	for (i = 0; i < test.files_count; i++) {
		struct test_file *file = &test.files[i];

		TEST(cmd_fd = open_commands_file(mount_dir), cmd_fd != -1);
		TESTEQUAL(crypto_emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, file->root_hash,
				     file->sig.add_data),
			  0);
		close(cmd_fd);
		cmd_fd = -1;

		TESTEQUAL(validate_hash_block_count(mount_dir, backing_dir,
						    &test.files[i]),
			  0);
	}

	result = TEST_SUCCESS;
out:
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	return result;
}

static int is_close(struct timespec *start, int expected_ms)
{
	const int allowed_variance = 100;
	int result = TEST_FAILURE;
	struct timespec finish;
	int diff;

	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &finish), 0);
	diff = (finish.tv_sec - start->tv_sec) * 1000 +
		(finish.tv_nsec - start->tv_nsec) / 1000000;

	TESTCOND(diff >= expected_ms - allowed_variance);
	TESTCOND(diff <= expected_ms + allowed_variance);
	result = TEST_SUCCESS;
out:
	return result;
}

static int per_uid_read_timeouts_test(const char *mount_dir)
{
	struct test_file file = {
		.name = "file",
		.size = 16 * INCFS_DATA_FILE_BLOCK_SIZE
	};

	int result = TEST_FAILURE;
	char *backing_dir = NULL;
	int pid = -1;
	int cmd_fd = -1;
	char *filename = NULL;
	int fd = -1;
	struct timespec start;
	char buffer[4096];
	struct incfs_per_uid_read_timeouts purt_get[1];
	struct incfs_get_read_timeouts_args grt = {
		ptr_to_u64(purt_get),
		sizeof(purt_get)
	};
	struct incfs_per_uid_read_timeouts purt_set[] = {
		{
			.uid = 0,
			.min_time_us = 1000000,
			.min_pending_time_us = 2000000,
			.max_pending_time_us = 3000000,
		},
	};
	struct incfs_set_read_timeouts_args srt = {
		ptr_to_u64(purt_set),
		sizeof(purt_set)
	};
	int status;

	TEST(backing_dir = create_backing_dir(mount_dir), backing_dir);
	TESTEQUAL(mount_fs_opt(mount_dir, backing_dir,
			       "read_timeout_ms=1000,readahead=0", false), 0);

	TEST(cmd_fd = open_commands_file(mount_dir), cmd_fd != -1);
	TESTEQUAL(emit_file(cmd_fd, NULL, file.name, &file.id, file.size,
			    NULL), 0);

	TEST(filename = concat_file_name(mount_dir, file.name), filename);
	TEST(fd = open(filename, O_RDONLY | O_CLOEXEC), fd != -1);
	TESTEQUAL(fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC), 0);

	/* Default mount options read failure is 1000 */
	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &start), 0);
	TESTEQUAL(pread(fd, buffer, sizeof(buffer), 0), -1);
	TESTEQUAL(is_close(&start, 1000), 0);

	grt.timeouts_array_size = 0;
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_GET_READ_TIMEOUTS, &grt), 0);
	TESTEQUAL(grt.timeouts_array_size_out, 0);

	/* Set it to 3000 */
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_SET_READ_TIMEOUTS, &srt), 0);
	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &start), 0);
	TESTEQUAL(pread(fd, buffer, sizeof(buffer), 0), -1);
	TESTEQUAL(is_close(&start, 3000), 0);
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_GET_READ_TIMEOUTS, &grt), -1);
	TESTEQUAL(errno, E2BIG);
	TESTEQUAL(grt.timeouts_array_size_out, sizeof(purt_get));
	grt.timeouts_array_size = sizeof(purt_get);
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_GET_READ_TIMEOUTS, &grt), 0);
	TESTEQUAL(grt.timeouts_array_size_out, sizeof(purt_get));
	TESTEQUAL(purt_get[0].uid, purt_set[0].uid);
	TESTEQUAL(purt_get[0].min_time_us, purt_set[0].min_time_us);
	TESTEQUAL(purt_get[0].min_pending_time_us,
		  purt_set[0].min_pending_time_us);
	TESTEQUAL(purt_get[0].max_pending_time_us,
		  purt_set[0].max_pending_time_us);

	/* Still 1000 in UID 2 */
	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &start), 0);
	TEST(pid = fork(), pid != -1);
	if (pid == 0) {
		TESTEQUAL(setuid(2), 0);
		TESTEQUAL(pread(fd, buffer, sizeof(buffer), 0), -1);
		exit(0);
	}
	TESTNE(wait(&status), -1);
	TESTEQUAL(WEXITSTATUS(status), 0);
	TESTEQUAL(is_close(&start, 1000), 0);

	/* Set it to default */
	purt_set[0].max_pending_time_us = UINT32_MAX;
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_SET_READ_TIMEOUTS, &srt), 0);
	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &start), 0);
	TESTEQUAL(pread(fd, buffer, sizeof(buffer), 0), -1);
	TESTEQUAL(is_close(&start, 1000), 0);

	/* Test min read time */
	TESTEQUAL(emit_test_block(mount_dir, &file, 0), 0);
	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &start), 0);
	TESTEQUAL(pread(fd, buffer, sizeof(buffer), 0), sizeof(buffer));
	TESTEQUAL(is_close(&start, 1000), 0);

	/* Test min pending time */
	purt_set[0].uid = 2;
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_SET_READ_TIMEOUTS, &srt), 0);
	TESTEQUAL(clock_gettime(CLOCK_MONOTONIC, &start), 0);
	TEST(pid = fork(), pid != -1);
	if (pid == 0) {
		TESTEQUAL(setuid(2), 0);
		TESTEQUAL(pread(fd, buffer, sizeof(buffer), sizeof(buffer)),
			  sizeof(buffer));
		exit(0);
	}
	sleep(1);
	TESTEQUAL(emit_test_block(mount_dir, &file, 1), 0);
	TESTNE(wait(&status), -1);
	TESTEQUAL(WEXITSTATUS(status), 0);
	TESTEQUAL(is_close(&start, 2000), 0);

	/* Clear timeouts */
	srt.timeouts_array_size = 0;
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_SET_READ_TIMEOUTS, &srt), 0);
	grt.timeouts_array_size = 0;
	TESTEQUAL(ioctl(cmd_fd, INCFS_IOC_GET_READ_TIMEOUTS, &grt), 0);
	TESTEQUAL(grt.timeouts_array_size_out, 0);

	result = TEST_SUCCESS;
out:
	close(fd);

	if (pid == 0)
		exit(result);

	free(filename);
	close(cmd_fd);
	umount(backing_dir);
	free(backing_dir);
	return result;
}

#define DIRS 3
static int inotify_test(const char *mount_dir)
{
	struct test_file file = {
		.name = "file",
		.size = 16 * INCFS_DATA_FILE_BLOCK_SIZE
	};

	int result = TEST_FAILURE;
	char *backing_dir = NULL, *index_dir = NULL, *incomplete_dir = NULL;
	char *file_name = NULL;
	int cmd_fd = -1;
	int notify_fd = -1;
	int wds[DIRS];
	char buffer[DIRS * (sizeof(struct inotify_event) + NAME_MAX + 1)];
	char *ptr = buffer;
	struct inotify_event *event;
	struct inotify_event *events[DIRS] = {};
	const char *names[DIRS] = {};
	int res;
	char id[sizeof(incfs_uuid_t) * 2 + 1];

	/* File creation triggers inotify events in .index and .incomplete? */
	TEST(backing_dir = create_backing_dir(mount_dir), backing_dir);
	TEST(index_dir = concat_file_name(mount_dir, ".index"), index_dir);
	TEST(incomplete_dir = concat_file_name(mount_dir, ".incomplete"),
	     incomplete_dir);
	TESTEQUAL(mount_fs(mount_dir, backing_dir, 50), 0);
	TEST(cmd_fd = open_commands_file(mount_dir), cmd_fd != -1);
	TEST(notify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC),
	     notify_fd != -1);
	TEST(wds[0] = inotify_add_watch(notify_fd, mount_dir,
					IN_CREATE | IN_DELETE),
	     wds[0] != -1);
	TEST(wds[1] = inotify_add_watch(notify_fd, index_dir,
					IN_CREATE | IN_DELETE),
	     wds[1] != -1);
	TEST(wds[2] = inotify_add_watch(notify_fd, incomplete_dir,
					IN_CREATE | IN_DELETE),
	     wds[2] != -1);
	TESTEQUAL(emit_file(cmd_fd, NULL, file.name, &file.id, file.size,
			   NULL), 0);
	TEST(res = read(notify_fd, buffer, sizeof(buffer)), res != -1);

	while (ptr < buffer + res) {
		int i;

		event = (struct inotify_event *) ptr;
		TESTCOND(ptr + sizeof(*event) <= buffer + res);
		for (i = 0; i < DIRS; ++i)
			if (event->wd == wds[i]) {
				TESTEQUAL(events[i], NULL);
				events[i] = event;
				ptr += sizeof(*event);
				names[i] = ptr;
				ptr += events[i]->len;
				TESTCOND(ptr <= buffer + res);
				break;
			}
		TESTCOND(i < DIRS);
	}

	TESTNE(events[0], NULL);
	TESTNE(events[1], NULL);
	TESTNE(events[2], NULL);

	bin2hex(id, file.id.bytes, sizeof(incfs_uuid_t));

	TESTEQUAL(events[0]->mask, IN_CREATE);
	TESTEQUAL(events[1]->mask, IN_CREATE);
	TESTEQUAL(events[2]->mask, IN_CREATE);
	TESTEQUAL(strcmp(names[0], file.name), 0);
	TESTEQUAL(strcmp(names[1], id), 0);
	TESTEQUAL(strcmp(names[2], id), 0);

	/* File completion triggers inotify event in .incomplete? */
	TESTEQUAL(emit_test_file_data(mount_dir, &file), 0);
	TEST(res = read(notify_fd, buffer, sizeof(buffer)), res != -1);
	event = (struct inotify_event *) buffer;
	TESTEQUAL(event->wd, wds[2]);
	TESTEQUAL(event->mask, IN_DELETE);
	TESTEQUAL(strcmp(event->name, id), 0);

	/* File unlinking triggers inotify event in .index? */
	TEST(file_name = concat_file_name(mount_dir, file.name), file_name);
	TESTEQUAL(unlink(file_name), 0);
	TEST(res = read(notify_fd, buffer, sizeof(buffer)), res != -1);
	memset(events, 0, sizeof(events));
	memset(names, 0, sizeof(names));
	for (ptr = buffer; ptr < buffer + res;) {
		event = (struct inotify_event *) ptr;
		int i;

		TESTCOND(ptr + sizeof(*event) <= buffer + res);
		for (i = 0; i < DIRS; ++i)
			if (event->wd == wds[i]) {
				TESTEQUAL(events[i], NULL);
				events[i] = event;
				ptr += sizeof(*event);
				names[i] = ptr;
				ptr += events[i]->len;
				TESTCOND(ptr <= buffer + res);
				break;
			}
		TESTCOND(i < DIRS);
	}

	TESTNE(events[0], NULL);
	TESTNE(events[1], NULL);
	TESTEQUAL(events[2], NULL);

	TESTEQUAL(events[0]->mask, IN_DELETE);
	TESTEQUAL(events[1]->mask, IN_DELETE);
	TESTEQUAL(strcmp(names[0], file.name), 0);
	TESTEQUAL(strcmp(names[1], id), 0);

	result = TEST_SUCCESS;
out:
	free(file_name);
	close(notify_fd);
	close(cmd_fd);
	umount(mount_dir);
	free(backing_dir);
	free(index_dir);
	free(incomplete_dir);
	return result;
}

static char *setup_mount_dir()
{
	struct stat st;
	char *current_dir = getcwd(NULL, 0);
	char *mount_dir = concat_file_name(current_dir, "incfs-mount-dir");

	free(current_dir);
	if (stat(mount_dir, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return mount_dir;

		ksft_print_msg("%s is a file, not a dir.\n", mount_dir);
		return NULL;
	}

	if (mkdir(mount_dir, 0777)) {
		print_error("Can't create mount dir.");
		return NULL;
	}

	return mount_dir;
}

int parse_options(int argc, char *const *argv)
{
	signed char c;

	while ((c = getopt(argc, argv, "f:t:v")) != -1)
		switch (c) {
		case 'f':
			options.file = strtol(optarg, NULL, 10);
			break;

		case 't':
			options.test = strtol(optarg, NULL, 10);
			break;

		case 'v':
			options.verbose = true;
			break;

		default:
			return -EINVAL;
		}

	return 0;
}

struct test_case {
	int (*pfunc)(const char *dir);
	const char *name;
};

void run_one_test(const char *mount_dir, struct test_case *test_case)
{
	ksft_print_msg("Running %s\n", test_case->name);
	if (test_case->pfunc(mount_dir) == TEST_SUCCESS)
		ksft_test_result_pass("%s\n", test_case->name);
	else
		ksft_test_result_fail("%s\n", test_case->name);
}

int main(int argc, char *argv[])
{
	char *mount_dir = NULL;
	int i;
	int fd, count;

	if (parse_options(argc, argv))
		ksft_exit_fail_msg("Bad options\n");

	// Seed randomness pool for testing on QEMU
	// NOTE - this abuses the concept of randomness - do *not* ever do this
	// on a machine for production use - the device will think it has good
	// randomness when it does not.
	fd = open("/dev/urandom", O_WRONLY | O_CLOEXEC);
	count = 4096;
	for (int i = 0; i < 128; ++i)
		ioctl(fd, RNDADDTOENTCNT, &count);
	close(fd);

	ksft_print_header();

	if (geteuid() != 0)
		ksft_print_msg("Not a root, might fail to mount.\n");

	mount_dir = setup_mount_dir();
	if (mount_dir == NULL)
		ksft_exit_fail_msg("Can't create a mount dir\n");

#define MAKE_TEST(test)                                                        \
	{                                                                      \
		test, #test                                                    \
	}
	struct test_case cases[] = {
		MAKE_TEST(basic_file_ops_test),
		MAKE_TEST(cant_touch_index_test),
		MAKE_TEST(dynamic_files_and_data_test),
		MAKE_TEST(concurrent_reads_and_writes_test),
		MAKE_TEST(attribute_test),
		MAKE_TEST(work_after_remount_test),
		MAKE_TEST(child_procs_waiting_for_data_test),
		MAKE_TEST(multiple_providers_test),
		MAKE_TEST(hash_tree_test),
		MAKE_TEST(read_log_test),
		MAKE_TEST(get_blocks_test),
		MAKE_TEST(get_hash_blocks_test),
		MAKE_TEST(large_file_test),
		MAKE_TEST(mapped_file_test),
		MAKE_TEST(compatibility_test),
		MAKE_TEST(data_block_count_test),
		MAKE_TEST(hash_block_count_test),
		MAKE_TEST(per_uid_read_timeouts_test),
		MAKE_TEST(inotify_test),
	};
#undef MAKE_TEST

	if (options.test) {
		if (options.test <= 0 || options.test > ARRAY_SIZE(cases))
			ksft_exit_fail_msg("Invalid test\n");

		ksft_set_plan(1);
		run_one_test(mount_dir, &cases[options.test - 1]);
	} else {
		ksft_set_plan(ARRAY_SIZE(cases));
		for (i = 0; i < ARRAY_SIZE(cases); ++i)
			run_one_test(mount_dir, &cases[i]);
	}

	umount2(mount_dir, MNT_FORCE);
	rmdir(mount_dir);
	return !ksft_get_fail_cnt() ? ksft_exit_pass() : ksft_exit_fail();
}
