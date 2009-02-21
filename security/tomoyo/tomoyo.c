/*
 * security/tomoyo/tomoyo.c
 *
 * LSM hooks for TOMOYO Linux.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0-pre   2009/02/01
 *
 */

#include <linux/security.h>
#include "common.h"
#include "tomoyo.h"
#include "realpath.h"

static int tomoyo_cred_prepare(struct cred *new, const struct cred *old,
			       gfp_t gfp)
{
	/*
	 * Since "struct tomoyo_domain_info *" is a sharable pointer,
	 * we don't need to duplicate.
	 */
	new->security = old->security;
	return 0;
}

static int tomoyo_bprm_set_creds(struct linux_binprm *bprm)
{
	/*
	 * Do only if this function is called for the first time of an execve
	 * operation.
	 */
	if (bprm->cred_prepared)
		return 0;
	/*
	 * Load policy if /sbin/tomoyo-init exists and /sbin/init is requested
	 * for the first time.
	 */
	if (!tomoyo_policy_loaded)
		tomoyo_load_policy(bprm->filename);
	/*
	 * Tell tomoyo_bprm_check_security() is called for the first time of an
	 * execve operation.
	 */
	bprm->cred->security = NULL;
	return 0;
}

static int tomoyo_bprm_check_security(struct linux_binprm *bprm)
{
	struct tomoyo_domain_info *domain = bprm->cred->security;

	/*
	 * Execute permission is checked against pathname passed to do_execve()
	 * using current domain.
	 */
	if (!domain) {
		struct tomoyo_domain_info *next_domain = NULL;
		int retval = tomoyo_find_next_domain(bprm, &next_domain);

		if (!retval)
			bprm->cred->security = next_domain;
		return retval;
	}
	/*
	 * Read permission is checked against interpreters using next domain.
	 * '1' is the result of open_to_namei_flags(O_RDONLY).
	 */
	return tomoyo_check_open_permission(domain, &bprm->file->f_path, 1);
}

#ifdef CONFIG_SYSCTL

static int tomoyo_prepend(char **buffer, int *buflen, const char *str)
{
	int namelen = strlen(str);

	if (*buflen < namelen)
		return -ENOMEM;
	*buflen -= namelen;
	*buffer -= namelen;
	memcpy(*buffer, str, namelen);
	return 0;
}

/**
 * tomoyo_sysctl_path - return the realpath of a ctl_table.
 * @table: pointer to "struct ctl_table".
 *
 * Returns realpath(3) of the @table on success.
 * Returns NULL on failure.
 *
 * This function uses tomoyo_alloc(), so the caller must call tomoyo_free()
 * if this function didn't return NULL.
 */
static char *tomoyo_sysctl_path(struct ctl_table *table)
{
	int buflen = TOMOYO_MAX_PATHNAME_LEN;
	char *buf = tomoyo_alloc(buflen);
	char *end = buf + buflen;
	int error = -ENOMEM;

	if (!buf)
		return NULL;

	*--end = '\0';
	buflen--;
	while (table) {
		char num[32];
		const char *sp = table->procname;

		if (!sp) {
			memset(num, 0, sizeof(num));
			snprintf(num, sizeof(num) - 1, "=%d=", table->ctl_name);
			sp = num;
		}
		if (tomoyo_prepend(&end, &buflen, sp) ||
		    tomoyo_prepend(&end, &buflen, "/"))
			goto out;
		table = table->parent;
	}
	if (tomoyo_prepend(&end, &buflen, "/proc/sys"))
		goto out;
	error = tomoyo_encode(buf, end - buf, end);
 out:
	if (!error)
		return buf;
	tomoyo_free(buf);
	return NULL;
}

static int tomoyo_sysctl(struct ctl_table *table, int op)
{
	int error;
	char *name;

	op &= MAY_READ | MAY_WRITE;
	if (!op)
		return 0;
	name = tomoyo_sysctl_path(table);
	if (!name)
		return -ENOMEM;
	error = tomoyo_check_file_perm(tomoyo_domain(), name, op);
	tomoyo_free(name);
	return error;
}
#endif

static int tomoyo_path_truncate(struct path *path, loff_t length,
				unsigned int time_attrs)
{
	return tomoyo_check_1path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_TRUNCATE_ACL,
				       path);
}

static int tomoyo_path_unlink(struct path *parent, struct dentry *dentry)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_check_1path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_UNLINK_ACL,
				       &path);
}

static int tomoyo_path_mkdir(struct path *parent, struct dentry *dentry,
			     int mode)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_check_1path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_MKDIR_ACL,
				       &path);
}

static int tomoyo_path_rmdir(struct path *parent, struct dentry *dentry)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_check_1path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_RMDIR_ACL,
				       &path);
}

static int tomoyo_path_symlink(struct path *parent, struct dentry *dentry,
			       const char *old_name)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_check_1path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_SYMLINK_ACL,
				       &path);
}

static int tomoyo_path_mknod(struct path *parent, struct dentry *dentry,
			     int mode, unsigned int dev)
{
	struct path path = { parent->mnt, dentry };
	int type = TOMOYO_TYPE_CREATE_ACL;

	switch (mode & S_IFMT) {
	case S_IFCHR:
		type = TOMOYO_TYPE_MKCHAR_ACL;
		break;
	case S_IFBLK:
		type = TOMOYO_TYPE_MKBLOCK_ACL;
		break;
	case S_IFIFO:
		type = TOMOYO_TYPE_MKFIFO_ACL;
		break;
	case S_IFSOCK:
		type = TOMOYO_TYPE_MKSOCK_ACL;
		break;
	}
	return tomoyo_check_1path_perm(tomoyo_domain(),
				       type, &path);
}

static int tomoyo_path_link(struct dentry *old_dentry, struct path *new_dir,
			    struct dentry *new_dentry)
{
	struct path path1 = { new_dir->mnt, old_dentry };
	struct path path2 = { new_dir->mnt, new_dentry };
	return tomoyo_check_2path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_LINK_ACL,
				       &path1, &path2);
}

static int tomoyo_path_rename(struct path *old_parent,
			      struct dentry *old_dentry,
			      struct path *new_parent,
			      struct dentry *new_dentry)
{
	struct path path1 = { old_parent->mnt, old_dentry };
	struct path path2 = { new_parent->mnt, new_dentry };
	return tomoyo_check_2path_perm(tomoyo_domain(),
				       TOMOYO_TYPE_RENAME_ACL,
				       &path1, &path2);
}

static int tomoyo_file_fcntl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	if (cmd == F_SETFL && ((arg ^ file->f_flags) & O_APPEND))
		return tomoyo_check_rewrite_permission(tomoyo_domain(), file);
	return 0;
}

static int tomoyo_dentry_open(struct file *f, const struct cred *cred)
{
	int flags = f->f_flags;

	if ((flags + 1) & O_ACCMODE)
		flags++;
	flags |= f->f_flags & (O_APPEND | O_TRUNC);
	/* Don't check read permission here if called from do_execve(). */
	if (current->in_execve)
		return 0;
	return tomoyo_check_open_permission(tomoyo_domain(), &f->f_path, flags);
}

static struct security_operations tomoyo_security_ops = {
	.name                = "tomoyo",
	.cred_prepare        = tomoyo_cred_prepare,
	.bprm_set_creds      = tomoyo_bprm_set_creds,
	.bprm_check_security = tomoyo_bprm_check_security,
#ifdef CONFIG_SYSCTL
	.sysctl              = tomoyo_sysctl,
#endif
	.file_fcntl          = tomoyo_file_fcntl,
	.dentry_open         = tomoyo_dentry_open,
	.path_truncate       = tomoyo_path_truncate,
	.path_unlink         = tomoyo_path_unlink,
	.path_mkdir          = tomoyo_path_mkdir,
	.path_rmdir          = tomoyo_path_rmdir,
	.path_symlink        = tomoyo_path_symlink,
	.path_mknod          = tomoyo_path_mknod,
	.path_link           = tomoyo_path_link,
	.path_rename         = tomoyo_path_rename,
};

static int __init tomoyo_init(void)
{
	struct cred *cred = (struct cred *) current_cred();

	if (!security_module_enable(&tomoyo_security_ops))
		return 0;
	/* register ourselves with the security framework */
	if (register_security(&tomoyo_security_ops))
		panic("Failure registering TOMOYO Linux");
	printk(KERN_INFO "TOMOYO Linux initialized\n");
	cred->security = &tomoyo_kernel_domain;
	tomoyo_realpath_init();
	return 0;
}

security_initcall(tomoyo_init);
