/*
 * security/tomoyo/common.h
 *
 * Common functions for TOMOYO.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0   2009/04/01
 *
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

struct dentry;
struct vfsmount;

/*
 * tomoyo_page_buffer is a structure which is used for holding a pathname
 * obtained from "struct dentry" and "struct vfsmount" pair.
 * As of now, it is 4096 bytes. If users complain that 4096 bytes is too small
 * (because TOMOYO escapes non ASCII printable characters using \ooo format),
 * we will make the buffer larger.
 */
struct tomoyo_page_buffer {
	char buffer[4096];
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
 * This is the max length of a token.
 *
 * A token consists of only ASCII printable characters.
 * Non printable characters in a token is represented in \ooo style
 * octal string. Thus, \ itself is represented as \\.
 */
#define TOMOYO_MAX_PATHNAME_LEN 4000

/*
 * tomoyo_path_info_with_data is a structure which is used for holding a
 * pathname obtained from "struct dentry" and "struct vfsmount" pair.
 *
 * "struct tomoyo_path_info_with_data" consists of "struct tomoyo_path_info"
 * and buffer for the pathname, while "struct tomoyo_page_buffer" consists of
 * buffer for the pathname only.
 *
 * "struct tomoyo_path_info_with_data" is intended to allow TOMOYO to release
 * both "struct tomoyo_path_info" and buffer for the pathname by single kfree()
 * so that we don't need to return two pointers to the caller. If the caller
 * puts "struct tomoyo_path_info" on stack memory, we will be able to remove
 * "struct tomoyo_path_info_with_data".
 */
struct tomoyo_path_info_with_data {
	/* Keep "head" first, for this pointer is passed to tomoyo_free(). */
	struct tomoyo_path_info head;
	char barrier1[16]; /* Safeguard for overrun. */
	char body[TOMOYO_MAX_PATHNAME_LEN];
	char barrier2[16]; /* Safeguard for overrun. */
};

/*
 * tomoyo_acl_info is a structure which is used for holding
 *
 *  (1) "list" which is linked to the ->acl_info_list of
 *      "struct tomoyo_domain_info"
 *  (2) "type" which tells
 *      (a) type & 0x7F : type of the entry (either
 *          "struct tomoyo_single_path_acl_record" or
 *          "struct tomoyo_double_path_acl_record")
 *      (b) type & 0x80 : whether the entry is marked as "deleted".
 *
 * Packing "struct tomoyo_acl_info" allows
 * "struct tomoyo_single_path_acl_record" to embed "u16" and
 * "struct tomoyo_double_path_acl_record" to embed "u8"
 * without enlarging their structure size.
 */
struct tomoyo_acl_info {
	struct list_head list;
	/*
	 * Type of this ACL entry.
	 *
	 * MSB is is_deleted flag.
	 */
	u8 type;
} __packed;

/* This ACL entry is deleted.           */
#define TOMOYO_ACL_DELETED        0x80

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
 *  (7) "flags" which remembers this domain's attributes.
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
	/* DOMAIN_FLAGS_*. Use tomoyo_set_domain_flag() to modify. */
	u8 flags;
};

/* Profile number is an integer between 0 and 255. */
#define TOMOYO_MAX_PROFILES 256

/* Ignore "allow_read" directive in exception policy. */
#define TOMOYO_DOMAIN_FLAGS_IGNORE_GLOBAL_ALLOW_READ 1
/*
 * This domain was unable to create a new domain at tomoyo_find_next_domain()
 * because the name of the domain to be created was too long or
 * it could not allocate memory.
 * More than one process continued execve() without domain transition.
 */
#define TOMOYO_DOMAIN_FLAGS_TRANSITION_FAILED        2

/*
 * tomoyo_single_path_acl_record is a structure which is used for holding an
 * entry with one pathname operation (e.g. open(), mkdir()).
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "perm" which is a bitmask of permitted operations.
 *  (3) "filename" is the pathname.
 *
 * Directives held by this structure are "allow_read/write", "allow_execute",
 * "allow_read", "allow_write", "allow_create", "allow_unlink", "allow_mkdir",
 * "allow_rmdir", "allow_mkfifo", "allow_mksock", "allow_mkblock",
 * "allow_mkchar", "allow_truncate", "allow_symlink" and "allow_rewrite".
 */
struct tomoyo_single_path_acl_record {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_SINGLE_PATH_ACL */
	u16 perm;
	/* Pointer to single pathname. */
	const struct tomoyo_path_info *filename;
};

/*
 * tomoyo_double_path_acl_record is a structure which is used for holding an
 * entry with two pathnames operation (i.e. link() and rename()).
 * It has following fields.
 *
 *  (1) "head" which is a "struct tomoyo_acl_info".
 *  (2) "perm" which is a bitmask of permitted operations.
 *  (3) "filename1" is the source/old pathname.
 *  (4) "filename2" is the destination/new pathname.
 *
 * Directives held by this structure are "allow_rename" and "allow_link".
 */
struct tomoyo_double_path_acl_record {
	struct tomoyo_acl_info head; /* type = TOMOYO_TYPE_DOUBLE_PATH_ACL */
	u8 perm;
	/* Pointer to single pathname. */
	const struct tomoyo_path_info *filename1;
	/* Pointer to single pathname. */
	const struct tomoyo_path_info *filename2;
};

/* Keywords for ACLs. */
#define TOMOYO_KEYWORD_ALIAS                     "alias "
#define TOMOYO_KEYWORD_ALLOW_READ                "allow_read "
#define TOMOYO_KEYWORD_DELETE                    "delete "
#define TOMOYO_KEYWORD_DENY_REWRITE              "deny_rewrite "
#define TOMOYO_KEYWORD_FILE_PATTERN              "file_pattern "
#define TOMOYO_KEYWORD_INITIALIZE_DOMAIN         "initialize_domain "
#define TOMOYO_KEYWORD_KEEP_DOMAIN               "keep_domain "
#define TOMOYO_KEYWORD_NO_INITIALIZE_DOMAIN      "no_initialize_domain "
#define TOMOYO_KEYWORD_NO_KEEP_DOMAIN            "no_keep_domain "
#define TOMOYO_KEYWORD_SELECT                    "select "
#define TOMOYO_KEYWORD_USE_PROFILE               "use_profile "
#define TOMOYO_KEYWORD_IGNORE_GLOBAL_ALLOW_READ  "ignore_global_allow_read"
/* A domain definition starts with <kernel>. */
#define TOMOYO_ROOT_NAME                         "<kernel>"
#define TOMOYO_ROOT_NAME_LEN                     (sizeof(TOMOYO_ROOT_NAME) - 1)

/* Index numbers for Access Controls. */
#define TOMOYO_MAC_FOR_FILE                  0  /* domain_policy.conf */
#define TOMOYO_MAX_ACCEPT_ENTRY              1
#define TOMOYO_VERBOSE                       2
#define TOMOYO_MAX_CONTROL_INDEX             3

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
	/* Exclusive lock for this structure.   */
	struct mutex io_sem;
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
};

/* Check whether the domain has too many ACL entries to hold. */
bool tomoyo_domain_quota_is_ok(struct tomoyo_domain_info * const domain);
/* Transactional sprintf() for policy dump. */
bool tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt, ...)
	__attribute__ ((format(printf, 2, 3)));
/* Check whether the domainname is correct. */
bool tomoyo_is_correct_domain(const unsigned char *domainname,
			      const char *function);
/* Check whether the token is correct. */
bool tomoyo_is_correct_path(const char *filename, const s8 start_type,
			    const s8 pattern_type, const s8 end_type,
			    const char *function);
/* Check whether the token can be a domainname. */
bool tomoyo_is_domain_def(const unsigned char *buffer);
/* Check whether the given filename matches the given pattern. */
bool tomoyo_path_matches_pattern(const struct tomoyo_path_info *filename,
				 const struct tomoyo_path_info *pattern);
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
/* Read "allow_read" entry in exception policy. */
bool tomoyo_read_globally_readable_policy(struct tomoyo_io_buffer *head);
/* Read "deny_rewrite" entry in exception policy. */
bool tomoyo_read_no_rewrite_policy(struct tomoyo_io_buffer *head);
/* Write domain policy violation warning message to console? */
bool tomoyo_verbose_mode(const struct tomoyo_domain_info *domain);
/* Convert double path operation to operation name. */
const char *tomoyo_dp2keyword(const u8 operation);
/* Get the last component of the given domainname. */
const char *tomoyo_get_last_name(const struct tomoyo_domain_info *domain);
/* Get warning message. */
const char *tomoyo_get_msg(const bool is_enforce);
/* Convert single path operation to operation name. */
const char *tomoyo_sp2keyword(const u8 operation);
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
/* Create "deny_rewrite" entry in exception policy. */
int tomoyo_write_no_rewrite_policy(char *data, const bool is_delete);
/* Create "file_pattern" entry in exception policy. */
int tomoyo_write_pattern_policy(char *data, const bool is_delete);
/* Find a domain by the given name. */
struct tomoyo_domain_info *tomoyo_find_domain(const char *domainname);
/* Find or create a domain by the given name. */
struct tomoyo_domain_info *tomoyo_find_or_assign_new_domain(const char *
							    domainname,
							    const u8 profile);
/* Check mode for specified functionality. */
unsigned int tomoyo_check_flags(const struct tomoyo_domain_info *domain,
				const u8 index);
/* Allocate memory for structures. */
void *tomoyo_alloc_acl_element(const u8 acl_type);
/* Fill in "struct tomoyo_path_info" members. */
void tomoyo_fill_path_info(struct tomoyo_path_info *ptr);
/* Run policy loader when /sbin/init starts. */
void tomoyo_load_policy(const char *filename);
/* Change "struct tomoyo_domain_info"->flags. */
void tomoyo_set_domain_flag(struct tomoyo_domain_info *domain,
			    const bool is_delete, const u8 flags);

/* strcmp() for "struct tomoyo_path_info" structure. */
static inline bool tomoyo_pathcmp(const struct tomoyo_path_info *a,
				  const struct tomoyo_path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

/* Get type of an ACL entry. */
static inline u8 tomoyo_acl_type1(struct tomoyo_acl_info *ptr)
{
	return ptr->type & ~TOMOYO_ACL_DELETED;
}

/* Get type of an ACL entry. */
static inline u8 tomoyo_acl_type2(struct tomoyo_acl_info *ptr)
{
	return ptr->type;
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

/* The list for "struct tomoyo_domain_info". */
extern struct list_head tomoyo_domain_list;
extern struct rw_semaphore tomoyo_domain_list_lock;

/* Lock for domain->acl_info_list. */
extern struct rw_semaphore tomoyo_domain_acl_info_list_lock;

/* Has /sbin/init started? */
extern bool tomoyo_policy_loaded;

/* The kernel's domain. */
extern struct tomoyo_domain_info tomoyo_kernel_domain;

/**
 * list_for_each_cookie - iterate over a list with cookie.
 * @pos:        the &struct list_head to use as a loop cursor.
 * @cookie:     the &struct list_head to use as a cookie.
 * @head:       the head for your list.
 *
 * Same with list_for_each() except that this primitive uses @cookie
 * so that we can continue iteration.
 * @cookie must be NULL when iteration starts, and @cookie will become
 * NULL when iteration finishes.
 */
#define list_for_each_cookie(pos, cookie, head)                       \
	for (({ if (!cookie)                                          \
				     cookie = head; }),               \
	     pos = (cookie)->next;                                    \
	     prefetch(pos->next), pos != (head) || ((cookie) = NULL); \
	     (cookie) = pos, pos = pos->next)

#endif /* !defined(_SECURITY_TOMOYO_COMMON_H) */
