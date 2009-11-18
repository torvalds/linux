/*
 * security/tomoyo/tomoyo.h
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0   2009/04/01
 *
 */

#ifndef _SECURITY_TOMOYO_TOMOYO_H
#define _SECURITY_TOMOYO_TOMOYO_H

struct tomoyo_path_info;
struct path;
struct inode;
struct linux_binprm;
struct pt_regs;

int tomoyo_check_file_perm(struct tomoyo_domain_info *domain,
			   const char *filename, const u8 perm);
int tomoyo_check_exec_perm(struct tomoyo_domain_info *domain,
			   const struct tomoyo_path_info *filename);
int tomoyo_check_open_permission(struct tomoyo_domain_info *domain,
				 struct path *path, const int flag);
int tomoyo_check_1path_perm(struct tomoyo_domain_info *domain,
			    const u8 operation, struct path *path);
int tomoyo_check_2path_perm(struct tomoyo_domain_info *domain,
			    const u8 operation, struct path *path1,
			    struct path *path2);
int tomoyo_check_rewrite_permission(struct tomoyo_domain_info *domain,
				    struct file *filp);
int tomoyo_find_next_domain(struct linux_binprm *bprm);

/* Index numbers for Access Controls. */

#define TOMOYO_TYPE_SINGLE_PATH_ACL                 0
#define TOMOYO_TYPE_DOUBLE_PATH_ACL                 1

/* Index numbers for File Controls. */

/*
 * TYPE_READ_WRITE_ACL is special. TYPE_READ_WRITE_ACL is automatically set
 * if both TYPE_READ_ACL and TYPE_WRITE_ACL are set. Both TYPE_READ_ACL and
 * TYPE_WRITE_ACL are automatically set if TYPE_READ_WRITE_ACL is set.
 * TYPE_READ_WRITE_ACL is automatically cleared if either TYPE_READ_ACL or
 * TYPE_WRITE_ACL is cleared. Both TYPE_READ_ACL and TYPE_WRITE_ACL are
 * automatically cleared if TYPE_READ_WRITE_ACL is cleared.
 */

#define TOMOYO_TYPE_READ_WRITE_ACL    0
#define TOMOYO_TYPE_EXECUTE_ACL       1
#define TOMOYO_TYPE_READ_ACL          2
#define TOMOYO_TYPE_WRITE_ACL         3
#define TOMOYO_TYPE_CREATE_ACL        4
#define TOMOYO_TYPE_UNLINK_ACL        5
#define TOMOYO_TYPE_MKDIR_ACL         6
#define TOMOYO_TYPE_RMDIR_ACL         7
#define TOMOYO_TYPE_MKFIFO_ACL        8
#define TOMOYO_TYPE_MKSOCK_ACL        9
#define TOMOYO_TYPE_MKBLOCK_ACL      10
#define TOMOYO_TYPE_MKCHAR_ACL       11
#define TOMOYO_TYPE_TRUNCATE_ACL     12
#define TOMOYO_TYPE_SYMLINK_ACL      13
#define TOMOYO_TYPE_REWRITE_ACL      14
#define TOMOYO_MAX_SINGLE_PATH_OPERATION 15

#define TOMOYO_TYPE_LINK_ACL         0
#define TOMOYO_TYPE_RENAME_ACL       1
#define TOMOYO_MAX_DOUBLE_PATH_OPERATION 2

#define TOMOYO_DOMAINPOLICY          0
#define TOMOYO_EXCEPTIONPOLICY       1
#define TOMOYO_DOMAIN_STATUS         2
#define TOMOYO_PROCESS_STATUS        3
#define TOMOYO_MEMINFO               4
#define TOMOYO_SELFDOMAIN            5
#define TOMOYO_VERSION               6
#define TOMOYO_PROFILE               7
#define TOMOYO_MANAGER               8

extern struct tomoyo_domain_info tomoyo_kernel_domain;

static inline struct tomoyo_domain_info *tomoyo_domain(void)
{
	return current_cred()->security;
}

static inline struct tomoyo_domain_info *tomoyo_real_domain(struct task_struct
							    *task)
{
	return task_cred_xxx(task, security);
}

#endif /* !defined(_SECURITY_TOMOYO_TOMOYO_H) */
