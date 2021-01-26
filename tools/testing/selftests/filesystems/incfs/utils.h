/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#include <stdbool.h>
#include <sys/stat.h>

#include <include/uapi/linux/incrementalfs.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define __packed __attribute__((__packed__))

#ifdef __LP64__
#define ptr_to_u64(p) ((__u64)p)
#else
#define ptr_to_u64(p) ((__u64)(__u32)p)
#endif

#define SHA256_DIGEST_SIZE 32
#define INCFS_MAX_MTREE_LEVELS 8

unsigned int rnd(unsigned int max, unsigned int *seed);

int remove_dir(const char *dir);

int drop_caches(void);

int mount_fs(const char *mount_dir, const char *backing_dir,
	     int read_timeout_ms);

int mount_fs_opt(const char *mount_dir, const char *backing_dir,
		 const char *opt, bool remount);

int get_file_bmap(int cmd_fd, int ino, unsigned char *buf, int buf_size);

int get_file_signature(int fd, unsigned char *buf, int buf_size);

int emit_node(int fd, char *filename, int *ino_out, int parent_ino,
		size_t size, mode_t mode, char *attr);

int emit_file(int fd, const char *dir, const char *filename,
	      incfs_uuid_t *id_out, size_t size, const char *attr);

int crypto_emit_file(int fd, const char *dir, const char *filename,
		     incfs_uuid_t *id_out, size_t size, const char *root_hash,
		     const char *add_data);

loff_t get_file_size(const char *name);

int open_commands_file(const char *mount_dir);

int open_log_file(const char *mount_dir);

int wait_for_pending_reads(int fd, int timeout_ms,
	struct incfs_pending_read_info *prs, int prs_count);

int wait_for_pending_reads2(int fd, int timeout_ms,
	struct incfs_pending_read_info2 *prs, int prs_count);

char *concat_file_name(const char *dir, char *file);

void sha256(const char *data, size_t dsize, char *hash);

void md5(const char *data, size_t dsize, char *hash);

int delete_dir_tree(const char *path);
