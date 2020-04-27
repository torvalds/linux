// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

#define err_msg(...)                                                           \
	do {                                                                   \
		fprintf(stderr, "%s: (%d) ", TAG, __LINE__);                   \
		fprintf(stderr, __VA_ARGS__);                                  \
		fprintf(stderr, " (%s)\n", strerror(errno));                   \
	} while (false)

#define TAG "incfs_stress"

struct options {
	bool no_cleanup; /* -c */
	const char *test_dir; /* -d */
	unsigned int rng_seed; /* -g */
	int num_reads; /* -n */
	int readers; /* -r */
	int size; /* -s */
	int timeout; /* -t */
};

struct read_data {
	const char *filename;
	int dir_fd;
	size_t filesize;
	int num_reads;
	unsigned int rng_seed;
};

int cancel_threads;

int parse_options(int argc, char *const *argv, struct options *options)
{
	char c;

	/* Set defaults here */
	*options = (struct options){
		.test_dir = ".",
		.num_reads = 1000,
		.readers = 10,
		.size = 10,
	};

	/* Load options from command line here */
	while ((c = getopt(argc, argv, "cd:g:n:r:s:t:")) != -1) {
		switch (c) {
		case 'c':
			options->no_cleanup = true;
			break;

		case 'd':
			options->test_dir = optarg;
			break;

		case 'g':
			options->rng_seed = atoi(optarg);
			break;

		case 'n':
			options->num_reads = atoi(optarg);
			break;

		case 'r':
			options->readers = atoi(optarg);
			break;

		case 's':
			options->size = atoi(optarg);
			break;

		case 't':
			options->timeout = atoi(optarg);
			break;
		}
	}

	return 0;
}

unsigned int rnd(unsigned int max, unsigned int *seed)
{
	return rand_r(seed) * ((uint64_t)max + 1) / RAND_MAX;
}

int remove_dir(const char *dir)
{
	int err = rmdir(dir);

	if (err && errno == ENOTEMPTY) {
		err = delete_dir_tree(dir);
		if (err) {
			err_msg("Can't delete dir %s", dir);
			return err;
		}

		return 0;
	}

	if (err && errno != ENOENT) {
		err_msg("Can't delete dir %s", dir);
		return -errno;
	}

	return 0;
}

void *reader(void *data)
{
	struct read_data *read_data = (struct read_data *)data;
	int i;
	int fd = -1;
	void *buffer = malloc(read_data->filesize);

	if (!buffer) {
		err_msg("Failed to alloc read buffer");
		goto out;
	}

	fd = openat(read_data->dir_fd, read_data->filename,
		    O_RDONLY | O_CLOEXEC);
	if (fd == -1) {
		err_msg("Failed to open file");
		goto out;
	}

	for (i = 0; i < read_data->num_reads && !cancel_threads; ++i) {
		off_t offset = rnd(read_data->filesize, &read_data->rng_seed);
		size_t count =
			rnd(read_data->filesize - offset, &read_data->rng_seed);
		ssize_t err = pread(fd, buffer, count, offset);

		if (err != count)
			err_msg("failed to read with value %lu", err);
	}

out:
	close(fd);
	free(read_data);
	free(buffer);
	return NULL;
}

int write_data(int cmd_fd, int dir_fd, const char *name, size_t size)
{
	int fd = openat(dir_fd, name, O_RDWR | O_CLOEXEC);
	struct incfs_permit_fill permit_fill = {
		.file_descriptor = fd,
	};
	int error = 0;
	int i;
	int block_count = 1 + (size - 1) / INCFS_DATA_FILE_BLOCK_SIZE;

	if (fd == -1) {
		err_msg("Could not open file for writing %s", name);
		return -errno;
	}

	if (ioctl(cmd_fd, INCFS_IOC_PERMIT_FILL, &permit_fill)) {
		err_msg("Failed to call PERMIT_FILL");
		error = -errno;
		goto out;
	}

	for (i = 0; i < block_count; ++i) {
		uint8_t data[INCFS_DATA_FILE_BLOCK_SIZE] = {};
		size_t block_size =
			size > i * INCFS_DATA_FILE_BLOCK_SIZE ?
				INCFS_DATA_FILE_BLOCK_SIZE :
				size - (i * INCFS_DATA_FILE_BLOCK_SIZE);
		struct incfs_fill_block fill_block = {
			.compression = COMPRESSION_NONE,
			.block_index = i,
			.data_len = block_size,
			.data = ptr_to_u64(data),
		};
		struct incfs_fill_blocks fill_blocks = {
			.count = 1,
			.fill_blocks = ptr_to_u64(&fill_block),
		};
		int written = ioctl(fd, INCFS_IOC_FILL_BLOCKS, &fill_blocks);

		if (written != 1) {
			error = -errno;
			err_msg("Failed to write block %d in file %s", i, name);
			break;
		}
	}
out:
	close(fd);
	return error;
}

int test_files(int src_dir, int dst_dir, struct options const *options)
{
	unsigned int seed = options->rng_seed;
	int cmd_file = openat(dst_dir, INCFS_PENDING_READS_FILENAME,
			      O_RDONLY | O_CLOEXEC);
	int err;
	const char *name = "001";
	incfs_uuid_t id;
	size_t size;
	int i;
	pthread_t *threads = NULL;

	size = 1 << (rnd(options->size, &seed) + 12);
	size += rnd(size, &seed);

	if (cmd_file == -1) {
		err_msg("Could not open command file");
		return -errno;
	}

	err = emit_file(cmd_file, NULL, name, &id, size, NULL);
	if (err) {
		err_msg("Failed to create file %s", name);
		return err;
	}

	threads = malloc(sizeof(pthread_t) * options->readers);
	if (!threads) {
		err_msg("Could not allocate memory for threads");
		return -ENOMEM;
	}

	for (i = 0; i < options->readers; ++i) {
		struct read_data *read_data = malloc(sizeof(*read_data));

		if (!read_data) {
			err_msg("Failed to allocate read_data");
			err = -ENOMEM;
			break;
		}

		*read_data = (struct read_data){
			.filename = name,
			.dir_fd = dst_dir,
			.filesize = size,
			.num_reads = options->num_reads,
			.rng_seed = seed,
		};

		rnd(0, &seed);

		err = pthread_create(threads + i, 0, reader, read_data);
		if (err) {
			err_msg("Failed to create thread");
			free(read_data);
			break;
		}
	}

	if (err)
		cancel_threads = 1;
	else
		err = write_data(cmd_file, dst_dir, name, size);

	for (; i > 0; --i) {
		if (pthread_join(threads[i - 1], NULL)) {
			err_msg("FATAL: failed to join thread");
			exit(-errno);
		}
	}

	free(threads);
	close(cmd_file);
	return err;
}

int main(int argc, char *const *argv)
{
	struct options options;
	int err;
	const char *src_dir = "src";
	const char *dst_dir = "dst";
	int src_dir_fd = -1;
	int dst_dir_fd = -1;

	err = parse_options(argc, argv, &options);
	if (err)
		return err;

	err = chdir(options.test_dir);
	if (err) {
		err_msg("Failed to change to %s", options.test_dir);
		return -errno;
	}

	err = remove_dir(src_dir) || remove_dir(dst_dir);
	if (err)
		return err;

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

	err = mount_fs(dst_dir, src_dir, options.timeout);
	if (err) {
		err_msg("Failed to mount incfs");
		goto cleanup;
	}

	src_dir_fd = open(src_dir, O_RDONLY | O_CLOEXEC);
	dst_dir_fd = open(dst_dir, O_RDONLY | O_CLOEXEC);
	if (src_dir_fd == -1 || dst_dir_fd == -1) {
		err_msg("Failed to open src or dst dir");
		err = -errno;
		goto cleanup;
	}

	err = test_files(src_dir_fd, dst_dir_fd, &options);

cleanup:
	close(src_dir_fd);
	close(dst_dir_fd);
	if (!options.no_cleanup) {
		umount(dst_dir);
		remove_dir(dst_dir);
		remove_dir(src_dir);
	}

	return err;
}
