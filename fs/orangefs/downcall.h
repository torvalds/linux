/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Definitions of downcalls used in Linux kernel module.
 */

#ifndef __DOWNCALL_H
#define __DOWNCALL_H

/*
 * Sanitized the device-client core interaction
 * for clean 32-64 bit usage
 */
struct orangefs_io_response {
	__s64 amt_complete;
};

struct orangefs_lookup_response {
	struct orangefs_object_kref refn;
};

struct orangefs_create_response {
	struct orangefs_object_kref refn;
};

struct orangefs_symlink_response {
	struct orangefs_object_kref refn;
};

struct orangefs_getattr_response {
	struct ORANGEFS_sys_attr_s attributes;
	char link_target[ORANGEFS_NAME_MAX];
};

struct orangefs_mkdir_response {
	struct orangefs_object_kref refn;
};

/*
 * duplication of some system interface structures so that I don't have
 * to allocate extra memory
 */
struct orangefs_dirent {
	char *d_name;
	int d_length;
	struct orangefs_khandle khandle;
};

struct orangefs_statfs_response {
	__s64 block_size;
	__s64 blocks_total;
	__s64 blocks_avail;
	__s64 files_total;
	__s64 files_avail;
};

struct orangefs_fs_mount_response {
	__s32 fs_id;
	__s32 id;
	struct orangefs_khandle root_khandle;
};

/* the getxattr response is the attribute value */
struct orangefs_getxattr_response {
	__s32 val_sz;
	__s32 __pad1;
	char val[ORANGEFS_MAX_XATTR_VALUELEN];
};

/* the listxattr response is an array of attribute names */
struct orangefs_listxattr_response {
	__s32 returned_count;
	__s32 __pad1;
	__u64 token;
	char key[ORANGEFS_MAX_XATTR_LISTLEN * ORANGEFS_MAX_XATTR_NAMELEN];
	__s32 keylen;
	__s32 __pad2;
	__s32 lengths[ORANGEFS_MAX_XATTR_LISTLEN];
};

struct orangefs_param_response {
	union {
		__s64 value64;
		__s32 value32[2];
	} u;
};

#define PERF_COUNT_BUF_SIZE 4096
struct orangefs_perf_count_response {
	char buffer[PERF_COUNT_BUF_SIZE];
};

#define FS_KEY_BUF_SIZE 4096
struct orangefs_fs_key_response {
	__s32 fs_keylen;
	__s32 __pad1;
	char fs_key[FS_KEY_BUF_SIZE];
};

/* 2.9.6 */
struct orangefs_features_response {
	__u64 features;
};

struct orangefs_downcall_s {
	__s32 type;
	__s32 status;
	/* currently trailer is used only by readdir */
	__s64 trailer_size;
	char *trailer_buf;

	union {
		struct orangefs_io_response io;
		struct orangefs_lookup_response lookup;
		struct orangefs_create_response create;
		struct orangefs_symlink_response sym;
		struct orangefs_getattr_response getattr;
		struct orangefs_mkdir_response mkdir;
		struct orangefs_statfs_response statfs;
		struct orangefs_fs_mount_response fs_mount;
		struct orangefs_getxattr_response getxattr;
		struct orangefs_listxattr_response listxattr;
		struct orangefs_param_response param;
		struct orangefs_perf_count_response perf_count;
		struct orangefs_fs_key_response fs_key;
		struct orangefs_features_response features;
	} resp;
};

struct orangefs_readdir_response_s {
	__u64 token;
	__u64 directory_version;
	__u32 __pad2;
	__u32 orangefs_dirent_outcount;
	struct orangefs_dirent *dirent_array;
};

#endif /* __DOWNCALL_H */
