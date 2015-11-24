/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __UPCALL_H
#define __UPCALL_H

/*
 * Sanitized this header file to fix
 * 32-64 bit interaction issues between
 * client-core and device
 */
struct orangefs_io_request_s {
	__s32 async_vfs_io;
	__s32 buf_index;
	__s32 count;
	__s32 __pad1;
	__s64 offset;
	struct orangefs_object_kref refn;
	enum ORANGEFS_io_type io_type;
	__s32 readahead_size;
};

struct orangefs_lookup_request_s {
	__s32 sym_follow;
	__s32 __pad1;
	struct orangefs_object_kref parent_refn;
	char d_name[ORANGEFS_NAME_LEN];
};

struct orangefs_create_request_s {
	struct orangefs_object_kref parent_refn;
	struct ORANGEFS_sys_attr_s attributes;
	char d_name[ORANGEFS_NAME_LEN];
};

struct orangefs_symlink_request_s {
	struct orangefs_object_kref parent_refn;
	struct ORANGEFS_sys_attr_s attributes;
	char entry_name[ORANGEFS_NAME_LEN];
	char target[ORANGEFS_NAME_LEN];
};

struct orangefs_getattr_request_s {
	struct orangefs_object_kref refn;
	__u32 mask;
	__u32 __pad1;
};

struct orangefs_setattr_request_s {
	struct orangefs_object_kref refn;
	struct ORANGEFS_sys_attr_s attributes;
};

struct orangefs_remove_request_s {
	struct orangefs_object_kref parent_refn;
	char d_name[ORANGEFS_NAME_LEN];
};

struct orangefs_mkdir_request_s {
	struct orangefs_object_kref parent_refn;
	struct ORANGEFS_sys_attr_s attributes;
	char d_name[ORANGEFS_NAME_LEN];
};

struct orangefs_readdir_request_s {
	struct orangefs_object_kref refn;
	__u64 token;
	__s32 max_dirent_count;
	__s32 buf_index;
};

struct orangefs_readdirplus_request_s {
	struct orangefs_object_kref refn;
	__u64 token;
	__s32 max_dirent_count;
	__u32 mask;
	__s32 buf_index;
	__s32 __pad1;
};

struct orangefs_rename_request_s {
	struct orangefs_object_kref old_parent_refn;
	struct orangefs_object_kref new_parent_refn;
	char d_old_name[ORANGEFS_NAME_LEN];
	char d_new_name[ORANGEFS_NAME_LEN];
};

struct orangefs_statfs_request_s {
	__s32 fs_id;
	__s32 __pad1;
};

struct orangefs_truncate_request_s {
	struct orangefs_object_kref refn;
	__s64 size;
};

struct orangefs_mmap_ra_cache_flush_request_s {
	struct orangefs_object_kref refn;
};

struct orangefs_fs_mount_request_s {
	char orangefs_config_server[ORANGEFS_MAX_SERVER_ADDR_LEN];
};

struct orangefs_fs_umount_request_s {
	__s32 id;
	__s32 fs_id;
	char orangefs_config_server[ORANGEFS_MAX_SERVER_ADDR_LEN];
};

struct orangefs_getxattr_request_s {
	struct orangefs_object_kref refn;
	__s32 key_sz;
	__s32 __pad1;
	char key[ORANGEFS_MAX_XATTR_NAMELEN];
};

struct orangefs_setxattr_request_s {
	struct orangefs_object_kref refn;
	struct ORANGEFS_keyval_pair keyval;
	__s32 flags;
	__s32 __pad1;
};

struct orangefs_listxattr_request_s {
	struct orangefs_object_kref refn;
	__s32 requested_count;
	__s32 __pad1;
	__u64 token;
};

struct orangefs_removexattr_request_s {
	struct orangefs_object_kref refn;
	__s32 key_sz;
	__s32 __pad1;
	char key[ORANGEFS_MAX_XATTR_NAMELEN];
};

struct orangefs_op_cancel_s {
	__u64 op_tag;
};

struct orangefs_fsync_request_s {
	struct orangefs_object_kref refn;
};

enum orangefs_param_request_type {
	ORANGEFS_PARAM_REQUEST_SET = 1,
	ORANGEFS_PARAM_REQUEST_GET = 2
};

enum orangefs_param_request_op {
	ORANGEFS_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS = 1,
	ORANGEFS_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT = 2,
	ORANGEFS_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT = 3,
	ORANGEFS_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE = 4,
	ORANGEFS_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS = 5,
	ORANGEFS_PARAM_REQUEST_OP_PERF_HISTORY_SIZE = 6,
	ORANGEFS_PARAM_REQUEST_OP_PERF_RESET = 7,
	ORANGEFS_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS = 8,
	ORANGEFS_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT = 9,
	ORANGEFS_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT = 10,
	ORANGEFS_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE = 11,
	ORANGEFS_PARAM_REQUEST_OP_STATIC_ACACHE_TIMEOUT_MSECS = 12,
	ORANGEFS_PARAM_REQUEST_OP_STATIC_ACACHE_HARD_LIMIT = 13,
	ORANGEFS_PARAM_REQUEST_OP_STATIC_ACACHE_SOFT_LIMIT = 14,
	ORANGEFS_PARAM_REQUEST_OP_STATIC_ACACHE_RECLAIM_PERCENTAGE = 15,
	ORANGEFS_PARAM_REQUEST_OP_CLIENT_DEBUG = 16,
	ORANGEFS_PARAM_REQUEST_OP_CCACHE_TIMEOUT_SECS = 17,
	ORANGEFS_PARAM_REQUEST_OP_CCACHE_HARD_LIMIT = 18,
	ORANGEFS_PARAM_REQUEST_OP_CCACHE_SOFT_LIMIT = 19,
	ORANGEFS_PARAM_REQUEST_OP_CCACHE_RECLAIM_PERCENTAGE = 20,
	ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_TIMEOUT_SECS = 21,
	ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_HARD_LIMIT = 22,
	ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_SOFT_LIMIT = 23,
	ORANGEFS_PARAM_REQUEST_OP_CAPCACHE_RECLAIM_PERCENTAGE = 24,
	ORANGEFS_PARAM_REQUEST_OP_TWO_MASK_VALUES = 25,
};

struct orangefs_param_request_s {
	enum orangefs_param_request_type type;
	enum orangefs_param_request_op op;
	__s64 value;
	char s_value[ORANGEFS_MAX_DEBUG_STRING_LEN];
};

enum orangefs_perf_count_request_type {
	ORANGEFS_PERF_COUNT_REQUEST_ACACHE = 1,
	ORANGEFS_PERF_COUNT_REQUEST_NCACHE = 2,
	ORANGEFS_PERF_COUNT_REQUEST_CAPCACHE = 3,
};

struct orangefs_perf_count_request_s {
	enum orangefs_perf_count_request_type type;
	__s32 __pad1;
};

struct orangefs_fs_key_request_s {
	__s32 fsid;
	__s32 __pad1;
};

struct orangefs_upcall_s {
	__s32 type;
	__u32 uid;
	__u32 gid;
	int pid;
	int tgid;
	/* Trailers unused but must be retained for protocol compatibility. */
	__s64 trailer_size;
	char *trailer_buf;

	union {
		struct orangefs_io_request_s io;
		struct orangefs_lookup_request_s lookup;
		struct orangefs_create_request_s create;
		struct orangefs_symlink_request_s sym;
		struct orangefs_getattr_request_s getattr;
		struct orangefs_setattr_request_s setattr;
		struct orangefs_remove_request_s remove;
		struct orangefs_mkdir_request_s mkdir;
		struct orangefs_readdir_request_s readdir;
		struct orangefs_readdirplus_request_s readdirplus;
		struct orangefs_rename_request_s rename;
		struct orangefs_statfs_request_s statfs;
		struct orangefs_truncate_request_s truncate;
		struct orangefs_mmap_ra_cache_flush_request_s ra_cache_flush;
		struct orangefs_fs_mount_request_s fs_mount;
		struct orangefs_fs_umount_request_s fs_umount;
		struct orangefs_getxattr_request_s getxattr;
		struct orangefs_setxattr_request_s setxattr;
		struct orangefs_listxattr_request_s listxattr;
		struct orangefs_removexattr_request_s removexattr;
		struct orangefs_op_cancel_s cancel;
		struct orangefs_fsync_request_s fsync;
		struct orangefs_param_request_s param;
		struct orangefs_perf_count_request_s perf_count;
		struct orangefs_fs_key_request_s fs_key;
	} req;
};

#endif /* __UPCALL_H */
