/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Google LLC
 */

#ifndef TEST_FUSE__H
#define TEST_FUSE__H

#define _GNU_SOURCE

#include "test_framework.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>

#include <include/uapi/linux/android_fuse.h>
#include <include/uapi/linux/fuse.h>

#define PAGE_SIZE 4096
#define FUSE_POSTFILTER 0x20000

extern struct _test_options test_options;

/* Slow but semantically easy string functions */

/*
 * struct s just wraps a char pointer
 * It is a pointer to a malloc'd string, or null
 * All consumers handle null input correctly
 * All consumers free the string
 */
struct s {
	char *s;
};

struct s s(const char *s1);
struct s sn(const char *s1, const char *s2);
int s_cmp(struct s s1, struct s s2);
struct s s_cat(struct s s1, struct s s2);
struct s s_splitleft(struct s s1, char c);
struct s s_splitright(struct s s1, char c);
struct s s_word(struct s s1, char c, size_t n);
struct s s_path(struct s s1, struct s s2);
struct s s_pathn(size_t n, struct s s1, ...);
int s_link(struct s src_pathname, struct s dst_pathname);
int s_symlink(struct s src_pathname, struct s dst_pathname);
int s_mkdir(struct s pathname, mode_t mode);
int s_rmdir(struct s pathname);
int s_unlink(struct s pathname);
int s_open(struct s pathname, int flags, ...);
int s_openat(int dirfd, struct s pathname, int flags, ...);
int s_creat(struct s pathname, mode_t mode);
int s_mkfifo(struct s pathname, mode_t mode);
int s_stat(struct s pathname, struct stat *st);
int s_statfs(struct s pathname, struct statfs *st);
int s_fuse_attr(struct s pathname, struct fuse_attr *fuse_attr_out);
DIR *s_opendir(struct s pathname);
int s_getxattr(struct s pathname, const char name[], void *value, size_t size,
	       ssize_t *ret_size);
int s_listxattr(struct s pathname, void *list, size_t size, ssize_t *ret_size);
int s_setxattr(struct s pathname, const char name[], const void *value,
	       size_t size, int flags);
int s_removexattr(struct s pathname, const char name[]);
int s_rename(struct s oldpathname, struct s newpathname);

struct s tracing_folder(void);
int tracing_on(void);

char *concat_file_name(const char *dir, const char *file);
char *setup_mount_dir(const char *name);
int delete_dir_tree(const char *dir_path, bool remove_root);

#define TESTFUSEINNULL(_opcode)						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTEQUAL(in_header->opcode, _opcode);			\
		TESTEQUAL(res, sizeof(*in_header));			\
	} while (false)

#define TESTFUSEIN(_opcode, in_struct)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTEQUAL(in_header->opcode, _opcode);			\
		TESTEQUAL(res, sizeof(*in_header) + sizeof(*in_struct));\
	} while (false)

#define TESTFUSEIN2(_opcode, in_struct1, in_struct2)			\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTEQUAL(in_header->opcode, _opcode);			\
		TESTEQUAL(res, sizeof(*in_header) + sizeof(*in_struct1) \
						+ sizeof(*in_struct2)); \
		in_struct1 = (void *)(bytes_in + sizeof(*in_header));	\
		in_struct2 = (void *)(bytes_in + sizeof(*in_header)	\
				      + sizeof(*in_struct1));		\
	} while (false)

#define TESTFUSEINEXT(_opcode, in_struct, extra)			\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTEQUAL(in_header->opcode, _opcode);			\
		TESTEQUAL(res,						\
		       sizeof(*in_header) + sizeof(*in_struct) + extra);\
	} while (false)

#define TESTFUSEINUNKNOWN()						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		ssize_t res = read(fuse_dev, &bytes_in,			\
			sizeof(bytes_in));				\
									\
		TESTGE(res, sizeof(*in_header));			\
		TESTEQUAL(in_header->opcode, -1);			\
	} while (false)

/* Special case lookup since it is asymmetric */
#define TESTFUSELOOKUP(expected, filter)				\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		char *name = (char *) (bytes_in + sizeof(*in_header));	\
		ssize_t res;						\
									\
		TEST(res = read(fuse_dev, &bytes_in, sizeof(bytes_in)),	\
			  res != -1);					\
		/* TODO once we handle forgets properly, remove */	\
		if (in_header->opcode == FUSE_FORGET)			\
			continue;					\
		if (in_header->opcode == FUSE_BATCH_FORGET)		\
			continue;					\
		TESTGE(res, sizeof(*in_header));			\
		TESTEQUAL(in_header->opcode,				\
			FUSE_LOOKUP | filter);				\
		TESTEQUAL(res,						\
			  sizeof(*in_header) + strlen(expected) + 1 +	\
				(filter == FUSE_POSTFILTER ?		\
				sizeof(struct fuse_entry_out) +		\
				sizeof(struct fuse_entry_bpf_out) : 0));\
		TESTCOND(!strcmp(name, expected));			\
		break;							\
	} while (true)

#define TESTFUSEOUTEMPTY()						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header),			\
			.unique = in_header->unique,			\
		};							\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while (false)

#define TESTFUSEOUTERROR(errno)						\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header),			\
			.error = errno,					\
			.unique = in_header->unique,			\
		};							\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while (false)

#define TESTFUSEOUTREAD(data, length)					\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header) + length,		\
			.unique = in_header->unique,			\
		};							\
		memcpy(bytes_out + sizeof(*out_header), data, length);	\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while (false)

#define TESTFUSEDIROUTREAD(read_out, data, length)			\
	do {								\
		struct fuse_in_header *in_header =			\
				(struct fuse_in_header *)bytes_in;	\
		struct fuse_out_header *out_header =			\
			(struct fuse_out_header *)bytes_out;		\
									\
		*out_header = (struct fuse_out_header) {		\
			.len = sizeof(*out_header) +			\
			       sizeof(*read_out) + length,		\
			.unique = in_header->unique,			\
		};							\
		memcpy(bytes_out + sizeof(*out_header) +		\
				sizeof(*read_out), data, length);	\
		memcpy(bytes_out + sizeof(*out_header),			\
				read_out, sizeof(*read_out));		\
		TESTEQUAL(write(fuse_dev, bytes_out, out_header->len),	\
			  out_header->len);				\
	} while (false)

#define TESTFUSEOUT1(type1, obj1)					\
	do {								\
		*(struct fuse_out_header *) bytes_out			\
			= (struct fuse_out_header) {			\
			.len = sizeof(struct fuse_out_header)		\
				+ sizeof(struct type1),			\
			.unique = ((struct fuse_in_header *)		\
				   bytes_in)->unique,			\
		};							\
		*(struct type1 *) (bytes_out				\
			+ sizeof(struct fuse_out_header))		\
			= obj1;						\
		TESTEQUAL(write(fuse_dev, bytes_out,			\
			((struct fuse_out_header *)bytes_out)->len),	\
			((struct fuse_out_header *)bytes_out)->len);	\
	} while (false)

#define TESTFUSEOUT2(type1, obj1, type2, obj2)				\
	do {								\
		*(struct fuse_out_header *) bytes_out			\
			= (struct fuse_out_header) {			\
			.len = sizeof(struct fuse_out_header)		\
				+ sizeof(struct type1)			\
				+ sizeof(struct type2),			\
			.unique = ((struct fuse_in_header *)		\
				   bytes_in)->unique,			\
		};							\
		*(struct type1 *) (bytes_out				\
			+ sizeof(struct fuse_out_header))		\
			= obj1;						\
		*(struct type2 *) (bytes_out				\
			+ sizeof(struct fuse_out_header)		\
			+ sizeof(struct type1))				\
			= obj2;						\
		TESTEQUAL(write(fuse_dev, bytes_out,			\
			((struct fuse_out_header *)bytes_out)->len),	\
			((struct fuse_out_header *)bytes_out)->len);	\
	} while (false)

#define TESTFUSEINITFLAGS(fuse_connection_flags)			\
	do {								\
		DECL_FUSE_IN(init);					\
									\
		TESTFUSEIN(FUSE_INIT, init_in);				\
		TESTEQUAL(init_in->major, FUSE_KERNEL_VERSION);		\
		TESTEQUAL(init_in->minor, FUSE_KERNEL_MINOR_VERSION);	\
		TESTFUSEOUT1(fuse_init_out, ((struct fuse_init_out) {	\
			.major = FUSE_KERNEL_VERSION,			\
			.minor = FUSE_KERNEL_MINOR_VERSION,		\
			.max_readahead = 4096,				\
			.flags = fuse_connection_flags,			\
			.max_background = 0,				\
			.congestion_threshold = 0,			\
			.max_write = 4096,				\
			.time_gran = 1000,				\
			.max_pages = 12,				\
			.map_alignment = 4096,				\
		}));							\
	} while (false)

#define TESTFUSEINIT()							\
	TESTFUSEINITFLAGS(0)

#define DECL_FUSE_IN(name)						\
	struct fuse_##name##_in *name##_in =				\
		(struct fuse_##name##_in *)				\
		(bytes_in + sizeof(struct fuse_in_header))

#define DECL_FUSE(name)							\
	struct fuse_##name##_in *name##_in __maybe_unused;		\
	struct fuse_##name##_out *name##_out __maybe_unused

#define FUSE_DECLARE_DAEMON						\
	int daemon = -1;						\
	int status;							\
	bool action;							\
	uint8_t bytes_in[FUSE_MIN_READ_BUFFER] __maybe_unused;		\
	uint8_t bytes_out[FUSE_MIN_READ_BUFFER]	__maybe_unused

#define FUSE_START_DAEMON()						\
	do {								\
		TEST(daemon = fork(), daemon != -1);			\
		action = daemon != 0;					\
	} while (false)

#define FUSE_END_DAEMON()						\
	do {								\
		TESTEQUAL(waitpid(daemon, &status, 0), daemon);		\
		TESTEQUAL(status, TEST_SUCCESS);			\
		result = TEST_SUCCESS;					\
out:									\
		if (!daemon)						\
			exit(TEST_FAILURE);				\
	} while (false)


struct map_relocation {
	char *name;
	int fd;
	int value;
};

int mount_fuse(const char *mount_dir, int bpf_fd, int dir_fd,
	       int *fuse_dev_ptr);
int mount_fuse_no_init(const char *mount_dir, int bpf_fd, int dir_fd,
	       int *fuse_dev_ptr);
int install_elf_bpf(const char *file, const char *section, int *fd,
		    struct map_relocation **map_relocations, size_t *map_count);
#endif
