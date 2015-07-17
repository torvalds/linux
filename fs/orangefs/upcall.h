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
struct pvfs2_io_request_s {
	__s32 async_vfs_io;
	__s32 buf_index;
	__s32 count;
	__s32 __pad1;
	__s64 offset;
	struct pvfs2_object_kref refn;
	enum PVFS_io_type io_type;
	__s32 readahead_size;
};

struct pvfs2_iox_request_s {
	__s32 buf_index;
	__s32 count;
	struct pvfs2_object_kref refn;
	enum PVFS_io_type io_type;
	__s32 __pad1;
};

struct pvfs2_lookup_request_s {
	__s32 sym_follow;
	__s32 __pad1;
	struct pvfs2_object_kref parent_refn;
	char d_name[PVFS2_NAME_LEN];
};

struct pvfs2_create_request_s {
	struct pvfs2_object_kref parent_refn;
	struct PVFS_sys_attr_s attributes;
	char d_name[PVFS2_NAME_LEN];
};

struct pvfs2_symlink_request_s {
	struct pvfs2_object_kref parent_refn;
	struct PVFS_sys_attr_s attributes;
	char entry_name[PVFS2_NAME_LEN];
	char target[PVFS2_NAME_LEN];
};

struct pvfs2_getattr_request_s {
	struct pvfs2_object_kref refn;
	__u32 mask;
	__u32 __pad1;
};

struct pvfs2_setattr_request_s {
	struct pvfs2_object_kref refn;
	struct PVFS_sys_attr_s attributes;
};

struct pvfs2_remove_request_s {
	struct pvfs2_object_kref parent_refn;
	char d_name[PVFS2_NAME_LEN];
};

struct pvfs2_mkdir_request_s {
	struct pvfs2_object_kref parent_refn;
	struct PVFS_sys_attr_s attributes;
	char d_name[PVFS2_NAME_LEN];
};

struct pvfs2_readdir_request_s {
	struct pvfs2_object_kref refn;
	__u64 token;
	__s32 max_dirent_count;
	__s32 buf_index;
};

struct pvfs2_readdirplus_request_s {
	struct pvfs2_object_kref refn;
	__u64 token;
	__s32 max_dirent_count;
	__u32 mask;
	__s32 buf_index;
	__s32 __pad1;
};

struct pvfs2_rename_request_s {
	struct pvfs2_object_kref old_parent_refn;
	struct pvfs2_object_kref new_parent_refn;
	char d_old_name[PVFS2_NAME_LEN];
	char d_new_name[PVFS2_NAME_LEN];
};

struct pvfs2_statfs_request_s {
	__s32 fs_id;
	__s32 __pad1;
};

struct pvfs2_truncate_request_s {
	struct pvfs2_object_kref refn;
	__s64 size;
};

struct pvfs2_mmap_ra_cache_flush_request_s {
	struct pvfs2_object_kref refn;
};

struct pvfs2_fs_mount_request_s {
	char pvfs2_config_server[PVFS_MAX_SERVER_ADDR_LEN];
};

struct pvfs2_fs_umount_request_s {
	__s32 id;
	__s32 fs_id;
	char pvfs2_config_server[PVFS_MAX_SERVER_ADDR_LEN];
};

struct pvfs2_getxattr_request_s {
	struct pvfs2_object_kref refn;
	__s32 key_sz;
	__s32 __pad1;
	char key[PVFS_MAX_XATTR_NAMELEN];
};

struct pvfs2_setxattr_request_s {
	struct pvfs2_object_kref refn;
	struct PVFS_keyval_pair keyval;
	__s32 flags;
	__s32 __pad1;
};

struct pvfs2_listxattr_request_s {
	struct pvfs2_object_kref refn;
	__s32 requested_count;
	__s32 __pad1;
	__u64 token;
};

struct pvfs2_removexattr_request_s {
	struct pvfs2_object_kref refn;
	__s32 key_sz;
	__s32 __pad1;
	char key[PVFS_MAX_XATTR_NAMELEN];
};

struct pvfs2_op_cancel_s {
	__u64 op_tag;
};

struct pvfs2_fsync_request_s {
	struct pvfs2_object_kref refn;
};

enum pvfs2_param_request_type {
	PVFS2_PARAM_REQUEST_SET = 1,
	PVFS2_PARAM_REQUEST_GET = 2
};

enum pvfs2_param_request_op {
	PVFS2_PARAM_REQUEST_OP_ACACHE_TIMEOUT_MSECS = 1,
	PVFS2_PARAM_REQUEST_OP_ACACHE_HARD_LIMIT = 2,
	PVFS2_PARAM_REQUEST_OP_ACACHE_SOFT_LIMIT = 3,
	PVFS2_PARAM_REQUEST_OP_ACACHE_RECLAIM_PERCENTAGE = 4,
	PVFS2_PARAM_REQUEST_OP_PERF_TIME_INTERVAL_SECS = 5,
	PVFS2_PARAM_REQUEST_OP_PERF_HISTORY_SIZE = 6,
	PVFS2_PARAM_REQUEST_OP_PERF_RESET = 7,
	PVFS2_PARAM_REQUEST_OP_NCACHE_TIMEOUT_MSECS = 8,
	PVFS2_PARAM_REQUEST_OP_NCACHE_HARD_LIMIT = 9,
	PVFS2_PARAM_REQUEST_OP_NCACHE_SOFT_LIMIT = 10,
	PVFS2_PARAM_REQUEST_OP_NCACHE_RECLAIM_PERCENTAGE = 11,
	PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_TIMEOUT_MSECS = 12,
	PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_HARD_LIMIT = 13,
	PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_SOFT_LIMIT = 14,
	PVFS2_PARAM_REQUEST_OP_STATIC_ACACHE_RECLAIM_PERCENTAGE = 15,
	PVFS2_PARAM_REQUEST_OP_CLIENT_DEBUG = 16,
	PVFS2_PARAM_REQUEST_OP_CCACHE_TIMEOUT_SECS = 17,
	PVFS2_PARAM_REQUEST_OP_CCACHE_HARD_LIMIT = 18,
	PVFS2_PARAM_REQUEST_OP_CCACHE_SOFT_LIMIT = 19,
	PVFS2_PARAM_REQUEST_OP_CCACHE_RECLAIM_PERCENTAGE = 20,
	PVFS2_PARAM_REQUEST_OP_CAPCACHE_TIMEOUT_SECS = 21,
	PVFS2_PARAM_REQUEST_OP_CAPCACHE_HARD_LIMIT = 22,
	PVFS2_PARAM_REQUEST_OP_CAPCACHE_SOFT_LIMIT = 23,
	PVFS2_PARAM_REQUEST_OP_CAPCACHE_RECLAIM_PERCENTAGE = 24,
	PVFS2_PARAM_REQUEST_OP_TWO_MASK_VALUES = 25,
};

struct pvfs2_param_request_s {
	enum pvfs2_param_request_type type;
	enum pvfs2_param_request_op op;
	__s64 value;
	char s_value[PVFS2_MAX_DEBUG_STRING_LEN];
};

enum pvfs2_perf_count_request_type {
	PVFS2_PERF_COUNT_REQUEST_ACACHE = 1,
	PVFS2_PERF_COUNT_REQUEST_NCACHE = 2,
	PVFS2_PERF_COUNT_REQUEST_CAPCACHE = 3,
};

struct pvfs2_perf_count_request_s {
	enum pvfs2_perf_count_request_type type;
	__s32 __pad1;
};

struct pvfs2_fs_key_request_s {
	__s32 fsid;
	__s32 __pad1;
};

struct pvfs2_upcall_s {
	__s32 type;
	__u32 uid;
	__u32 gid;
	int pid;
	int tgid;
	/* currently trailer is used only by readx/writex (iox) */
	__s64 trailer_size;
	char *trailer_buf;

	union {
		struct pvfs2_io_request_s io;
		struct pvfs2_iox_request_s iox;
		struct pvfs2_lookup_request_s lookup;
		struct pvfs2_create_request_s create;
		struct pvfs2_symlink_request_s sym;
		struct pvfs2_getattr_request_s getattr;
		struct pvfs2_setattr_request_s setattr;
		struct pvfs2_remove_request_s remove;
		struct pvfs2_mkdir_request_s mkdir;
		struct pvfs2_readdir_request_s readdir;
		struct pvfs2_readdirplus_request_s readdirplus;
		struct pvfs2_rename_request_s rename;
		struct pvfs2_statfs_request_s statfs;
		struct pvfs2_truncate_request_s truncate;
		struct pvfs2_mmap_ra_cache_flush_request_s ra_cache_flush;
		struct pvfs2_fs_mount_request_s fs_mount;
		struct pvfs2_fs_umount_request_s fs_umount;
		struct pvfs2_getxattr_request_s getxattr;
		struct pvfs2_setxattr_request_s setxattr;
		struct pvfs2_listxattr_request_s listxattr;
		struct pvfs2_removexattr_request_s removexattr;
		struct pvfs2_op_cancel_s cancel;
		struct pvfs2_fsync_request_s fsync;
		struct pvfs2_param_request_s param;
		struct pvfs2_perf_count_request_s perf_count;
		struct pvfs2_fs_key_request_s fs_key;
	} req;
};

#endif /* __UPCALL_H */
