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
struct pvfs2_io_response {
	__s64 amt_complete;
};

struct pvfs2_iox_response {
	__s64 amt_complete;
};

struct pvfs2_lookup_response {
	struct pvfs2_object_kref refn;
};

struct pvfs2_create_response {
	struct pvfs2_object_kref refn;
};

struct pvfs2_symlink_response {
	struct pvfs2_object_kref refn;
};

struct pvfs2_getattr_response {
	struct PVFS_sys_attr_s attributes;
	char link_target[PVFS2_NAME_LEN];
};

struct pvfs2_mkdir_response {
	struct pvfs2_object_kref refn;
};

/*
 * duplication of some system interface structures so that I don't have
 * to allocate extra memory
 */
struct pvfs2_dirent {
	char *d_name;
	int d_length;
	struct pvfs2_khandle khandle;
};

struct pvfs2_statfs_response {
	__s64 block_size;
	__s64 blocks_total;
	__s64 blocks_avail;
	__s64 files_total;
	__s64 files_avail;
};

struct pvfs2_fs_mount_response {
	__s32 fs_id;
	__s32 id;
	struct pvfs2_khandle root_khandle;
};

/* the getxattr response is the attribute value */
struct pvfs2_getxattr_response {
	__s32 val_sz;
	__s32 __pad1;
	char val[PVFS_MAX_XATTR_VALUELEN];
};

/* the listxattr response is an array of attribute names */
struct pvfs2_listxattr_response {
	__s32 returned_count;
	__s32 __pad1;
	__u64 token;
	char key[PVFS_MAX_XATTR_LISTLEN * PVFS_MAX_XATTR_NAMELEN];
	__s32 keylen;
	__s32 __pad2;
	__s32 lengths[PVFS_MAX_XATTR_LISTLEN];
};

struct pvfs2_param_response {
	__s64 value;
};

#define PERF_COUNT_BUF_SIZE 4096
struct pvfs2_perf_count_response {
	char buffer[PERF_COUNT_BUF_SIZE];
};

#define FS_KEY_BUF_SIZE 4096
struct pvfs2_fs_key_response {
	__s32 fs_keylen;
	__s32 __pad1;
	char fs_key[FS_KEY_BUF_SIZE];
};

struct pvfs2_downcall_s {
	__s32 type;
	__s32 status;
	/* currently trailer is used only by readdir */
	__s64 trailer_size;
	char * trailer_buf;

	union {
		struct pvfs2_io_response io;
		struct pvfs2_iox_response iox;
		struct pvfs2_lookup_response lookup;
		struct pvfs2_create_response create;
		struct pvfs2_symlink_response sym;
		struct pvfs2_getattr_response getattr;
		struct pvfs2_mkdir_response mkdir;
		struct pvfs2_statfs_response statfs;
		struct pvfs2_fs_mount_response fs_mount;
		struct pvfs2_getxattr_response getxattr;
		struct pvfs2_listxattr_response listxattr;
		struct pvfs2_param_response param;
		struct pvfs2_perf_count_response perf_count;
		struct pvfs2_fs_key_response fs_key;
	} resp;
};

struct pvfs2_readdir_response_s {
	__u64 token;
	__u64 directory_version;
	__u32 __pad2;
	__u32 pvfs_dirent_outcount;
	struct pvfs2_dirent *dirent_array;
};

#endif /* __DOWNCALL_H */
