// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <lz4.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>

#include "utils.h"

#define err_msg(...)                                                           \
	do {                                                                   \
		fprintf(stderr, "%s: (%d) ", TAG, __LINE__);                   \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, " (%s)\n", strerror(errno));                   \
	} while (false)

#define TAG "incfs_perf"

struct options {
	int blocks; /* -b number of diff block sizes */
	bool no_cleanup; /* -c don't clean up after */
	const char *test_dir; /* -d working directory */
	const char *file_types; /* -f sScCvV */
	bool no_native; /* -n don't test native files */
	bool no_random; /* -r don't do random reads*/
	bool no_linear; /* -R random reads only */
	size_t size; /* -s file size as power of 2 */
	int tries; /* -t times to run test*/
};

enum flags {
	SHUFFLE = 1,
	COMPRESS = 2,
	VERIFY = 4,
	LAST_FLAG = 8,
};

void print_help(void)
{
	puts(
	"incfs_perf. Performance test tool for incfs\n"
	"\tTests read performance of incfs by creating files of various types\n"
	"\tflushing caches and then reading them back.\n"
	"\tEach file is read with different block sizes and average\n"
	"\tthroughput in megabytes/second and memory usage are reported for\n"
	"\teach block size\n"
	"\tNative files are tested for comparison\n"
	"\tNative files are created in native folder, incfs files are created\n"
	"\tin src folder which is mounted on dst folder\n"
	"\n"
	"\t-bn (default 8) number of different block sizes, starting at 4096\n"
	"\t                and doubling\n"
	"\t-c		   don't Clean up - leave files and mount point\n"
	"\t-d dir          create directories in dir\n"
	"\t-fs|Sc|Cv|V     restrict which files are created.\n"
	"\t                s blocks not shuffled, S blocks shuffled\n"
	"\t                c blocks not compress, C blocks compressed\n"
	"\t                v files not verified, V files verified\n"
	"\t                If a letter is omitted, both options are tested\n"
	"\t                If no letter are given, incfs is not tested\n"
	"\t-n              Don't test native files\n"
	"\t-r              No random reads (sequential only)\n"
	"\t-R              Random reads only (no sequential)\n"
	"\t-sn (default 30)File size as power of 2\n"
	"\t-tn (default 5) Number of tries per file. Results are averaged\n"
	);
}

int parse_options(int argc, char *const *argv, struct options *options)
{
	signed char c;

	/* Set defaults here */
	*options = (struct options){
		.blocks = 8,
		.test_dir = ".",
		.tries = 5,
		.size = 30,
	};

	/* Load options from command line here */
	while ((c = getopt(argc, argv, "b:cd:f::hnrRs:t:")) != -1) {
		switch (c) {
		case 'b':
			options->blocks = strtol(optarg, NULL, 10);
			break;

		case 'c':
			options->no_cleanup = true;
			break;

		case 'd':
			options->test_dir = optarg;
			break;

		case 'f':
			if (optarg)
				options->file_types = optarg;
			else
				options->file_types = "sS";
			break;

		case 'h':
			print_help();
			exit(0);

		case 'n':
			options->no_native = true;
			break;

		case 'r':
			options->no_random = true;
			break;

		case 'R':
			options->no_linear = true;
			break;

		case 's':
			options->size = strtol(optarg, NULL, 10);
			break;

		case 't':
			options->tries = strtol(optarg, NULL, 10);
			break;

		default:
			print_help();
			return -EINVAL;
		}
	}

	options->size = 1L << options->size;

	return 0;
}

void shuffle(size_t *buffer, size_t size)
{
	size_t i;

	for (i = 0; i < size; ++i) {
		size_t j = random() * (size - i - 1) / RAND_MAX;
		size_t temp = buffer[i];

		buffer[i] = buffer[j];
		buffer[j] = temp;
	}
}

int get_free_memory(void)
{
	FILE *meminfo = fopen("/proc/meminfo", "re");
	char field[256];
	char value[256] = {};

	if (!meminfo)
		return -ENOENT;

	while (fscanf(meminfo, "%[^:]: %s kB\n", field, value) == 2) {
		if (!strcmp(field, "MemFree"))
			break;
		*value = 0;
	}

	fclose(meminfo);

	if (!*value)
		return -ENOENT;

	return strtol(value, NULL, 10);
}

int write_data(int cmd_fd, int dir_fd, const char *name, size_t size, int flags)
{
	int fd = openat(dir_fd, name, O_RDWR | O_CLOEXEC);
	struct incfs_permit_fill permit_fill = {
		.file_descriptor = fd,
	};
	int block_count = 1 + (size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	size_t *blocks = malloc(sizeof(size_t) * block_count);
	int error = 0;
	size_t i;
	uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE] = {};
	uint8_t compressed_data[INCFS_DATA_FILE_BLOCK_SIZE] = {};
	struct incfs_fill_block fill_block = {
		.compression = COMPRESSION_NONE,
		.data_len = sizeof(data),
		.data = ptr_to_u64(data),
	};

	if (!blocks) {
		err_msg("Out of memory");
		error = -errno;
		goto out;
	}

	if (fd == -1) {
		err_msg("Could not open file for writing %s", name);
		error = -errno;
		goto out;
	}

	if (ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill)) {
		err_msg("Failed to call PERMIT_FILL");
		error = -errno;
		goto out;
	}

	for (i = 0; i < block_count; ++i)
		blocks[i] = i;

	if (flags & SHUFFLE)
		shuffle(blocks, block_count);

	if (flags & COMPRESS) {
		size_t comp_size = LZ4_compress_default(
			(char *)data, (char *)compressed_data, sizeof(data),
			ARRAY_SIZE(compressed_data));

		if (comp_size <= 0) {
			error = -EBADMSG;
			goto out;
		}
		fill_block.compression = COMPRESSION_LZ4;
		fill_block.data = ptr_to_u64(compressed_data);
		fill_block.data_len = comp_size;
	}

	for (i = 0; i < block_count; ++i) {
		struct incfs_fill_blocks fill_blocks = {
			.count = 1,
			.fill_blocks = ptr_to_u64(&fill_block),
		};

		fill_block.block_index = blocks[i];
		int written = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);

		if (written != 1) {
			error = -errno;
			err_msg("Failed to write block %lu in file %s", i,
				name);
			break;
		}
	}

out:
	free(blocks);
	close(fd);
	sync();
	return error;
}

int measure_read_throughput_internal(const char *tag, int dir, const char *name,
				     const struct options *options, bool random)
{
	int block;

	if (random)
		printf("%32s(random)", tag);
	else
		printf("%40s", tag);

	for (block = 0; block < options->blocks; ++block) {
		size_t buffer_size;
		char *buffer;
		int try;
		double time = 0;
		double throughput;
		int memory = 0;

		buffer_size = 1 << (block + 12);
		buffer = malloc(buffer_size);

		for (try = 0; try < options->tries; ++try) {
			int err;
			struct timespec start_time, end_time;
			off_t i;
			int fd;
			size_t offsets_size = options->size / buffer_size;
			size_t *offsets =
				malloc(offsets_size * sizeof(*offsets));
			int start_memory, end_memory;

			if (!offsets) {
				err_msg("Not enough memory");
				return -ENOMEM;
			}

			for (i = 0; i < offsets_size; ++i)
				offsets[i] = i * buffer_size;

			if (random)
				shuffle(offsets, offsets_size);

			err = drop_caches();
			if (err) {
				err_msg("Failed to drop caches");
				return err;
			}

			start_memory = get_free_memory();
			if (start_memory < 0) {
				err_msg("Failed to get start memory");
				return start_memory;
			}

			fd = openat(dir, name, O_RDONLY | O_CLOEXEC);
			if (fd == -1) {
				err_msg("Failed to open file");
				return err;
			}

			err = clock_gettime(CLOCK_MONOTONIC, &start_time);
			if (err) {
				err_msg("Failed to get start time");
				return err;
			}

			for (i = 0; i < offsets_size; ++i)
				if (pread(fd, buffer, buffer_size,
					  offsets[i]) != buffer_size) {
					err_msg("Failed to read file");
					err = -errno;
					goto fail;
				}

			err = clock_gettime(CLOCK_MONOTONIC, &end_time);
			if (err) {
				err_msg("Failed to get start time");
				goto fail;
			}

			end_memory = get_free_memory();
			if (end_memory < 0) {
				err_msg("Failed to get end memory");
				return end_memory;
			}

			time += end_time.tv_sec - start_time.tv_sec;
			time += (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

			close(fd);
			fd = -1;
			memory += start_memory - end_memory;

fail:
			free(offsets);
			close(fd);
			if (err)
				return err;
		}

		throughput = options->size * options->tries / time;
		printf("%10.3e %10d", throughput, memory / options->tries);
		free(buffer);
	}

	printf("\n");
	return 0;
}

int measure_read_throughput(const char *tag, int dir, const char *name,
			    const struct options *options)
{
	int err = 0;

	if (!options->no_linear)
		err = measure_read_throughput_internal(tag, dir, name, options,
						       false);

	if (!err && !options->no_random)
		err = measure_read_throughput_internal(tag, dir, name, options,
						       true);
	return err;
}

int test_native_file(int dir, const struct options *options)
{
	const char *name = "file";
	int fd;
	char buffer[4096] = {};
	off_t i;
	int err;

	fd = openat(dir, name, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
	if (fd == -1) {
		err_msg("Could not open native file");
		return -errno;
	}

	for (i = 0; i < options->size; i += sizeof(buffer))
		if (pwrite(fd, buffer, sizeof(buffer), i) != sizeof(buffer)) {
			err_msg("Failed to write file");
			err = -errno;
			goto fail;
		}

	close(fd);
	sync();
	fd = -1;

	err = measure_read_throughput("native", dir, name, options);

fail:
	close(fd);
	return err;
}

struct hash_block {
	char data[INCFS_DATA_FILE_BLOCK_SIZE];
};

static struct hash_block *build_mtree(size_t size, char *root_hash,
				      int *mtree_block_count)
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
	struct hash_block *mtree;

	if (size == 0)
		return 0;

	block_count = 1 + (size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;
	hash_block_count = block_count;
	for (i = 0; hash_block_count > 1; i++) {
		hash_block_count = (hash_block_count + hash_per_block - 1) /
				   hash_per_block;
		tree_lvl_count[i] = hash_block_count;
		total_tree_block_count += hash_block_count;
	}
	levels_count = i;

	for (i = 0; i < levels_count; i++) {
		int prev_lvl_base = (i == 0) ? total_tree_block_count :
					       tree_lvl_index[i - 1];

		tree_lvl_index[i] = prev_lvl_base - tree_lvl_count[i];
	}

	*mtree_block_count = total_tree_block_count;
	mtree = calloc(total_tree_block_count, sizeof(*mtree));
	/* Build level 0 hashes. */
	for (i = 0; i < block_count; i++) {
		int block_index = tree_lvl_index[0] + i / hash_per_block;
		int block_off = (i % hash_per_block) * digest_size;
		char *hash_ptr = mtree[block_index].data + block_off;

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
			char *hash_ptr = mtree[block_index].data + block_off;

			sha256(mtree[i + prev_lvl_base].data,
			       INCFS_DATA_FILE_BLOCK_SIZE, hash_ptr);
		}
	}

	/* Calculate root hash from the top block */
	sha256(mtree[0].data, INCFS_DATA_FILE_BLOCK_SIZE, root_hash);

	return mtree;
}

static int load_hash_tree(int cmd_fd, int dir, const char *name,
			  struct hash_block *mtree, int mtree_block_count)
{
	int err;
	int i;
	int fd;
	struct incfs_fill_block *fill_block_array =
		calloc(mtree_block_count, sizeof(struct incfs_fill_block));
	struct incfs_fill_blocks fill_blocks = {
		.count = mtree_block_count,
		.fill_blocks = ptr_to_u64(fill_block_array),
	};
	struct incfs_permit_fill permit_fill;

	if (!fill_block_array)
		return -ENOMEM;

	for (i = 0; i < fill_blocks.count; i++) {
		fill_block_array[i] = (struct incfs_fill_block){
			.block_index = i,
			.data_len = INCFS_DATA_FILE_BLOCK_SIZE,
			.data = ptr_to_u64(mtree[i].data),
			.flags = INCFS_BLOCK_FLAGS_HASH
		};
	}

	fd = openat(dir, name, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		err = errno;
		goto failure;
	}

	permit_fill.file_descriptor = fd;
	if (ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill)) {
		err_msg("Failed to call PERMIT_FILL");
		err = -errno;
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

int test_incfs_file(int dst_dir, const struct options *options, int flags)
{
	int cmd_file = openat(dst_dir, INCFS_PENDING_READS_FILENAME,
			      O_RDONLY | O_CLOEXEC);
	int err;
	char name[4];
	incfs_uuid_t id;
	char tag[256];

	snprintf(name, sizeof(name), "%c%c%c",
		 flags & SHUFFLE ? 'S' : 's',
		 flags & COMPRESS ? 'C' : 'c',
		 flags & VERIFY ? 'V' : 'v');

	if (cmd_file == -1) {
		err_msg("Could not open command file");
		return -errno;
	}

	if (flags & VERIFY) {
		char root_hash[INCFS_MAX_HASH_SIZE];
		int mtree_block_count;
		struct hash_block *mtree = build_mtree(options->size, root_hash,
						       &mtree_block_count);

		if (!mtree) {
			err_msg("Failed to build hash tree");
			err = -ENOMEM;
			goto fail;
		}

		err = crypto_emit_file(cmd_file, NULL, name, &id, options->size,
				       root_hash, "add_data");

		if (!err)
			err = load_hash_tree(cmd_file, dst_dir, name, mtree,
					     mtree_block_count);

		free(mtree);
	} else
		err = emit_file(cmd_file, NULL, name, &id, options->size, NULL);

	if (err) {
		err_msg("Failed to create file %s", name);
		goto fail;
	}

	if (write_data(cmd_file, dst_dir, name, options->size, flags))
		goto fail;

	snprintf(tag, sizeof(tag), "incfs%s%s%s",
		 flags & SHUFFLE ? "(shuffle)" : "",
		 flags & COMPRESS ? "(compress)" : "",
		 flags & VERIFY ? "(verify)" : "");

	err = measure_read_throughput(tag, dst_dir, name, options);

fail:
	close(cmd_file);
	return err;
}

bool skip(struct options const *options, int flag, char c)
{
	if (!options->file_types)
		return false;

	if (flag && strchr(options->file_types, tolower(c)))
		return true;

	if (!flag && strchr(options->file_types, toupper(c)))
		return true;

	return false;
}

int main(int argc, char *const *argv)
{
	struct options options;
	int err;
	const char *native_dir = "native";
	const char *src_dir = "src";
	const char *dst_dir = "dst";
	int native_dir_fd = -1;
	int src_dir_fd = -1;
	int dst_dir_fd = -1;
	int block;
	int flags;

	err = parse_options(argc, argv, &options);
	if (err)
		return err;

	err = chdir(options.test_dir);
	if (err) {
		err_msg("Failed to change to %s", options.test_dir);
		return -errno;
	}

	/* Clean up any interrupted previous runs */
	while (!umount(dst_dir))
		;

	err = remove_dir(native_dir) || remove_dir(src_dir) ||
	      remove_dir(dst_dir);
	if (err)
		return err;

	err = mkdir(native_dir, 0700);
	if (err) {
		err_msg("Failed to make directory %s", src_dir);
		err = -errno;
		goto cleanup;
	}

	err = mkdir(src_dir, 0700);
	if (err) {
		err_msg("Failed to make directory %s", src_dir);
		err = -errno;
		goto cleanup;
	}

	err = mkdir(dst_dir, 0700);
	if (err) {
		err_msg("Failed to make directory %s", src_dir);
		err = -errno;
		goto cleanup;
	}

	err = mount_fs_opt(dst_dir, src_dir, "readahead=0,rlog_pages=0", 0);
	if (err) {
		err_msg("Failed to mount incfs");
		goto cleanup;
	}

	native_dir_fd = open(native_dir, O_RDONLY | O_CLOEXEC);
	src_dir_fd = open(src_dir, O_RDONLY | O_CLOEXEC);
	dst_dir_fd = open(dst_dir, O_RDONLY | O_CLOEXEC);
	if (native_dir_fd == -1 || src_dir_fd == -1 || dst_dir_fd == -1) {
		err_msg("Failed to open native, src or dst dir");
		err = -errno;
		goto cleanup;
	}

	printf("%40s", "");
	for (block = 0; block < options.blocks; ++block)
		printf("%21d", 1 << (block + 12));
	printf("\n");

	if (!err && !options.no_native)
		err = test_native_file(native_dir_fd, &options);

	for (flags = 0; flags < LAST_FLAG && !err; ++flags) {
		if (skip(&options, flags & SHUFFLE, 's') ||
		    skip(&options, flags & COMPRESS, 'c') ||
		    skip(&options, flags & VERIFY, 'v'))
			continue;
		err = test_incfs_file(dst_dir_fd, &options, flags);
	}

cleanup:
	close(native_dir_fd);
	close(src_dir_fd);
	close(dst_dir_fd);
	if (!options.no_cleanup) {
		umount(dst_dir);
		remove_dir(native_dir);
		remove_dir(dst_dir);
		remove_dir(src_dir);
	}

	return err;
}
