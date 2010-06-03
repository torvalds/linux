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
	TOMOYO_CONFIG_ENFORCING
};

/* Keywords for ACLs. */
#define TOMOYO_KEYWORD_AGGREGATOR                "aggregator "
#define TOMOYO_KEYWORD_ALIAS                     "alias "
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

/* Index numbers for Access Controls. */
enum tomoyo_mac_index {
	TOMOYO_MAC_FOR_FILE,  /* domain_policy.conf */
	TOMOYO_MAX_ACCEPT_ENTRY,
	TOMOYO_VERBOSE,
	TOMOYO_MAX_CONTROL_INDEX
};

/* Index numbers for Access Controls. */
enum tomoyo_acl_entry_type_index {
	TOMOYO_TYPE_PATH_ACL,
	TOMOYO_TYPE_PATH2_ACL,
	TOMOYO_TYPE_PATH_NUMBER_ACL,
	TOMOYO_TYPE_PATH_NUMBER3_ACL,
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

enum tomoyo_path_number3_acl_index {
	TOMOYO_TYPE_MKBLOCK,
	TOMOYO_TYPE_MKCHAR,
	TOMOYO_MAX_PATH_NUMBER3_OPERATION
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

#define TOMOYO_RETRY_REQUEST 1 /* Retry this request. */

/********** Structure definitions. **********/

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
	u8 retry;
	u8 profile;
	u8 mode; /* One of tomoyo_mode_index . */
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
 * tomoyo_name_entry is a structure which is used for linking
 * "struct tomoyo_path_info" into tomoyo_name_list .
 */
struct tomoyo_name_entry {
	struct list_head list;
	atomic_t users;
	struct tomoyo_path_info entry;
};

struct tomoyo_name_union {
	const struct tomoyo_path_info *filename;
	struct tomoyo_path_group *group;
	u8 is_group;
};

struct tomoyo_number_union {
	unsigned long values[2];
	struct tomoyo_number_group *group;
	u8 min_type;
	u8 max_type;
	u8 is_group;
};

/* Structure for "path_group" directive. */
struct tomoyo_path_group {
	struct list_head list;
	const struct tomoyo_path_info *group_name;
	struct list_head member_list;
	atomic_t users;
};

/* Structure for "number_group" directive. */
struct tomoyo_number_group {
	struct list_head list;
	const struct tomoyo_path_info *group_name;
	struct list_head member_list;
	atomic_t users;
};

/* Structure for "path_group" directive. */
struct tomoyo_path_group_member {
	struct list_head list;
	bool is_deleted;
	const struct tomoyo_path_info *member_name;
};

/* Structure for "number_group" directive. */
struct tomoyo_number_group_member {
	struct list_head list;
	bool is_deleted;
	struct tomoyo_number_union number;
};

/*
 * tomoyo_acl_info is a structure which is used for holding
 *
 *  (1) "list" which is linked to the ->acl_info_list of
 *      "struct tomoyo_domain_info"
 *  (2) "type" which tells type of the entry (either
 *      "struct tomoyo_path_acl" or "struct tomoyo_path2_acl").
 *
 * Packing "struct tomoyo_acl_info" allows
 * "struct tomoyo_path_acl" to embed "u8" + "u16" and
 * "struct tomoyo_path2_acl" to embed "u8"
 * without enlarging their structure size.
 */
struct tomoyo_acl_info {
	struct list_head list;
	u8 type;
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
 * tomoyo_path_number3_acl is a structure which is used for holding an
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
struct tomoyo_path_number3_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_PATH_NUMBER3_ACL */
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
 *  (2) "is_deleted" is boolean.
 *  (3) "dev_name" is the device name.
 *  (4) "dir_name" is the mount point.
 *  (5) "flags" is the mount flags.
 *
 * Directives held by this structure are "allow_rename", "allow_link" and
 * "allow_pivot_root".
 */
struct tomoyo_mount_acl {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_MOUNT_ACL */
	bool is_deleted;
	struct tomoyo_name_union dev_name;
	struct tomoyo_name_union dir_name;
	struct tomoyo_name_union fs_type;
	struct tomoyo_number_union flags;
};

/*
 * tomoyo_io_buffer is a structure which is used for reading and modifying
 * configuration via /sys/kernel/security/tomoyo/ interface.
 * It has many fields. ->read_var1 , ->read_var2 , ->write_var1 are used as
 * cursors.
 *
 * Since the content of /sys/kernel/security/tomoyo/domain_policy is a list of
 * "struct tomoyo_domain_info" entries and each "struct tomoyo_domain_info"
 * entry has a list of "struct tomoyo_acl_info", we need two cursors when
 * reading (one is for traversing tomoyo_domain_list and the other is for
 * traversing "struct tomoyo_acl_info"->acl_info_list ).
 *
 * If a line written to /sys/kernel/security/tomoyo/domain_policy starts with
 * "select ", TOMOYO seeks the cursor ->read_var1 and ->write_var1 to the
 * domain with the domainname specified by the rest of that line (NULL is set
 * if seek failed).
 * If a line written to /sys/kernel/security/tomoyo/domain_policy starts with
 * "delete ", TOMOYO deletes an entry or a domain specified by the rest of that
 * line (->write_var1 is set to NULL if a domain was deleted).
 * If a line written to /sys/kernel/security/tomoyo/domain_policy starts with
 * neither "select " nor "delete ", an entry or a domain specified by that line
 * is appended.
 */
struct tomoyo_io_buffer {
	int (*read) (struct tomoyo_io_buffer *);
	int (*write) (struct tomoyo_io_buffer *);
	int (*poll) (struct file *file, poll_table *wait);
	/* Exclusive lock for this structure.   */
	struct mutex io_sem;
	/* Index returned by tomoyo_read_lock(). */
	int reader_idx;
	/* The position currently reading from. */
	struct list_head *read_var1;
	/* Extra variables for reading.         */
	struct list_head *read_var2;
	/* The position currently writing to.   */
	struct tomoyo_domain_info *write_var1;
	/* The step for reading.                */
	int read_step;
	/* Buffer for reading.                  */
	char *read_buf;
	/* EOF flag for reading.                */
	bool read_eof;
	/* Read domain ACL of specified PID?    */
	bool read_single_domain;
	/* Extra variable for reading.          */
	u8 read_bit;
	/* Bytes available for reading.         */
	int read_avail;
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
 * tomoyo_globally_readable_file_entry is a structure which is used for holding
 * "allow_read" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_globally_readable_list .
 *  (2) "filename" is a pathname which is allowed to open(O_RDONLY).
 *  (3) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_globally_readable_file_entry {
	struct list_head list;
	const struct tomoyo_path_info *filename;
	bool is_deleted;
};

/*
 * tomoyo_pattern_entry is a structure which is used for holding
 * "tomoyo_pattern_list" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_pattern_list .
 *  (2) "pattern" is a pathname pattern which is used for converting pathnames
 *      to pathname patterns during learning mode.
 *  (3) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_pattern_entry {
	struct list_head list;
	const struct tomoyo_path_info *pattern;
	bool is_deleted;
};

/*
 * tomoyo_no_rewrite_entry is a structure which is used for holding
 * "deny_rewrite" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_no_rewrite_list .
 *  (2) "pattern" is a pathname which is by default not permitted to modify
 *      already existing content.
 *  (3) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_no_rewrite_entry {
	struct list_head list;
	const struct tomoyo_path_info *pattern;
	bool is_deleted;
};

/*
 * tomoyo_domain_initializer_entry is a structure which is used for holding
 * "initialize_domain" and "no_initialize_domain" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_domain_initializer_list .
 *  (2) "domainname" which is "a domainname" or "the last component of a
 *      domainname". This field is NULL if "from" clause is not specified.
 *  (3) "program" which is a program's pathname.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 *  (5) "is_not" is a bool which is true if "no_initialize_domain", false
 *      otherwise.
 *  (6) "is_last_name" is a bool which is true if "domainname" is "the last
 *      component of a domainname", false otherwise.
 */
struct tomoyo_domain_initializer_entry {
	struct list_head list;
	const struct tomoyo_path_info *domainname;    /* This may be NULL */
	const struct tomoyo_path_info *program;
	bool is_deleted;
	bool is_not;       /* True if this entry is "no_initialize_domain".  */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
};

/*
 * tomoyo_domain_keeper_entry is a structure which is used for holding
 * "keep_domain" and "no_keep_domain" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_domain_keeper_list .
 *  (2) "domainname" which is "a domainname" or "the last component of a
 *      domainname".
 *  (3) "program" which is a program's pathname.
 *      This field is NULL if "from" clause is not specified.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 *  (5) "is_not" is a bool which is true if "no_initialize_domain", false
 *      otherwise.
 *  (6) "is_last_name" is a bool which is true if "domainname" is "the last
 *      component of a domainname", false otherwise.
 */
struct tomoyo_domain_keeper_entry {
	struct list_head list;
	const struct tomoyo_path_info *domainname;
	const struct tomoyo_path_info *program;       /* This may be NULL */
	bool is_deleted;
	bool is_not;       /* True if this entry is "no_keep_domain".        */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
};

/*
 * tomoyo_aggregator_entry is a structure which is used for holding
 * "aggregator" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_aggregator_list .
 *  (2) "original_name" which is originally requested name.
 *  (3) "aggregated_name" which is name to rewrite.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_aggregator_entry {
	struct list_head list;
	const struct tomoyo_path_info *original_name;
	const struct tomoyo_path_info *aggregated_name;
	bool is_deleted;
};

/*
 * tomoyo_alias_entry is a structure which is used for holding "alias" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_alias_list .
 *  (2) "original_name" which is a dereferenced pathname.
 *  (3) "aliased_name" which is a symlink's pathname.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_alias_entry {
	struct list_head list;
	const struct tomoyo_path_info *original_name;
	const struct tomoyo_path_info *aliased_name;
	bool is_deleted;
};

/*
 * tomoyo_policy_manager_entry is a structure which is used for holding list of
 * domainnames or programs which are permitted to modify configuration via
 * /sys/kernel/security/tomoyo/ interface.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_policy_manager_list .
 *  (2) "manager" is a domainname or a program's pathname.
 *  (3) "is_domain" is a bool which is true if "manager" is a domainname, false
 *      otherwise.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_policy_manager_entry {
	struct list_head list;
	/* A path to program or a domainname. */
	const struct tomoyo_path_info *manager;
	bool is_domain;  /* True if manager is a domainname. */
	bool is_deleted; /* True if this entry is deleted. */
};

/********** Function prototypes. **********/

extern asmlinkage long sys_getpid(void);
extern asmlinkage long sys_getppid(void);

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
bool tomoyo_compare_name_union(const struct tomoyo_path_info *name,
			       const struct tomoyo_name_union *ptr);
/* Check whether the given number matches the given number_union. */
bool tomoyo_compare_number_union(const unsigned long value,
				 const struct tomoyo_number_union *ptr);
/* Transactional sprintf() for policy dump. */
bool tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt, ...)
	__attribute__ ((format(printf, 2, 3)));
/* Check whether the domainname is correct. */
bool tomoyo_is_correct_domain(const unsigned char *domainname);
/* Check whether the token is correct. */
bool tomoyo_is_correct_path(const char *filename);
bool tomoyo_is_correct_word(const char *string);
/* Check whether the token can be a domainname. */
bool tomoyo_is_domain_def(const unsigned char *buffer);
bool tomoyo_parse_name_union(const char *filename,
			     struct tomoyo_name_union *ptr);
/* Check whether the given filename matches the given path_group. */
bool tomoyo_path_matches_group(const struct tomoyo_path_info *pathname,
			       const struct tomoyo_path_group *group);
/* Check whether the given value matches the given number_group. */
bool tomoyo_number_matches_group(const unsigned long min,
				 const unsigned long max,
				 const struct tomoyo_number_group *group);
/* Check whether the given filename matches the given pattern. */
bool tomoyo_path_matches_pattern(const struct tomoyo_path_info *filename,
				 const struct tomoyo_path_info *pattern);

bool tomoyo_print_number_union(struct tomoyo_io_buffer *head,
			       const struct tomoyo_number_union *ptr);
bool tomoyo_parse_number_union(char *data, struct tomoyo_number_union *num);

/* Read "aggregator" entry in exception policy. */
bool tomoyo_read_aggregator_policy(struct tomoyo_io_buffer *head);
/* Read "alias" entry in exception policy. */
bool tomoyo_read_alias_policy(struct tomoyo_io_buffer *head);
/*
 * Read "initialize_domain" and "no_initialize_domain" entry
 * in exception policy.
 */
bool tomoyo_read_domain_initializer_policy(struct tomoyo_io_buffer *head);
/* Read "keep_domain" and "no_keep_domain" entry in exception policy. */
bool tomoyo_read_domain_keeper_policy(struct tomoyo_io_buffer *head);
/* Read "file_pattern" entry in exception policy. */
bool tomoyo_read_file_pattern(struct tomoyo_io_buffer *head);
/* Read "path_group" entry in exception policy. */
bool tomoyo_read_path_group_policy(struct tomoyo_io_buffer *head);
/* Read "number_group" entry in exception policy. */
bool tomoyo_read_number_group_policy(struct tomoyo_io_buffer *head);
/* Read "allow_read" entry in exception policy. */
bool tomoyo_read_globally_readable_policy(struct tomoyo_io_buffer *head);
/* Read "deny_rewrite" entry in exception policy. */
bool tomoyo_read_no_rewrite_policy(struct tomoyo_io_buffer *head);
/* Tokenize a line. */
bool tomoyo_tokenize(char *buffer, char *w[], size_t size);
/* Write domain policy violation warning message to console? */
bool tomoyo_verbose_mode(const struct tomoyo_domain_info *domain);
/* Convert double path operation to operation name. */
const char *tomoyo_path22keyword(const u8 operation);
const char *tomoyo_path_number2keyword(const u8 operation);
const char *tomoyo_path_number32keyword(const u8 operation);
/* Get the last component of the given domainname. */
const char *tomoyo_get_last_name(const struct tomoyo_domain_info *domain);
/* Convert single path operation to operation name. */
const char *tomoyo_path2keyword(const u8 operation);
/* Fill "struct tomoyo_request_info". */
int tomoyo_init_request_info(struct tomoyo_request_info *r,
			     struct tomoyo_domain_info *domain);
/* Check permission for mount operation. */
int tomoyo_mount_permission(char *dev_name, struct path *path, char *type,
			    unsigned long flags, void *data_page);
/* Create "aggregator" entry in exception policy. */
int tomoyo_write_aggregator_policy(char *data, const bool is_delete);
/* Create "alias" entry in exception policy. */
int tomoyo_write_alias_policy(char *data, const bool is_delete);
/*
 * Create "initialize_domain" and "no_initialize_domain" entry
 * in exception policy.
 */
int tomoyo_write_domain_initializer_policy(char *data, const bool is_not,
					   const bool is_delete);
/* Create "keep_domain" and "no_keep_domain" entry in exception policy. */
int tomoyo_write_domain_keeper_policy(char *data, const bool is_not,
				      const bool is_delete);
/*
 * Create "allow_read/write", "allow_execute", "allow_read", "allow_write",
 * "allow_create", "allow_unlink", "allow_mkdir", "allow_rmdir",
 * "allow_mkfifo", "allow_mksock", "allow_mkblock", "allow_mkchar",
 * "allow_truncate", "allow_symlink", "allow_rewrite", "allow_rename" and
 * "allow_link" entry in domain policy.
 */
int tomoyo_write_file_policy(char *data, struct tomoyo_domain_info *domain,
			     const bool is_delete);
/* Create "allow_read" entry in exception policy. */
int tomoyo_write_globally_readable_policy(char *data, const bool is_delete);
/* Create "allow_mount" entry in domain policy. */
int tomoyo_write_mount_policy(char *data, struct tomoyo_domain_info *domain,
			      const bool is_delete);
/* Create "deny_rewrite" entry in exception policy. */
int tomoyo_write_no_rewrite_policy(char *data, const bool is_delete);
/* Create "file_pattern" entry in exception policy. */
int tomoyo_write_pattern_policy(char *data, const bool is_delete);
/* Create "path_group" entry in exception policy. */
int tomoyo_write_path_group_policy(char *data, const bool is_delete);
int tomoyo_supervisor(struct tomoyo_request_info *r, const char *fmt, ...)
     __attribute__ ((format(printf, 2, 3)));
/* Create "number_group" entry in exception policy. */
int tomoyo_write_number_group_policy(char *data, const bool is_delete);
/* Find a domain by the given name. */
struct tomoyo_domain_info *tomoyo_find_domain(const char *domainname);
/* Find or create a domain by the given name. */
struct tomoyo_domain_info *tomoyo_find_or_assign_new_domain(const char *
							    domainname,
							    const u8 profile);
/* Allocate memory for "struct tomoyo_path_group". */
struct tomoyo_path_group *tomoyo_get_path_group(const char *group_name);
struct tomoyo_number_group *tomoyo_get_number_group(const char *group_name);

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
 * Returns realpath(3) of the given pathname but ignores chroot'ed root.
 * These functions use kzalloc(), so the caller must call kfree()
 * if these functions didn't return NULL.
 */
char *tomoyo_realpath(const char *pathname);
/*
 * Same with tomoyo_realpath() except that it doesn't follow the final symlink.
 */
char *tomoyo_realpath_nofollow(const char *pathname);
/* Same with tomoyo_realpath() except that the pathname is already solved. */
char *tomoyo_realpath_from_path(struct path *path);
/* Get patterned pathname. */
const char *tomoyo_file_pattern(const struct tomoyo_path_info *filename);

/* Check memory quota. */
bool tomoyo_memory_ok(void *ptr);
void *tomoyo_commit_ok(void *data, const unsigned int size);

/*
 * Keep the given name on the RAM.
 * The RAM is shared, so NEVER try to modify or kfree() the returned name.
 */
const struct tomoyo_path_info *tomoyo_get_name(const char *name);

/* Check for memory usage. */
int tomoyo_read_memory_counter(struct tomoyo_io_buffer *head);

/* Set memory quota. */
int tomoyo_write_memory_quota(struct tomoyo_io_buffer *head);

/* Initialize mm related code. */
void __init tomoyo_mm_init(void);
int tomoyo_check_exec_perm(struct tomoyo_domain_info *domain,
			   const struct tomoyo_path_info *filename);
int tomoyo_check_open_permission(struct tomoyo_domain_info *domain,
				 struct path *path, const int flag);
int tomoyo_path_number_perm(const u8 operation, struct path *path,
			    unsigned long number);
int tomoyo_path_number3_perm(const u8 operation, struct path *path,
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

/********** External variable definitions. **********/

/* Lock for GC. */
extern struct srcu_struct tomoyo_ss;

/* The list for "struct tomoyo_domain_info". */
extern struct list_head tomoyo_domain_list;

extern struct list_head tomoyo_path_group_list;
extern struct list_head tomoyo_number_group_list;
extern struct list_head tomoyo_domain_initializer_list;
extern struct list_head tomoyo_domain_keeper_list;
extern struct list_head tomoyo_aggregator_list;
extern struct list_head tomoyo_alias_list;
extern struct list_head tomoyo_globally_readable_list;
extern struct list_head tomoyo_pattern_list;
extern struct list_head tomoyo_no_rewrite_list;
extern struct list_head tomoyo_policy_manager_list;
extern struct list_head tomoyo_name_list[TOMOYO_MAX_HASH];

/* Lock for protecting policy. */
extern struct mutex tomoyo_policy_lock;

/* Has /sbin/init started? */
extern bool tomoyo_policy_loaded;

/* The kernel's domain. */
extern struct tomoyo_domain_info tomoyo_kernel_domain;

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
 * tomoyo_is_valid - Check whether the character is a valid char.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a valid character, false otherwise.
 */
static inline bool tomoyo_is_valid(const unsigned char c)
{
	return c > ' ' && c < 127;
}

/**
 * tomoyo_is_invalid - Check whether the character is an invalid char.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an invalid character, false otherwise.
 */
static inline bool tomoyo_is_invalid(const unsigned char c)
{
	return c && (c <= ' ' || c >= 127);
}

static inline void tomoyo_put_name(const struct tomoyo_path_info *name)
{
	if (name) {
		struct tomoyo_name_entry *ptr =
			container_of(name, struct tomoyo_name_entry, entry);
		atomic_dec(&ptr->users);
	}
}

static inline void tomoyo_put_path_group(struct tomoyo_path_group *group)
{
	if (group)
		atomic_dec(&group->users);
}

static inline void tomoyo_put_number_group(struct tomoyo_number_group *group)
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

static inline bool tomoyo_is_same_acl_head(const struct tomoyo_acl_info *p1,
					   const struct tomoyo_acl_info *p2)
{
	return p1->type == p2->type;
}

static inline bool tomoyo_is_same_name_union
(const struct tomoyo_name_union *p1, const struct tomoyo_name_union *p2)
{
	return p1->filename == p2->filename && p1->group == p2->group &&
		p1->is_group == p2->is_group;
}

static inline bool tomoyo_is_same_number_union
(const struct tomoyo_number_union *p1, const struct tomoyo_number_union *p2)
{
	return p1->values[0] == p2->values[0] && p1->values[1] == p2->values[1]
		&& p1->group == p2->group && p1->min_type == p2->min_type &&
		p1->max_type == p2->max_type && p1->is_group == p2->is_group;
}

static inline bool tomoyo_is_same_path_acl(const struct tomoyo_path_acl *p1,
					   const struct tomoyo_path_acl *p2)
{
	return tomoyo_is_same_acl_head(&p1->head, &p2->head) &&
		tomoyo_is_same_name_union(&p1->name, &p2->name);
}

static inline bool tomoyo_is_same_path_number3_acl
(const struct tomoyo_path_number3_acl *p1,
 const struct tomoyo_path_number3_acl *p2)
{
	return tomoyo_is_same_acl_head(&p1->head, &p2->head)
		&& tomoyo_is_same_name_union(&p1->name, &p2->name)
		&& tomoyo_is_same_number_union(&p1->mode, &p2->mode)
		&& tomoyo_is_same_number_union(&p1->major, &p2->major)
		&& tomoyo_is_same_number_union(&p1->minor, &p2->minor);
}


static inline bool tomoyo_is_same_path2_acl(const struct tomoyo_path2_acl *p1,
					    const struct tomoyo_path2_acl *p2)
{
	return tomoyo_is_same_acl_head(&p1->head, &p2->head) &&
		tomoyo_is_same_name_union(&p1->name1, &p2->name1) &&
		tomoyo_is_same_name_union(&p1->name2, &p2->name2);
}

static inline bool tomoyo_is_same_path_number_acl
(const struct tomoyo_path_number_acl *p1,
 const struct tomoyo_path_number_acl *p2)
{
	return tomoyo_is_same_acl_head(&p1->head, &p2->head)
		&& tomoyo_is_same_name_union(&p1->name, &p2->name)
		&& tomoyo_is_same_number_union(&p1->number, &p2->number);
}

static inline bool tomoyo_is_same_mount_acl(const struct tomoyo_mount_acl *p1,
					    const struct tomoyo_mount_acl *p2)
{
	return tomoyo_is_same_acl_head(&p1->head, &p2->head) &&
		tomoyo_is_same_name_union(&p1->dev_name, &p2->dev_name) &&
		tomoyo_is_same_name_union(&p1->dir_name, &p2->dir_name) &&
		tomoyo_is_same_name_union(&p1->fs_type, &p2->fs_type) &&
		tomoyo_is_same_number_union(&p1->flags, &p2->flags);
}

static inline bool tomoyo_is_same_domain_initializer_entry
(const struct tomoyo_domain_initializer_entry *p1,
 const struct tomoyo_domain_initializer_entry *p2)
{
	return p1->is_not == p2->is_not && p1->is_last_name == p2->is_last_name
		&& p1->domainname == p2->domainname
		&& p1->program == p2->program;
}

static inline bool tomoyo_is_same_domain_keeper_entry
(const struct tomoyo_domain_keeper_entry *p1,
 const struct tomoyo_domain_keeper_entry *p2)
{
	return p1->is_not == p2->is_not && p1->is_last_name == p2->is_last_name
		&& p1->domainname == p2->domainname
		&& p1->program == p2->program;
}

static inline bool tomoyo_is_same_aggregator_entry
(const struct tomoyo_aggregator_entry *p1,
 const struct tomoyo_aggregator_entry *p2)
{
	return p1->original_name == p2->original_name &&
		p1->aggregated_name == p2->aggregated_name;
}

static inline bool tomoyo_is_same_alias_entry
(const struct tomoyo_alias_entry *p1, const struct tomoyo_alias_entry *p2)
{
	return p1->original_name == p2->original_name &&
		p1->aliased_name == p2->aliased_name;
}

/**
 * list_for_each_cookie - iterate over a list with cookie.
 * @pos:        the &struct list_head to use as a loop cursor.
 * @cookie:     the &struct list_head to use as a cookie.
 * @head:       the head for your list.
 *
 * Same with list_for_each_rcu() except that this primitive uses @cookie
 * so that we can continue iteration.
 * @cookie must be NULL when iteration starts, and @cookie will become
 * NULL when iteration finishes.
 */
#define list_for_each_cookie(pos, cookie, head)				\
	for (({ if (!cookie)						\
				     cookie = head; }),			\
		     pos = rcu_dereference((cookie)->next);		\
	     prefetch(pos->next), pos != (head) || ((cookie) = NULL);	\
	     (cookie) = pos, pos = rcu_dereference(pos->next))

#endif /* !defined(_SECURITY_TOMOYO_COMMON_H) */
