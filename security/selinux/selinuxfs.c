// SPDX-License-Identifier: GPL-2.0-only
/* Updated: Karl MacMillan <kmacmillan@tresys.com>
 *
 *	Added conditional policy language extensions
 *
 *  Updated: Hewlett-Packard <paul@paul-moore.com>
 *
 *	Added support for the policy capability bitmap
 *
 * Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2003 - 2004 Tresys Technology, LLC
 * Copyright (C) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/security.h>
#include <linux/major.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#include <linux/audit.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/ctype.h>

/* selinuxfs pseudo filesystem for exporting the security policy API.
   Based on the proc code and the fs/nfsd/nfsctl.c code. */

#include "flask.h"
#include "avc.h"
#include "avc_ss.h"
#include "security.h"
#include "objsec.h"
#include "conditional.h"

enum sel_inos {
	SEL_ROOT_INO = 2,
	SEL_LOAD,	/* load policy */
	SEL_ENFORCE,	/* get or set enforcing status */
	SEL_CONTEXT,	/* validate context */
	SEL_ACCESS,	/* compute access decision */
	SEL_CREATE,	/* compute create labeling decision */
	SEL_RELABEL,	/* compute relabeling decision */
	SEL_USER,	/* compute reachable user contexts */
	SEL_POLICYVERS,	/* return policy version for this kernel */
	SEL_COMMIT_BOOLS, /* commit new boolean values */
	SEL_MLS,	/* return if MLS policy is enabled */
	SEL_DISABLE,	/* disable SELinux until next reboot */
	SEL_MEMBER,	/* compute polyinstantiation membership decision */
	SEL_CHECKREQPROT, /* check requested protection, not kernel-applied one */
	SEL_COMPAT_NET,	/* whether to use old compat network packet controls */
	SEL_REJECT_UNKNOWN, /* export unknown reject handling to userspace */
	SEL_DENY_UNKNOWN, /* export unknown deny handling to userspace */
	SEL_STATUS,	/* export current status using mmap() */
	SEL_POLICY,	/* allow userspace to read the in kernel policy */
	SEL_VALIDATE_TRANS, /* compute validatetrans decision */
	SEL_INO_NEXT,	/* The next inode number to use */
};

struct selinux_fs_info {
	struct dentry *bool_dir;
	unsigned int bool_num;
	char **bool_pending_names;
	unsigned int *bool_pending_values;
	struct dentry *class_dir;
	unsigned long last_class_ino;
	bool policy_opened;
	struct dentry *policycap_dir;
	unsigned long last_ino;
	struct selinux_state *state;
	struct super_block *sb;
};

static int selinux_fs_info_create(struct super_block *sb)
{
	struct selinux_fs_info *fsi;

	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	fsi->last_ino = SEL_INO_NEXT - 1;
	fsi->state = &selinux_state;
	fsi->sb = sb;
	sb->s_fs_info = fsi;
	return 0;
}

static void selinux_fs_info_free(struct super_block *sb)
{
	struct selinux_fs_info *fsi = sb->s_fs_info;
	int i;

	if (fsi) {
		for (i = 0; i < fsi->bool_num; i++)
			kfree(fsi->bool_pending_names[i]);
		kfree(fsi->bool_pending_names);
		kfree(fsi->bool_pending_values);
	}
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

#define SEL_INITCON_INO_OFFSET		0x01000000
#define SEL_BOOL_INO_OFFSET		0x02000000
#define SEL_CLASS_INO_OFFSET		0x04000000
#define SEL_POLICYCAP_INO_OFFSET	0x08000000
#define SEL_INO_MASK			0x00ffffff

#define BOOL_DIR_NAME "booleans"
#define CLASS_DIR_NAME "class"
#define POLICYCAP_DIR_NAME "policy_capabilities"

#define TMPBUFLEN	12
static ssize_t sel_read_enforce(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	char tmpbuf[TMPBUFLEN];
	ssize_t length;

	length = scnprintf(tmpbuf, TMPBUFLEN, "%d",
			   enforcing_enabled(fsi->state));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
static ssize_t sel_write_enforce(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)

{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *page = NULL;
	ssize_t length;
	int old_value, new_value;

	if (count >= PAGE_SIZE)
		return -ENOMEM;

	/* No partial writes. */
	if (*ppos != 0)
		return -EINVAL;

	page = memdup_user_nul(buf, count);
	if (IS_ERR(page))
		return PTR_ERR(page);

	length = -EINVAL;
	if (sscanf(page, "%d", &new_value) != 1)
		goto out;

	new_value = !!new_value;

	old_value = enforcing_enabled(state);
	if (new_value != old_value) {
		length = avc_has_perm(&selinux_state,
				      current_sid(), SECINITSID_SECURITY,
				      SECCLASS_SECURITY, SECURITY__SETENFORCE,
				      NULL);
		if (length)
			goto out;
		audit_log(audit_context(), GFP_KERNEL, AUDIT_MAC_STATUS,
			"enforcing=%d old_enforcing=%d auid=%u ses=%u"
			" enabled=1 old-enabled=1 lsm=selinux res=1",
			new_value, old_value,
			from_kuid(&init_user_ns, audit_get_loginuid(current)),
			audit_get_sessionid(current));
		enforcing_set(state, new_value);
		if (new_value)
			avc_ss_reset(state->avc, 0);
		selnl_notify_setenforce(new_value);
		selinux_status_update_setenforce(state, new_value);
		if (!new_value)
			call_blocking_lsm_notifier(LSM_POLICY_CHANGE, NULL);
	}
	length = count;
out:
	kfree(page);
	return length;
}
#else
#define sel_write_enforce NULL
#endif

static const struct file_operations sel_enforce_ops = {
	.read		= sel_read_enforce,
	.write		= sel_write_enforce,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_read_handle_unknown(struct file *filp, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char tmpbuf[TMPBUFLEN];
	ssize_t length;
	ino_t ino = file_inode(filp)->i_ino;
	int handle_unknown = (ino == SEL_REJECT_UNKNOWN) ?
		security_get_reject_unknown(state) :
		!security_get_allow_unknown(state);

	length = scnprintf(tmpbuf, TMPBUFLEN, "%d", handle_unknown);
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static const struct file_operations sel_handle_unknown_ops = {
	.read		= sel_read_handle_unknown,
	.llseek		= generic_file_llseek,
};

static int sel_open_handle_status(struct inode *inode, struct file *filp)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	struct page    *status = selinux_kernel_status_page(fsi->state);

	if (!status)
		return -ENOMEM;

	filp->private_data = status;

	return 0;
}

static ssize_t sel_read_handle_status(struct file *filp, char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct page    *status = filp->private_data;

	BUG_ON(!status);

	return simple_read_from_buffer(buf, count, ppos,
				       page_address(status),
				       sizeof(struct selinux_kernel_status));
}

static int sel_mmap_handle_status(struct file *filp,
				  struct vm_area_struct *vma)
{
	struct page    *status = filp->private_data;
	unsigned long	size = vma->vm_end - vma->vm_start;

	BUG_ON(!status);

	/* only allows one page from the head */
	if (vma->vm_pgoff > 0 || size != PAGE_SIZE)
		return -EIO;
	/* disallow writable mapping */
	if (vma->vm_flags & VM_WRITE)
		return -EPERM;
	/* disallow mprotect() turns it into writable */
	vma->vm_flags &= ~VM_MAYWRITE;

	return remap_pfn_range(vma, vma->vm_start,
			       page_to_pfn(status),
			       size, vma->vm_page_prot);
}

static const struct file_operations sel_handle_status_ops = {
	.open		= sel_open_handle_status,
	.read		= sel_read_handle_status,
	.mmap		= sel_mmap_handle_status,
	.llseek		= generic_file_llseek,
};

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
static ssize_t sel_write_disable(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)

{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	char *page;
	ssize_t length;
	int new_value;
	int enforcing;

	/* NOTE: we are now officially considering runtime disable as
	 *       deprecated, and using it will become increasingly painful
	 *       (e.g. sleeping/blocking) as we progress through future
	 *       kernel releases until eventually it is removed
	 */
	pr_err("SELinux:  Runtime disable is deprecated, use selinux=0 on the kernel cmdline.\n");

	if (count >= PAGE_SIZE)
		return -ENOMEM;

	/* No partial writes. */
	if (*ppos != 0)
		return -EINVAL;

	page = memdup_user_nul(buf, count);
	if (IS_ERR(page))
		return PTR_ERR(page);

	length = -EINVAL;
	if (sscanf(page, "%d", &new_value) != 1)
		goto out;

	if (new_value) {
		enforcing = enforcing_enabled(fsi->state);
		length = selinux_disable(fsi->state);
		if (length)
			goto out;
		audit_log(audit_context(), GFP_KERNEL, AUDIT_MAC_STATUS,
			"enforcing=%d old_enforcing=%d auid=%u ses=%u"
			" enabled=0 old-enabled=1 lsm=selinux res=1",
			enforcing, enforcing,
			from_kuid(&init_user_ns, audit_get_loginuid(current)),
			audit_get_sessionid(current));
	}

	length = count;
out:
	kfree(page);
	return length;
}
#else
#define sel_write_disable NULL
#endif

static const struct file_operations sel_disable_ops = {
	.write		= sel_write_disable,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_read_policyvers(struct file *filp, char __user *buf,
				   size_t count, loff_t *ppos)
{
	char tmpbuf[TMPBUFLEN];
	ssize_t length;

	length = scnprintf(tmpbuf, TMPBUFLEN, "%u", POLICYDB_VERSION_MAX);
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static const struct file_operations sel_policyvers_ops = {
	.read		= sel_read_policyvers,
	.llseek		= generic_file_llseek,
};

/* declaration for sel_write_load */
static int sel_make_bools(struct selinux_policy *newpolicy, struct dentry *bool_dir,
			  unsigned int *bool_num, char ***bool_pending_names,
			  unsigned int **bool_pending_values);
static int sel_make_classes(struct selinux_policy *newpolicy,
			    struct dentry *class_dir,
			    unsigned long *last_class_ino);

/* declaration for sel_make_class_dirs */
static struct dentry *sel_make_dir(struct dentry *dir, const char *name,
			unsigned long *ino);

/* declaration for sel_make_policy_nodes */
static struct dentry *sel_make_disconnected_dir(struct super_block *sb,
						unsigned long *ino);

/* declaration for sel_make_policy_nodes */
static void sel_remove_entries(struct dentry *de);

static ssize_t sel_read_mls(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	char tmpbuf[TMPBUFLEN];
	ssize_t length;

	length = scnprintf(tmpbuf, TMPBUFLEN, "%d",
			   security_mls_enabled(fsi->state));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static const struct file_operations sel_mls_ops = {
	.read		= sel_read_mls,
	.llseek		= generic_file_llseek,
};

struct policy_load_memory {
	size_t len;
	void *data;
};

static int sel_open_policy(struct inode *inode, struct file *filp)
{
	struct selinux_fs_info *fsi = inode->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	struct policy_load_memory *plm = NULL;
	int rc;

	BUG_ON(filp->private_data);

	mutex_lock(&fsi->state->policy_mutex);

	rc = avc_has_perm(&selinux_state,
			  current_sid(), SECINITSID_SECURITY,
			  SECCLASS_SECURITY, SECURITY__READ_POLICY, NULL);
	if (rc)
		goto err;

	rc = -EBUSY;
	if (fsi->policy_opened)
		goto err;

	rc = -ENOMEM;
	plm = kzalloc(sizeof(*plm), GFP_KERNEL);
	if (!plm)
		goto err;

	rc = security_read_policy(state, &plm->data, &plm->len);
	if (rc)
		goto err;

	if ((size_t)i_size_read(inode) != plm->len) {
		inode_lock(inode);
		i_size_write(inode, plm->len);
		inode_unlock(inode);
	}

	fsi->policy_opened = 1;

	filp->private_data = plm;

	mutex_unlock(&fsi->state->policy_mutex);

	return 0;
err:
	mutex_unlock(&fsi->state->policy_mutex);

	if (plm)
		vfree(plm->data);
	kfree(plm);
	return rc;
}

static int sel_release_policy(struct inode *inode, struct file *filp)
{
	struct selinux_fs_info *fsi = inode->i_sb->s_fs_info;
	struct policy_load_memory *plm = filp->private_data;

	BUG_ON(!plm);

	fsi->policy_opened = 0;

	vfree(plm->data);
	kfree(plm);

	return 0;
}

static ssize_t sel_read_policy(struct file *filp, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct policy_load_memory *plm = filp->private_data;
	int ret;

	ret = avc_has_perm(&selinux_state,
			   current_sid(), SECINITSID_SECURITY,
			  SECCLASS_SECURITY, SECURITY__READ_POLICY, NULL);
	if (ret)
		return ret;

	return simple_read_from_buffer(buf, count, ppos, plm->data, plm->len);
}

static vm_fault_t sel_mmap_policy_fault(struct vm_fault *vmf)
{
	struct policy_load_memory *plm = vmf->vma->vm_file->private_data;
	unsigned long offset;
	struct page *page;

	if (vmf->flags & (FAULT_FLAG_MKWRITE | FAULT_FLAG_WRITE))
		return VM_FAULT_SIGBUS;

	offset = vmf->pgoff << PAGE_SHIFT;
	if (offset >= roundup(plm->len, PAGE_SIZE))
		return VM_FAULT_SIGBUS;

	page = vmalloc_to_page(plm->data + offset);
	get_page(page);

	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct sel_mmap_policy_ops = {
	.fault = sel_mmap_policy_fault,
	.page_mkwrite = sel_mmap_policy_fault,
};

static int sel_mmap_policy(struct file *filp, struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHARED) {
		/* do not allow mprotect to make mapping writable */
		vma->vm_flags &= ~VM_MAYWRITE;

		if (vma->vm_flags & VM_WRITE)
			return -EACCES;
	}

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &sel_mmap_policy_ops;

	return 0;
}

static const struct file_operations sel_policy_ops = {
	.open		= sel_open_policy,
	.read		= sel_read_policy,
	.mmap		= sel_mmap_policy,
	.release	= sel_release_policy,
	.llseek		= generic_file_llseek,
};

static void sel_remove_old_bool_data(unsigned int bool_num, char **bool_names,
				unsigned int *bool_values)
{
	u32 i;

	/* bool_dir cleanup */
	for (i = 0; i < bool_num; i++)
		kfree(bool_names[i]);
	kfree(bool_names);
	kfree(bool_values);
}

static int sel_make_policy_nodes(struct selinux_fs_info *fsi,
				struct selinux_policy *newpolicy)
{
	int ret = 0;
	struct dentry *tmp_parent, *tmp_bool_dir, *tmp_class_dir, *old_dentry;
	unsigned int tmp_bool_num, old_bool_num;
	char **tmp_bool_names, **old_bool_names;
	unsigned int *tmp_bool_values, *old_bool_values;
	unsigned long tmp_ino = fsi->last_ino; /* Don't increment last_ino in this function */

	tmp_parent = sel_make_disconnected_dir(fsi->sb, &tmp_ino);
	if (IS_ERR(tmp_parent))
		return PTR_ERR(tmp_parent);

	tmp_ino = fsi->bool_dir->d_inode->i_ino - 1; /* sel_make_dir will increment and set */
	tmp_bool_dir = sel_make_dir(tmp_parent, BOOL_DIR_NAME, &tmp_ino);
	if (IS_ERR(tmp_bool_dir)) {
		ret = PTR_ERR(tmp_bool_dir);
		goto out;
	}

	tmp_ino = fsi->class_dir->d_inode->i_ino - 1; /* sel_make_dir will increment and set */
	tmp_class_dir = sel_make_dir(tmp_parent, CLASS_DIR_NAME, &tmp_ino);
	if (IS_ERR(tmp_class_dir)) {
		ret = PTR_ERR(tmp_class_dir);
		goto out;
	}

	ret = sel_make_bools(newpolicy, tmp_bool_dir, &tmp_bool_num,
			     &tmp_bool_names, &tmp_bool_values);
	if (ret) {
		pr_err("SELinux: failed to load policy booleans\n");
		goto out;
	}

	ret = sel_make_classes(newpolicy, tmp_class_dir,
			       &fsi->last_class_ino);
	if (ret) {
		pr_err("SELinux: failed to load policy classes\n");
		goto out;
	}

	/* booleans */
	old_dentry = fsi->bool_dir;
	lock_rename(tmp_bool_dir, old_dentry);
	d_exchange(tmp_bool_dir, fsi->bool_dir);

	old_bool_num = fsi->bool_num;
	old_bool_names = fsi->bool_pending_names;
	old_bool_values = fsi->bool_pending_values;

	fsi->bool_num = tmp_bool_num;
	fsi->bool_pending_names = tmp_bool_names;
	fsi->bool_pending_values = tmp_bool_values;

	sel_remove_old_bool_data(old_bool_num, old_bool_names, old_bool_values);

	fsi->bool_dir = tmp_bool_dir;
	unlock_rename(tmp_bool_dir, old_dentry);

	/* classes */
	old_dentry = fsi->class_dir;
	lock_rename(tmp_class_dir, old_dentry);
	d_exchange(tmp_class_dir, fsi->class_dir);
	fsi->class_dir = tmp_class_dir;
	unlock_rename(tmp_class_dir, old_dentry);

out:
	/* Since the other temporary dirs are children of tmp_parent
	 * this will handle all the cleanup in the case of a failure before
	 * the swapover
	 */
	sel_remove_entries(tmp_parent);
	dput(tmp_parent); /* d_genocide() only handles the children */

	return ret;
}

static ssize_t sel_write_load(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)

{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_policy *newpolicy;
	ssize_t length;
	void *data = NULL;

	mutex_lock(&fsi->state->policy_mutex);

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__LOAD_POLICY, NULL);
	if (length)
		goto out;

	/* No partial writes. */
	length = -EINVAL;
	if (*ppos != 0)
		goto out;

	length = -ENOMEM;
	data = vmalloc(count);
	if (!data)
		goto out;

	length = -EFAULT;
	if (copy_from_user(data, buf, count) != 0)
		goto out;

	length = security_load_policy(fsi->state, data, count, &newpolicy);
	if (length) {
		pr_warn_ratelimited("SELinux: failed to load policy\n");
		goto out;
	}

	length = sel_make_policy_nodes(fsi, newpolicy);
	if (length) {
		selinux_policy_cancel(fsi->state, newpolicy);
		goto out1;
	}

	selinux_policy_commit(fsi->state, newpolicy);

	length = count;

out1:
	audit_log(audit_context(), GFP_KERNEL, AUDIT_MAC_POLICY_LOAD,
		"auid=%u ses=%u lsm=selinux res=1",
		from_kuid(&init_user_ns, audit_get_loginuid(current)),
		audit_get_sessionid(current));
out:
	mutex_unlock(&fsi->state->policy_mutex);
	vfree(data);
	return length;
}

static const struct file_operations sel_load_ops = {
	.write		= sel_write_load,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_write_context(struct file *file, char *buf, size_t size)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *canon = NULL;
	u32 sid, len;
	ssize_t length;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__CHECK_CONTEXT, NULL);
	if (length)
		goto out;

	length = security_context_to_sid(state, buf, size, &sid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_sid_to_context(state, sid, &canon, &len);
	if (length)
		goto out;

	length = -ERANGE;
	if (len > SIMPLE_TRANSACTION_LIMIT) {
		pr_err("SELinux: %s:  context size (%u) exceeds "
			"payload max\n", __func__, len);
		goto out;
	}

	memcpy(buf, canon, len);
	length = len;
out:
	kfree(canon);
	return length;
}

static ssize_t sel_read_checkreqprot(struct file *filp, char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	char tmpbuf[TMPBUFLEN];
	ssize_t length;

	length = scnprintf(tmpbuf, TMPBUFLEN, "%u",
			   checkreqprot_get(fsi->state));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static ssize_t sel_write_checkreqprot(struct file *file, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	char *page;
	ssize_t length;
	unsigned int new_value;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__SETCHECKREQPROT,
			      NULL);
	if (length)
		return length;

	if (count >= PAGE_SIZE)
		return -ENOMEM;

	/* No partial writes. */
	if (*ppos != 0)
		return -EINVAL;

	page = memdup_user_nul(buf, count);
	if (IS_ERR(page))
		return PTR_ERR(page);

	length = -EINVAL;
	if (sscanf(page, "%u", &new_value) != 1)
		goto out;

	if (new_value) {
		char comm[sizeof(current->comm)];

		memcpy(comm, current->comm, sizeof(comm));
		pr_warn_once("SELinux: %s (%d) set checkreqprot to 1. This is deprecated and will be rejected in a future kernel release.\n",
			     comm, current->pid);
	}

	checkreqprot_set(fsi->state, (new_value ? 1 : 0));
	length = count;
out:
	kfree(page);
	return length;
}
static const struct file_operations sel_checkreqprot_ops = {
	.read		= sel_read_checkreqprot,
	.write		= sel_write_checkreqprot,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_write_validatetrans(struct file *file,
					const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *oldcon = NULL, *newcon = NULL, *taskcon = NULL;
	char *req = NULL;
	u32 osid, nsid, tsid;
	u16 tclass;
	int rc;

	rc = avc_has_perm(&selinux_state,
			  current_sid(), SECINITSID_SECURITY,
			  SECCLASS_SECURITY, SECURITY__VALIDATE_TRANS, NULL);
	if (rc)
		goto out;

	rc = -ENOMEM;
	if (count >= PAGE_SIZE)
		goto out;

	/* No partial writes. */
	rc = -EINVAL;
	if (*ppos != 0)
		goto out;

	req = memdup_user_nul(buf, count);
	if (IS_ERR(req)) {
		rc = PTR_ERR(req);
		req = NULL;
		goto out;
	}

	rc = -ENOMEM;
	oldcon = kzalloc(count + 1, GFP_KERNEL);
	if (!oldcon)
		goto out;

	newcon = kzalloc(count + 1, GFP_KERNEL);
	if (!newcon)
		goto out;

	taskcon = kzalloc(count + 1, GFP_KERNEL);
	if (!taskcon)
		goto out;

	rc = -EINVAL;
	if (sscanf(req, "%s %s %hu %s", oldcon, newcon, &tclass, taskcon) != 4)
		goto out;

	rc = security_context_str_to_sid(state, oldcon, &osid, GFP_KERNEL);
	if (rc)
		goto out;

	rc = security_context_str_to_sid(state, newcon, &nsid, GFP_KERNEL);
	if (rc)
		goto out;

	rc = security_context_str_to_sid(state, taskcon, &tsid, GFP_KERNEL);
	if (rc)
		goto out;

	rc = security_validate_transition_user(state, osid, nsid, tsid, tclass);
	if (!rc)
		rc = count;
out:
	kfree(req);
	kfree(oldcon);
	kfree(newcon);
	kfree(taskcon);
	return rc;
}

static const struct file_operations sel_transition_ops = {
	.write		= sel_write_validatetrans,
	.llseek		= generic_file_llseek,
};

/*
 * Remaining nodes use transaction based IO methods like nfsd/nfsctl.c
 */
static ssize_t sel_write_access(struct file *file, char *buf, size_t size);
static ssize_t sel_write_create(struct file *file, char *buf, size_t size);
static ssize_t sel_write_relabel(struct file *file, char *buf, size_t size);
static ssize_t sel_write_user(struct file *file, char *buf, size_t size);
static ssize_t sel_write_member(struct file *file, char *buf, size_t size);

static ssize_t (*const write_op[])(struct file *, char *, size_t) = {
	[SEL_ACCESS] = sel_write_access,
	[SEL_CREATE] = sel_write_create,
	[SEL_RELABEL] = sel_write_relabel,
	[SEL_USER] = sel_write_user,
	[SEL_MEMBER] = sel_write_member,
	[SEL_CONTEXT] = sel_write_context,
};

static ssize_t selinux_transaction_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
	ino_t ino = file_inode(file)->i_ino;
	char *data;
	ssize_t rv;

	if (ino >= ARRAY_SIZE(write_op) || !write_op[ino])
		return -EINVAL;

	data = simple_transaction_get(file, buf, size);
	if (IS_ERR(data))
		return PTR_ERR(data);

	rv = write_op[ino](file, data, size);
	if (rv > 0) {
		simple_transaction_set(file, rv);
		rv = size;
	}
	return rv;
}

static const struct file_operations transaction_ops = {
	.write		= selinux_transaction_write,
	.read		= simple_transaction_read,
	.release	= simple_transaction_release,
	.llseek		= generic_file_llseek,
};

/*
 * payload - write methods
 * If the method has a response, the response should be put in buf,
 * and the length returned.  Otherwise return 0 or and -error.
 */

static ssize_t sel_write_access(struct file *file, char *buf, size_t size)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *scon = NULL, *tcon = NULL;
	u32 ssid, tsid;
	u16 tclass;
	struct av_decision avd;
	ssize_t length;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__COMPUTE_AV, NULL);
	if (length)
		goto out;

	length = -ENOMEM;
	scon = kzalloc(size + 1, GFP_KERNEL);
	if (!scon)
		goto out;

	length = -ENOMEM;
	tcon = kzalloc(size + 1, GFP_KERNEL);
	if (!tcon)
		goto out;

	length = -EINVAL;
	if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
		goto out;

	length = security_context_str_to_sid(state, scon, &ssid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_context_str_to_sid(state, tcon, &tsid, GFP_KERNEL);
	if (length)
		goto out;

	security_compute_av_user(state, ssid, tsid, tclass, &avd);

	length = scnprintf(buf, SIMPLE_TRANSACTION_LIMIT,
			  "%x %x %x %x %u %x",
			  avd.allowed, 0xffffffff,
			  avd.auditallow, avd.auditdeny,
			  avd.seqno, avd.flags);
out:
	kfree(tcon);
	kfree(scon);
	return length;
}

static ssize_t sel_write_create(struct file *file, char *buf, size_t size)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *scon = NULL, *tcon = NULL;
	char *namebuf = NULL, *objname = NULL;
	u32 ssid, tsid, newsid;
	u16 tclass;
	ssize_t length;
	char *newcon = NULL;
	u32 len;
	int nargs;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__COMPUTE_CREATE,
			      NULL);
	if (length)
		goto out;

	length = -ENOMEM;
	scon = kzalloc(size + 1, GFP_KERNEL);
	if (!scon)
		goto out;

	length = -ENOMEM;
	tcon = kzalloc(size + 1, GFP_KERNEL);
	if (!tcon)
		goto out;

	length = -ENOMEM;
	namebuf = kzalloc(size + 1, GFP_KERNEL);
	if (!namebuf)
		goto out;

	length = -EINVAL;
	nargs = sscanf(buf, "%s %s %hu %s", scon, tcon, &tclass, namebuf);
	if (nargs < 3 || nargs > 4)
		goto out;
	if (nargs == 4) {
		/*
		 * If and when the name of new object to be queried contains
		 * either whitespace or multibyte characters, they shall be
		 * encoded based on the percentage-encoding rule.
		 * If not encoded, the sscanf logic picks up only left-half
		 * of the supplied name; splitted by a whitespace unexpectedly.
		 */
		char   *r, *w;
		int     c1, c2;

		r = w = namebuf;
		do {
			c1 = *r++;
			if (c1 == '+')
				c1 = ' ';
			else if (c1 == '%') {
				c1 = hex_to_bin(*r++);
				if (c1 < 0)
					goto out;
				c2 = hex_to_bin(*r++);
				if (c2 < 0)
					goto out;
				c1 = (c1 << 4) | c2;
			}
			*w++ = c1;
		} while (c1 != '\0');

		objname = namebuf;
	}

	length = security_context_str_to_sid(state, scon, &ssid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_context_str_to_sid(state, tcon, &tsid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_transition_sid_user(state, ssid, tsid, tclass,
					      objname, &newsid);
	if (length)
		goto out;

	length = security_sid_to_context(state, newsid, &newcon, &len);
	if (length)
		goto out;

	length = -ERANGE;
	if (len > SIMPLE_TRANSACTION_LIMIT) {
		pr_err("SELinux: %s:  context size (%u) exceeds "
			"payload max\n", __func__, len);
		goto out;
	}

	memcpy(buf, newcon, len);
	length = len;
out:
	kfree(newcon);
	kfree(namebuf);
	kfree(tcon);
	kfree(scon);
	return length;
}

static ssize_t sel_write_relabel(struct file *file, char *buf, size_t size)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *scon = NULL, *tcon = NULL;
	u32 ssid, tsid, newsid;
	u16 tclass;
	ssize_t length;
	char *newcon = NULL;
	u32 len;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__COMPUTE_RELABEL,
			      NULL);
	if (length)
		goto out;

	length = -ENOMEM;
	scon = kzalloc(size + 1, GFP_KERNEL);
	if (!scon)
		goto out;

	length = -ENOMEM;
	tcon = kzalloc(size + 1, GFP_KERNEL);
	if (!tcon)
		goto out;

	length = -EINVAL;
	if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
		goto out;

	length = security_context_str_to_sid(state, scon, &ssid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_context_str_to_sid(state, tcon, &tsid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_change_sid(state, ssid, tsid, tclass, &newsid);
	if (length)
		goto out;

	length = security_sid_to_context(state, newsid, &newcon, &len);
	if (length)
		goto out;

	length = -ERANGE;
	if (len > SIMPLE_TRANSACTION_LIMIT)
		goto out;

	memcpy(buf, newcon, len);
	length = len;
out:
	kfree(newcon);
	kfree(tcon);
	kfree(scon);
	return length;
}

static ssize_t sel_write_user(struct file *file, char *buf, size_t size)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *con = NULL, *user = NULL, *ptr;
	u32 sid, *sids = NULL;
	ssize_t length;
	char *newcon;
	int i, rc;
	u32 len, nsids;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__COMPUTE_USER,
			      NULL);
	if (length)
		goto out;

	length = -ENOMEM;
	con = kzalloc(size + 1, GFP_KERNEL);
	if (!con)
		goto out;

	length = -ENOMEM;
	user = kzalloc(size + 1, GFP_KERNEL);
	if (!user)
		goto out;

	length = -EINVAL;
	if (sscanf(buf, "%s %s", con, user) != 2)
		goto out;

	length = security_context_str_to_sid(state, con, &sid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_get_user_sids(state, sid, user, &sids, &nsids);
	if (length)
		goto out;

	length = sprintf(buf, "%u", nsids) + 1;
	ptr = buf + length;
	for (i = 0; i < nsids; i++) {
		rc = security_sid_to_context(state, sids[i], &newcon, &len);
		if (rc) {
			length = rc;
			goto out;
		}
		if ((length + len) >= SIMPLE_TRANSACTION_LIMIT) {
			kfree(newcon);
			length = -ERANGE;
			goto out;
		}
		memcpy(ptr, newcon, len);
		kfree(newcon);
		ptr += len;
		length += len;
	}
out:
	kfree(sids);
	kfree(user);
	kfree(con);
	return length;
}

static ssize_t sel_write_member(struct file *file, char *buf, size_t size)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *scon = NULL, *tcon = NULL;
	u32 ssid, tsid, newsid;
	u16 tclass;
	ssize_t length;
	char *newcon = NULL;
	u32 len;

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__COMPUTE_MEMBER,
			      NULL);
	if (length)
		goto out;

	length = -ENOMEM;
	scon = kzalloc(size + 1, GFP_KERNEL);
	if (!scon)
		goto out;

	length = -ENOMEM;
	tcon = kzalloc(size + 1, GFP_KERNEL);
	if (!tcon)
		goto out;

	length = -EINVAL;
	if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
		goto out;

	length = security_context_str_to_sid(state, scon, &ssid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_context_str_to_sid(state, tcon, &tsid, GFP_KERNEL);
	if (length)
		goto out;

	length = security_member_sid(state, ssid, tsid, tclass, &newsid);
	if (length)
		goto out;

	length = security_sid_to_context(state, newsid, &newcon, &len);
	if (length)
		goto out;

	length = -ERANGE;
	if (len > SIMPLE_TRANSACTION_LIMIT) {
		pr_err("SELinux: %s:  context size (%u) exceeds "
			"payload max\n", __func__, len);
		goto out;
	}

	memcpy(buf, newcon, len);
	length = len;
out:
	kfree(newcon);
	kfree(tcon);
	kfree(scon);
	return length;
}

static struct inode *sel_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_mode = mode;
		ret->i_atime = ret->i_mtime = ret->i_ctime = current_time(ret);
	}
	return ret;
}

static ssize_t sel_read_bool(struct file *filep, char __user *buf,
			     size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filep)->i_sb->s_fs_info;
	char *page = NULL;
	ssize_t length;
	ssize_t ret;
	int cur_enforcing;
	unsigned index = file_inode(filep)->i_ino & SEL_INO_MASK;
	const char *name = filep->f_path.dentry->d_name.name;

	mutex_lock(&fsi->state->policy_mutex);

	ret = -EINVAL;
	if (index >= fsi->bool_num || strcmp(name,
					     fsi->bool_pending_names[index]))
		goto out_unlock;

	ret = -ENOMEM;
	page = (char *)get_zeroed_page(GFP_KERNEL);
	if (!page)
		goto out_unlock;

	cur_enforcing = security_get_bool_value(fsi->state, index);
	if (cur_enforcing < 0) {
		ret = cur_enforcing;
		goto out_unlock;
	}
	length = scnprintf(page, PAGE_SIZE, "%d %d", cur_enforcing,
			  fsi->bool_pending_values[index]);
	mutex_unlock(&fsi->state->policy_mutex);
	ret = simple_read_from_buffer(buf, count, ppos, page, length);
out_free:
	free_page((unsigned long)page);
	return ret;

out_unlock:
	mutex_unlock(&fsi->state->policy_mutex);
	goto out_free;
}

static ssize_t sel_write_bool(struct file *filep, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filep)->i_sb->s_fs_info;
	char *page = NULL;
	ssize_t length;
	int new_value;
	unsigned index = file_inode(filep)->i_ino & SEL_INO_MASK;
	const char *name = filep->f_path.dentry->d_name.name;

	if (count >= PAGE_SIZE)
		return -ENOMEM;

	/* No partial writes. */
	if (*ppos != 0)
		return -EINVAL;

	page = memdup_user_nul(buf, count);
	if (IS_ERR(page))
		return PTR_ERR(page);

	mutex_lock(&fsi->state->policy_mutex);

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__SETBOOL,
			      NULL);
	if (length)
		goto out;

	length = -EINVAL;
	if (index >= fsi->bool_num || strcmp(name,
					     fsi->bool_pending_names[index]))
		goto out;

	length = -EINVAL;
	if (sscanf(page, "%d", &new_value) != 1)
		goto out;

	if (new_value)
		new_value = 1;

	fsi->bool_pending_values[index] = new_value;
	length = count;

out:
	mutex_unlock(&fsi->state->policy_mutex);
	kfree(page);
	return length;
}

static const struct file_operations sel_bool_ops = {
	.read		= sel_read_bool,
	.write		= sel_write_bool,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_commit_bools_write(struct file *filep,
				      const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filep)->i_sb->s_fs_info;
	char *page = NULL;
	ssize_t length;
	int new_value;

	if (count >= PAGE_SIZE)
		return -ENOMEM;

	/* No partial writes. */
	if (*ppos != 0)
		return -EINVAL;

	page = memdup_user_nul(buf, count);
	if (IS_ERR(page))
		return PTR_ERR(page);

	mutex_lock(&fsi->state->policy_mutex);

	length = avc_has_perm(&selinux_state,
			      current_sid(), SECINITSID_SECURITY,
			      SECCLASS_SECURITY, SECURITY__SETBOOL,
			      NULL);
	if (length)
		goto out;

	length = -EINVAL;
	if (sscanf(page, "%d", &new_value) != 1)
		goto out;

	length = 0;
	if (new_value && fsi->bool_pending_values)
		length = security_set_bools(fsi->state, fsi->bool_num,
					    fsi->bool_pending_values);

	if (!length)
		length = count;

out:
	mutex_unlock(&fsi->state->policy_mutex);
	kfree(page);
	return length;
}

static const struct file_operations sel_commit_bools_ops = {
	.write		= sel_commit_bools_write,
	.llseek		= generic_file_llseek,
};

static void sel_remove_entries(struct dentry *de)
{
	d_genocide(de);
	shrink_dcache_parent(de);
}

static int sel_make_bools(struct selinux_policy *newpolicy, struct dentry *bool_dir,
			  unsigned int *bool_num, char ***bool_pending_names,
			  unsigned int **bool_pending_values)
{
	int ret;
	ssize_t len;
	struct dentry *dentry = NULL;
	struct inode *inode = NULL;
	struct inode_security_struct *isec;
	char **names = NULL, *page;
	u32 i, num;
	int *values = NULL;
	u32 sid;

	ret = -ENOMEM;
	page = (char *)get_zeroed_page(GFP_KERNEL);
	if (!page)
		goto out;

	ret = security_get_bools(newpolicy, &num, &names, &values);
	if (ret)
		goto out;

	for (i = 0; i < num; i++) {
		ret = -ENOMEM;
		dentry = d_alloc_name(bool_dir, names[i]);
		if (!dentry)
			goto out;

		ret = -ENOMEM;
		inode = sel_make_inode(bool_dir->d_sb, S_IFREG | S_IRUGO | S_IWUSR);
		if (!inode) {
			dput(dentry);
			goto out;
		}

		ret = -ENAMETOOLONG;
		len = snprintf(page, PAGE_SIZE, "/%s/%s", BOOL_DIR_NAME, names[i]);
		if (len >= PAGE_SIZE) {
			dput(dentry);
			iput(inode);
			goto out;
		}

		isec = selinux_inode(inode);
		ret = selinux_policy_genfs_sid(newpolicy, "selinuxfs", page,
					 SECCLASS_FILE, &sid);
		if (ret) {
			pr_warn_ratelimited("SELinux: no sid found, defaulting to security isid for %s\n",
					   page);
			sid = SECINITSID_SECURITY;
		}

		isec->sid = sid;
		isec->initialized = LABEL_INITIALIZED;
		inode->i_fop = &sel_bool_ops;
		inode->i_ino = i|SEL_BOOL_INO_OFFSET;
		d_add(dentry, inode);
	}
	*bool_num = num;
	*bool_pending_names = names;
	*bool_pending_values = values;

	free_page((unsigned long)page);
	return 0;
out:
	free_page((unsigned long)page);

	if (names) {
		for (i = 0; i < num; i++)
			kfree(names[i]);
		kfree(names);
	}
	kfree(values);
	sel_remove_entries(bool_dir);

	return ret;
}

static ssize_t sel_read_avc_cache_threshold(struct file *filp, char __user *buf,
					    size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char tmpbuf[TMPBUFLEN];
	ssize_t length;

	length = scnprintf(tmpbuf, TMPBUFLEN, "%u",
			   avc_get_cache_threshold(state->avc));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static ssize_t sel_write_avc_cache_threshold(struct file *file,
					     const char __user *buf,
					     size_t count, loff_t *ppos)

{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *page;
	ssize_t ret;
	unsigned int new_value;

	ret = avc_has_perm(&selinux_state,
			   current_sid(), SECINITSID_SECURITY,
			   SECCLASS_SECURITY, SECURITY__SETSECPARAM,
			   NULL);
	if (ret)
		return ret;

	if (count >= PAGE_SIZE)
		return -ENOMEM;

	/* No partial writes. */
	if (*ppos != 0)
		return -EINVAL;

	page = memdup_user_nul(buf, count);
	if (IS_ERR(page))
		return PTR_ERR(page);

	ret = -EINVAL;
	if (sscanf(page, "%u", &new_value) != 1)
		goto out;

	avc_set_cache_threshold(state->avc, new_value);

	ret = count;
out:
	kfree(page);
	return ret;
}

static ssize_t sel_read_avc_hash_stats(struct file *filp, char __user *buf,
				       size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *page;
	ssize_t length;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	length = avc_get_hash_stats(state->avc, page);
	if (length >= 0)
		length = simple_read_from_buffer(buf, count, ppos, page, length);
	free_page((unsigned long)page);

	return length;
}

static ssize_t sel_read_sidtab_hash_stats(struct file *filp, char __user *buf,
					size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(filp)->i_sb->s_fs_info;
	struct selinux_state *state = fsi->state;
	char *page;
	ssize_t length;

	page = (char *)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	length = security_sidtab_hash_stats(state, page);
	if (length >= 0)
		length = simple_read_from_buffer(buf, count, ppos, page,
						length);
	free_page((unsigned long)page);

	return length;
}

static const struct file_operations sel_sidtab_hash_stats_ops = {
	.read		= sel_read_sidtab_hash_stats,
	.llseek		= generic_file_llseek,
};

static const struct file_operations sel_avc_cache_threshold_ops = {
	.read		= sel_read_avc_cache_threshold,
	.write		= sel_write_avc_cache_threshold,
	.llseek		= generic_file_llseek,
};

static const struct file_operations sel_avc_hash_stats_ops = {
	.read		= sel_read_avc_hash_stats,
	.llseek		= generic_file_llseek,
};

#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
static struct avc_cache_stats *sel_avc_get_stat_idx(loff_t *idx)
{
	int cpu;

	for (cpu = *idx; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*idx = cpu + 1;
		return &per_cpu(avc_cache_stats, cpu);
	}
	(*idx)++;
	return NULL;
}

static void *sel_avc_stats_seq_start(struct seq_file *seq, loff_t *pos)
{
	loff_t n = *pos - 1;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	return sel_avc_get_stat_idx(&n);
}

static void *sel_avc_stats_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return sel_avc_get_stat_idx(pos);
}

static int sel_avc_stats_seq_show(struct seq_file *seq, void *v)
{
	struct avc_cache_stats *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq,
			 "lookups hits misses allocations reclaims frees\n");
	} else {
		unsigned int lookups = st->lookups;
		unsigned int misses = st->misses;
		unsigned int hits = lookups - misses;
		seq_printf(seq, "%u %u %u %u %u %u\n", lookups,
			   hits, misses, st->allocations,
			   st->reclaims, st->frees);
	}
	return 0;
}

static void sel_avc_stats_seq_stop(struct seq_file *seq, void *v)
{ }

static const struct seq_operations sel_avc_cache_stats_seq_ops = {
	.start		= sel_avc_stats_seq_start,
	.next		= sel_avc_stats_seq_next,
	.show		= sel_avc_stats_seq_show,
	.stop		= sel_avc_stats_seq_stop,
};

static int sel_open_avc_cache_stats(struct inode *inode, struct file *file)
{
	return seq_open(file, &sel_avc_cache_stats_seq_ops);
}

static const struct file_operations sel_avc_cache_stats_ops = {
	.open		= sel_open_avc_cache_stats,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};
#endif

static int sel_make_avc_files(struct dentry *dir)
{
	struct super_block *sb = dir->d_sb;
	struct selinux_fs_info *fsi = sb->s_fs_info;
	int i;
	static const struct tree_descr files[] = {
		{ "cache_threshold",
		  &sel_avc_cache_threshold_ops, S_IRUGO|S_IWUSR },
		{ "hash_stats", &sel_avc_hash_stats_ops, S_IRUGO },
#ifdef CONFIG_SECURITY_SELINUX_AVC_STATS
		{ "cache_stats", &sel_avc_cache_stats_ops, S_IRUGO },
#endif
	};

	for (i = 0; i < ARRAY_SIZE(files); i++) {
		struct inode *inode;
		struct dentry *dentry;

		dentry = d_alloc_name(dir, files[i].name);
		if (!dentry)
			return -ENOMEM;

		inode = sel_make_inode(dir->d_sb, S_IFREG|files[i].mode);
		if (!inode) {
			dput(dentry);
			return -ENOMEM;
		}

		inode->i_fop = files[i].ops;
		inode->i_ino = ++fsi->last_ino;
		d_add(dentry, inode);
	}

	return 0;
}

static int sel_make_ss_files(struct dentry *dir)
{
	struct super_block *sb = dir->d_sb;
	struct selinux_fs_info *fsi = sb->s_fs_info;
	int i;
	static struct tree_descr files[] = {
		{ "sidtab_hash_stats", &sel_sidtab_hash_stats_ops, S_IRUGO },
	};

	for (i = 0; i < ARRAY_SIZE(files); i++) {
		struct inode *inode;
		struct dentry *dentry;

		dentry = d_alloc_name(dir, files[i].name);
		if (!dentry)
			return -ENOMEM;

		inode = sel_make_inode(dir->d_sb, S_IFREG|files[i].mode);
		if (!inode) {
			dput(dentry);
			return -ENOMEM;
		}

		inode->i_fop = files[i].ops;
		inode->i_ino = ++fsi->last_ino;
		d_add(dentry, inode);
	}

	return 0;
}

static ssize_t sel_read_initcon(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	char *con;
	u32 sid, len;
	ssize_t ret;

	sid = file_inode(file)->i_ino&SEL_INO_MASK;
	ret = security_sid_to_context(fsi->state, sid, &con, &len);
	if (ret)
		return ret;

	ret = simple_read_from_buffer(buf, count, ppos, con, len);
	kfree(con);
	return ret;
}

static const struct file_operations sel_initcon_ops = {
	.read		= sel_read_initcon,
	.llseek		= generic_file_llseek,
};

static int sel_make_initcon_files(struct dentry *dir)
{
	int i;

	for (i = 1; i <= SECINITSID_NUM; i++) {
		struct inode *inode;
		struct dentry *dentry;
		const char *s = security_get_initial_sid_context(i);

		if (!s)
			continue;
		dentry = d_alloc_name(dir, s);
		if (!dentry)
			return -ENOMEM;

		inode = sel_make_inode(dir->d_sb, S_IFREG|S_IRUGO);
		if (!inode) {
			dput(dentry);
			return -ENOMEM;
		}

		inode->i_fop = &sel_initcon_ops;
		inode->i_ino = i|SEL_INITCON_INO_OFFSET;
		d_add(dentry, inode);
	}

	return 0;
}

static inline unsigned long sel_class_to_ino(u16 class)
{
	return (class * (SEL_VEC_MAX + 1)) | SEL_CLASS_INO_OFFSET;
}

static inline u16 sel_ino_to_class(unsigned long ino)
{
	return (ino & SEL_INO_MASK) / (SEL_VEC_MAX + 1);
}

static inline unsigned long sel_perm_to_ino(u16 class, u32 perm)
{
	return (class * (SEL_VEC_MAX + 1) + perm) | SEL_CLASS_INO_OFFSET;
}

static inline u32 sel_ino_to_perm(unsigned long ino)
{
	return (ino & SEL_INO_MASK) % (SEL_VEC_MAX + 1);
}

static ssize_t sel_read_class(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long ino = file_inode(file)->i_ino;
	char res[TMPBUFLEN];
	ssize_t len = scnprintf(res, sizeof(res), "%d", sel_ino_to_class(ino));
	return simple_read_from_buffer(buf, count, ppos, res, len);
}

static const struct file_operations sel_class_ops = {
	.read		= sel_read_class,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_read_perm(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long ino = file_inode(file)->i_ino;
	char res[TMPBUFLEN];
	ssize_t len = scnprintf(res, sizeof(res), "%d", sel_ino_to_perm(ino));
	return simple_read_from_buffer(buf, count, ppos, res, len);
}

static const struct file_operations sel_perm_ops = {
	.read		= sel_read_perm,
	.llseek		= generic_file_llseek,
};

static ssize_t sel_read_policycap(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct selinux_fs_info *fsi = file_inode(file)->i_sb->s_fs_info;
	int value;
	char tmpbuf[TMPBUFLEN];
	ssize_t length;
	unsigned long i_ino = file_inode(file)->i_ino;

	value = security_policycap_supported(fsi->state, i_ino & SEL_INO_MASK);
	length = scnprintf(tmpbuf, TMPBUFLEN, "%d", value);

	return simple_read_from_buffer(buf, count, ppos, tmpbuf, length);
}

static const struct file_operations sel_policycap_ops = {
	.read		= sel_read_policycap,
	.llseek		= generic_file_llseek,
};

static int sel_make_perm_files(struct selinux_policy *newpolicy,
			char *objclass, int classvalue,
			struct dentry *dir)
{
	int i, rc, nperms;
	char **perms;

	rc = security_get_permissions(newpolicy, objclass, &perms, &nperms);
	if (rc)
		return rc;

	for (i = 0; i < nperms; i++) {
		struct inode *inode;
		struct dentry *dentry;

		rc = -ENOMEM;
		dentry = d_alloc_name(dir, perms[i]);
		if (!dentry)
			goto out;

		rc = -ENOMEM;
		inode = sel_make_inode(dir->d_sb, S_IFREG|S_IRUGO);
		if (!inode) {
			dput(dentry);
			goto out;
		}

		inode->i_fop = &sel_perm_ops;
		/* i+1 since perm values are 1-indexed */
		inode->i_ino = sel_perm_to_ino(classvalue, i + 1);
		d_add(dentry, inode);
	}
	rc = 0;
out:
	for (i = 0; i < nperms; i++)
		kfree(perms[i]);
	kfree(perms);
	return rc;
}

static int sel_make_class_dir_entries(struct selinux_policy *newpolicy,
				char *classname, int index,
				struct dentry *dir)
{
	struct super_block *sb = dir->d_sb;
	struct selinux_fs_info *fsi = sb->s_fs_info;
	struct dentry *dentry = NULL;
	struct inode *inode = NULL;
	int rc;

	dentry = d_alloc_name(dir, "index");
	if (!dentry)
		return -ENOMEM;

	inode = sel_make_inode(dir->d_sb, S_IFREG|S_IRUGO);
	if (!inode) {
		dput(dentry);
		return -ENOMEM;
	}

	inode->i_fop = &sel_class_ops;
	inode->i_ino = sel_class_to_ino(index);
	d_add(dentry, inode);

	dentry = sel_make_dir(dir, "perms", &fsi->last_class_ino);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	rc = sel_make_perm_files(newpolicy, classname, index, dentry);

	return rc;
}

static int sel_make_classes(struct selinux_policy *newpolicy,
			    struct dentry *class_dir,
			    unsigned long *last_class_ino)
{

	int rc, nclasses, i;
	char **classes;

	rc = security_get_classes(newpolicy, &classes, &nclasses);
	if (rc)
		return rc;

	/* +2 since classes are 1-indexed */
	*last_class_ino = sel_class_to_ino(nclasses + 2);

	for (i = 0; i < nclasses; i++) {
		struct dentry *class_name_dir;

		class_name_dir = sel_make_dir(class_dir, classes[i],
					      last_class_ino);
		if (IS_ERR(class_name_dir)) {
			rc = PTR_ERR(class_name_dir);
			goto out;
		}

		/* i+1 since class values are 1-indexed */
		rc = sel_make_class_dir_entries(newpolicy, classes[i], i + 1,
				class_name_dir);
		if (rc)
			goto out;
	}
	rc = 0;
out:
	for (i = 0; i < nclasses; i++)
		kfree(classes[i]);
	kfree(classes);
	return rc;
}

static int sel_make_policycap(struct selinux_fs_info *fsi)
{
	unsigned int iter;
	struct dentry *dentry = NULL;
	struct inode *inode = NULL;

	for (iter = 0; iter <= POLICYDB_CAPABILITY_MAX; iter++) {
		if (iter < ARRAY_SIZE(selinux_policycap_names))
			dentry = d_alloc_name(fsi->policycap_dir,
					      selinux_policycap_names[iter]);
		else
			dentry = d_alloc_name(fsi->policycap_dir, "unknown");

		if (dentry == NULL)
			return -ENOMEM;

		inode = sel_make_inode(fsi->sb, S_IFREG | 0444);
		if (inode == NULL) {
			dput(dentry);
			return -ENOMEM;
		}

		inode->i_fop = &sel_policycap_ops;
		inode->i_ino = iter | SEL_POLICYCAP_INO_OFFSET;
		d_add(dentry, inode);
	}

	return 0;
}

static struct dentry *sel_make_dir(struct dentry *dir, const char *name,
			unsigned long *ino)
{
	struct dentry *dentry = d_alloc_name(dir, name);
	struct inode *inode;

	if (!dentry)
		return ERR_PTR(-ENOMEM);

	inode = sel_make_inode(dir->d_sb, S_IFDIR | S_IRUGO | S_IXUGO);
	if (!inode) {
		dput(dentry);
		return ERR_PTR(-ENOMEM);
	}

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_ino = ++(*ino);
	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_add(dentry, inode);
	/* bump link count on parent directory, too */
	inc_nlink(d_inode(dir));

	return dentry;
}

static struct dentry *sel_make_disconnected_dir(struct super_block *sb,
						unsigned long *ino)
{
	struct inode *inode = sel_make_inode(sb, S_IFDIR | S_IRUGO | S_IXUGO);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_ino = ++(*ino);
	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	return d_obtain_alias(inode);
}

#define NULL_FILE_NAME "null"

static int sel_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct selinux_fs_info *fsi;
	int ret;
	struct dentry *dentry;
	struct inode *inode;
	struct inode_security_struct *isec;

	static const struct tree_descr selinux_files[] = {
		[SEL_LOAD] = {"load", &sel_load_ops, S_IRUSR|S_IWUSR},
		[SEL_ENFORCE] = {"enforce", &sel_enforce_ops, S_IRUGO|S_IWUSR},
		[SEL_CONTEXT] = {"context", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_ACCESS] = {"access", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_CREATE] = {"create", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_RELABEL] = {"relabel", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_USER] = {"user", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_POLICYVERS] = {"policyvers", &sel_policyvers_ops, S_IRUGO},
		[SEL_COMMIT_BOOLS] = {"commit_pending_bools", &sel_commit_bools_ops, S_IWUSR},
		[SEL_MLS] = {"mls", &sel_mls_ops, S_IRUGO},
		[SEL_DISABLE] = {"disable", &sel_disable_ops, S_IWUSR},
		[SEL_MEMBER] = {"member", &transaction_ops, S_IRUGO|S_IWUGO},
		[SEL_CHECKREQPROT] = {"checkreqprot", &sel_checkreqprot_ops, S_IRUGO|S_IWUSR},
		[SEL_REJECT_UNKNOWN] = {"reject_unknown", &sel_handle_unknown_ops, S_IRUGO},
		[SEL_DENY_UNKNOWN] = {"deny_unknown", &sel_handle_unknown_ops, S_IRUGO},
		[SEL_STATUS] = {"status", &sel_handle_status_ops, S_IRUGO},
		[SEL_POLICY] = {"policy", &sel_policy_ops, S_IRUGO},
		[SEL_VALIDATE_TRANS] = {"validatetrans", &sel_transition_ops,
					S_IWUGO},
		/* last one */ {""}
	};

	ret = selinux_fs_info_create(sb);
	if (ret)
		goto err;

	ret = simple_fill_super(sb, SELINUX_MAGIC, selinux_files);
	if (ret)
		goto err;

	fsi = sb->s_fs_info;
	fsi->bool_dir = sel_make_dir(sb->s_root, BOOL_DIR_NAME, &fsi->last_ino);
	if (IS_ERR(fsi->bool_dir)) {
		ret = PTR_ERR(fsi->bool_dir);
		fsi->bool_dir = NULL;
		goto err;
	}

	ret = -ENOMEM;
	dentry = d_alloc_name(sb->s_root, NULL_FILE_NAME);
	if (!dentry)
		goto err;

	ret = -ENOMEM;
	inode = sel_make_inode(sb, S_IFCHR | S_IRUGO | S_IWUGO);
	if (!inode) {
		dput(dentry);
		goto err;
	}

	inode->i_ino = ++fsi->last_ino;
	isec = selinux_inode(inode);
	isec->sid = SECINITSID_DEVNULL;
	isec->sclass = SECCLASS_CHR_FILE;
	isec->initialized = LABEL_INITIALIZED;

	init_special_inode(inode, S_IFCHR | S_IRUGO | S_IWUGO, MKDEV(MEM_MAJOR, 3));
	d_add(dentry, inode);

	dentry = sel_make_dir(sb->s_root, "avc", &fsi->last_ino);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto err;
	}

	ret = sel_make_avc_files(dentry);

	dentry = sel_make_dir(sb->s_root, "ss", &fsi->last_ino);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto err;
	}

	ret = sel_make_ss_files(dentry);
	if (ret)
		goto err;

	dentry = sel_make_dir(sb->s_root, "initial_contexts", &fsi->last_ino);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto err;
	}

	ret = sel_make_initcon_files(dentry);
	if (ret)
		goto err;

	fsi->class_dir = sel_make_dir(sb->s_root, CLASS_DIR_NAME, &fsi->last_ino);
	if (IS_ERR(fsi->class_dir)) {
		ret = PTR_ERR(fsi->class_dir);
		fsi->class_dir = NULL;
		goto err;
	}

	fsi->policycap_dir = sel_make_dir(sb->s_root, POLICYCAP_DIR_NAME,
					  &fsi->last_ino);
	if (IS_ERR(fsi->policycap_dir)) {
		ret = PTR_ERR(fsi->policycap_dir);
		fsi->policycap_dir = NULL;
		goto err;
	}

	ret = sel_make_policycap(fsi);
	if (ret) {
		pr_err("SELinux: failed to load policy capabilities\n");
		goto err;
	}

	return 0;
err:
	pr_err("SELinux: %s:  failed while creating inodes\n",
		__func__);

	selinux_fs_info_free(sb);

	return ret;
}

static int sel_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, sel_fill_super);
}

static const struct fs_context_operations sel_context_ops = {
	.get_tree	= sel_get_tree,
};

static int sel_init_fs_context(struct fs_context *fc)
{
	fc->ops = &sel_context_ops;
	return 0;
}

static void sel_kill_sb(struct super_block *sb)
{
	selinux_fs_info_free(sb);
	kill_litter_super(sb);
}

static struct file_system_type sel_fs_type = {
	.name		= "selinuxfs",
	.init_fs_context = sel_init_fs_context,
	.kill_sb	= sel_kill_sb,
};

struct vfsmount *selinuxfs_mount;
struct path selinux_null;

static int __init init_sel_fs(void)
{
	struct qstr null_name = QSTR_INIT(NULL_FILE_NAME,
					  sizeof(NULL_FILE_NAME)-1);
	int err;

	if (!selinux_enabled_boot)
		return 0;

	err = sysfs_create_mount_point(fs_kobj, "selinux");
	if (err)
		return err;

	err = register_filesystem(&sel_fs_type);
	if (err) {
		sysfs_remove_mount_point(fs_kobj, "selinux");
		return err;
	}

	selinux_null.mnt = selinuxfs_mount = kern_mount(&sel_fs_type);
	if (IS_ERR(selinuxfs_mount)) {
		pr_err("selinuxfs:  could not mount!\n");
		err = PTR_ERR(selinuxfs_mount);
		selinuxfs_mount = NULL;
	}
	selinux_null.dentry = d_hash_and_lookup(selinux_null.mnt->mnt_root,
						&null_name);
	if (IS_ERR(selinux_null.dentry)) {
		pr_err("selinuxfs:  could not lookup null!\n");
		err = PTR_ERR(selinux_null.dentry);
		selinux_null.dentry = NULL;
	}

	return err;
}

__initcall(init_sel_fs);

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
void exit_sel_fs(void)
{
	sysfs_remove_mount_point(fs_kobj, "selinux");
	dput(selinux_null.dentry);
	kern_unmount(selinuxfs_mount);
	unregister_filesystem(&sel_fs_type);
}
#endif
