/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_NSFS_H
#define __LINUX_NSFS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define NSIO	0xb7

/* Returns a file descriptor that refers to an owning user namespace */
#define NS_GET_USERNS		_IO(NSIO, 0x1)
/* Returns a file descriptor that refers to a parent namespace */
#define NS_GET_PARENT		_IO(NSIO, 0x2)
/* Returns the type of namespace (CLONE_NEW* value) referred to by
   file descriptor */
#define NS_GET_NSTYPE		_IO(NSIO, 0x3)
/* Get owner UID (in the caller's user namespace) for a user namespace */
#define NS_GET_OWNER_UID	_IO(NSIO, 0x4)
/* Translate pid from target pid namespace into the caller's pid namespace. */
#define NS_GET_PID_FROM_PIDNS	_IOR(NSIO, 0x6, int)
/* Return thread-group leader id of pid in the callers pid namespace. */
#define NS_GET_TGID_FROM_PIDNS	_IOR(NSIO, 0x7, int)
/* Translate pid from caller's pid namespace into a target pid namespace. */
#define NS_GET_PID_IN_PIDNS	_IOR(NSIO, 0x8, int)
/* Return thread-group leader id of pid in the target pid namespace. */
#define NS_GET_TGID_IN_PIDNS	_IOR(NSIO, 0x9, int)

struct mnt_ns_info {
	__u32 size;
	__u32 nr_mounts;
	__u64 mnt_ns_id;
};

#define MNT_NS_INFO_SIZE_VER0 16 /* size of first published struct */

/* Get information about namespace. */
#define NS_MNT_GET_INFO		_IOR(NSIO, 10, struct mnt_ns_info)
/* Get next namespace. */
#define NS_MNT_GET_NEXT		_IOR(NSIO, 11, struct mnt_ns_info)
/* Get previous namespace. */
#define NS_MNT_GET_PREV		_IOR(NSIO, 12, struct mnt_ns_info)

/* Retrieve namespace identifiers. */
#define NS_GET_MNTNS_ID		_IOR(NSIO, 5,  __u64)
#define NS_GET_ID		_IOR(NSIO, 13, __u64)

enum init_ns_ino {
	IPC_NS_INIT_INO		= 0xEFFFFFFFU,
	UTS_NS_INIT_INO		= 0xEFFFFFFEU,
	USER_NS_INIT_INO	= 0xEFFFFFFDU,
	PID_NS_INIT_INO		= 0xEFFFFFFCU,
	CGROUP_NS_INIT_INO	= 0xEFFFFFFBU,
	TIME_NS_INIT_INO	= 0xEFFFFFFAU,
	NET_NS_INIT_INO		= 0xEFFFFFF9U,
	MNT_NS_INIT_INO		= 0xEFFFFFF8U,
#ifdef __KERNEL__
	MNT_NS_ANON_INO		= 0xEFFFFFF7U,
#endif
};

struct nsfs_file_handle {
	__u64 ns_id;
	__u32 ns_type;
	__u32 ns_inum;
};

#define NSFS_FILE_HANDLE_SIZE_VER0 16 /* sizeof first published struct */
#define NSFS_FILE_HANDLE_SIZE_LATEST sizeof(struct nsfs_file_handle) /* sizeof latest published struct */

enum init_ns_id {
	IPC_NS_INIT_ID		= 1ULL,
	UTS_NS_INIT_ID		= 2ULL,
	USER_NS_INIT_ID		= 3ULL,
	PID_NS_INIT_ID		= 4ULL,
	CGROUP_NS_INIT_ID	= 5ULL,
	TIME_NS_INIT_ID		= 6ULL,
	NET_NS_INIT_ID		= 7ULL,
	MNT_NS_INIT_ID		= 8ULL,
#ifdef __KERNEL__
	NS_LAST_INIT_ID		= MNT_NS_INIT_ID,
#endif
};

enum ns_type {
	TIME_NS    = (1ULL << 7),  /* CLONE_NEWTIME */
	MNT_NS     = (1ULL << 17), /* CLONE_NEWNS */
	CGROUP_NS  = (1ULL << 25), /* CLONE_NEWCGROUP */
	UTS_NS     = (1ULL << 26), /* CLONE_NEWUTS */
	IPC_NS     = (1ULL << 27), /* CLONE_NEWIPC */
	USER_NS    = (1ULL << 28), /* CLONE_NEWUSER */
	PID_NS     = (1ULL << 29), /* CLONE_NEWPID */
	NET_NS     = (1ULL << 30), /* CLONE_NEWNET */
};

/**
 * struct ns_id_req - namespace ID request structure
 * @size: size of this structure
 * @spare: reserved for future use
 * @filter: filter mask
 * @ns_id: last namespace id
 * @user_ns_id: owning user namespace ID
 *
 * Structure for passing namespace ID and miscellaneous parameters to
 * statns(2) and listns(2).
 *
 * For statns(2) @param represents the request mask.
 * For listns(2) @param represents the last listed mount id (or zero).
 */
struct ns_id_req {
	__u32 size;
	__u32 spare;
	__u64 ns_id;
	struct /* listns */ {
		__u32 ns_type;
		__u32 spare2;
		__u64 user_ns_id;
	};
};

/*
 * Special @user_ns_id value that can be passed to listns()
 */
#define LISTNS_CURRENT_USER 0xffffffffffffffff /* Caller's userns */

/* List of all ns_id_req versions. */
#define NS_ID_REQ_SIZE_VER0 32 /* sizeof first published struct */

#endif /* __LINUX_NSFS_H */
