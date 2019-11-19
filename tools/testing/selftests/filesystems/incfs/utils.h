/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#include <stdbool.h>
#include <sys/stat.h>

#include "../../include/uapi/linux/incrementalfs.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#ifdef __LP64__
#define ptr_to_u64(p) ((__u64)p)
#else
#define ptr_to_u64(p) ((__u64)(__u32)p)
#endif

#define SHA256_DIGEST_SIZE 32

int mount_fs(char *mount_dir, char *backing_dir, int read_timeout_ms);

int mount_fs_opt(char *mount_dir, char *backing_dir, char *opt);

int get_file_bmap(int cmd_fd, int ino, unsigned char *buf, int buf_size);

int get_file_signature(int fd, unsigned char *buf, int buf_size);

int emit_node(int fd, char *filename, int *ino_out, int parent_ino,
		size_t size, mode_t mode, char *attr);

int emit_file(int fd, char *dir, char *filename, incfs_uuid_t *id_out,
		size_t size, char *attr);

int crypto_emit_file(int fd, char *dir, char *filename, incfs_uuid_t *id_out,
	size_t size, const char *root_hash, char *sig, size_t sig_size,
	char *add_data);

int unlink_node(int fd, int parent_ino, char *filename);

loff_t get_file_size(char *name);

int open_commands_file(char *mount_dir);

int open_log_file(char *mount_dir);

int wait_for_pending_reads(int fd, int timeout_ms,
	struct incfs_pending_read_info *prs, int prs_count);

char *concat_file_name(const char *dir, char *file);

void sha256(char *data, size_t dsize, char *hash);

void md5(char *data, size_t dsize, char *hash);

bool sign_pkcs7(const void *data_to_sign, size_t data_size,
		char *pkey_pem, char *cert_pem,
		void **sig_ret, size_t *sig_size_ret);

int delete_dir_tree(const char *path);
