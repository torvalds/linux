// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <openssl/sha.h>
#include <openssl/md5.h>

#include "utils.h"

#ifndef __S_IFREG
#define __S_IFREG S_IFREG
#endif

int mount_fs(const char *mount_dir, const char *backing_dir,
	     int read_timeout_ms)
{
	static const char fs_name[] = INCFS_NAME;
	char mount_options[512];
	int result;

	snprintf(mount_options, ARRAY_SIZE(mount_options),
		 "read_timeout_ms=%u",
		  read_timeout_ms);

	result = mount(backing_dir, mount_dir, fs_name, 0, mount_options);
	if (result != 0)
		perror("Error mounting fs.");
	return result;
}

int mount_fs_opt(const char *mount_dir, const char *backing_dir,
		 const char *opt, bool remount)
{
	static const char fs_name[] = INCFS_NAME;
	int result;

	result = mount(backing_dir, mount_dir, fs_name,
		       remount ? MS_REMOUNT : 0, opt);
	if (result != 0)
		perror("Error mounting fs.");
	return result;
}

struct hash_section {
	uint32_t algorithm;
	uint8_t log2_blocksize;
	uint32_t salt_size;
	/* no salt */
	uint32_t hash_size;
	uint8_t hash[SHA256_DIGEST_SIZE];
} __packed;

struct signature_blob {
	uint32_t version;
	uint32_t hash_section_size;
	struct hash_section hash_section;
	uint32_t signing_section_size;
	uint8_t signing_section[];
} __packed;

size_t format_signature(void **buf, const char *root_hash, const char *add_data)
{
	size_t size = sizeof(struct signature_blob) + strlen(add_data) + 1;
	struct signature_blob *sb = malloc(size);

	*sb = (struct signature_blob){
		.version = INCFS_SIGNATURE_VERSION,
		.hash_section_size = sizeof(struct hash_section),
		.hash_section =
			(struct hash_section){
				.algorithm = INCFS_HASH_TREE_SHA256,
				.log2_blocksize = 12,
				.salt_size = 0,
				.hash_size = SHA256_DIGEST_SIZE,
			},
		.signing_section_size = sizeof(uint32_t) + strlen(add_data) + 1,
	};

	memcpy(sb->hash_section.hash, root_hash, SHA256_DIGEST_SIZE);
	memcpy((char *)sb->signing_section, add_data, strlen(add_data) + 1);
	*buf = sb;
	return size;
}

int crypto_emit_file(int fd, const char *dir, const char *filename,
		     incfs_uuid_t *id_out, size_t size, const char *root_hash,
		     const char *add_data)
{
	int mode = __S_IFREG | 0555;
	void *signature;
	int error = 0;

	struct incfs_new_file_args args = {
			.size = size,
			.mode = mode,
			.file_name = ptr_to_u64(filename),
			.directory_path = ptr_to_u64(dir),
			.file_attr = 0,
			.file_attr_len = 0
	};

	args.signature_size = format_signature(&signature, root_hash, add_data);
	args.signature_info = ptr_to_u64(signature);

	md5(filename, strlen(filename), (char *)args.file_id.bytes);

	if (ioctl(fd, INCFS_IOC_CREATE_FILE, &args) != 0) {
		error = -errno;
		goto out;
	}

	*id_out = args.file_id;

out:
	free(signature);
	return error;
}

int emit_file(int fd, const char *dir, const char *filename,
	      incfs_uuid_t *id_out, size_t size, const char *attr)
{
	int mode = __S_IFREG | 0555;
	struct incfs_new_file_args args = { .size = size,
					    .mode = mode,
					    .file_name = ptr_to_u64(filename),
					    .directory_path = ptr_to_u64(dir),
					    .signature_info = ptr_to_u64(NULL),
					    .signature_size = 0,
					    .file_attr = ptr_to_u64(attr),
					    .file_attr_len =
						    attr ? strlen(attr) : 0 };

	md5(filename, strlen(filename), (char *)args.file_id.bytes);

	if (ioctl(fd, INCFS_IOC_CREATE_FILE, &args) != 0)
		return -errno;

	*id_out = args.file_id;
	return 0;
}

int get_file_bmap(int cmd_fd, int ino, unsigned char *buf, int buf_size)
{
	return 0;
}

int get_file_signature(int fd, unsigned char *buf, int buf_size)
{
	struct incfs_get_file_sig_args args = {
		.file_signature = ptr_to_u64(buf),
		.file_signature_buf_size = buf_size
	};

	if (ioctl(fd, INCFS_IOC_READ_FILE_SIGNATURE, &args) == 0)
		return args.file_signature_len_out;
	return -errno;
}

loff_t get_file_size(const char *name)
{
	struct stat st;

	if (stat(name, &st) == 0)
		return st.st_size;
	return -ENOENT;
}

int open_commands_file(const char *mount_dir)
{
	char cmd_file[255];
	int cmd_fd;

	snprintf(cmd_file, ARRAY_SIZE(cmd_file),
			"%s/%s", mount_dir, INCFS_PENDING_READS_FILENAME);
	cmd_fd = open(cmd_file, O_RDONLY | O_CLOEXEC);

	if (cmd_fd < 0)
		perror("Can't open commands file");
	return cmd_fd;
}

int open_log_file(const char *mount_dir)
{
	char cmd_file[255];
	int cmd_fd;

	snprintf(cmd_file, ARRAY_SIZE(cmd_file), "%s/.log", mount_dir);
	cmd_fd = open(cmd_file, O_RDWR | O_CLOEXEC);
	if (cmd_fd < 0)
		perror("Can't open log file");
	return cmd_fd;
}

int wait_for_pending_reads(int fd, int timeout_ms,
	struct incfs_pending_read_info *prs, int prs_count)
{
	ssize_t read_res = 0;

	if (timeout_ms > 0) {
		int poll_res = 0;
		struct pollfd pollfd = {
			.fd = fd,
			.events = POLLIN
		};

		poll_res = poll(&pollfd, 1, timeout_ms);
		if (poll_res < 0)
			return -errno;
		if (poll_res == 0)
			return 0;
		if (!(pollfd.revents | POLLIN))
			return 0;
	}

	read_res = read(fd, prs, prs_count * sizeof(*prs));
	if (read_res < 0)
		return -errno;

	return read_res / sizeof(*prs);
}

char *concat_file_name(const char *dir, char *file)
{
	char full_name[FILENAME_MAX] = "";

	if (snprintf(full_name, ARRAY_SIZE(full_name), "%s/%s", dir, file) < 0)
		return NULL;
	return strdup(full_name);
}

int delete_dir_tree(const char *dir_path)
{
	DIR *dir = NULL;
	struct dirent *dp;
	int result = 0;

	dir = opendir(dir_path);
	if (!dir) {
		result = -errno;
		goto out;
	}

	while ((dp = readdir(dir))) {
		char *full_path;

		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

		full_path = concat_file_name(dir_path, dp->d_name);
		if (dp->d_type == DT_DIR)
			result = delete_dir_tree(full_path);
		else
			result = unlink(full_path);
		free(full_path);
		if (result)
			goto out;
	}

out:
	if (dir)
		closedir(dir);
	if (!result)
		rmdir(dir_path);
	return result;
}

void sha256(const char *data, size_t dsize, char *hash)
{
	SHA256_CTX ctx;

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, dsize);
	SHA256_Final((unsigned char *)hash, &ctx);
}

void md5(const char *data, size_t dsize, char *hash)
{
	MD5_CTX ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx, data, dsize);
	MD5_Final((unsigned char *)hash, &ctx);
}
