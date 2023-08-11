/* SPDX-License-Identifier: GPL-2.0 */
/*
 * security/tomoyo/common.h
 *
 * Header file for TOMOYO.
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#ifndef _SECURITY_TOMOYO_COMMON_H
#define _SECURITY_TOMOYO_COMMON_H

#define pr_fmt(fmt) fmt

#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/kmod.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/list.h>
#include <linux/cred.h>
#include <linux/poll.h>
#include <linux/binfmts.h>
#include <linux/highmem.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/un.h>
#include <linux/lsm_hooks.h>
#include <net/sock.h>
#include <net/af_unix.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>

/********** Constants definitions. **********/

/*
 * TOMOYO uses this hash only when appending a string into the string
 * table. Frequency of appending strings is very low. So we don't need
 * large (e.g. 64k) hash size. 256 will be sufficient.
 */
#define TOMOYO_HASH_BITS  8
#define TOMOYO_MAX_HASH (1u<<TOMOYO_HASH_BITS)

/*
 * TOMOYO checks only SOCK_STREAM, SOCK_DGRAM, SOCK_RAW, SOCK_SEQPACKET.
 * Therefore, we don't need SOCK_MAX.
 */
#define TOMOYO_SOCK_MAX 6

#define TOMOYO_EXEC_TMPSIZE     4096

/* Garbage collector is trying to kfree() this element. */
#define TOMOYO_GC_IN_PROGRESS -1

/* Profile number is an integer between 0 and 255. */
#define TOMOYO_MAX_PROFILES 256

/* Group number is an integer between 0 and 255. */
#define TOMOYO_MAX_ACL_GROUPS 256

/* Index numbers for "struct tomoyo_condition". */
enum tomoyo_conditions_index {
	TOMOYO_TASK_UID,             /* current_uid()   */
	TOMOYO_TASK_EUID,            /* current_euid()  */
	TOMOYO_TASK_SUID,            /* current_suid()  */
	TOMOYO_TASK_FSUID,           /* current_fsuid() */
	TOMOYO_TASK_GID,             /* current_gid()   */
	TOMOYO_TASK_EGID,            /* current_egid()  */
	TOMOYO_TASK_SGID,            /* current_sgid()  */
	TOMOYO_TASK_FSGID,           /* current_fsgid() */
	TOMOYO_TASK_PID,             /* sys_getpid()   */
	TOMOYO_TASK_PPID,            /* sys_getppid()  */
	TOMOYO_EXEC_ARGC,            /* "struct linux_binprm *"->argc */
	TOMOYO_EXEC_ENVC,            /* "struct linux_binprm *"->envc */
	TOMOYO_TYPE_IS_SOCKET,       /* S_IFSOCK */
	TOMOYO_TYPE_IS_SYMLINK,      /* S_IFLNK */
	TOMOYO_TYPE_IS_FILE,         /* S_IFREG */
	TOMOYO_TYPE_IS_BLOCK_DEV,    /* S_IFBLK */
	TOMOYO_TYPE_IS_DIRECTORY,    /* S_IFDIR */
	TOMOYO_TYPE_IS_CHAR_DEV,     /* S_IFCHR */
	TOMOYO_TYPE_IS_FIFO,         /* S_IFIFO */
	TOMOYO_MODE_SETUID,          /* S_ISUID */
	TOMOYO_MODE_SETGID,          /* S_ISGID */
	TOMOYO_MODE_STICKY,          /* S_ISVTX */
	TOMOYO_MODE_OWNER_READ,      /* S_IRUSR */
	TOMOYO_MODE_OWNER_WRITE,     /* S_IWUSR */
	TOMOYO_MODE_OWNER_EXECUTE,   /* S_IXUSR */
	TOMOYO_MODE_GROUP_READ,      /* S_IRGRP */
	TOMOYO_MODE_GROUP_WRITE,     /* S_IWGRP */
	TOMOYO_MODE_GROUP_EXECUTE,   /* S_IXGRP */
	TOMOYO_MODE_OTHERS_READ,     /* S_IROTH */
	TOMOYO_MODE_OTHERS_WRITE,    /* S_IWOTH */
	TOMOYO_MODE_OTHERS_EXECUTE,  /* S_IXOTH */
	TOMOYO_EXEC_REALPATH,
	TOMOYO_SYMLINK_TARGET,
	TOMOYO_PATH1_UID,
	TOMOYO_PATH1_GID,
	TOMOYO_PATH1_INO,
	TOMOYO_PATH1_MAJOR,
	TOMOYO_PATH1_MINOR,
	TOMOYO_PATH1_PERM,
	TOMOYO_PATH1_TYPE,
	TOMOYO_PATH1_DEV_MAJOR,
	TOMOYO_PATH1_DEV_MINOR,
	TOMOYO_PATH2_UID,
	TOMOYO_PATH2_GID,
	TOMOYO_PATH2_INO,
	TOMOYO_PATH2_MAJOR,
	TOMOYO_PATH2_MINOR,
	TOMOYO_PATH2_PERM,
	TOMOYO_PATH2_TYPE,
	TOMOYO_PATH2_DEV_MAJOR,
	TOMOYO_PATH2_DEV_MINOR,
	TOMOYO_PATH1_PARENT_UID,
	TOMOYO_PATH1_PARENT_GID,
	TOMOYO_PATH1_PARENT_INO,
	TOMOYO_PATH1_PARENT_PERM,
	TOMOYO_PATH2_PARENT_UID,
	TOMOYO_PATH2_PARENT_GID,
	TOMOYO_PATH2_PARENT_INO,
	TOMOYO_PATH2_PARENT_PERM,
	TOMOYO_MAX_CONDITION_KEYWORD,
	TOMOYO_NUMBER_UNION,
	TOMOYO_NAME_UNION,
	TOMOYO_ARGV_ENTRY,
	TOMOYO_ENVP_ENTRY,
};


/* Index numbers for stat(). */
enum tomoyo_path_stat_index {
	/* Do not change this order. */
	TOMOYO_PATH1,
	TOMOYO_PATH1_PARENT,
	TOMOYO_PATH2,
	TOMOYO_PATH2_PARENT,
	TOMOYO_MAX_PATH_STAT
};

/* Index numbers for operation mode. */
enum tomoyo_mode_index {
	TOMOYO_CONFIG_DISABLED,
	TOMOYO_CONFIG_LEARNING,
	TOMOYO_CONFIG_PERMISSIVE,
	TOMOYO_CONFIG_ENFORCING,
	TOMOYO_CONFIG_MAX_MODE,
	TOMOYO_CONFIG_WANT_REJECT_LOG =  64,
	TOMOYO_CONFIG_WANT_GRANT_LOG  = 128,
	TOMOYO_CONFIG_USE_DEFAULT     = 255,
};

/* Index numbers for entry type. */
enum tomoyo_policy_id {
	TOMOYO_ID_GROUP,
	TOMOYO_ID_ADDRESS_GROUP,
	TOMOYO_ID_PATH_GROUP,
	TOMOYO_ID_NUMBER_GROUP,
	TOMOYO_ID_TRANSITION_CONTROL,
	TOMOYO_ID_AGGREGATOR,
	TOMOYO_ID_MANAGER,
	TOMOYO_ID_CONDITION,
	TOMOYO_ID_NAME,
	TOMOYO_ID_ACL,
	TOMOYO_ID_DOMAIN,
	TOMOYO_MAX_POLICY
};

/* Index numbers for domain's attributes. */
enum tomoyo_domain_info_flags_index {
	/* Quota warnning flag.   */
	TOMOYO_DIF_QUOTA_WARNED,
	/*
	 * This domain was unable to create a new domain at
	 * tomoyo_find_next_domain() because the name of the domain to be
	 * created was too long or it could not allocate memory.
	 * More than one process continued execve() without domain transition.
	 */
	TOMOYO_DIF_TRANSITION_FAILED,
	TOMOYO_MAX_DOMAIN_INFO_FLAGS
};

/* Index numbers for audit type. */
enum tomoyo_grant_log {
	/* Follow profile's configuration. */
	TOMOYO_GRANTLOG_AUTO,
	/* Do not generate grant log. */
	TOMOYO_GRANTLOG_NO,
	/* Generate grant_log. */
	TOMOYO_GRANTLOG_YES,
};

/* Index numbers for group entries. */
enum tomoyo_group_id {
	TOMOYO_PATH_GROUP,
	TOMOYO_NUMBER_GROUP,
	TOMOYO_ADDRESS_GROUP,
	TOMOYO_MAX_GROUP
};

/* Index numbers for type of numeric values. */
enum tomoyo_value_type {
	TOMOYO_VALUE_TYPE_INVALID,
	TOMOYO_VALUE_TYPE_DECIMAL,
	TOMOYO_VALUE_TYPE_OCTAL,
	TOMOYO_VALUE_TYPE_HEXADECIMAL,
};

/* Index numbers for domain transition control keywords. */
enum tomoyo_transition_type {
	/* Do not change this order, */
	TOMOYO_TRANSITION_CONTROL_NO_RESET,
	TOMOYO_TRANSITION_CONTROL_RESET,
	TOMOYO_TRANSITION_CONTROL_NO_INITIALIZE,
	TOMOYO_TRANSITION_CONTROL_INITIALIZE,
	TOMOYO_TRANSITION_CONTROL_NO_KEEP,
	TOMOYO_TRANSITION_CONTROL_KEEP,
	TOMOYO_MAX_TRANSITION_TYPE
};

/* Index numbers for Access Controls. */
enum tomoyo_acl_entry_type_index {
	TOMOYO_TYPE_PATH_ACL,
	TOMOYO_TYPE_PATH2_ACL,
	TOMOYO_TYPE_PATH_NUMBER_ACL,
	TOMOYO_TYPE_MKDEV_ACL,
	TOMOYO_TYPE_MOUNT_ACL,
	TOMOYO_TYPE_INET_ACL,
	TOMOYO_TYPE_UNIX_ACL,
	TOMOYO_TYPE_ENV_ACL,
	TOMOYO_TYPE_MANUAL_TASK_ACL,
};

/* Index numbers for access controls with one pathname. */
enum tomoyo_path_acl_index {
	TOMOYO_TYPE_EXECUTE,
	TOMOYO_TYPE_READ,
	TOMOYO_TYPE_WRITE,
	TOMOYO_TYPE_APPEND,
	TOMOYO_TYPE_UNLINK,
	TOMOYO_TYPE_GETATTR,
	TOMOYO_TYPE_RMDIR,
	TOMOYO_TYPE_TRUNCATE,
	TOMOYO_TYPE_SYMLINK,
	TOMOYO_TYPE_CHROOT,
	TOMOYO_TYPE_UMOUNT,
	TOMOYO_MAX_PATH_OPERATION
};

/* Index numbers for /sys/kernel/security/tomoyo/stat interface. */
enum tomoyo_memory_stat_type {
	TOMOYO_MEMORY_POLICY,
	TOMOYO_MEMORY_AUDIT,
	TOMOYO_MEMORY_QUERY,
	TOMOYO_MAX_MEMORY_STAT
};

enum tomoyo_mkdev_acl_index {
	TOMOYO_TYPE_MKBLOCK,
	TOMOYO_TYPE_MKCHAR,
	TOMOYO_MAX_MKDEV_OPERATION
};

/* Index numbers for socket operations. */
enum tomoyo_network_acl_index {
	TOMOYO_NETWORK_BIND,    /* bind() operation. */
	TOMOYO_NETWORK_LISTEN,  /* listen() operation. */
	TOMOYO_NETWORK_CONNECT, /* connect() operation. */
	TOMOYO_NETWORK_SEND,    /* send() operation. */
	TOMOYO_MAX_NETWORK_OPERATION
};

/* Index numbers for access controls with two pathnames. */
enum tomoyo_path2_acl_index {
	TOMOYO_TYPE_LINK,
	TOMOYO_TYPE_RENAME,
	TOMOYO_TYPE_PIVOT_ROOT,
	TOMOYO_MAX_PATH2_OPERATION
};

/* Index numbers for access controls with one pathname and one number. */
enum tomoyo_path_number_acl_index {
	TOMOYO_TYPE_CREATE,
	TOMOYO_TYPE_MKDIR,
	TOMOYO_TYPE_MKFIFO,
	TOMOYO_TYPE_MKSOCK,
	TOMOYO_TYPE_IOCTL,
	TOMOYO_TYPE_CHMOD,
	TOMOYO_TYPE_CHOWN,
	TOMOYO_TYPE_CHGRP,
	TOMOYO_MAX_PATH_NUMBER_OPERATION
};

/* Index numbers for /sys/kernel/security/tomoyo/ interfaces. */
enum tomoyo_securityfs_interface_index {
	TOMOYO_DOMAINPOLICY,
	TOMOYO_EXCEPTIONPOLICY,
	TOMOYO_PROCESS_STATUS,
	TOMOYO_STAT,
	TOMOYO_AUDIT,
	TOMOYO_VERSION,
	TOMOYO_PROFILE,
	TOMOYO_QUERY,
	TOMOYO_MANAGER
};

/* Index numbers for special mount operations. */
enum tomoyo_special_mount {
	TOMOYO_MOUNT_BIND,            /* mount --bind /source /dest   */
	TOMOYO_MOUNT_MOVE,            /* mount --move /old /new       */
	TOMOYO_MOUNT_REMOUNT,         /* mount -o remount /dir        */
	TOMOYO_MOUNT_MAKE_UNBINDABLE, /* mount --make-unbindable /dir */
	TOMOYO_MOUNT_MAKE_PRIVATE,    /* mount --make-private /dir    */
	TOMOYO_MOUNT_MAKE_SLAVE,      /* mount --make-slave /dir      */
	TOMOYO_MOUNT_MAKE_SHARED,     /* mount --make-shared /dir     */
	TOMOYO_MAX_SPECIAL_MOUNT
};

/* Index numbers for functionality. */
enum tomoyo_mac_index {
	TOMOYO_MAC_FILE_EXECUTE,
	TOMOYO_MAC_FILE_OPEN,
	TOMOYO_MAC_FILE_CREATE,
	TOMOYO_MAC_FILE_UNLINK,
	TOMOYO_MAC_FILE_GETATTR,
	TOMOYO_MAC_FILE_MKDIR,
	TOMOYO_MAC_FILE_RMDIR,
	TOMOYO_MAC_FILE_MKFIFO,
	TOMOYO_MAC_FILE_MKSOCK,
	TOMOYO_MAC_FILE_TRUNCATE,
	TOMOYO_MAC_FILE_SYMLINK,
	TOMOYO_MAC_FILE_MKBLOCK,
	TOMOYO_MAC_FILE_MKCHAR,
	TOMOYO_MAC_FILE_LINK,
	TOMOYO_MAC_FILE_RENAME,
	TOMOYO_MAC_FILE_CHMOD,
	TOMOYO_MAC_FILE_CHOWN,
	TOMOYO_MAC_FILE_CHGRP,
	TOMOYO_MAC_FILE_IOCTL,
	TOMOYO_MAC_FILE_CHROOT,
	TOMOYO_MAC_FILE_MOUNT,
	TOMOYO_MAC_FILE_UMOUNT,
	TOMOYO_MAC_FILE_PIVOT_ROOT,
	TOMOYO_MAC_NETWORK_INET_STREAM_BIND,
	TOMOYO_MAC_NETWORK_INET_STREAM_LISTEN,
	TOMOYO_MAC_NETWORK_INET_STREAM_CONNECT,
	TOMOYO_MAC_NETWORK_INET_DGRAM_BIND,
	TOMOYO_MAC_NETWORK_INET_DGRAM_SEND,
	TOMOYO_MAC_NETWORK_INET_RAW_BIND,
	TOMOYO_MAC_NETWORK_INET_RAW_SEND,
	TOMOYO_MAC_NETWORK_UNIX_STREAM_BIND,
	TOMOYO_MAC_NETWORK_UNIX_STREAM_LISTEN,
	TOMOYO_MAC_NETWORK_UNIX_STREAM_CONNECT,
	TOMOYO_MAC_NETWORK_UNIX_DGRAM_BIND,
	TOMOYO_MAC_NETWORK_UNIX_DGRAM_SEND,
	TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_BIND,
	TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_LISTEN,
	TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_CONNECT,
	TOMOYO_MAC_ENVIRON,
	TOMOYO_MAX_MAC_INDEX
};

/* Index numbers for category of functionality. */
enum tomoyo_mac_category_index {
	TOMOYO_MAC_CATEGORY_FILE,
	TOMOYO_MAC_CATEGORY_NETWORK,
	TOMOYO_MAC_CATEGORY_MISC,
	TOMOYO_MAX_MAC_CATEGORY_INDEX
};

/*
 * Retry this request. Returned by tomoyo_supervisor() if policy violation has
 * occurred in enforcing mode and the userspace daemon decided to retry.
 *
 * We must choose a positive value in order to distinguish "granted" (which is
 * 0) and "rejected" (which is a negative value) and "retry".
 */
#define TOMOYO_RETRY_REQUEST 1

/* Index numbers for /sys/kernel/security/tomoyo/stat interface. */
enum tomoyo_policy_stat_type {
	/* Do not change this order. */
	TOMOYO_STAT_POLICY_UPDATES,
	TOMOYO_STAT_POLICY_LEARNING,   /* == TOMOYO_CONFIG_LEARNING */
	TOMOYO_STAT_POLICY_PERMISSIVE, /* == TOMOYO_CONFIG_PERMISSIVE */
	TOMOYO_STAT_POLICY_ENFORCING,  /* == TOMOYO_CONFIG_ENFORCING */
	TOMOYO_MAX_POLICY_STAT
};

/* Index numbers for profile's PREFERENCE values. */
enum tomoyo_pref_index {
	TOMOYO_PREF_MAX_AUDIT_LOG,
	TOMOYO_PREF_MAX_LEARNING_ENTRY,
	TOMOYO_MAX_PREF
};

/********** Structure definitions. **********/

/* Common header for holding ACL entries. */
struct tomoyo_acl_head {
	struct list_head list;
	s8 is_deleted; /* true or false or TOMOYO_GC_IN_PROGRESS */
} __packed;

/* Common header for shared entries. */
struct tomoyo_shared_acl_head {
	struct list_head list;
	atomic_t users;
} __packed;

struct tomoyo_policy_namespace;

/* Structure for request info. */
struct tomoyo_request_info {
	/*
	 * For holding parameters specific to operations which deal files.
	 * NULL if not dealing files.
	 */
	struct tomoyo_obj_info *obj;
	/*
	 * For holding parameters specific to execve() request.
	 * NULL if not dealing execve().
	 */
	struct tomoyo_execve *ee;
	struct tomoyo_domain_info *domain;
	/* For holding parameters. */
	union {
		struct {
			const struct tomoyo_path_info *filename;
			/* For using wildcards at tomoyo_find_next_domain(). */
			const struct tomoyo_path_info *matched_path;
			/* One of values in "enum tomoyo_path_acl_index". */
			u8 operation;
		} path;
		struct {
			const struct tomoyo_path_info *filename1;
			const struct tomoyo_path_info *filename2;
			/* One of values in "enum tomoyo_path2_acl_index". */
			u8 operation;
		} path2;
		struct {
			const struct tomoyo_path_info *filename;
			unsigned int mode;
			unsigned int major;
			unsigned int minor;
			/* One of values in "enum tomoyo_mkdev_acl_index". */
			u8 operation;
		} mkdev;
		struct {
			const struct tomoyo_path_info *filename;
			unsigned long number;
			/*
			 * One of values in
			 * "enum tomoyo_path_number_acl_index".
			 */
			u8 operation;
		} path_number;
		struct {
			const struct tomoyo_path_info *name;
		} environ;
		struct {
			const __be32 *address;
			u16 port;
			/* One of values smaller than TOMOYO_SOCK_MAX. */
			u8 protocol;
			/* One of values in "enum tomoyo_network_acl_index". */
			u8 operation;
			bool is_ipv6;
		} inet_network;
		struct {
			const struct tomoyo_path_info *address;
			/* One of values smaller than TOMOYO_SOCK_MAX. */
			u8 protocol;
			/* One of values in "enum tomoyo_network_acl_index". */
			u8 operation;
		} unix_network;
		struct {
			const struct tomoyo_path_info *type;
			const struct tomoyo_path_info *dir;
			const struct tomoyo_path_info *dev;
			unsigned long flags;
			int need_dev;
		} mount;
		struct {
			const struct tomoyo_path_info *domainname;
		} task;
	} param;
	struct tomoyo_acl_info *matched_acl;
	u8 param_type;
	bool granted;
	u8 retry;
	u8 profile;
	u8 mode; /* One of tomoyo_mode_index . */
	u8 type;
};

/* Structure for holding a token. */
struct tomoyo_path_info {
	const char *name;
	u32 hash;          /* = full_name_hash(name, strlen(name)) */
	u16 const_len;     /* = tomoyo_const_part_length(name)     */
	bool is_dir;       /* = tomoyo_strendswith(name, "/")      */
	bool is_patterned; /* = tomoyo_path_contains_pattern(name) */
};

/* Structure for holding string data. */
struct tomoyo_name {
	struct tomoyo_shared_acl_head head;
	struct tomoyo_path_info entry;
};

/* Structure for holding a word. */
struct tomoyo_name_union {
	/* Either @filename or @group is NULL. */
	const struct tomoyo_path_info *filename;
	struct tomoyo_group *group;
};

/* Structure for holding a number. */
struct tomoyo_number_union {
	unsigned long values[2];
	struct tomoyo_group *group; /* Maybe NULL. */
	/* One of values in "enum tomoyo_value_type". */
	u8 value_type[2];
};

/* Structure for holding an IP address. */
struct tomoyo_ipaddr_union {
	struct in6_addr ip[2]; /* Big endian. */
	struct tomoyo_group *group; /* Pointer to address group. */
	bool is_ipv6; /* Valid only if @group == NULL. */
};

/* Structure for "path_group"/"number_group"/"address_group" directive. */
struct tomoyo_group {
	struct tomoyo_shared_acl_head head;
	const struct tomoyo_path_info *group_name;
	struct list_head member_list;
};

/* Structure for "path_group" directive. */
struct tomoyo_path_group {
	struct tomoyo_acl_head head;
	const struct tomoyo_path_info *member_name;
};

/* Structure for "number_group" directive. */
struct tomoyo_number_group {
	struct tomoyo_acl_head head;
	struct tomoyo_number_union number;
};

/* Structure for "address_group" directive. */
struct tomoyo_address_group {
	struct tomoyo_acl_head head;
	/* Structure for holding an IP address. */
	struct tomoyo_ipaddr_union address;
};

/* Subset of "struct stat". Used by conditional ACL and audit logs. */
struct tomoyo_mini_stat {
	kuid_t uid;
	kgid_t gid;
	ino_t ino;
	umode_t mode;
	dev_t dev;
	dev_t rdev;
};

/* Structure for dumping argv[] and envp[] of "struct linux_binprm". */
struct tomoyo_page_dump {
	struct page *page;    /* Previously dumped page. */
	char *data;           /* Contents of "page". Size is PAGE_SIZE. */
};

/* Structure for attribute checks in addition to pathname checks. */
struct tomoyo_obj_info {
	/*
	 * True if tomoyo_get_attributes() was already called, false otherwise.
	 */
	bool validate_done;
	/* True if @stat[] is valid. */
	bool stat_valid[TOMOYO_MAX_PATH_STAT];
	/* First pathname. Initialized with { NULL, NULL } if no path. */
	struct path path1;
	/* Second pathname. Initialized with { NULL, NULL } if no path. */
	struct path path2;
	/*
	 * Information on @path1, @path1's parent directory, @path2, @path2's
	 * parent directory.
	 */
	struct tomoyo_mini_stat stat[TOMOYO_MAX_PATH_STAT];
	/*
	 * Content of symbolic link to be created. NULL for operations other
	 * than symlink().
	 */
	struct tomoyo_path_info *symlink_target;
};

/* Structure for argv[]. */
struct tomoyo_argv {
	unsigned long index;
	const struct tomoyo_path_info *value;
	bool is_not;
};

/* Structure for envp[]. */
struct tomoyo_envp {
	const struct tomoyo_path_info *name;
	const struct tomoyo_path_info *value;
	bool is_not;
};

/* Structure for execve() operation. */
struct tomoyo_execve {
	struct tomoyo_request_info r;
	struct tomoyo_obj_info obj;
	struct linux_binprm *bprm;
	const struct tomoyo_path_info *transition;
	/* For dumping argv[] and envp[]. */
	struct tomoyo_page_dump dump;
	/* For temporary use. */
	char *tmp; /* Size is TOMOYO_EXEC_TMPSIZE bytes */
};

/* Structure for entries which follows "struct tomoyo_condition". */
struct tomoyo_condition_element {
	/*
	 * Left hand operand. A "struct tomoyo_argv" for TOMOYO_ARGV_ENTRY, a
	 * "struct tomoyo_envp" for TOMOYO_ENVP_ENTRY is attached to the tail
	 * of the array of this struct.
	 */
	u8 left;
	/*
	 * Right hand operand. A "struct tomoyo_number_union" for
	 * TOMOYO_NUMBER_UNION, a "struct tomoyo_name_union" for
	 * TOMOYO_NAME_UNION is attached to the tail of the array of this
	 * struct.
	 */
	u8 right;
	/* Equation operator. True if equals or overlaps, false otherwise. */
	bool equals;
};

/* Structure for optional arguments. */
struct tomoyo_condition {
	struct tomoyo_shared_acl_head head;
	u32 size; /* Memory size allocated for this entry. */
	u16 condc; /* Number of conditions in this struct. */
	u16 numbers_count; /* Number of "struct tomoyo_number_union values". */
	u16 names_count; /* Number of "struct tomoyo_name_union names". */
	u16 argc; /* Number of "struct tomoyo_argv". */
	u16 envc; /* Number of "struct tomoyo_envp". */
	u8 grant_log; /* One of values in "enum tomoyo_grant_log". */
	const struct tomoyo_path_info *transit; /* Maybe NULL. */
	/*
	 * struct tomoyo_condition_element condition[condc];
	 * struct tomoyo_number_union values[numbers_count];
	 * struct tomoyo_name_union names[names_count];
	 * struct tomoyo_argv argv[argc];
	 * struct tomoyo_envp envp[envc];
	 */
};

/* Common header for individual entries. */
struct tomoyo_acl_info {
	struct list_head list;
	struct tomoyo_condition *cond; /* Maybe NULL. */
	s8 is_deleted; /* true or false or TOMOYO_GC_IN_PROGRESS */
	u8 type; /* One of values in "enum tomoyo_acl_entry_type_index". */
} __packed;

/* Structure for domain information. */
struct tomoyo_domain_info {
	struct list_head list;
	struct list_head acl_info_list;
	/* Name of this domain. Never NULL.          */
	const struct tomoyo_path_info *domainname;
	/* Namespace for this domain. Never NULL. */
	struct tomoyo_policy_namespace *ns;
	/* Group numbers to use.   */
	unsigned long group[TOMOYO_MAX_ACL_GROUPS / BITS_PER_LONG];
	u8 profile;        /* Profile number to use. */
	bool is_deleted;   /* Delete flag.           */
	bool flags[TOMOYO_MAX_DOMAIN_INFO_FLAGS];
	atomic_t users; /* Number of referring tasks. */
};

/*
 * Structure for "task manual_domain_transition" directive.
 */
struct tomoyo_task_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_MANUAL_TASK_ACL */
	/* Pointer to domainname. */
	const struct tomoyo_path_info *domainname;
};

/*
 * Structure for "file execute", "file read", "file write", "file append",
 * "file unlink", "file getattr", "file rmdir", "file truncate",
 * "file symlink", "file chroot" and "file unmount" directive.
 */
struct tomoyo_path_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH_ACL */
	u16 perm; /* Bitmask of values in "enum tomoyo_path_acl_index". */
	struct tomoyo_name_union name;
};

/*
 * Structure for "file create", "file mkdir", "file mkfifo", "file mksock",
 * "file ioctl", "file chmod", "file chown" and "file chgrp" directive.
 */
struct tomoyo_path_number_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH_NUMBER_ACL */
	/* Bitmask of values in "enum tomoyo_path_number_acl_index". */
	u8 perm;
	struct tomoyo_name_union name;
	struct tomoyo_number_union number;
};

/* Structure for "file mkblock" and "file mkchar" directive. */
struct tomoyo_mkdev_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_MKDEV_ACL */
	u8 perm; /* Bitmask of values in "enum tomoyo_mkdev_acl_index". */
	struct tomoyo_name_union name;
	struct tomoyo_number_union mode;
	struct tomoyo_number_union major;
	struct tomoyo_number_union minor;
};

/*
 * Structure for "file rename", "file link" and "file pivot_root" directive.
 */
struct tomoyo_path2_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH2_ACL */
	u8 perm; /* Bitmask of values in "enum tomoyo_path2_acl_index". */
	struct tomoyo_name_union name1;
	struct tomoyo_name_union name2;
};

/* Structure for "file mount" directive. */
struct tomoyo_mount_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_MOUNT_ACL */
	struct tomoyo_name_union dev_name;
	struct tomoyo_name_union dir_name;
	struct tomoyo_name_union fs_type;
	struct tomoyo_number_union flags;
};

/* Structure for "misc env" directive in domain policy. */
struct tomoyo_env_acl {
	struct tomoyo_acl_info head;        /* type = TOMOYO_TYPE_ENV_ACL  */
	const struct tomoyo_path_info *env; /* environment variable */
};

/* Structure for "network inet" directive. */
struct tomoyo_inet_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_INET_ACL */
	u8 protocol;
	u8 perm; /* Bitmask of values in "enum tomoyo_network_acl_index" */
	struct tomoyo_ipaddr_union address;
	struct tomoyo_number_union port;
};

/* Structure for "network unix" directive. */
struct tomoyo_unix_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_UNIX_ACL */
	u8 protocol;
	u8 perm; /* Bitmask of values in "enum tomoyo_network_acl_index" */
	struct tomoyo_name_union name;
};

/* Structure for holding a line from /sys/kernel/security/tomoyo/ interface. */
struct tomoyo_acl_param {
	char *data;
	struct list_head *list;
	struct tomoyo_policy_namespace *ns;
	bool is_delete;
};

#define TOMOYO_MAX_IO_READ_QUEUE 64

/*
 * Structure for reading/writing policy via /sys/kernel/security/tomoyo
 * interfaces.
 */
struct tomoyo_io_buffer {
	void (*read)(struct tomoyo_io_buffer *head);
	int (*write)(struct tomoyo_io_buffer *head);
	__poll_t (*poll)(struct file *file, poll_table *wait);
	/* Exclusive lock for this structure.   */
	struct mutex io_sem;
	char __user *read_user_buf;
	size_t read_user_buf_avail;
	struct {
		struct list_head *ns;
		struct list_head *domain;
		struct list_head *group;
		struct list_head *acl;
		size_t avail;
		unsigned int step;
		unsigned int query_index;
		u16 index;
		u16 cond_index;
		u8 acl_group_index;
		u8 cond_step;
		u8 bit;
		u8 w_pos;
		bool eof;
		bool print_this_domain_only;
		bool print_transition_related_only;
		bool print_cond_part;
		const char *w[TOMOYO_MAX_IO_READ_QUEUE];
	} r;
	struct {
		struct tomoyo_policy_namespace *ns;
		/* The position currently writing to.   */
		struct tomoyo_domain_info *domain;
		/* Bytes available for writing.         */
		size_t avail;
		bool is_delete;
	} w;
	/* Buffer for reading.                  */
	char *read_buf;
	/* Size of read buffer.                 */
	size_t readbuf_size;
	/* Buffer for writing.                  */
	char *write_buf;
	/* Size of write buffer.                */
	size_t writebuf_size;
	/* Type of this interface.              */
	enum tomoyo_securityfs_interface_index type;
	/* Users counter protected by tomoyo_io_buffer_list_lock. */
	u8 users;
	/* List for telling GC not to kfree() elements. */
	struct list_head list;
};

/*
 * Structure for "initialize_domain"/"no_initialize_domain"/"keep_domain"/
 * "no_keep_domain" keyword.
 */
struct tomoyo_transition_control {
	struct tomoyo_acl_head head;
	u8 type; /* One of values in "enum tomoyo_transition_type".  */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
	const struct tomoyo_path_info *domainname; /* Maybe NULL */
	const struct tomoyo_path_info *program;    /* Maybe NULL */
};

/* Structure for "aggregator" keyword. */
struct tomoyo_aggregator {
	struct tomoyo_acl_head head;
	const struct tomoyo_path_info *original_name;
	const struct tomoyo_path_info *aggregated_name;
};

/* Structure for policy manager. */
struct tomoyo_manager {
	struct tomoyo_acl_head head;
	/* A path to program or a domainname. */
	const struct tomoyo_path_info *manager;
};

struct tomoyo_preference {
	unsigned int learning_max_entry;
	bool enforcing_verbose;
	bool learning_verbose;
	bool permissive_verbose;
};

/* Structure for /sys/kernel/security/tomnoyo/profile interface. */
struct tomoyo_profile {
	const struct tomoyo_path_info *comment;
	struct tomoyo_preference *learning;
	struct tomoyo_preference *permissive;
	struct tomoyo_preference *enforcing;
	struct tomoyo_preference preference;
	u8 default_config;
	u8 config[TOMOYO_MAX_MAC_INDEX + TOMOYO_MAX_MAC_CATEGORY_INDEX];
	unsigned int pref[TOMOYO_MAX_PREF];
};

/* Structure for representing YYYY/MM/DD hh/mm/ss. */
struct tomoyo_time {
	u16 year;
	u8 month;
	u8 day;
	u8 hour;
	u8 min;
	u8 sec;
};

/* Structure for policy namespace. */
struct tomoyo_policy_namespace {
	/* Profile table. Memory is allocated as needed. */
	struct tomoyo_profile *profile_ptr[TOMOYO_MAX_PROFILES];
	/* List of "struct tomoyo_group". */
	struct list_head group_list[TOMOYO_MAX_GROUP];
	/* List of policy. */
	struct list_head policy_list[TOMOYO_MAX_POLICY];
	/* The global ACL referred by "use_group" keyword. */
	struct list_head acl_group[TOMOYO_MAX_ACL_GROUPS];
	/* List for connecting to tomoyo_namespace_list list. */
	struct list_head namespace_list;
	/* Profile version. Currently only 20150505 is defined. */
	unsigned int profile_version;
	/* Name of this namespace (e.g. "<kernel>", "</usr/sbin/httpd>" ). */
	const char *name;
};

/* Structure for "struct task_struct"->security. */
struct tomoyo_task {
	struct tomoyo_domain_info *domain_info;
	struct tomoyo_domain_info *old_domain_info;
};

/********** Function prototypes. **********/

bool tomoyo_address_matches_group(const bool is_ipv6, const __be32 *address,
				  const struct tomoyo_group *group);
bool tomoyo_compare_number_union(const unsigned long value,
				 const struct tomoyo_number_union *ptr);
bool tomoyo_condition(struct tomoyo_request_info *r,
		      const struct tomoyo_condition *cond);
bool tomoyo_correct_domain(const unsigned char *domainname);
bool tomoyo_correct_path(const char *filename);
bool tomoyo_correct_word(const char *string);
bool tomoyo_domain_def(const unsigned char *buffer);
bool tomoyo_domain_quota_is_ok(struct tomoyo_request_info *r);
bool tomoyo_dump_page(struct linux_binprm *bprm, unsigned long pos,
		      struct tomoyo_page_dump *dump);
bool tomoyo_memory_ok(void *ptr);
bool tomoyo_number_matches_group(const unsigned long min,
				 const unsigned long max,
				 const struct tomoyo_group *group);
bool tomoyo_parse_ipaddr_union(struct tomoyo_acl_param *param,
			       struct tomoyo_ipaddr_union *ptr);
bool tomoyo_parse_name_union(struct tomoyo_acl_param *param,
			     struct tomoyo_name_union *ptr);
bool tomoyo_parse_number_union(struct tomoyo_acl_param *param,
			       struct tomoyo_number_union *ptr);
bool tomoyo_path_matches_pattern(const struct tomoyo_path_info *filename,
				 const struct tomoyo_path_info *pattern);
bool tomoyo_permstr(const char *string, const char *keyword);
bool tomoyo_str_starts(char **src, const char *find);
char *tomoyo_encode(const char *str);
char *tomoyo_encode2(const char *str, int str_len);
char *tomoyo_init_log(struct tomoyo_request_info *r, int len, const char *fmt,
		      va_list args) __printf(3, 0);
char *tomoyo_read_token(struct tomoyo_acl_param *param);
char *tomoyo_realpath_from_path(const struct path *path);
char *tomoyo_realpath_nofollow(const char *pathname);
const char *tomoyo_get_exe(void);
const struct tomoyo_path_info *tomoyo_compare_name_union
(const struct tomoyo_path_info *name, const struct tomoyo_name_union *ptr);
const struct tomoyo_path_info *tomoyo_get_domainname
(struct tomoyo_acl_param *param);
const struct tomoyo_path_info *tomoyo_get_name(const char *name);
const struct tomoyo_path_info *tomoyo_path_matches_group
(const struct tomoyo_path_info *pathname, const struct tomoyo_group *group);
int tomoyo_check_open_permission(struct tomoyo_domain_info *domain,
				 const struct path *path, const int flag);
void tomoyo_close_control(struct tomoyo_io_buffer *head);
int tomoyo_env_perm(struct tomoyo_request_info *r, const char *env);
int tomoyo_execute_permission(struct tomoyo_request_info *r,
			      const struct tomoyo_path_info *filename);
int tomoyo_find_next_domain(struct linux_binprm *bprm);
int tomoyo_get_mode(const struct tomoyo_policy_namespace *ns, const u8 profile,
		    const u8 index);
int tomoyo_init_request_info(struct tomoyo_request_info *r,
			     struct tomoyo_domain_info *domain,
			     const u8 index);
int tomoyo_mkdev_perm(const u8 operation, const struct path *path,
		      const unsigned int mode, unsigned int dev);
int tomoyo_mount_permission(const char *dev_name, const struct path *path,
			    const char *type, unsigned long flags,
			    void *data_page);
int tomoyo_open_control(const u8 type, struct file *file);
int tomoyo_path2_perm(const u8 operation, const struct path *path1,
		      const struct path *path2);
int tomoyo_path_number_perm(const u8 operation, const struct path *path,
			    unsigned long number);
int tomoyo_path_perm(const u8 operation, const struct path *path,
		     const char *target);
__poll_t tomoyo_poll_control(struct file *file, poll_table *wait);
__poll_t tomoyo_poll_log(struct file *file, poll_table *wait);
int tomoyo_socket_bind_permission(struct socket *sock, struct sockaddr *addr,
				  int addr_len);
int tomoyo_socket_connect_permission(struct socket *sock,
				     struct sockaddr *addr, int addr_len);
int tomoyo_socket_listen_permission(struct socket *sock);
int tomoyo_socket_sendmsg_permission(struct socket *sock, struct msghdr *msg,
				     int size);
int tomoyo_supervisor(struct tomoyo_request_info *r, const char *fmt, ...)
	__printf(2, 3);
int tomoyo_update_domain(struct tomoyo_acl_info *new_entry, const int size,
			 struct tomoyo_acl_param *param,
			 bool (*check_duplicate)
			 (const struct tomoyo_acl_info *,
			  const struct tomoyo_acl_info *),
			 bool (*merge_duplicate)
			 (struct tomoyo_acl_info *, struct tomoyo_acl_info *,
			  const bool));
int tomoyo_update_policy(struct tomoyo_acl_head *new_entry, const int size,
			 struct tomoyo_acl_param *param,
			 bool (*check_duplicate)
			 (const struct tomoyo_acl_head *,
			  const struct tomoyo_acl_head *));
int tomoyo_write_aggregator(struct tomoyo_acl_param *param);
int tomoyo_write_file(struct tomoyo_acl_param *param);
int tomoyo_write_group(struct tomoyo_acl_param *param, const u8 type);
int tomoyo_write_misc(struct tomoyo_acl_param *param);
int tomoyo_write_inet_network(struct tomoyo_acl_param *param);
int tomoyo_write_transition_control(struct tomoyo_acl_param *param,
				    const u8 type);
int tomoyo_write_unix_network(struct tomoyo_acl_param *param);
ssize_t tomoyo_read_control(struct tomoyo_io_buffer *head, char __user *buffer,
			    const int buffer_len);
ssize_t tomoyo_write_control(struct tomoyo_io_buffer *head,
			     const char __user *buffer, const int buffer_len);
struct tomoyo_condition *tomoyo_get_condition(struct tomoyo_acl_param *param);
struct tomoyo_domain_info *tomoyo_assign_domain(const char *domainname,
						const bool transit);
struct tomoyo_domain_info *tomoyo_domain(void);
struct tomoyo_domain_info *tomoyo_find_domain(const char *domainname);
struct tomoyo_group *tomoyo_get_group(struct tomoyo_acl_param *param,
				      const u8 idx);
struct tomoyo_policy_namespace *tomoyo_assign_namespace
(const char *domainname);
struct tomoyo_profile *tomoyo_profile(const struct tomoyo_policy_namespace *ns,
				      const u8 profile);
u8 tomoyo_parse_ulong(unsigned long *result, char **str);
void *tomoyo_commit_ok(void *data, const unsigned int size);
void __init tomoyo_load_builtin_policy(void);
void __init tomoyo_mm_init(void);
void tomoyo_check_acl(struct tomoyo_request_info *r,
		      bool (*check_entry)(struct tomoyo_request_info *,
					  const struct tomoyo_acl_info *));
void tomoyo_check_profile(void);
void tomoyo_convert_time(time64_t time, struct tomoyo_time *stamp);
void tomoyo_del_condition(struct list_head *element);
void tomoyo_fill_path_info(struct tomoyo_path_info *ptr);
void tomoyo_get_attributes(struct tomoyo_obj_info *obj);
void tomoyo_init_policy_namespace(struct tomoyo_policy_namespace *ns);
void tomoyo_load_policy(const char *filename);
void tomoyo_normalize_line(unsigned char *buffer);
void tomoyo_notify_gc(struct tomoyo_io_buffer *head, const bool is_register);
void tomoyo_print_ip(char *buf, const unsigned int size,
		     const struct tomoyo_ipaddr_union *ptr);
void tomoyo_print_ulong(char *buffer, const int buffer_len,
			const unsigned long value, const u8 type);
void tomoyo_put_name_union(struct tomoyo_name_union *ptr);
void tomoyo_put_number_union(struct tomoyo_number_union *ptr);
void tomoyo_read_log(struct tomoyo_io_buffer *head);
void tomoyo_update_stat(const u8 index);
void tomoyo_warn_oom(const char *function);
void tomoyo_write_log(struct tomoyo_request_info *r, const char *fmt, ...)
	__printf(2, 3);
void tomoyo_write_log2(struct tomoyo_request_info *r, int len, const char *fmt,
		       va_list args) __printf(3, 0);

/********** External variable definitions. **********/

extern bool tomoyo_policy_loaded;
extern int tomoyo_enabled;
extern const char * const tomoyo_condition_keyword
[TOMOYO_MAX_CONDITION_KEYWORD];
extern const char * const tomoyo_dif[TOMOYO_MAX_DOMAIN_INFO_FLAGS];
extern const char * const tomoyo_mac_keywords[TOMOYO_MAX_MAC_INDEX
					      + TOMOYO_MAX_MAC_CATEGORY_INDEX];
extern const char * const tomoyo_mode[TOMOYO_CONFIG_MAX_MODE];
extern const char * const tomoyo_path_keyword[TOMOYO_MAX_PATH_OPERATION];
extern const char * const tomoyo_proto_keyword[TOMOYO_SOCK_MAX];
extern const char * const tomoyo_socket_keyword[TOMOYO_MAX_NETWORK_OPERATION];
extern const u8 tomoyo_index2category[TOMOYO_MAX_MAC_INDEX];
extern const u8 tomoyo_pn2mac[TOMOYO_MAX_PATH_NUMBER_OPERATION];
extern const u8 tomoyo_pnnn2mac[TOMOYO_MAX_MKDEV_OPERATION];
extern const u8 tomoyo_pp2mac[TOMOYO_MAX_PATH2_OPERATION];
extern struct list_head tomoyo_condition_list;
extern struct list_head tomoyo_domain_list;
extern struct list_head tomoyo_name_list[TOMOYO_MAX_HASH];
extern struct list_head tomoyo_namespace_list;
extern struct mutex tomoyo_policy_lock;
extern struct srcu_struct tomoyo_ss;
extern struct tomoyo_domain_info tomoyo_kernel_domain;
extern struct tomoyo_policy_namespace tomoyo_kernel_namespace;
extern unsigned int tomoyo_memory_quota[TOMOYO_MAX_MEMORY_STAT];
extern unsigned int tomoyo_memory_used[TOMOYO_MAX_MEMORY_STAT];
extern struct lsm_blob_sizes tomoyo_blob_sizes;

/********** Inlined functions. **********/

/**
 * tomoyo_read_lock - Take lock for protecting policy.
 *
 * Returns index number for tomoyo_read_unlock().
 */
static inline int tomoyo_read_lock(void)
{
	return srcu_read_lock(&tomoyo_ss);
}

/**
 * tomoyo_read_unlock - Release lock for protecting policy.
 *
 * @idx: Index number returned by tomoyo_read_lock().
 *
 * Returns nothing.
 */
static inline void tomoyo_read_unlock(int idx)
{
	srcu_read_unlock(&tomoyo_ss, idx);
}

/**
 * tomoyo_sys_getppid - Copy of getppid().
 *
 * Returns parent process's PID.
 *
 * Alpha does not have getppid() defined. To be able to build this module on
 * Alpha, I have to copy getppid() from kernel/timer.c.
 */
static inline pid_t tomoyo_sys_getppid(void)
{
	pid_t pid;

	rcu_read_lock();
	pid = task_tgid_vnr(rcu_dereference(current->real_parent));
	rcu_read_unlock();
	return pid;
}

/**
 * tomoyo_sys_getpid - Copy of getpid().
 *
 * Returns current thread's PID.
 *
 * Alpha does not have getpid() defined. To be able to build this module on
 * Alpha, I have to copy getpid() from kernel/timer.c.
 */
static inline pid_t tomoyo_sys_getpid(void)
{
	return task_tgid_vnr(current);
}

/**
 * tomoyo_pathcmp - strcmp() for "struct tomoyo_path_info" structure.
 *
 * @a: Pointer to "struct tomoyo_path_info".
 * @b: Pointer to "struct tomoyo_path_info".
 *
 * Returns true if @a == @b, false otherwise.
 */
static inline bool tomoyo_pathcmp(const struct tomoyo_path_info *a,
				  const struct tomoyo_path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

/**
 * tomoyo_put_name - Drop reference on "struct tomoyo_name".
 *
 * @name: Pointer to "struct tomoyo_path_info". Maybe NULL.
 *
 * Returns nothing.
 */
static inline void tomoyo_put_name(const struct tomoyo_path_info *name)
{
	if (name) {
		struct tomoyo_name *ptr =
			container_of(name, typeof(*ptr), entry);
		atomic_dec(&ptr->head.users);
	}
}

/**
 * tomoyo_put_condition - Drop reference on "struct tomoyo_condition".
 *
 * @cond: Pointer to "struct tomoyo_condition". Maybe NULL.
 *
 * Returns nothing.
 */
static inline void tomoyo_put_condition(struct tomoyo_condition *cond)
{
	if (cond)
		atomic_dec(&cond->head.users);
}

/**
 * tomoyo_put_group - Drop reference on "struct tomoyo_group".
 *
 * @group: Pointer to "struct tomoyo_group". Maybe NULL.
 *
 * Returns nothing.
 */
static inline void tomoyo_put_group(struct tomoyo_group *group)
{
	if (group)
		atomic_dec(&group->head.users);
}

/**
 * tomoyo_task - Get "struct tomoyo_task" for specified thread.
 *
 * @task - Pointer to "struct task_struct".
 *
 * Returns pointer to "struct tomoyo_task" for specified thread.
 */
static inline struct tomoyo_task *tomoyo_task(struct task_struct *task)
{
	return task->security + tomoyo_blob_sizes.lbs_task;
}

/**
 * tomoyo_same_name_union - Check for duplicated "struct tomoyo_name_union" entry.
 *
 * @a: Pointer to "struct tomoyo_name_union".
 * @b: Pointer to "struct tomoyo_name_union".
 *
 * Returns true if @a == @b, false otherwise.
 */
static inline bool tomoyo_same_name_union
(const struct tomoyo_name_union *a, const struct tomoyo_name_union *b)
{
	return a->filename == b->filename && a->group == b->group;
}

/**
 * tomoyo_same_number_union - Check for duplicated "struct tomoyo_number_union" entry.
 *
 * @a: Pointer to "struct tomoyo_number_union".
 * @b: Pointer to "struct tomoyo_number_union".
 *
 * Returns true if @a == @b, false otherwise.
 */
static inline bool tomoyo_same_number_union
(const struct tomoyo_number_union *a, const struct tomoyo_number_union *b)
{
	return a->values[0] == b->values[0] && a->values[1] == b->values[1] &&
		a->group == b->group && a->value_type[0] == b->value_type[0] &&
		a->value_type[1] == b->value_type[1];
}

/**
 * tomoyo_same_ipaddr_union - Check for duplicated "struct tomoyo_ipaddr_union" entry.
 *
 * @a: Pointer to "struct tomoyo_ipaddr_union".
 * @b: Pointer to "struct tomoyo_ipaddr_union".
 *
 * Returns true if @a == @b, false otherwise.
 */
static inline bool tomoyo_same_ipaddr_union
(const struct tomoyo_ipaddr_union *a, const struct tomoyo_ipaddr_union *b)
{
	return !memcmp(a->ip, b->ip, sizeof(a->ip)) && a->group == b->group &&
		a->is_ipv6 == b->is_ipv6;
}

/**
 * tomoyo_current_namespace - Get "struct tomoyo_policy_namespace" for current thread.
 *
 * Returns pointer to "struct tomoyo_policy_namespace" for current thread.
 */
static inline struct tomoyo_policy_namespace *tomoyo_current_namespace(void)
{
	return tomoyo_domain()->ns;
}

/**
 * list_for_each_cookie - iterate over a list with cookie.
 * @pos:        the &struct list_head to use as a loop cursor.
 * @head:       the head for your list.
 */
#define list_for_each_cookie(pos, head)					\
	if (!pos)							\
		pos =  srcu_dereference((head)->next, &tomoyo_ss);	\
	for ( ; pos != (head); pos = srcu_dereference(pos->next, &tomoyo_ss))

#endif /* !defined(_SECURITY_TOMOYO_COMMON_H) */
