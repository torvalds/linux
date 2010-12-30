/*
 * security/tomoyo/common.h
 *
 * Header file for TOMOYO.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 */

#ifndef _SECURITY_TOMOYO_COMMON_H
#define _SECURITY_TOMOYO_COMMON_H

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
struct linux_binprm;

/********** Constants definitions. **********/

/*
 * TOMOYO uses this hash only when appending a string into the string
 * table. Frequency of appending strings is very low. So we don't need
 * large (e.g. 64k) hash size. 256 will be sufficient.
 */
#define TOMOYO_HASH_BITS  8
#define TOMOYO_MAX_HASH (1u<<TOMOYO_HASH_BITS)

#define TOMOYO_EXEC_TMPSIZE     4096

/* Profile number is an integer between 0 and 255. */
#define TOMOYO_MAX_PROFILES 256

enum tomoyo_mode_index {
	TOMOYO_CONFIG_DISABLED,
	TOMOYO_CONFIG_LEARNING,
	TOMOYO_CONFIG_PERMISSIVE,
	TOMOYO_CONFIG_ENFORCING,
	TOMOYO_CONFIG_USE_DEFAULT = 255
};

enum tomoyo_policy_id {
	TOMOYO_ID_GROUP,
	TOMOYO_ID_PATH_GROUP,
	TOMOYO_ID_NUMBER_GROUP,
	TOMOYO_ID_TRANSITION_CONTROL,
	TOMOYO_ID_AGGREGATOR,
	TOMOYO_ID_GLOBALLY_READABLE,
	TOMOYO_ID_PATTERN,
	TOMOYO_ID_NO_REWRITE,
	TOMOYO_ID_MANAGER,
	TOMOYO_ID_NAME,
	TOMOYO_ID_ACL,
	TOMOYO_ID_DOMAIN,
	TOMOYO_MAX_POLICY
};

enum tomoyo_group_id {
	TOMOYO_PATH_GROUP,
	TOMOYO_NUMBER_GROUP,
	TOMOYO_MAX_GROUP
};

/* Keywords for ACLs. */
#define TOMOYO_KEYWORD_AGGREGATOR                "aggregator "
#define TOMOYO_KEYWORD_ALLOW_MOUNT               "allow_mount "
#define TOMOYO_KEYWORD_ALLOW_READ                "allow_read "
#define TOMOYO_KEYWORD_DELETE                    "delete "
#define TOMOYO_KEYWORD_DENY_REWRITE              "deny_rewrite "
#define TOMOYO_KEYWORD_FILE_PATTERN              "file_pattern "
#define TOMOYO_KEYWORD_INITIALIZE_DOMAIN         "initialize_domain "
#define TOMOYO_KEYWORD_KEEP_DOMAIN               "keep_domain "
#define TOMOYO_KEYWORD_NO_INITIALIZE_DOMAIN      "no_initialize_domain "
#define TOMOYO_KEYWORD_NO_KEEP_DOMAIN            "no_keep_domain "
#define TOMOYO_KEYWORD_PATH_GROUP                "path_group "
#define TOMOYO_KEYWORD_NUMBER_GROUP              "number_group "
#define TOMOYO_KEYWORD_SELECT                    "select "
#define TOMOYO_KEYWORD_USE_PROFILE               "use_profile "
#define TOMOYO_KEYWORD_IGNORE_GLOBAL_ALLOW_READ  "ignore_global_allow_read"
#define TOMOYO_KEYWORD_QUOTA_EXCEEDED            "quota_exceeded"
#define TOMOYO_KEYWORD_TRANSITION_FAILED         "transition_failed"
/* A domain definition starts with <kernel>. */
#define TOMOYO_ROOT_NAME                         "<kernel>"
#define TOMOYO_ROOT_NAME_LEN                     (sizeof(TOMOYO_ROOT_NAME) - 1)

/* Value type definition. */
#define TOMOYO_VALUE_TYPE_INVALID     0
#define TOMOYO_VALUE_TYPE_DECIMAL     1
#define TOMOYO_VALUE_TYPE_OCTAL       2
#define TOMOYO_VALUE_TYPE_HEXADECIMAL 3

enum tomoyo_transition_type {
	/* Do not change this order, */
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
};

/* Index numbers for File Controls. */

/*
 * TOMOYO_TYPE_READ_WRITE is special. TOMOYO_TYPE_READ_WRITE is automatically
 * set if both TOMOYO_TYPE_READ and TOMOYO_TYPE_WRITE are set.
 * Both TOMOYO_TYPE_READ and TOMOYO_TYPE_WRITE are automatically set if
 * TOMOYO_TYPE_READ_WRITE is set.
 * TOMOYO_TYPE_READ_WRITE is automatically cleared if either TOMOYO_TYPE_READ
 * or TOMOYO_TYPE_WRITE is cleared.
 * Both TOMOYO_TYPE_READ and TOMOYO_TYPE_WRITE are automatically cleared if
 * TOMOYO_TYPE_READ_WRITE is cleared.
 */

enum tomoyo_path_acl_index {
	TOMOYO_TYPE_READ_WRITE,
	TOMOYO_TYPE_EXECUTE,
	TOMOYO_TYPE_READ,
	TOMOYO_TYPE_WRITE,
	TOMOYO_TYPE_UNLINK,
	TOMOYO_TYPE_RMDIR,
	TOMOYO_TYPE_TRUNCATE,
	TOMOYO_TYPE_SYMLINK,
	TOMOYO_TYPE_REWRITE,
	TOMOYO_TYPE_CHROOT,
	TOMOYO_TYPE_UMOUNT,
	TOMOYO_MAX_PATH_OPERATION
};

#define TOMOYO_RW_MASK ((1 << TOMOYO_TYPE_READ) | (1 << TOMOYO_TYPE_WRITE))

enum tomoyo_mkdev_acl_index {
	TOMOYO_TYPE_MKBLOCK,
	TOMOYO_TYPE_MKCHAR,
	TOMOYO_MAX_MKDEV_OPERATION
};

enum tomoyo_path2_acl_index {
	TOMOYO_TYPE_LINK,
	TOMOYO_TYPE_RENAME,
	TOMOYO_TYPE_PIVOT_ROOT,
	TOMOYO_MAX_PATH2_OPERATION
};

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

enum tomoyo_securityfs_interface_index {
	TOMOYO_DOMAINPOLICY,
	TOMOYO_EXCEPTIONPOLICY,
	TOMOYO_DOMAIN_STATUS,
	TOMOYO_PROCESS_STATUS,
	TOMOYO_MEMINFO,
	TOMOYO_SELFDOMAIN,
	TOMOYO_VERSION,
	TOMOYO_PROFILE,
	TOMOYO_QUERY,
	TOMOYO_MANAGER
};

enum tomoyo_mac_index {
	TOMOYO_MAC_FILE_EXECUTE,
	TOMOYO_MAC_FILE_OPEN,
	TOMOYO_MAC_FILE_CREATE,
	TOMOYO_MAC_FILE_UNLINK,
	TOMOYO_MAC_FILE_MKDIR,
	TOMOYO_MAC_FILE_RMDIR,
	TOMOYO_MAC_FILE_MKFIFO,
	TOMOYO_MAC_FILE_MKSOCK,
	TOMOYO_MAC_FILE_TRUNCATE,
	TOMOYO_MAC_FILE_SYMLINK,
	TOMOYO_MAC_FILE_REWRITE,
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
	TOMOYO_MAX_MAC_INDEX
};

enum tomoyo_mac_category_index {
	TOMOYO_MAC_CATEGORY_FILE,
	TOMOYO_MAX_MAC_CATEGORY_INDEX
};

#define TOMOYO_RETRY_REQUEST 1 /* Retry this request. */

/********** Structure definitions. **********/

/*
 * tomoyo_acl_head is a structure which is used for holding elements not in
 * domain policy.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_policy_list[] .
 *  (2) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_acl_head {
	struct list_head list;
	bool is_deleted;
} __packed;

/*
 * tomoyo_request_info is a structure which is used for holding
 *
 * (1) Domain information of current process.
 * (2) How many retries are made for this request.
 * (3) Profile number used for this request.
 * (4) Access control mode of the profile.
 */
struct tomoyo_request_info {
	struct tomoyo_domain_info *domain;
	/* For holding parameters. */
	union {
		struct {
			const struct tomoyo_path_info *filename;
			/* For using wildcards at tomoyo_find_next_domain(). */
			const struct tomoyo_path_info *matched_path;
			u8 operation;
		} path;
		struct {
			const struct tomoyo_path_info *filename1;
			const struct tomoyo_path_info *filename2;
			u8 operation;
		} path2;
		struct {
			const struct tomoyo_path_info *filename;
			unsigned int mode;
			unsigned int major;
			unsigned int minor;
			u8 operation;
		} mkdev;
		struct {
			const struct tomoyo_path_info *filename;
			unsigned long number;
			u8 operation;
		} path_number;
		struct {
			const struct tomoyo_path_info *type;
			const struct tomoyo_path_info *dir;
			const struct tomoyo_path_info *dev;
			unsigned long flags;
			int need_dev;
		} mount;
	} param;
	u8 param_type;
	bool granted;
	u8 retry;
	u8 profile;
	u8 mode; /* One of tomoyo_mode_index . */
	u8 type;
};

/*
 * tomoyo_path_info is a structure which is used for holding a string data
 * used by TOMOYO.
 * This structure has several fields for supporting pattern matching.
 *
 * (1) "name" is the '\0' terminated string data.
 * (2) "hash" is full_name_hash(name, strlen(name)).
 *     This allows tomoyo_pathcmp() to compare by hash before actually compare
 *     using strcmp().
 * (3) "const_len" is the length of the initial segment of "name" which
 *     consists entirely of non wildcard characters. In other words, the length
 *     which we can compare two strings using strncmp().
 * (4) "is_dir" is a bool which is true if "name" ends with "/",
 *     false otherwise.
 *     TOMOYO distinguishes directory and non-directory. A directory ends with
 *     "/" and non-directory does not end with "/".
 * (5) "is_patterned" is a bool which is true if "name" contains wildcard
 *     characters, false otherwise. This allows TOMOYO to use "hash" and
 *     strcmp() for string comparison if "is_patterned" is false.
 */
struct tomoyo_path_info {
	const char *name;
	u32 hash;          /* = full_name_hash(name, strlen(name)) */
	u16 const_len;     /* = tomoyo_const_part_length(name)     */
	bool is_dir;       /* = tomoyo_strendswith(name, "/")      */
	bool is_patterned; /* = tomoyo_path_contains_pattern(name) */
};

/*
 * tomoyo_name is a structure which is used for linking
 * "struct tomoyo_path_info" into tomoyo_name_list .
 */
struct tomoyo_name {
	struct list_head list;
	atomic_t users;
	struct tomoyo_path_info entry;
};

struct tomoyo_name_union {
	const struct tomoyo_path_info *filename;
	struct tomoyo_group *group;
	u8 is_group;
};

struct tomoyo_number_union {
	unsigned long values[2];
	struct tomoyo_group *group;
	u8 min_type;
	u8 max_type;
	u8 is_group;
};

/* Structure for "path_group"/"number_group" directive. */
struct tomoyo_group {
	struct list_head list;
	const struct tomoyo_path_info *group_name;
	struct list_head member_list;
	atomic_t users;
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

/*
 * tomoyo_acl_info is a structure which is used for holding
 *
 *  (1) "list" which is linked to the ->acl_info_list of
 *      "struct tomoyo_domain_info"
 *  (2) "is_deleted" is a bool which is true if this domain is marked as
 *      "deleted", false otherwise.
 *  (3) "type" which tells type of the entry.
 *
 * Packing "struct tomoyo_acl_info" allows
 * "struct tomoyo_path_acl" to embed "u16" and "struct tomoyo_path2_acl"
 * "struct tomoyo_path_number_acl" "struct tomoyo_mkdev_acl" to embed
 * "u8" without enlarging their structure size.
 */
struct tomoyo_acl_info {
	struct list_head list;
	bool is_deleted;
	u8 type; /* = one of values in "enum tomoyo_acl_entry_type_index". */
} __packed;

/*
 * tomoyo_domain_info is a structure which is used for holding permissions
 * (e.g. "allow_read /lib/libc-2.5.so") given to each domain.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_domain_list .
 *  (2) "acl_info_list" which is linked to "struct tomoyo_acl_info".
 *  (3) "domainname" which holds the name of the domain.
 *  (4) "profile" which remembers profile number assigned to this domain.
 *  (5) "is_deleted" is a bool which is true if this domain is marked as
 *      "deleted", false otherwise.
 *  (6) "quota_warned" is a bool which is used for suppressing warning message
 *      when learning mode learned too much entries.
 *  (7) "ignore_global_allow_read" is a bool which is true if this domain
 *      should ignore "allow_read" directive in exception policy.
 *  (8) "transition_failed" is a bool which is set to true when this domain was
 *      unable to create a new domain at tomoyo_find_next_domain() because the
 *      name of the domain to be created was too long or it could not allocate
 *      memory. If set to true, more than one process continued execve()
 *      without domain transition.
 *  (9) "users" is an atomic_t that holds how many "struct cred"->security
 *      are referring this "struct tomoyo_domain_info". If is_deleted == true
 *      and users == 0, this struct will be kfree()d upon next garbage
 *      collection.
 *
 * A domain's lifecycle is an analogy of files on / directory.
 * Multiple domains with the same domainname cannot be created (as with
 * creating files with the same filename fails with -EEXIST).
 * If a process reached a domain, that process can reside in that domain after
 * that domain is marked as "deleted" (as with a process can access an already
 * open()ed file after that file was unlink()ed).
 */
struct tomoyo_domain_info {
	struct list_head list;
	struct list_head acl_info_list;
	/* Name of this domain. Never NULL.          */
	const struct tomoyo_path_info *domainname;
	u8 profile;        /* Profile number to use. */
	bool is_deleted;   /* Delete flag.           */
	bool quota_warned; /* Quota warnning flag.   */
	bool ignore_global_allow_read; /* Ignore "allow_read" flag. */
	bool transition_failed; /* Domain transition failed flag. */
	atomic_t users; /* Number of referring credentials. */
};

/*
 * tomoyo_path_acl is a structure which is used for holding an
 * entry with one pathname operation (e.g. open(), mkdir()).
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "perm" which is a bitmask of permitted operations.
 *  (3) "name" is the pathname.
 *
 * Directives held by this structure are "allow_read/write", "allow_execute",
 * "allow_read", "allow_write", "allow_unlink", "allow_rmdir",
 * "allow_truncate", "allow_symlink", "allow_rewrite", "allow_chroot" and
 * "allow_unmount".
 */
struct tomoyo_path_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH_ACL */
	u16 perm;
	struct tomoyo_name_union name;
};

/*
 * tomoyo_path_number_acl is a structure which is used for holding an
 * entry with one pathname and one number operation.
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "perm" which is a bitmask of permitted operations.
 *  (3) "name" is the pathname.
 *  (4) "number" is the numeric value.
 *
 * Directives held by this structure are "allow_create", "allow_mkdir",
 * "allow_ioctl", "allow_mkfifo", "allow_mksock", "allow_chmod", "allow_chown"
 * and "allow_chgrp".
 *
 */
struct tomoyo_path_number_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH_NUMBER_ACL */
	u8 perm;
	struct tomoyo_name_union name;
	struct tomoyo_number_union number;
};

/*
 * tomoyo_mkdev_acl is a structure which is used for holding an
 * entry with one pathname and three numbers operation.
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "perm" which is a bitmask of permitted operations.
 *  (3) "mode" is the create mode.
 *  (4) "major" is the major number of device node.
 *  (5) "minor" is the minor number of device node.
 *
 * Directives held by this structure are "allow_mkchar", "allow_mkblock".
 *
 */
struct tomoyo_mkdev_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_MKDEV_ACL */
	u8 perm;
	struct tomoyo_name_union name;
	struct tomoyo_number_union mode;
	struct tomoyo_number_union major;
	struct tomoyo_number_union minor;
};

/*
 * tomoyo_path2_acl is a structure which is used for holding an
 * entry with two pathnames operation (i.e. link(), rename() and pivot_root()).
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "perm" which is a bitmask of permitted operations.
 *  (3) "name1" is the source/old pathname.
 *  (4) "name2" is the destination/new pathname.
 *
 * Directives held by this structure are "allow_rename", "allow_link" and
 * "allow_pivot_root".
 */
struct tomoyo_path2_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH2_ACL */
	u8 perm;
	struct tomoyo_name_union name1;
	struct tomoyo_name_union name2;
};

/*
 * tomoyo_mount_acl is a structure which is used for holding an
 * entry for mount operation.
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "dev_name" is the device name.
 *  (3) "dir_name" is the mount point.
 *  (4) "fs_type" is the filesystem type.
 *  (5) "flags" is the mount flags.
 *
 * Directive held by this structure is "allow_mount".
 */
struct tomoyo_mount_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_MOUNT_ACL */
	struct tomoyo_name_union dev_name;
	struct tomoyo_name_union dir_name;
	struct tomoyo_name_union fs_type;
	struct tomoyo_number_union flags;
};

#define TOMOYO_MAX_IO_READ_QUEUE 32

/*
 * Structure for reading/writing policy via /sys/kernel/security/tomoyo
 * interfaces.
 */
struct tomoyo_io_buffer {
	void (*read) (struct tomoyo_io_buffer *);
	int (*write) (struct tomoyo_io_buffer *);
	int (*poll) (struct file *file, poll_table *wait);
	/* Exclusive lock for this structure.   */
	struct mutex io_sem;
	/* Index returned by tomoyo_read_lock(). */
	int reader_idx;
	char __user *read_user_buf;
	int read_user_buf_avail;
	struct {
		struct list_head *domain;
		struct list_head *group;
		struct list_head *acl;
		int avail;
		int step;
		int query_index;
		u16 index;
		u8 bit;
		u8 w_pos;
		bool eof;
		bool print_this_domain_only;
		bool print_execute_only;
		const char *w[TOMOYO_MAX_IO_READ_QUEUE];
	} r;
	/* The position currently writing to.   */
	struct tomoyo_domain_info *write_var1;
	/* Buffer for reading.                  */
	char *read_buf;
	/* Size of read buffer.                 */
	int readbuf_size;
	/* Buffer for writing.                  */
	char *write_buf;
	/* Bytes available for writing.         */
	int write_avail;
	/* Size of write buffer.                */
	int writebuf_size;
	/* Type of this interface.              */
	u8 type;
};

/*
 * tomoyo_readable_file is a structure which is used for holding
 * "allow_read" entries.
 * It has following fields.
 *
 *  (1) "head" is "struct tomoyo_acl_head".
 *  (2) "filename" is a pathname which is allowed to open(O_RDONLY).
 */
struct tomoyo_readable_file {
	struct tomoyo_acl_head head;
	const struct tomoyo_path_info *filename;
};

/*
 * tomoyo_no_pattern is a structure which is used for holding
 * "file_pattern" entries.
 * It has following fields.
 *
 *  (1) "head" is "struct tomoyo_acl_head".
 *  (2) "pattern" is a pathname pattern which is used for converting pathnames
 *      to pathname patterns during learning mode.
 */
struct tomoyo_no_pattern {
	struct tomoyo_acl_head head;
	const struct tomoyo_path_info *pattern;
};

/*
 * tomoyo_no_rewrite is a structure which is used for holding
 * "deny_rewrite" entries.
 * It has following fields.
 *
 *  (1) "head" is "struct tomoyo_acl_head".
 *  (2) "pattern" is a pathname which is by default not permitted to modify
 *      already existing content.
 */
struct tomoyo_no_rewrite {
	struct tomoyo_acl_head head;
	const struct tomoyo_path_info *pattern;
};

/*
 * tomoyo_transition_control is a structure which is used for holding
 * "initialize_domain"/"no_initialize_domain"/"keep_domain"/"no_keep_domain"
 * entries.
 * It has following fields.
 *
 *  (1) "head" is "struct tomoyo_acl_head".
 *  (2) "type" is type of this entry.
 *  (3) "is_last_name" is a bool which is true if "domainname" is "the last
 *      component of a domainname", false otherwise.
 *  (4) "domainname" which is "a domainname" or "the last component of a
 *      domainname".
 *  (5) "program" which is a program's pathname.
 */
struct tomoyo_transition_control {
	struct tomoyo_acl_head head;
	u8 type; /* One of values in "enum tomoyo_transition_type".  */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
	const struct tomoyo_path_info *domainname; /* Maybe NULL */
	const struct tomoyo_path_info *program;    /* Maybe NULL */
};

/*
 * tomoyo_aggregator is a structure which is used for holding
 * "aggregator" entries.
 * It has following fields.
 *
 *  (1) "head" is "struct tomoyo_acl_head".
 *  (2) "original_name" which is originally requested name.
 *  (3) "aggregated_name" which is name to rewrite.
 */
struct tomoyo_aggregator {
	struct tomoyo_acl_head head;
	const struct tomoyo_path_info *original_name;
	const struct tomoyo_path_info *aggregated_name;
};

/*
 * tomoyo_manager is a structure which is used for holding list of
 * domainnames or programs which are permitted to modify configuration via
 * /sys/kernel/security/tomoyo/ interface.
 * It has following fields.
 *
 *  (1) "head" is "struct tomoyo_acl_head".
 *  (2) "is_domain" is a bool which is true if "manager" is a domainname, false
 *      otherwise.
 *  (3) "manager" is a domainname or a program's pathname.
 */
struct tomoyo_manager {
	struct tomoyo_acl_head head;
	bool is_domain;  /* True if manager is a domainname. */
	/* A path to program or a domainname. */
	const struct tomoyo_path_info *manager;
};

struct tomoyo_preference {
	unsigned int learning_max_entry;
	bool enforcing_verbose;
	bool learning_verbose;
	bool permissive_verbose;
};

struct tomoyo_profile {
	const struct tomoyo_path_info *comment;
	struct tomoyo_preference *learning;
	struct tomoyo_preference *permissive;
	struct tomoyo_preference *enforcing;
	struct tomoyo_preference preference;
	u8 default_config;
	u8 config[TOMOYO_MAX_MAC_INDEX + TOMOYO_MAX_MAC_CATEGORY_INDEX];
};

/********** Function prototypes. **********/

/* Check whether the given string starts with the given keyword. */
bool tomoyo_str_starts(char **src, const char *find);
/* Get tomoyo_realpath() of current process. */
const char *tomoyo_get_exe(void);
/* Format string. */
void tomoyo_normalize_line(unsigned char *buffer);
/* Print warning or error message on console. */
void tomoyo_warn_log(struct tomoyo_request_info *r, const char *fmt, ...)
     __attribute__ ((format(printf, 2, 3)));
/* Check all profiles currently assigned to domains are defined. */
void tomoyo_check_profile(void);
/* Open operation for /sys/kernel/security/tomoyo/ interface. */
int tomoyo_open_control(const u8 type, struct file *file);
/* Close /sys/kernel/security/tomoyo/ interface. */
int tomoyo_close_control(struct file *file);
/* Poll operation for /sys/kernel/security/tomoyo/ interface. */
int tomoyo_poll_control(struct file *file, poll_table *wait);
/* Read operation for /sys/kernel/security/tomoyo/ interface. */
int tomoyo_read_control(struct file *file, char __user *buffer,
			const int buffer_len);
/* Write operation for /sys/kernel/security/tomoyo/ interface. */
int tomoyo_write_control(struct file *file, const char __user *buffer,
			 const int buffer_len);
/* Check whether the domain has too many ACL entries to hold. */
bool tomoyo_domain_quota_is_ok(struct tomoyo_request_info *r);
/* Print out of memory warning message. */
void tomoyo_warn_oom(const char *function);
/* Check whether the given name matches the given name_union. */
const struct tomoyo_path_info *
tomoyo_compare_name_union(const struct tomoyo_path_info *name,
			  const struct tomoyo_name_union *ptr);
/* Check whether the given number matches the given number_union. */
bool tomoyo_compare_number_union(const unsigned long value,
				 const struct tomoyo_number_union *ptr);
int tomoyo_get_mode(const u8 profile, const u8 index);
void tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt, ...)
	__attribute__ ((format(printf, 2, 3)));
/* Check whether the domainname is correct. */
bool tomoyo_correct_domain(const unsigned char *domainname);
/* Check whether the token is correct. */
bool tomoyo_correct_path(const char *filename);
bool tomoyo_correct_word(const char *string);
/* Check whether the token can be a domainname. */
bool tomoyo_domain_def(const unsigned char *buffer);
bool tomoyo_parse_name_union(const char *filename,
			     struct tomoyo_name_union *ptr);
/* Check whether the given filename matches the given path_group. */
const struct tomoyo_path_info *
tomoyo_path_matches_group(const struct tomoyo_path_info *pathname,
			  const struct tomoyo_group *group);
/* Check whether the given value matches the given number_group. */
bool tomoyo_number_matches_group(const unsigned long min,
				 const unsigned long max,
				 const struct tomoyo_group *group);
/* Check whether the given filename matches the given pattern. */
bool tomoyo_path_matches_pattern(const struct tomoyo_path_info *filename,
				 const struct tomoyo_path_info *pattern);

bool tomoyo_parse_number_union(char *data, struct tomoyo_number_union *num);
/* Tokenize a line. */
bool tomoyo_tokenize(char *buffer, char *w[], size_t size);
/* Write domain policy violation warning message to console? */
bool tomoyo_verbose_mode(const struct tomoyo_domain_info *domain);
/* Fill "struct tomoyo_request_info". */
int tomoyo_init_request_info(struct tomoyo_request_info *r,
			     struct tomoyo_domain_info *domain,
			     const u8 index);
/* Check permission for mount operation. */
int tomoyo_mount_permission(char *dev_name, struct path *path, char *type,
			    unsigned long flags, void *data_page);
/* Create "aggregator" entry in exception policy. */
int tomoyo_write_aggregator(char *data, const bool is_delete);
int tomoyo_write_transition_control(char *data, const bool is_delete,
				    const u8 type);
/*
 * Create "allow_read/write", "allow_execute", "allow_read", "allow_write",
 * "allow_create", "allow_unlink", "allow_mkdir", "allow_rmdir",
 * "allow_mkfifo", "allow_mksock", "allow_mkblock", "allow_mkchar",
 * "allow_truncate", "allow_symlink", "allow_rewrite", "allow_rename" and
 * "allow_link" entry in domain policy.
 */
int tomoyo_write_file(char *data, struct tomoyo_domain_info *domain,
		      const bool is_delete);
/* Create "allow_read" entry in exception policy. */
int tomoyo_write_globally_readable(char *data, const bool is_delete);
/* Create "allow_mount" entry in domain policy. */
int tomoyo_write_mount(char *data, struct tomoyo_domain_info *domain,
		       const bool is_delete);
/* Create "deny_rewrite" entry in exception policy. */
int tomoyo_write_no_rewrite(char *data, const bool is_delete);
/* Create "file_pattern" entry in exception policy. */
int tomoyo_write_pattern(char *data, const bool is_delete);
/* Create "path_group"/"number_group" entry in exception policy. */
int tomoyo_write_group(char *data, const bool is_delete, const u8 type);
int tomoyo_supervisor(struct tomoyo_request_info *r, const char *fmt, ...)
     __attribute__ ((format(printf, 2, 3)));
/* Find a domain by the given name. */
struct tomoyo_domain_info *tomoyo_find_domain(const char *domainname);
/* Find or create a domain by the given name. */
struct tomoyo_domain_info *tomoyo_assign_domain(const char *domainname,
						const u8 profile);
struct tomoyo_profile *tomoyo_profile(const u8 profile);
/*
 * Allocate memory for "struct tomoyo_path_group"/"struct tomoyo_number_group".
 */
struct tomoyo_group *tomoyo_get_group(const char *group_name, const u8 type);

/* Check mode for specified functionality. */
unsigned int tomoyo_check_flags(const struct tomoyo_domain_info *domain,
				const u8 index);
/* Fill in "struct tomoyo_path_info" members. */
void tomoyo_fill_path_info(struct tomoyo_path_info *ptr);
/* Run policy loader when /sbin/init starts. */
void tomoyo_load_policy(const char *filename);

void tomoyo_put_number_union(struct tomoyo_number_union *ptr);

/* Convert binary string to ascii string. */
char *tomoyo_encode(const char *str);

/*
 * Returns realpath(3) of the given pathname except that
 * ignores chroot'ed root and does not follow the final symlink.
 */
char *tomoyo_realpath_nofollow(const char *pathname);
/*
 * Returns realpath(3) of the given pathname except that
 * ignores chroot'ed root and the pathname is already solved.
 */
char *tomoyo_realpath_from_path(struct path *path);
/* Get patterned pathname. */
const char *tomoyo_pattern(const struct tomoyo_path_info *filename);

/* Check memory quota. */
bool tomoyo_memory_ok(void *ptr);
void *tomoyo_commit_ok(void *data, const unsigned int size);

/*
 * Keep the given name on the RAM.
 * The RAM is shared, so NEVER try to modify or kfree() the returned name.
 */
const struct tomoyo_path_info *tomoyo_get_name(const char *name);

/* Check for memory usage. */
void tomoyo_read_memory_counter(struct tomoyo_io_buffer *head);

/* Set memory quota. */
int tomoyo_write_memory_quota(struct tomoyo_io_buffer *head);

/* Initialize mm related code. */
void __init tomoyo_mm_init(void);
int tomoyo_path_permission(struct tomoyo_request_info *r, u8 operation,
			   const struct tomoyo_path_info *filename);
int tomoyo_check_open_permission(struct tomoyo_domain_info *domain,
				 struct path *path, const int flag);
int tomoyo_path_number_perm(const u8 operation, struct path *path,
			    unsigned long number);
int tomoyo_mkdev_perm(const u8 operation, struct path *path,
		      const unsigned int mode, unsigned int dev);
int tomoyo_path_perm(const u8 operation, struct path *path);
int tomoyo_path2_perm(const u8 operation, struct path *path1,
		      struct path *path2);
int tomoyo_find_next_domain(struct linux_binprm *bprm);

void tomoyo_print_ulong(char *buffer, const int buffer_len,
			const unsigned long value, const u8 type);

/* Drop refcount on tomoyo_name_union. */
void tomoyo_put_name_union(struct tomoyo_name_union *ptr);

/* Run garbage collector. */
void tomoyo_run_gc(void);

void tomoyo_memory_free(void *ptr);

int tomoyo_update_domain(struct tomoyo_acl_info *new_entry, const int size,
			 bool is_delete, struct tomoyo_domain_info *domain,
			 bool (*check_duplicate) (const struct tomoyo_acl_info
						  *,
						  const struct tomoyo_acl_info
						  *),
			 bool (*merge_duplicate) (struct tomoyo_acl_info *,
						  struct tomoyo_acl_info *,
						  const bool));
int tomoyo_update_policy(struct tomoyo_acl_head *new_entry, const int size,
			 bool is_delete, struct list_head *list,
			 bool (*check_duplicate) (const struct tomoyo_acl_head
						  *,
						  const struct tomoyo_acl_head
						  *));
void tomoyo_check_acl(struct tomoyo_request_info *r,
		      bool (*check_entry) (struct tomoyo_request_info *,
					   const struct tomoyo_acl_info *));

/********** External variable definitions. **********/

/* Lock for GC. */
extern struct srcu_struct tomoyo_ss;

/* The list for "struct tomoyo_domain_info". */
extern struct list_head tomoyo_domain_list;

extern struct list_head tomoyo_policy_list[TOMOYO_MAX_POLICY];
extern struct list_head tomoyo_group_list[TOMOYO_MAX_GROUP];
extern struct list_head tomoyo_name_list[TOMOYO_MAX_HASH];

/* Lock for protecting policy. */
extern struct mutex tomoyo_policy_lock;

/* Has /sbin/init started? */
extern bool tomoyo_policy_loaded;

/* The kernel's domain. */
extern struct tomoyo_domain_info tomoyo_kernel_domain;

extern const char *tomoyo_path_keyword[TOMOYO_MAX_PATH_OPERATION];
extern const char *tomoyo_mkdev_keyword[TOMOYO_MAX_MKDEV_OPERATION];
extern const char *tomoyo_path2_keyword[TOMOYO_MAX_PATH2_OPERATION];
extern const char *tomoyo_path_number_keyword[TOMOYO_MAX_PATH_NUMBER_OPERATION];

extern unsigned int tomoyo_quota_for_query;
extern unsigned int tomoyo_query_memory_size;

/********** Inlined functions. **********/

static inline int tomoyo_read_lock(void)
{
	return srcu_read_lock(&tomoyo_ss);
}

static inline void tomoyo_read_unlock(int idx)
{
	srcu_read_unlock(&tomoyo_ss, idx);
}

/* strcmp() for "struct tomoyo_path_info" structure. */
static inline bool tomoyo_pathcmp(const struct tomoyo_path_info *a,
				  const struct tomoyo_path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

/**
 * tomoyo_valid - Check whether the character is a valid char.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a valid character, false otherwise.
 */
static inline bool tomoyo_valid(const unsigned char c)
{
	return c > ' ' && c < 127;
}

/**
 * tomoyo_invalid - Check whether the character is an invalid char.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an invalid character, false otherwise.
 */
static inline bool tomoyo_invalid(const unsigned char c)
{
	return c && (c <= ' ' || c >= 127);
}

static inline void tomoyo_put_name(const struct tomoyo_path_info *name)
{
	if (name) {
		struct tomoyo_name *ptr =
			container_of(name, typeof(*ptr), entry);
		atomic_dec(&ptr->users);
	}
}

static inline void tomoyo_put_group(struct tomoyo_group *group)
{
	if (group)
		atomic_dec(&group->users);
}

static inline struct tomoyo_domain_info *tomoyo_domain(void)
{
	return current_cred()->security;
}

static inline struct tomoyo_domain_info *tomoyo_real_domain(struct task_struct
							    *task)
{
	return task_cred_xxx(task, security);
}

static inline bool tomoyo_same_acl_head(const struct tomoyo_acl_info *p1,
					   const struct tomoyo_acl_info *p2)
{
	return p1->type == p2->type;
}

static inline bool tomoyo_same_name_union
(const struct tomoyo_name_union *p1, const struct tomoyo_name_union *p2)
{
	return p1->filename == p2->filename && p1->group == p2->group &&
		p1->is_group == p2->is_group;
}

static inline bool tomoyo_same_number_union
(const struct tomoyo_number_union *p1, const struct tomoyo_number_union *p2)
{
	return p1->values[0] == p2->values[0] && p1->values[1] == p2->values[1]
		&& p1->group == p2->group && p1->min_type == p2->min_type &&
		p1->max_type == p2->max_type && p1->is_group == p2->is_group;
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
