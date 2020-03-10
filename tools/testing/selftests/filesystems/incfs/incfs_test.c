// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <alloca.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <linux/random.h>
#include <linux/unistd.h>

#include "../../kselftest.h"

#include "lz4.h"
#include "utils.h"

#define __packed __attribute__((__packed__))

#define TEST_FAILURE 1
#define TEST_SUCCESS 0
#define INCFS_MAX_MTREE_LEVELS 8

#define INCFS_ROOT_INODE 0

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

/*
 * The certificate below and the private key were created by calling:
 *	openssl req -x509 -newkey rsa:4096 -keyout private.key -out cert.crt
 *	-days 1000 -sha256 -nodes -outform PEM -subj
 *	"/C=US/ST=WA/L=Kirkland/O=Example/OU=Org/CN=www.example.com"
 */
char x509_cert[] =
"-----BEGIN CERTIFICATE-----\n"
"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\n"
"-----END CERTIFICATE-----";

char private_key[] =
"-----BEGIN PRIVATE KEY-----\n"
"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\n"
"GyPbW1yZa5vE302a1Cr0Cd7RC4AFAAw=\n"
"-----END PRIVATE KEY-----";

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

static char *get_index_filename(char *mnt_dir, incfs_uuid_t id)
{
	char path[FILENAME_MAX];
	char str_id[1 + 2 * sizeof(id)];

	bin2hex(str_id, id.bytes, sizeof(id.bytes));
	snprintf(path, ARRAY_SIZE(path), "%s/.index/%s", mnt_dir, str_id);

	return strdup(path);
}

int open_file_by_id(char *mnt_dir, incfs_uuid_t id)
{
	char *path = get_index_filename(mnt_dir, id);
	int fd = open(path, O_RDWR);

	free(path);
	if (fd < 0) {
		print_error("Can't open file by id.");
		return -errno;
	}

	return fd;
}

int get_file_attr(char *mnt_dir, incfs_uuid_t id, char *value, int size)
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

static int emit_test_blocks(char *mnt_dir, struct test_file *file,
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
	int fd;
	int error = 0;
	int i = 0;
	int blocks_written = 0;

	fd = open_file_by_id(mnt_dir, file->id);
	if (fd <= 0) {
		error = -errno;
		goto out;
	}

	for (i = 0; i < block_count; i++) {
		int block_index = blocks[i];
		bool compress = (file->index + block_index) % 2 == 0;
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
		if (compress) {
			size_t comp_size = LZ4_compress_default(
				(char *)data, (char *)comp_data, block_size,
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

static int emit_test_block(char *mnt_dir, struct test_file *file,
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

static int emit_test_file_data(char *mount_dir, struct test_file *file)
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

static loff_t read_whole_file(char *filename)
{
	int fd = -1;
	loff_t result;
	loff_t bytes_read = 0;
	uint8_t buff[16 * 1024];

	fd = open(filename, O_RDONLY);
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

	fd = open(filename, O_RDONLY);
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

static char *create_backing_dir(char *mount_dir)
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

static int validate_test_file_content_with_seed(char *mount_dir,
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

static int validate_test_file_content(char *mount_dir, struct test_file *file)
{
	return validate_test_file_content_with_seed(mount_dir, file, 0);
}

static int data_producer(char *mount_dir, struct test_files_set *test_set)
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
	char data_to_sign[256] = {};
	int sig_data_size;
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

		rnd_buf((uint8_t *)data, file->size, seed);
		sha256(data, file->size, file->root_hash);
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

		if (file->size - offset < block_size)
			block_size = file->size - offset;

		rnd_buf((uint8_t *)data, block_size, seed);
		sha256(data, block_size, hash_ptr);
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

	/* Calculating digital signature */
	snprintf(file->sig.add_data, sizeof(file->sig.add_data), "%ld",
		 file->size);
	memcpy(data_to_sign, file->root_hash, SHA256_DIGEST_SIZE);
	memcpy(data_to_sign + SHA256_DIGEST_SIZE, file->sig.add_data,
		strlen(file->sig.add_data));
	sig_data_size = SHA256_DIGEST_SIZE + strlen(file->sig.add_data);
	if (!sign_pkcs7(data_to_sign, sig_data_size, private_key, x509_cert,
		       &file->sig.data, &file->sig.size)) {
		ksft_print_msg("Signing failed.\n");
		return -EINVAL;
	}

	return 0;
}

static int load_hash_tree(const char *mount_dir, struct test_file *file)
{
	int err;
	int i;
	int fd;
	char *file_path;
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

	file_path  = concat_file_name(mount_dir, file->name);
	fd = open(file_path, O_RDWR);
	free(file_path);
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

static int cant_touch_index_test(char *mount_dir)
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

static bool iterate_directory(char *dir_to_iterate, bool root, int file_count)
{
	struct expected_name {
		const char *name;
		bool root_only;
		bool found;
	} names[] = {
		{INCFS_LOG_FILENAME, true, false},
		{INCFS_PENDING_READS_FILENAME, true, false},
		{".index", true, false},
		{"..", false, false},
		{".", false, false},
	};

	bool pass = true, found;
	int i;

	/* Test directory iteration */
	int fd = open(dir_to_iterate, O_RDONLY | O_DIRECTORY);

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

static int basic_file_ops_test(char *mount_dir)
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

		fd = open(path, O_RDWR);
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

static int dynamic_files_and_data_test(char *mount_dir)
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

static int concurrent_reads_and_writes_test(char *mount_dir)
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

static int work_after_remount_test(char *mount_dir)
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

		if (access(filename_in_index, F_OK) != 0) {
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

static int attribute_test(char *mount_dir)
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

static int child_procs_waiting_for_data_test(char *mount_dir)
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

static int multiple_providers_test(char *mount_dir)
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
	if (mount_fs(mount_dir, backing_dir, 10000) != 0)
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

			ret = data_producer(mount_dir, &test);
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

static int signature_test(char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	int i = 0;
	unsigned char sig_buf[INCFS_MAX_SIGNATURE_SIZE];
	char *backing_dir;
	int cmd_fd = -1;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	/* Mount FS and release the backing file.  (10s wait time) */
	if (mount_fs(mount_dir, backing_dir, 10000) != 0)
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
			file->sig.data, file->sig.size, file->sig.add_data);

		if (res) {
			ksft_print_msg("Emit failed for %s. error: %s\n",
				file->name, strerror(-res));
			goto failure;
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
		int sig_len;
		char *path;
		int fd;

		if (validate_test_file_content(mount_dir, file) < 0)
			goto failure;

		path = concat_file_name(mount_dir, file->name);
		fd = open(path, O_RDWR);
		free(path);
		if (fd < 0) {
			print_error("Can't open file");
			goto failure;
		}

		sig_len = get_file_signature(fd, sig_buf, ARRAY_SIZE(sig_buf));

		if (close(fd)) {
			print_error("Can't close file");
			goto failure;
		}

		if (sig_len < 0) {
			ksft_print_msg("Can't load signature %s. error: %s\n",
				file->name, strerror(-sig_len));
			goto failure;
		}

		if (sig_len != file->sig.size ||
			memcmp(sig_buf, file->sig.data, sig_len)) {
			ksft_print_msg("Signature mismatch %s.\n",
				file->name);
			goto failure;
		}
	}

	/* Unmount and mount again, to make sure the signature is persistent. */
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
		int sig_len;
		char *path;
		int fd;

		if (validate_test_file_content(mount_dir, file) < 0)
			goto failure;

		path = concat_file_name(mount_dir, file->name);
		fd = open(path, O_RDWR);
		free(path);
		if (fd < 0) {
			print_error("Can't open file");
			goto failure;
		}

		sig_len = get_file_signature(fd, sig_buf, ARRAY_SIZE(sig_buf));

		if (close(fd)) {
			print_error("Can't close file");
			goto failure;
		}

		if (sig_len < 0) {
			ksft_print_msg("Can't load signature %s. error: %s\n",
				file->name, strerror(-sig_len));
			goto failure;
		}
		if (sig_len != file->sig.size ||
			memcmp(sig_buf, file->sig.data, sig_len)) {
			ksft_print_msg("Signature mismatch %s.\n",
				file->name);
			goto failure;
		}
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

static int hash_tree_test(char *mount_dir)
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
			file->sig.data, file->sig.size, file->sig.add_data);

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

static int validate_logs(char *mount_dir, int log_fd, struct test_file *file)
{
	uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE];
	struct incfs_pending_read_info prs[100] = {};
	int prs_size = ARRAY_SIZE(prs);
	int block_cnt = 1 + (file->size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	int res;
	int read_count;
	int i;
	char *filename = concat_file_name(mount_dir, file->name);
	int fd;

	fd = open(filename, O_RDONLY);
	free(filename);
	if (fd <= 0)
		return TEST_FAILURE;

	if (block_cnt > prs_size)
		block_cnt = prs_size;

	for (i = 0; i < block_cnt; i++) {
		res = pread(fd, data, sizeof(data),
			    INCFS_DATA_FILE_BLOCK_SIZE * i);
		if (res <= 0)
			goto failure;
	}

	read_count = wait_for_pending_reads(log_fd, 0, prs, prs_size);
	if (read_count < 0) {
		ksft_print_msg("Error reading logged reads %s.\n",
			       strerror(-read_count));
		goto failure;
	}

	if (read_count != block_cnt) {
		ksft_print_msg("Bad log read count %s %d %d.\n", file->name,
			       read_count, block_cnt);
		goto failure;
	}

	for (i = 0; i < read_count; i++) {
		struct incfs_pending_read_info *read = &prs[i];

		if (!same_id(&read->file_id, &file->id)) {
			ksft_print_msg("Bad log read ino %s\n", file->name);
			goto failure;
		}

		if (read->block_index != i) {
			ksft_print_msg("Bad log read ino %s %d %d.\n",
				       file->name, read->block_index, i);
			goto failure;
		}

		if (i != 0) {
			unsigned long psn = prs[i - 1].serial_number;

			if (read->serial_number != psn + 1) {
				ksft_print_msg("Bad log read sn %s %d %d.\n",
					       file->name, read->serial_number,
					       psn);
				goto failure;
			}
		}

		if (read->timestamp_us == 0) {
			ksft_print_msg("Bad log read timestamp %s.\n",
				       file->name);
			goto failure;
		}
	}
	close(fd);
	return TEST_SUCCESS;

failure:
	close(fd);
	return TEST_FAILURE;
}

static int read_log_test(char *mount_dir)
{
	struct test_files_set test = get_test_files_set();
	const int file_num = test.files_count;
	int i = 0;
	int cmd_fd = -1, log_fd = -1;
	char *backing_dir;

	backing_dir = create_backing_dir(mount_dir);
	if (!backing_dir)
		goto failure;

	if (mount_fs_opt(mount_dir, backing_dir, "readahead=0") != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	log_fd = open_log_file(mount_dir);
	if (cmd_fd < 0)
		ksft_print_msg("Can't open log file.\n");

	/* Write data. */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (emit_file(cmd_fd, NULL, file->name, &file->id,
				     file->size, NULL))
			goto failure;

		if (emit_test_file_data(mount_dir, file))
			goto failure;
	}

	/* Validate data */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (validate_logs(mount_dir, log_fd, file))
			goto failure;
	}

	/* Unmount and mount again, to see that logs work after remount. */
	close(cmd_fd);
	close(log_fd);
	cmd_fd = -1;
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	if (mount_fs_opt(mount_dir, backing_dir, "readahead=0") != 0)
		goto failure;

	cmd_fd = open_commands_file(mount_dir);
	if (cmd_fd < 0)
		goto failure;

	log_fd = open_log_file(mount_dir);
	if (cmd_fd < 0)
		ksft_print_msg("Can't open log file.\n");

	/* Validate data again */
	for (i = 0; i < file_num; i++) {
		struct test_file *file = &test.files[i];

		if (validate_logs(mount_dir, log_fd, file))
			goto failure;
	}

	/* Final unmount */
	close(cmd_fd);
	close(log_fd);
	free(backing_dir);
	if (umount(mount_dir) != 0) {
		print_error("Can't unmout FS");
		goto failure;
	}

	return TEST_SUCCESS;

failure:
	close(cmd_fd);
	close(log_fd);
	free(backing_dir);
	umount(mount_dir);
	return TEST_FAILURE;
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

int main(int argc, char *argv[])
{
	char *mount_dir = NULL;
	int fails = 0;
	int i;
	int fd, count;

	// Seed randomness pool for testing on QEMU
	// NOTE - this abuses the concept of randomness - do *not* ever do this
	// on a machine for production use - the device will think it has good
	// randomness when it does not.
	fd = open("/dev/urandom", O_WRONLY);
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
	struct {
		int (*pfunc)(char *dir);
		const char *name;
	} cases[] = {
		MAKE_TEST(basic_file_ops_test),
		MAKE_TEST(cant_touch_index_test),
		MAKE_TEST(dynamic_files_and_data_test),
		MAKE_TEST(concurrent_reads_and_writes_test),
		MAKE_TEST(attribute_test),
		MAKE_TEST(work_after_remount_test),
		MAKE_TEST(child_procs_waiting_for_data_test),
		MAKE_TEST(multiple_providers_test),
		MAKE_TEST(signature_test),
		MAKE_TEST(hash_tree_test),
		MAKE_TEST(read_log_test),
	};
#undef MAKE_TEST

	ksft_set_plan(ARRAY_SIZE(cases));

	for (i = 0; i < ARRAY_SIZE(cases); ++i) {
		ksft_print_msg("Running %s\n", cases[i].name);
		if (cases[i].pfunc(mount_dir) == TEST_SUCCESS)
			ksft_test_result_pass("%s\n", cases[i].name);
		else {
			ksft_test_result_fail("%s\n", cases[i].name);
			fails++;
		}
	}

	umount2(mount_dir, MNT_FORCE);
	rmdir(mount_dir);

	if (fails > 0)
		ksft_exit_pass();
	else
		ksft_exit_pass();
	return 0;
}
