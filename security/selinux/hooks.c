// SPDX-License-Identifier: GPL-2.0-only
/*
 *  NSA Security-Enhanced Linux (SELinux) security module
 *
 *  This file contains the SELinux hook function implementations.
 *
 *  Authors:  Stephen Smalley, <sds@tycho.nsa.gov>
 *	      Chris Vance, <cvance@nai.com>
 *	      Wayne Salamon, <wsalamon@nai.com>
 *	      James Morris <jmorris@redhat.com>
 *
 *  Copyright (C) 2001,2002 Networks Associates Technology, Inc.
 *  Copyright (C) 2003-2008 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *					   Eric Paris <eparis@redhat.com>
 *  Copyright (C) 2004-2005 Trusted Computer Solutions, Inc.
 *			    <dgoeddel@trustedcs.com>
 *  Copyright (C) 2006, 2007, 2009 Hewlett-Packard Development Company, L.P.
 *	Paul Moore <paul@paul-moore.com>
 *  Copyright (C) 2007 Hitachi Software Engineering Co., Ltd.
 *		       Yuichi Nakamura <ynakam@hitachisoft.jp>
 *  Copyright (C) 2016 Mellanox Technologies
 */

#include <linux/init.h>
#include <linux/kd.h>
#include <linux/kernel.h>
#include <linux/kernel_read_file.h>
#include <linux/tracehook.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/lsm_hooks.h>
#include <linux/xattr.h>
#include <linux/capability.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/tty.h>
#include <net/icmp.h>
#include <net/ip.h>		/* for local_port_range[] */
#include <net/tcp.h>		/* struct or_callable used in sock_rcv_skb */
#include <net/inet_connection_sock.h>
#include <net/net_namespace.h>
#include <net/netlabel.h>
#include <linux/uaccess.h>
#include <asm/ioctls.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>	/* for network interface checks */
#include <net/netlink.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/dccp.h>
#include <linux/sctp.h>
#include <net/sctp/structs.h>
#include <linux/quota.h>
#include <linux/un.h>		/* for Unix socket types */
#include <net/af_unix.h>	/* for Unix socket types */
#include <linux/parser.h>
#include <linux/nfs_mount.h>
#include <net/ipv6.h>
#include <linux/hugetlb.h>
#include <linux/personality.h>
#include <linux/audit.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/posix-timers.h>
#include <linux/syslog.h>
#include <linux/user_namespace.h>
#include <linux/export.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/bpf.h>
#include <linux/kernfs.h>
#include <linux/stringhash.h>	/* for hashlen_string() */
#include <uapi/linux/mount.h>
#include <linux/fsnotify.h>
#include <linux/fanotify.h>

#include "avc.h"
#include "objsec.h"
#include "netif.h"
#include "netnode.h"
#include "netport.h"
#include "ibpkey.h"
#include "xfrm.h"
#include "netlabel.h"
#include "audit.h"
#include "avc_ss.h"

struct selinux_state selinux_state;

/* SECMARK reference count */
static atomic_t selinux_secmark_refcount = ATOMIC_INIT(0);

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
static int selinux_enforcing_boot __initdata;

static int __init enforcing_setup(char *str)
{
	unsigned long enforcing;
	if (!kstrtoul(str, 0, &enforcing))
		selinux_enforcing_boot = enforcing ? 1 : 0;
	return 1;
}
__setup("enforcing=", enforcing_setup);
#else
#define selinux_enforcing_boot 1
#endif

int selinux_enabled_boot __initdata = 1;
#ifdef CONFIG_SECURITY_SELINUX_BOOTPARAM
static int __init selinux_enabled_setup(char *str)
{
	unsigned long enabled;
	if (!kstrtoul(str, 0, &enabled))
		selinux_enabled_boot = enabled ? 1 : 0;
	return 1;
}
__setup("selinux=", selinux_enabled_setup);
#endif

static unsigned int selinux_checkreqprot_boot =
	CONFIG_SECURITY_SELINUX_CHECKREQPROT_VALUE;

static int __init checkreqprot_setup(char *str)
{
	unsigned long checkreqprot;

	if (!kstrtoul(str, 0, &checkreqprot)) {
		selinux_checkreqprot_boot = checkreqprot ? 1 : 0;
		if (checkreqprot)
			pr_warn("SELinux: checkreqprot set to 1 via kernel parameter.  This is deprecated and will be rejected in a future kernel release.\n");
	}
	return 1;
}
__setup("checkreqprot=", checkreqprot_setup);

/**
 * selinux_secmark_enabled - Check to see if SECMARK is currently enabled
 *
 * Description:
 * This function checks the SECMARK reference counter to see if any SECMARK
 * targets are currently configured, if the reference counter is greater than
 * zero SECMARK is considered to be enabled.  Returns true (1) if SECMARK is
 * enabled, false (0) if SECMARK is disabled.  If the always_check_network
 * policy capability is enabled, SECMARK is always considered enabled.
 *
 */
static int selinux_secmark_enabled(void)
{
	return (selinux_policycap_alwaysnetwork() ||
		atomic_read(&selinux_secmark_refcount));
}

/**
 * selinux_peerlbl_enabled - Check to see if peer labeling is currently enabled
 *
 * Description:
 * This function checks if NetLabel or labeled IPSEC is enabled.  Returns true
 * (1) if any are enabled or false (0) if neither are enabled.  If the
 * always_check_network policy capability is enabled, peer labeling
 * is always considered enabled.
 *
 */
static int selinux_peerlbl_enabled(void)
{
	return (selinux_policycap_alwaysnetwork() ||
		netlbl_enabled() || selinux_xfrm_enabled());
}

static int selinux_netcache_avc_callback(u32 event)
{
	if (event == AVC_CALLBACK_RESET) {
		sel_netif_flush();
		sel_netnode_flush();
		sel_netport_flush();
		synchronize_net();
	}
	return 0;
}

static int selinux_lsm_notifier_avc_callback(u32 event)
{
	if (event == AVC_CALLBACK_RESET) {
		sel_ib_pkey_flush();
		call_blocking_lsm_notifier(LSM_POLICY_CHANGE, NULL);
	}

	return 0;
}

/*
 * initialise the security for the init task
 */
static void cred_init_security(void)
{
	struct cred *cred = (struct cred *) current->real_cred;
	struct task_security_struct *tsec;

	tsec = selinux_cred(cred);
	tsec->osid = tsec->sid = SECINITSID_KERNEL;
}

/*
 * get the security ID of a set of credentials
 */
static inline u32 cred_sid(const struct cred *cred)
{
	const struct task_security_struct *tsec;

	tsec = selinux_cred(cred);
	return tsec->sid;
}

/*
 * get the subjective security ID of a task
 */
static inline u32 task_sid_subj(const struct task_struct *task)
{
	u32 sid;

	rcu_read_lock();
	sid = cred_sid(rcu_dereference(task->cred));
	rcu_read_unlock();
	return sid;
}

/*
 * get the objective security ID of a task
 */
static inline u32 task_sid_obj(const struct task_struct *task)
{
	u32 sid;

	rcu_read_lock();
	sid = cred_sid(__task_cred(task));
	rcu_read_unlock();
	return sid;
}

/*
 * get the security ID of a task for use with binder
 */
static inline u32 task_sid_binder(const struct task_struct *task)
{
	/*
	 * In many case where this function is used we should be using the
	 * task's subjective SID, but we can't reliably access the subjective
	 * creds of a task other than our own so we must use the objective
	 * creds/SID, which are safe to access.  The downside is that if a task
	 * is temporarily overriding it's creds it will not be reflected here;
	 * however, it isn't clear that binder would handle that case well
	 * anyway.
	 *
	 * If this ever changes and we can safely reference the subjective
	 * creds/SID of another task, this function will make it easier to
	 * identify the various places where we make use of the task SIDs in
	 * the binder code.  It is also likely that we will need to adjust
	 * the main drivers/android binder code as well.
	 */
	return task_sid_obj(task);
}

static int inode_doinit_with_dentry(struct inode *inode, struct dentry *opt_dentry);

/*
 * Try reloading inode security labels that have been marked as invalid.  The
 * @may_sleep parameter indicates when sleeping and thus reloading labels is
 * allowed; when set to false, returns -ECHILD when the label is
 * invalid.  The @dentry parameter should be set to a dentry of the inode.
 */
static int __inode_security_revalidate(struct inode *inode,
				       struct dentry *dentry,
				       bool may_sleep)
{
	struct inode_security_struct *isec = selinux_inode(inode);

	might_sleep_if(may_sleep);

	if (selinux_initialized(&selinux_state) &&
	    isec->initialized != LABEL_INITIALIZED) {
		if (!may_sleep)
			return -ECHILD;

		/*
		 * Try reloading the inode security label.  This will fail if
		 * @opt_dentry is NULL and no dentry for this inode can be
		 * found; in that case, continue using the old label.
		 */
		inode_doinit_with_dentry(inode, dentry);
	}
	return 0;
}

static struct inode_security_struct *inode_security_novalidate(struct inode *inode)
{
	return selinux_inode(inode);
}

static struct inode_security_struct *inode_security_rcu(struct inode *inode, bool rcu)
{
	int error;

	error = __inode_security_revalidate(inode, NULL, !rcu);
	if (error)
		return ERR_PTR(error);
	return selinux_inode(inode);
}

/*
 * Get the security label of an inode.
 */
static struct inode_security_struct *inode_security(struct inode *inode)
{
	__inode_security_revalidate(inode, NULL, true);
	return selinux_inode(inode);
}

static struct inode_security_struct *backing_inode_security_novalidate(struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);

	return selinux_inode(inode);
}

/*
 * Get the security label of a dentry's backing inode.
 */
static struct inode_security_struct *backing_inode_security(struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);

	__inode_security_revalidate(inode, dentry, true);
	return selinux_inode(inode);
}

static void inode_free_security(struct inode *inode)
{
	struct inode_security_struct *isec = selinux_inode(inode);
	struct superblock_security_struct *sbsec;

	if (!isec)
		return;
	sbsec = selinux_superblock(inode->i_sb);
	/*
	 * As not all inode security structures are in a list, we check for
	 * empty list outside of the lock to make sure that we won't waste
	 * time taking a lock doing nothing.
	 *
	 * The list_del_init() function can be safely called more than once.
	 * It should not be possible for this function to be called with
	 * concurrent list_add(), but for better safety against future changes
	 * in the code, we use list_empty_careful() here.
	 */
	if (!list_empty_careful(&isec->list)) {
		spin_lock(&sbsec->isec_lock);
		list_del_init(&isec->list);
		spin_unlock(&sbsec->isec_lock);
	}
}

struct selinux_mnt_opts {
	const char *fscontext, *context, *rootcontext, *defcontext;
};

static void selinux_free_mnt_opts(void *mnt_opts)
{
	struct selinux_mnt_opts *opts = mnt_opts;
	kfree(opts->fscontext);
	kfree(opts->context);
	kfree(opts->rootcontext);
	kfree(opts->defcontext);
	kfree(opts);
}

enum {
	Opt_error = -1,
	Opt_context = 0,
	Opt_defcontext = 1,
	Opt_fscontext = 2,
	Opt_rootcontext = 3,
	Opt_seclabel = 4,
};

#define A(s, has_arg) {#s, sizeof(#s) - 1, Opt_##s, has_arg}
static struct {
	const char *name;
	int len;
	int opt;
	bool has_arg;
} tokens[] = {
	A(context, true),
	A(fscontext, true),
	A(defcontext, true),
	A(rootcontext, true),
	A(seclabel, false),
};
#undef A

static int match_opt_prefix(char *s, int l, char **arg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tokens); i++) {
		size_t len = tokens[i].len;
		if (len > l || memcmp(s, tokens[i].name, len))
			continue;
		if (tokens[i].has_arg) {
			if (len == l || s[len] != '=')
				continue;
			*arg = s + len + 1;
		} else if (len != l)
			continue;
		return tokens[i].opt;
	}
	return Opt_error;
}

#define SEL_MOUNT_FAIL_MSG "SELinux:  duplicate or incompatible mount options\n"

static int may_context_mount_sb_relabel(u32 sid,
			struct superblock_security_struct *sbsec,
			const struct cred *cred)
{
	const struct task_security_struct *tsec = selinux_cred(cred);
	int rc;

	rc = avc_has_perm(&selinux_state,
			  tsec->sid, sbsec->sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__RELABELFROM, NULL);
	if (rc)
		return rc;

	rc = avc_has_perm(&selinux_state,
			  tsec->sid, sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__RELABELTO, NULL);
	return rc;
}

static int may_context_mount_inode_relabel(u32 sid,
			struct superblock_security_struct *sbsec,
			const struct cred *cred)
{
	const struct task_security_struct *tsec = selinux_cred(cred);
	int rc;
	rc = avc_has_perm(&selinux_state,
			  tsec->sid, sbsec->sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__RELABELFROM, NULL);
	if (rc)
		return rc;

	rc = avc_has_perm(&selinux_state,
			  sid, sbsec->sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__ASSOCIATE, NULL);
	return rc;
}

static int selinux_is_genfs_special_handling(struct super_block *sb)
{
	/* Special handling. Genfs but also in-core setxattr handler */
	return	!strcmp(sb->s_type->name, "sysfs") ||
		!strcmp(sb->s_type->name, "pstore") ||
		!strcmp(sb->s_type->name, "debugfs") ||
		!strcmp(sb->s_type->name, "tracefs") ||
		!strcmp(sb->s_type->name, "rootfs") ||
		(selinux_policycap_cgroupseclabel() &&
		 (!strcmp(sb->s_type->name, "cgroup") ||
		  !strcmp(sb->s_type->name, "cgroup2")));
}

static int selinux_is_sblabel_mnt(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = selinux_superblock(sb);

	/*
	 * IMPORTANT: Double-check logic in this function when adding a new
	 * SECURITY_FS_USE_* definition!
	 */
	BUILD_BUG_ON(SECURITY_FS_USE_MAX != 7);

	switch (sbsec->behavior) {
	case SECURITY_FS_USE_XATTR:
	case SECURITY_FS_USE_TRANS:
	case SECURITY_FS_USE_TASK:
	case SECURITY_FS_USE_NATIVE:
		return 1;

	case SECURITY_FS_USE_GENFS:
		return selinux_is_genfs_special_handling(sb);

	/* Never allow relabeling on context mounts */
	case SECURITY_FS_USE_MNTPOINT:
	case SECURITY_FS_USE_NONE:
	default:
		return 0;
	}
}

static int sb_check_xattr_support(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = sb->s_security;
	struct dentry *root = sb->s_root;
	struct inode *root_inode = d_backing_inode(root);
	u32 sid;
	int rc;

	/*
	 * Make sure that the xattr handler exists and that no
	 * error other than -ENODATA is returned by getxattr on
	 * the root directory.  -ENODATA is ok, as this may be
	 * the first boot of the SELinux kernel before we have
	 * assigned xattr values to the filesystem.
	 */
	if (!(root_inode->i_opflags & IOP_XATTR)) {
		pr_warn("SELinux: (dev %s, type %s) has no xattr support\n",
			sb->s_id, sb->s_type->name);
		goto fallback;
	}

	rc = __vfs_getxattr(root, root_inode, XATTR_NAME_SELINUX, NULL, 0);
	if (rc < 0 && rc != -ENODATA) {
		if (rc == -EOPNOTSUPP) {
			pr_warn("SELinux: (dev %s, type %s) has no security xattr handler\n",
				sb->s_id, sb->s_type->name);
			goto fallback;
		} else {
			pr_warn("SELinux: (dev %s, type %s) getxattr errno %d\n",
				sb->s_id, sb->s_type->name, -rc);
			return rc;
		}
	}
	return 0;

fallback:
	/* No xattr support - try to fallback to genfs if possible. */
	rc = security_genfs_sid(&selinux_state, sb->s_type->name, "/",
				SECCLASS_DIR, &sid);
	if (rc)
		return -EOPNOTSUPP;

	pr_warn("SELinux: (dev %s, type %s) falling back to genfs\n",
		sb->s_id, sb->s_type->name);
	sbsec->behavior = SECURITY_FS_USE_GENFS;
	sbsec->sid = sid;
	return 0;
}

static int sb_finish_set_opts(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = selinux_superblock(sb);
	struct dentry *root = sb->s_root;
	struct inode *root_inode = d_backing_inode(root);
	int rc = 0;

	if (sbsec->behavior == SECURITY_FS_USE_XATTR) {
		rc = sb_check_xattr_support(sb);
		if (rc)
			return rc;
	}

	sbsec->flags |= SE_SBINITIALIZED;

	/*
	 * Explicitly set or clear SBLABEL_MNT.  It's not sufficient to simply
	 * leave the flag untouched because sb_clone_mnt_opts might be handing
	 * us a superblock that needs the flag to be cleared.
	 */
	if (selinux_is_sblabel_mnt(sb))
		sbsec->flags |= SBLABEL_MNT;
	else
		sbsec->flags &= ~SBLABEL_MNT;

	/* Initialize the root inode. */
	rc = inode_doinit_with_dentry(root_inode, root);

	/* Initialize any other inodes associated with the superblock, e.g.
	   inodes created prior to initial policy load or inodes created
	   during get_sb by a pseudo filesystem that directly
	   populates itself. */
	spin_lock(&sbsec->isec_lock);
	while (!list_empty(&sbsec->isec_head)) {
		struct inode_security_struct *isec =
				list_first_entry(&sbsec->isec_head,
					   struct inode_security_struct, list);
		struct inode *inode = isec->inode;
		list_del_init(&isec->list);
		spin_unlock(&sbsec->isec_lock);
		inode = igrab(inode);
		if (inode) {
			if (!IS_PRIVATE(inode))
				inode_doinit_with_dentry(inode, NULL);
			iput(inode);
		}
		spin_lock(&sbsec->isec_lock);
	}
	spin_unlock(&sbsec->isec_lock);
	return rc;
}

static int bad_option(struct superblock_security_struct *sbsec, char flag,
		      u32 old_sid, u32 new_sid)
{
	char mnt_flags = sbsec->flags & SE_MNTMASK;

	/* check if the old mount command had the same options */
	if (sbsec->flags & SE_SBINITIALIZED)
		if (!(sbsec->flags & flag) ||
		    (old_sid != new_sid))
			return 1;

	/* check if we were passed the same options twice,
	 * aka someone passed context=a,context=b
	 */
	if (!(sbsec->flags & SE_SBINITIALIZED))
		if (mnt_flags & flag)
			return 1;
	return 0;
}

static int parse_sid(struct super_block *sb, const char *s, u32 *sid)
{
	int rc = security_context_str_to_sid(&selinux_state, s,
					     sid, GFP_KERNEL);
	if (rc)
		pr_warn("SELinux: security_context_str_to_sid"
		       "(%s) failed for (dev %s, type %s) errno=%d\n",
		       s, sb->s_id, sb->s_type->name, rc);
	return rc;
}

/*
 * Allow filesystems with binary mount data to explicitly set mount point
 * labeling information.
 */
static int selinux_set_mnt_opts(struct super_block *sb,
				void *mnt_opts,
				unsigned long kern_flags,
				unsigned long *set_kern_flags)
{
	const struct cred *cred = current_cred();
	struct superblock_security_struct *sbsec = selinux_superblock(sb);
	struct dentry *root = sb->s_root;
	struct selinux_mnt_opts *opts = mnt_opts;
	struct inode_security_struct *root_isec;
	u32 fscontext_sid = 0, context_sid = 0, rootcontext_sid = 0;
	u32 defcontext_sid = 0;
	int rc = 0;

	mutex_lock(&sbsec->lock);

	if (!selinux_initialized(&selinux_state)) {
		if (!opts) {
			/* Defer initialization until selinux_complete_init,
			   after the initial policy is loaded and the security
			   server is ready to handle calls. */
			goto out;
		}
		rc = -EINVAL;
		pr_warn("SELinux: Unable to set superblock options "
			"before the security server is initialized\n");
		goto out;
	}
	if (kern_flags && !set_kern_flags) {
		/* Specifying internal flags without providing a place to
		 * place the results is not allowed */
		rc = -EINVAL;
		goto out;
	}

	/*
	 * Binary mount data FS will come through this function twice.  Once
	 * from an explicit call and once from the generic calls from the vfs.
	 * Since the generic VFS calls will not contain any security mount data
	 * we need to skip the double mount verification.
	 *
	 * This does open a hole in which we will not notice if the first
	 * mount using this sb set explict options and a second mount using
	 * this sb does not set any security options.  (The first options
	 * will be used for both mounts)
	 */
	if ((sbsec->flags & SE_SBINITIALIZED) && (sb->s_type->fs_flags & FS_BINARY_MOUNTDATA)
	    && !opts)
		goto out;

	root_isec = backing_inode_security_novalidate(root);

	/*
	 * parse the mount options, check if they are valid sids.
	 * also check if someone is trying to mount the same sb more
	 * than once with different security options.
	 */
	if (opts) {
		if (opts->fscontext) {
			rc = parse_sid(sb, opts->fscontext, &fscontext_sid);
			if (rc)
				goto out;
			if (bad_option(sbsec, FSCONTEXT_MNT, sbsec->sid,
					fscontext_sid))
				goto out_double_mount;
			sbsec->flags |= FSCONTEXT_MNT;
		}
		if (opts->context) {
			rc = parse_sid(sb, opts->context, &context_sid);
			if (rc)
				goto out;
			if (bad_option(sbsec, CONTEXT_MNT, sbsec->mntpoint_sid,
					context_sid))
				goto out_double_mount;
			sbsec->flags |= CONTEXT_MNT;
		}
		if (opts->rootcontext) {
			rc = parse_sid(sb, opts->rootcontext, &rootcontext_sid);
			if (rc)
				goto out;
			if (bad_option(sbsec, ROOTCONTEXT_MNT, root_isec->sid,
					rootcontext_sid))
				goto out_double_mount;
			sbsec->flags |= ROOTCONTEXT_MNT;
		}
		if (opts->defcontext) {
			rc = parse_sid(sb, opts->defcontext, &defcontext_sid);
			if (rc)
				goto out;
			if (bad_option(sbsec, DEFCONTEXT_MNT, sbsec->def_sid,
					defcontext_sid))
				goto out_double_mount;
			sbsec->flags |= DEFCONTEXT_MNT;
		}
	}

	if (sbsec->flags & SE_SBINITIALIZED) {
		/* previously mounted with options, but not on this attempt? */
		if ((sbsec->flags & SE_MNTMASK) && !opts)
			goto out_double_mount;
		rc = 0;
		goto out;
	}

	if (strcmp(sb->s_type->name, "proc") == 0)
		sbsec->flags |= SE_SBPROC | SE_SBGENFS;

	if (!strcmp(sb->s_type->name, "debugfs") ||
	    !strcmp(sb->s_type->name, "tracefs") ||
	    !strcmp(sb->s_type->name, "binder") ||
	    !strcmp(sb->s_type->name, "bpf") ||
	    !strcmp(sb->s_type->name, "pstore"))
		sbsec->flags |= SE_SBGENFS;

	if (!strcmp(sb->s_type->name, "sysfs") ||
	    !strcmp(sb->s_type->name, "cgroup") ||
	    !strcmp(sb->s_type->name, "cgroup2"))
		sbsec->flags |= SE_SBGENFS | SE_SBGENFS_XATTR;

	if (!sbsec->behavior) {
		/*
		 * Determine the labeling behavior to use for this
		 * filesystem type.
		 */
		rc = security_fs_use(&selinux_state, sb);
		if (rc) {
			pr_warn("%s: security_fs_use(%s) returned %d\n",
					__func__, sb->s_type->name, rc);
			goto out;
		}
	}

	/*
	 * If this is a user namespace mount and the filesystem type is not
	 * explicitly whitelisted, then no contexts are allowed on the command
	 * line and security labels must be ignored.
	 */
	if (sb->s_user_ns != &init_user_ns &&
	    strcmp(sb->s_type->name, "tmpfs") &&
	    strcmp(sb->s_type->name, "ramfs") &&
	    strcmp(sb->s_type->name, "devpts") &&
	    strcmp(sb->s_type->name, "overlay")) {
		if (context_sid || fscontext_sid || rootcontext_sid ||
		    defcontext_sid) {
			rc = -EACCES;
			goto out;
		}
		if (sbsec->behavior == SECURITY_FS_USE_XATTR) {
			sbsec->behavior = SECURITY_FS_USE_MNTPOINT;
			rc = security_transition_sid(&selinux_state,
						     current_sid(),
						     current_sid(),
						     SECCLASS_FILE, NULL,
						     &sbsec->mntpoint_sid);
			if (rc)
				goto out;
		}
		goto out_set_opts;
	}

	/* sets the context of the superblock for the fs being mounted. */
	if (fscontext_sid) {
		rc = may_context_mount_sb_relabel(fscontext_sid, sbsec, cred);
		if (rc)
			goto out;

		sbsec->sid = fscontext_sid;
	}

	/*
	 * Switch to using mount point labeling behavior.
	 * sets the label used on all file below the mountpoint, and will set
	 * the superblock context if not already set.
	 */
	if (kern_flags & SECURITY_LSM_NATIVE_LABELS && !context_sid) {
		sbsec->behavior = SECURITY_FS_USE_NATIVE;
		*set_kern_flags |= SECURITY_LSM_NATIVE_LABELS;
	}

	if (context_sid) {
		if (!fscontext_sid) {
			rc = may_context_mount_sb_relabel(context_sid, sbsec,
							  cred);
			if (rc)
				goto out;
			sbsec->sid = context_sid;
		} else {
			rc = may_context_mount_inode_relabel(context_sid, sbsec,
							     cred);
			if (rc)
				goto out;
		}
		if (!rootcontext_sid)
			rootcontext_sid = context_sid;

		sbsec->mntpoint_sid = context_sid;
		sbsec->behavior = SECURITY_FS_USE_MNTPOINT;
	}

	if (rootcontext_sid) {
		rc = may_context_mount_inode_relabel(rootcontext_sid, sbsec,
						     cred);
		if (rc)
			goto out;

		root_isec->sid = rootcontext_sid;
		root_isec->initialized = LABEL_INITIALIZED;
	}

	if (defcontext_sid) {
		if (sbsec->behavior != SECURITY_FS_USE_XATTR &&
			sbsec->behavior != SECURITY_FS_USE_NATIVE) {
			rc = -EINVAL;
			pr_warn("SELinux: defcontext option is "
			       "invalid for this filesystem type\n");
			goto out;
		}

		if (defcontext_sid != sbsec->def_sid) {
			rc = may_context_mount_inode_relabel(defcontext_sid,
							     sbsec, cred);
			if (rc)
				goto out;
		}

		sbsec->def_sid = defcontext_sid;
	}

out_set_opts:
	rc = sb_finish_set_opts(sb);
out:
	mutex_unlock(&sbsec->lock);
	return rc;
out_double_mount:
	rc = -EINVAL;
	pr_warn("SELinux: mount invalid.  Same superblock, different "
	       "security settings for (dev %s, type %s)\n", sb->s_id,
	       sb->s_type->name);
	goto out;
}

static int selinux_cmp_sb_context(const struct super_block *oldsb,
				    const struct super_block *newsb)
{
	struct superblock_security_struct *old = selinux_superblock(oldsb);
	struct superblock_security_struct *new = selinux_superblock(newsb);
	char oldflags = old->flags & SE_MNTMASK;
	char newflags = new->flags & SE_MNTMASK;

	if (oldflags != newflags)
		goto mismatch;
	if ((oldflags & FSCONTEXT_MNT) && old->sid != new->sid)
		goto mismatch;
	if ((oldflags & CONTEXT_MNT) && old->mntpoint_sid != new->mntpoint_sid)
		goto mismatch;
	if ((oldflags & DEFCONTEXT_MNT) && old->def_sid != new->def_sid)
		goto mismatch;
	if (oldflags & ROOTCONTEXT_MNT) {
		struct inode_security_struct *oldroot = backing_inode_security(oldsb->s_root);
		struct inode_security_struct *newroot = backing_inode_security(newsb->s_root);
		if (oldroot->sid != newroot->sid)
			goto mismatch;
	}
	return 0;
mismatch:
	pr_warn("SELinux: mount invalid.  Same superblock, "
			    "different security settings for (dev %s, "
			    "type %s)\n", newsb->s_id, newsb->s_type->name);
	return -EBUSY;
}

static int selinux_sb_clone_mnt_opts(const struct super_block *oldsb,
					struct super_block *newsb,
					unsigned long kern_flags,
					unsigned long *set_kern_flags)
{
	int rc = 0;
	const struct superblock_security_struct *oldsbsec =
						selinux_superblock(oldsb);
	struct superblock_security_struct *newsbsec = selinux_superblock(newsb);

	int set_fscontext =	(oldsbsec->flags & FSCONTEXT_MNT);
	int set_context =	(oldsbsec->flags & CONTEXT_MNT);
	int set_rootcontext =	(oldsbsec->flags & ROOTCONTEXT_MNT);

	/*
	 * if the parent was able to be mounted it clearly had no special lsm
	 * mount options.  thus we can safely deal with this superblock later
	 */
	if (!selinux_initialized(&selinux_state))
		return 0;

	/*
	 * Specifying internal flags without providing a place to
	 * place the results is not allowed.
	 */
	if (kern_flags && !set_kern_flags)
		return -EINVAL;

	/* how can we clone if the old one wasn't set up?? */
	BUG_ON(!(oldsbsec->flags & SE_SBINITIALIZED));

	/* if fs is reusing a sb, make sure that the contexts match */
	if (newsbsec->flags & SE_SBINITIALIZED) {
		if ((kern_flags & SECURITY_LSM_NATIVE_LABELS) && !set_context)
			*set_kern_flags |= SECURITY_LSM_NATIVE_LABELS;
		return selinux_cmp_sb_context(oldsb, newsb);
	}

	mutex_lock(&newsbsec->lock);

	newsbsec->flags = oldsbsec->flags;

	newsbsec->sid = oldsbsec->sid;
	newsbsec->def_sid = oldsbsec->def_sid;
	newsbsec->behavior = oldsbsec->behavior;

	if (newsbsec->behavior == SECURITY_FS_USE_NATIVE &&
		!(kern_flags & SECURITY_LSM_NATIVE_LABELS) && !set_context) {
		rc = security_fs_use(&selinux_state, newsb);
		if (rc)
			goto out;
	}

	if (kern_flags & SECURITY_LSM_NATIVE_LABELS && !set_context) {
		newsbsec->behavior = SECURITY_FS_USE_NATIVE;
		*set_kern_flags |= SECURITY_LSM_NATIVE_LABELS;
	}

	if (set_context) {
		u32 sid = oldsbsec->mntpoint_sid;

		if (!set_fscontext)
			newsbsec->sid = sid;
		if (!set_rootcontext) {
			struct inode_security_struct *newisec = backing_inode_security(newsb->s_root);
			newisec->sid = sid;
		}
		newsbsec->mntpoint_sid = sid;
	}
	if (set_rootcontext) {
		const struct inode_security_struct *oldisec = backing_inode_security(oldsb->s_root);
		struct inode_security_struct *newisec = backing_inode_security(newsb->s_root);

		newisec->sid = oldisec->sid;
	}

	sb_finish_set_opts(newsb);
out:
	mutex_unlock(&newsbsec->lock);
	return rc;
}

static int selinux_add_opt(int token, const char *s, void **mnt_opts)
{
	struct selinux_mnt_opts *opts = *mnt_opts;

	if (token == Opt_seclabel)	/* eaten and completely ignored */
		return 0;

	if (!opts) {
		opts = kzalloc(sizeof(struct selinux_mnt_opts), GFP_KERNEL);
		if (!opts)
			return -ENOMEM;
		*mnt_opts = opts;
	}
	if (!s)
		return -ENOMEM;
	switch (token) {
	case Opt_context:
		if (opts->context || opts->defcontext)
			goto Einval;
		opts->context = s;
		break;
	case Opt_fscontext:
		if (opts->fscontext)
			goto Einval;
		opts->fscontext = s;
		break;
	case Opt_rootcontext:
		if (opts->rootcontext)
			goto Einval;
		opts->rootcontext = s;
		break;
	case Opt_defcontext:
		if (opts->context || opts->defcontext)
			goto Einval;
		opts->defcontext = s;
		break;
	}
	return 0;
Einval:
	pr_warn(SEL_MOUNT_FAIL_MSG);
	return -EINVAL;
}

static int selinux_add_mnt_opt(const char *option, const char *val, int len,
			       void **mnt_opts)
{
	int token = Opt_error;
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(tokens); i++) {
		if (strcmp(option, tokens[i].name) == 0) {
			token = tokens[i].opt;
			break;
		}
	}

	if (token == Opt_error)
		return -EINVAL;

	if (token != Opt_seclabel) {
		val = kmemdup_nul(val, len, GFP_KERNEL);
		if (!val) {
			rc = -ENOMEM;
			goto free_opt;
		}
	}
	rc = selinux_add_opt(token, val, mnt_opts);
	if (unlikely(rc)) {
		kfree(val);
		goto free_opt;
	}
	return rc;

free_opt:
	if (*mnt_opts) {
		selinux_free_mnt_opts(*mnt_opts);
		*mnt_opts = NULL;
	}
	return rc;
}

static int show_sid(struct seq_file *m, u32 sid)
{
	char *context = NULL;
	u32 len;
	int rc;

	rc = security_sid_to_context(&selinux_state, sid,
					     &context, &len);
	if (!rc) {
		bool has_comma = context && strchr(context, ',');

		seq_putc(m, '=');
		if (has_comma)
			seq_putc(m, '\"');
		seq_escape(m, context, "\"\n\\");
		if (has_comma)
			seq_putc(m, '\"');
	}
	kfree(context);
	return rc;
}

static int selinux_sb_show_options(struct seq_file *m, struct super_block *sb)
{
	struct superblock_security_struct *sbsec = selinux_superblock(sb);
	int rc;

	if (!(sbsec->flags & SE_SBINITIALIZED))
		return 0;

	if (!selinux_initialized(&selinux_state))
		return 0;

	if (sbsec->flags & FSCONTEXT_MNT) {
		seq_putc(m, ',');
		seq_puts(m, FSCONTEXT_STR);
		rc = show_sid(m, sbsec->sid);
		if (rc)
			return rc;
	}
	if (sbsec->flags & CONTEXT_MNT) {
		seq_putc(m, ',');
		seq_puts(m, CONTEXT_STR);
		rc = show_sid(m, sbsec->mntpoint_sid);
		if (rc)
			return rc;
	}
	if (sbsec->flags & DEFCONTEXT_MNT) {
		seq_putc(m, ',');
		seq_puts(m, DEFCONTEXT_STR);
		rc = show_sid(m, sbsec->def_sid);
		if (rc)
			return rc;
	}
	if (sbsec->flags & ROOTCONTEXT_MNT) {
		struct dentry *root = sb->s_root;
		struct inode_security_struct *isec = backing_inode_security(root);
		seq_putc(m, ',');
		seq_puts(m, ROOTCONTEXT_STR);
		rc = show_sid(m, isec->sid);
		if (rc)
			return rc;
	}
	if (sbsec->flags & SBLABEL_MNT) {
		seq_putc(m, ',');
		seq_puts(m, SECLABEL_STR);
	}
	return 0;
}

static inline u16 inode_mode_to_security_class(umode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFSOCK:
		return SECCLASS_SOCK_FILE;
	case S_IFLNK:
		return SECCLASS_LNK_FILE;
	case S_IFREG:
		return SECCLASS_FILE;
	case S_IFBLK:
		return SECCLASS_BLK_FILE;
	case S_IFDIR:
		return SECCLASS_DIR;
	case S_IFCHR:
		return SECCLASS_CHR_FILE;
	case S_IFIFO:
		return SECCLASS_FIFO_FILE;

	}

	return SECCLASS_FILE;
}

static inline int default_protocol_stream(int protocol)
{
	return (protocol == IPPROTO_IP || protocol == IPPROTO_TCP ||
		protocol == IPPROTO_MPTCP);
}

static inline int default_protocol_dgram(int protocol)
{
	return (protocol == IPPROTO_IP || protocol == IPPROTO_UDP);
}

static inline u16 socket_type_to_security_class(int family, int type, int protocol)
{
	int extsockclass = selinux_policycap_extsockclass();

	switch (family) {
	case PF_UNIX:
		switch (type) {
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			return SECCLASS_UNIX_STREAM_SOCKET;
		case SOCK_DGRAM:
		case SOCK_RAW:
			return SECCLASS_UNIX_DGRAM_SOCKET;
		}
		break;
	case PF_INET:
	case PF_INET6:
		switch (type) {
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			if (default_protocol_stream(protocol))
				return SECCLASS_TCP_SOCKET;
			else if (extsockclass && protocol == IPPROTO_SCTP)
				return SECCLASS_SCTP_SOCKET;
			else
				return SECCLASS_RAWIP_SOCKET;
		case SOCK_DGRAM:
			if (default_protocol_dgram(protocol))
				return SECCLASS_UDP_SOCKET;
			else if (extsockclass && (protocol == IPPROTO_ICMP ||
						  protocol == IPPROTO_ICMPV6))
				return SECCLASS_ICMP_SOCKET;
			else
				return SECCLASS_RAWIP_SOCKET;
		case SOCK_DCCP:
			return SECCLASS_DCCP_SOCKET;
		default:
			return SECCLASS_RAWIP_SOCKET;
		}
		break;
	case PF_NETLINK:
		switch (protocol) {
		case NETLINK_ROUTE:
			return SECCLASS_NETLINK_ROUTE_SOCKET;
		case NETLINK_SOCK_DIAG:
			return SECCLASS_NETLINK_TCPDIAG_SOCKET;
		case NETLINK_NFLOG:
			return SECCLASS_NETLINK_NFLOG_SOCKET;
		case NETLINK_XFRM:
			return SECCLASS_NETLINK_XFRM_SOCKET;
		case NETLINK_SELINUX:
			return SECCLASS_NETLINK_SELINUX_SOCKET;
		case NETLINK_ISCSI:
			return SECCLASS_NETLINK_ISCSI_SOCKET;
		case NETLINK_AUDIT:
			return SECCLASS_NETLINK_AUDIT_SOCKET;
		case NETLINK_FIB_LOOKUP:
			return SECCLASS_NETLINK_FIB_LOOKUP_SOCKET;
		case NETLINK_CONNECTOR:
			return SECCLASS_NETLINK_CONNECTOR_SOCKET;
		case NETLINK_NETFILTER:
			return SECCLASS_NETLINK_NETFILTER_SOCKET;
		case NETLINK_DNRTMSG:
			return SECCLASS_NETLINK_DNRT_SOCKET;
		case NETLINK_KOBJECT_UEVENT:
			return SECCLASS_NETLINK_KOBJECT_UEVENT_SOCKET;
		case NETLINK_GENERIC:
			return SECCLASS_NETLINK_GENERIC_SOCKET;
		case NETLINK_SCSITRANSPORT:
			return SECCLASS_NETLINK_SCSITRANSPORT_SOCKET;
		case NETLINK_RDMA:
			return SECCLASS_NETLINK_RDMA_SOCKET;
		case NETLINK_CRYPTO:
			return SECCLASS_NETLINK_CRYPTO_SOCKET;
		default:
			return SECCLASS_NETLINK_SOCKET;
		}
	case PF_PACKET:
		return SECCLASS_PACKET_SOCKET;
	case PF_KEY:
		return SECCLASS_KEY_SOCKET;
	case PF_APPLETALK:
		return SECCLASS_APPLETALK_SOCKET;
	}

	if (extsockclass) {
		switch (family) {
		case PF_AX25:
			return SECCLASS_AX25_SOCKET;
		case PF_IPX:
			return SECCLASS_IPX_SOCKET;
		case PF_NETROM:
			return SECCLASS_NETROM_SOCKET;
		case PF_ATMPVC:
			return SECCLASS_ATMPVC_SOCKET;
		case PF_X25:
			return SECCLASS_X25_SOCKET;
		case PF_ROSE:
			return SECCLASS_ROSE_SOCKET;
		case PF_DECnet:
			return SECCLASS_DECNET_SOCKET;
		case PF_ATMSVC:
			return SECCLASS_ATMSVC_SOCKET;
		case PF_RDS:
			return SECCLASS_RDS_SOCKET;
		case PF_IRDA:
			return SECCLASS_IRDA_SOCKET;
		case PF_PPPOX:
			return SECCLASS_PPPOX_SOCKET;
		case PF_LLC:
			return SECCLASS_LLC_SOCKET;
		case PF_CAN:
			return SECCLASS_CAN_SOCKET;
		case PF_TIPC:
			return SECCLASS_TIPC_SOCKET;
		case PF_BLUETOOTH:
			return SECCLASS_BLUETOOTH_SOCKET;
		case PF_IUCV:
			return SECCLASS_IUCV_SOCKET;
		case PF_RXRPC:
			return SECCLASS_RXRPC_SOCKET;
		case PF_ISDN:
			return SECCLASS_ISDN_SOCKET;
		case PF_PHONET:
			return SECCLASS_PHONET_SOCKET;
		case PF_IEEE802154:
			return SECCLASS_IEEE802154_SOCKET;
		case PF_CAIF:
			return SECCLASS_CAIF_SOCKET;
		case PF_ALG:
			return SECCLASS_ALG_SOCKET;
		case PF_NFC:
			return SECCLASS_NFC_SOCKET;
		case PF_VSOCK:
			return SECCLASS_VSOCK_SOCKET;
		case PF_KCM:
			return SECCLASS_KCM_SOCKET;
		case PF_QIPCRTR:
			return SECCLASS_QIPCRTR_SOCKET;
		case PF_SMC:
			return SECCLASS_SMC_SOCKET;
		case PF_XDP:
			return SECCLASS_XDP_SOCKET;
		case PF_MCTP:
			return SECCLASS_MCTP_SOCKET;
#if PF_MAX > 46
#error New address family defined, please update this function.
#endif
		}
	}

	return SECCLASS_SOCKET;
}

static int selinux_genfs_get_sid(struct dentry *dentry,
				 u16 tclass,
				 u16 flags,
				 u32 *sid)
{
	int rc;
	struct super_block *sb = dentry->d_sb;
	char *buffer, *path;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	path = dentry_path_raw(dentry, buffer, PAGE_SIZE);
	if (IS_ERR(path))
		rc = PTR_ERR(path);
	else {
		if (flags & SE_SBPROC) {
			/* each process gets a /proc/PID/ entry. Strip off the
			 * PID part to get a valid selinux labeling.
			 * e.g. /proc/1/net/rpc/nfs -> /net/rpc/nfs */
			while (path[1] >= '0' && path[1] <= '9') {
				path[1] = '/';
				path++;
			}
		}
		rc = security_genfs_sid(&selinux_state, sb->s_type->name,
					path, tclass, sid);
		if (rc == -ENOENT) {
			/* No match in policy, mark as unlabeled. */
			*sid = SECINITSID_UNLABELED;
			rc = 0;
		}
	}
	free_page((unsigned long)buffer);
	return rc;
}

static int inode_doinit_use_xattr(struct inode *inode, struct dentry *dentry,
				  u32 def_sid, u32 *sid)
{
#define INITCONTEXTLEN 255
	char *context;
	unsigned int len;
	int rc;

	len = INITCONTEXTLEN;
	context = kmalloc(len + 1, GFP_NOFS);
	if (!context)
		return -ENOMEM;

	context[len] = '\0';
	rc = __vfs_getxattr(dentry, inode, XATTR_NAME_SELINUX, context, len);
	if (rc == -ERANGE) {
		kfree(context);

		/* Need a larger buffer.  Query for the right size. */
		rc = __vfs_getxattr(dentry, inode, XATTR_NAME_SELINUX, NULL, 0);
		if (rc < 0)
			return rc;

		len = rc;
		context = kmalloc(len + 1, GFP_NOFS);
		if (!context)
			return -ENOMEM;

		context[len] = '\0';
		rc = __vfs_getxattr(dentry, inode, XATTR_NAME_SELINUX,
				    context, len);
	}
	if (rc < 0) {
		kfree(context);
		if (rc != -ENODATA) {
			pr_warn("SELinux: %s:  getxattr returned %d for dev=%s ino=%ld\n",
				__func__, -rc, inode->i_sb->s_id, inode->i_ino);
			return rc;
		}
		*sid = def_sid;
		return 0;
	}

	rc = security_context_to_sid_default(&selinux_state, context, rc, sid,
					     def_sid, GFP_NOFS);
	if (rc) {
		char *dev = inode->i_sb->s_id;
		unsigned long ino = inode->i_ino;

		if (rc == -EINVAL) {
			pr_notice_ratelimited("SELinux: inode=%lu on dev=%s was found to have an invalid context=%s.  This indicates you may need to relabel the inode or the filesystem in question.\n",
					      ino, dev, context);
		} else {
			pr_warn("SELinux: %s:  context_to_sid(%s) returned %d for dev=%s ino=%ld\n",
				__func__, context, -rc, dev, ino);
		}
	}
	kfree(context);
	return 0;
}

/* The inode's security attributes must be initialized before first use. */
static int inode_doinit_with_dentry(struct inode *inode, struct dentry *opt_dentry)
{
	struct superblock_security_struct *sbsec = NULL;
	struct inode_security_struct *isec = selinux_inode(inode);
	u32 task_sid, sid = 0;
	u16 sclass;
	struct dentry *dentry;
	int rc = 0;

	if (isec->initialized == LABEL_INITIALIZED)
		return 0;

	spin_lock(&isec->lock);
	if (isec->initialized == LABEL_INITIALIZED)
		goto out_unlock;

	if (isec->sclass == SECCLASS_FILE)
		isec->sclass = inode_mode_to_security_class(inode->i_mode);

	sbsec = selinux_superblock(inode->i_sb);
	if (!(sbsec->flags & SE_SBINITIALIZED)) {
		/* Defer initialization until selinux_complete_init,
		   after the initial policy is loaded and the security
		   server is ready to handle calls. */
		spin_lock(&sbsec->isec_lock);
		if (list_empty(&isec->list))
			list_add(&isec->list, &sbsec->isec_head);
		spin_unlock(&sbsec->isec_lock);
		goto out_unlock;
	}

	sclass = isec->sclass;
	task_sid = isec->task_sid;
	sid = isec->sid;
	isec->initialized = LABEL_PENDING;
	spin_unlock(&isec->lock);

	switch (sbsec->behavior) {
	case SECURITY_FS_USE_NATIVE:
		break;
	case SECURITY_FS_USE_XATTR:
		if (!(inode->i_opflags & IOP_XATTR)) {
			sid = sbsec->def_sid;
			break;
		}
		/* Need a dentry, since the xattr API requires one.
		   Life would be simpler if we could just pass the inode. */
		if (opt_dentry) {
			/* Called from d_instantiate or d_splice_alias. */
			dentry = dget(opt_dentry);
		} else {
			/*
			 * Called from selinux_complete_init, try to find a dentry.
			 * Some filesystems really want a connected one, so try
			 * that first.  We could split SECURITY_FS_USE_XATTR in
			 * two, depending upon that...
			 */
			dentry = d_find_alias(inode);
			if (!dentry)
				dentry = d_find_any_alias(inode);
		}
		if (!dentry) {
			/*
			 * this is can be hit on boot when a file is accessed
			 * before the policy is loaded.  When we load policy we
			 * may find inodes that have no dentry on the
			 * sbsec->isec_head list.  No reason to complain as these
			 * will get fixed up the next time we go through
			 * inode_doinit with a dentry, before these inodes could
			 * be used again by userspace.
			 */
			goto out_invalid;
		}

		rc = inode_doinit_use_xattr(inode, dentry, sbsec->def_sid,
					    &sid);
		dput(dentry);
		if (rc)
			goto out;
		break;
	case SECURITY_FS_USE_TASK:
		sid = task_sid;
		break;
	case SECURITY_FS_USE_TRANS:
		/* Default to the fs SID. */
		sid = sbsec->sid;

		/* Try to obtain a transition SID. */
		rc = security_transition_sid(&selinux_state, task_sid, sid,
					     sclass, NULL, &sid);
		if (rc)
			goto out;
		break;
	case SECURITY_FS_USE_MNTPOINT:
		sid = sbsec->mntpoint_sid;
		break;
	default:
		/* Default to the fs superblock SID. */
		sid = sbsec->sid;

		if ((sbsec->flags & SE_SBGENFS) &&
		     (!S_ISLNK(inode->i_mode) ||
		      selinux_policycap_genfs_seclabel_symlinks())) {
			/* We must have a dentry to determine the label on
			 * procfs inodes */
			if (opt_dentry) {
				/* Called from d_instantiate or
				 * d_splice_alias. */
				dentry = dget(opt_dentry);
			} else {
				/* Called from selinux_complete_init, try to
				 * find a dentry.  Some filesystems really want
				 * a connected one, so try that first.
				 */
				dentry = d_find_alias(inode);
				if (!dentry)
					dentry = d_find_any_alias(inode);
			}
			/*
			 * This can be hit on boot when a file is accessed
			 * before the policy is loaded.  When we load policy we
			 * may find inodes that have no dentry on the
			 * sbsec->isec_head list.  No reason to complain as
			 * these will get fixed up the next time we go through
			 * inode_doinit() with a dentry, before these inodes
			 * could be used again by userspace.
			 */
			if (!dentry)
				goto out_invalid;
			rc = selinux_genfs_get_sid(dentry, sclass,
						   sbsec->flags, &sid);
			if (rc) {
				dput(dentry);
				goto out;
			}

			if ((sbsec->flags & SE_SBGENFS_XATTR) &&
			    (inode->i_opflags & IOP_XATTR)) {
				rc = inode_doinit_use_xattr(inode, dentry,
							    sid, &sid);
				if (rc) {
					dput(dentry);
					goto out;
				}
			}
			dput(dentry);
		}
		break;
	}

out:
	spin_lock(&isec->lock);
	if (isec->initialized == LABEL_PENDING) {
		if (rc) {
			isec->initialized = LABEL_INVALID;
			goto out_unlock;
		}
		isec->initialized = LABEL_INITIALIZED;
		isec->sid = sid;
	}

out_unlock:
	spin_unlock(&isec->lock);
	return rc;

out_invalid:
	spin_lock(&isec->lock);
	if (isec->initialized == LABEL_PENDING) {
		isec->initialized = LABEL_INVALID;
		isec->sid = sid;
	}
	spin_unlock(&isec->lock);
	return 0;
}

/* Convert a Linux signal to an access vector. */
static inline u32 signal_to_av(int sig)
{
	u32 perm = 0;

	switch (sig) {
	case SIGCHLD:
		/* Commonly granted from child to parent. */
		perm = PROCESS__SIGCHLD;
		break;
	case SIGKILL:
		/* Cannot be caught or ignored */
		perm = PROCESS__SIGKILL;
		break;
	case SIGSTOP:
		/* Cannot be caught or ignored */
		perm = PROCESS__SIGSTOP;
		break;
	default:
		/* All other signals. */
		perm = PROCESS__SIGNAL;
		break;
	}

	return perm;
}

#if CAP_LAST_CAP > 63
#error Fix SELinux to handle capabilities > 63.
#endif

/* Check whether a task is allowed to use a capability. */
static int cred_has_capability(const struct cred *cred,
			       int cap, unsigned int opts, bool initns)
{
	struct common_audit_data ad;
	struct av_decision avd;
	u16 sclass;
	u32 sid = cred_sid(cred);
	u32 av = CAP_TO_MASK(cap);
	int rc;

	ad.type = LSM_AUDIT_DATA_CAP;
	ad.u.cap = cap;

	switch (CAP_TO_INDEX(cap)) {
	case 0:
		sclass = initns ? SECCLASS_CAPABILITY : SECCLASS_CAP_USERNS;
		break;
	case 1:
		sclass = initns ? SECCLASS_CAPABILITY2 : SECCLASS_CAP2_USERNS;
		break;
	default:
		pr_err("SELinux:  out of range capability %d\n", cap);
		BUG();
		return -EINVAL;
	}

	rc = avc_has_perm_noaudit(&selinux_state,
				  sid, sid, sclass, av, 0, &avd);
	if (!(opts & CAP_OPT_NOAUDIT)) {
		int rc2 = avc_audit(&selinux_state,
				    sid, sid, sclass, av, &avd, rc, &ad);
		if (rc2)
			return rc2;
	}
	return rc;
}

/* Check whether a task has a particular permission to an inode.
   The 'adp' parameter is optional and allows other audit
   data to be passed (e.g. the dentry). */
static int inode_has_perm(const struct cred *cred,
			  struct inode *inode,
			  u32 perms,
			  struct common_audit_data *adp)
{
	struct inode_security_struct *isec;
	u32 sid;

	validate_creds(cred);

	if (unlikely(IS_PRIVATE(inode)))
		return 0;

	sid = cred_sid(cred);
	isec = selinux_inode(inode);

	return avc_has_perm(&selinux_state,
			    sid, isec->sid, isec->sclass, perms, adp);
}

/* Same as inode_has_perm, but pass explicit audit data containing
   the dentry to help the auditing code to more easily generate the
   pathname if needed. */
static inline int dentry_has_perm(const struct cred *cred,
				  struct dentry *dentry,
				  u32 av)
{
	struct inode *inode = d_backing_inode(dentry);
	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = dentry;
	__inode_security_revalidate(inode, dentry, true);
	return inode_has_perm(cred, inode, av, &ad);
}

/* Same as inode_has_perm, but pass explicit audit data containing
   the path to help the auditing code to more easily generate the
   pathname if needed. */
static inline int path_has_perm(const struct cred *cred,
				const struct path *path,
				u32 av)
{
	struct inode *inode = d_backing_inode(path->dentry);
	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_PATH;
	ad.u.path = *path;
	__inode_security_revalidate(inode, path->dentry, true);
	return inode_has_perm(cred, inode, av, &ad);
}

/* Same as path_has_perm, but uses the inode from the file struct. */
static inline int file_path_has_perm(const struct cred *cred,
				     struct file *file,
				     u32 av)
{
	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_FILE;
	ad.u.file = file;
	return inode_has_perm(cred, file_inode(file), av, &ad);
}

#ifdef CONFIG_BPF_SYSCALL
static int bpf_fd_pass(struct file *file, u32 sid);
#endif

/* Check whether a task can use an open file descriptor to
   access an inode in a given way.  Check access to the
   descriptor itself, and then use dentry_has_perm to
   check a particular permission to the file.
   Access to the descriptor is implicitly granted if it
   has the same SID as the process.  If av is zero, then
   access to the file is not checked, e.g. for cases
   where only the descriptor is affected like seek. */
static int file_has_perm(const struct cred *cred,
			 struct file *file,
			 u32 av)
{
	struct file_security_struct *fsec = selinux_file(file);
	struct inode *inode = file_inode(file);
	struct common_audit_data ad;
	u32 sid = cred_sid(cred);
	int rc;

	ad.type = LSM_AUDIT_DATA_FILE;
	ad.u.file = file;

	if (sid != fsec->sid) {
		rc = avc_has_perm(&selinux_state,
				  sid, fsec->sid,
				  SECCLASS_FD,
				  FD__USE,
				  &ad);
		if (rc)
			goto out;
	}

#ifdef CONFIG_BPF_SYSCALL
	rc = bpf_fd_pass(file, cred_sid(cred));
	if (rc)
		return rc;
#endif

	/* av is zero if only checking access to the descriptor. */
	rc = 0;
	if (av)
		rc = inode_has_perm(cred, inode, av, &ad);

out:
	return rc;
}

/*
 * Determine the label for an inode that might be unioned.
 */
static int
selinux_determine_inode_label(const struct task_security_struct *tsec,
				 struct inode *dir,
				 const struct qstr *name, u16 tclass,
				 u32 *_new_isid)
{
	const struct superblock_security_struct *sbsec =
						selinux_superblock(dir->i_sb);

	if ((sbsec->flags & SE_SBINITIALIZED) &&
	    (sbsec->behavior == SECURITY_FS_USE_MNTPOINT)) {
		*_new_isid = sbsec->mntpoint_sid;
	} else if ((sbsec->flags & SBLABEL_MNT) &&
		   tsec->create_sid) {
		*_new_isid = tsec->create_sid;
	} else {
		const struct inode_security_struct *dsec = inode_security(dir);
		return security_transition_sid(&selinux_state, tsec->sid,
					       dsec->sid, tclass,
					       name, _new_isid);
	}

	return 0;
}

/* Check whether a task can create a file. */
static int may_create(struct inode *dir,
		      struct dentry *dentry,
		      u16 tclass)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	struct inode_security_struct *dsec;
	struct superblock_security_struct *sbsec;
	u32 sid, newsid;
	struct common_audit_data ad;
	int rc;

	dsec = inode_security(dir);
	sbsec = selinux_superblock(dir->i_sb);

	sid = tsec->sid;

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = dentry;

	rc = avc_has_perm(&selinux_state,
			  sid, dsec->sid, SECCLASS_DIR,
			  DIR__ADD_NAME | DIR__SEARCH,
			  &ad);
	if (rc)
		return rc;

	rc = selinux_determine_inode_label(tsec, dir, &dentry->d_name, tclass,
					   &newsid);
	if (rc)
		return rc;

	rc = avc_has_perm(&selinux_state,
			  sid, newsid, tclass, FILE__CREATE, &ad);
	if (rc)
		return rc;

	return avc_has_perm(&selinux_state,
			    newsid, sbsec->sid,
			    SECCLASS_FILESYSTEM,
			    FILESYSTEM__ASSOCIATE, &ad);
}

#define MAY_LINK	0
#define MAY_UNLINK	1
#define MAY_RMDIR	2

/* Check whether a task can link, unlink, or rmdir a file/directory. */
static int may_link(struct inode *dir,
		    struct dentry *dentry,
		    int kind)

{
	struct inode_security_struct *dsec, *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();
	u32 av;
	int rc;

	dsec = inode_security(dir);
	isec = backing_inode_security(dentry);

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = dentry;

	av = DIR__SEARCH;
	av |= (kind ? DIR__REMOVE_NAME : DIR__ADD_NAME);
	rc = avc_has_perm(&selinux_state,
			  sid, dsec->sid, SECCLASS_DIR, av, &ad);
	if (rc)
		return rc;

	switch (kind) {
	case MAY_LINK:
		av = FILE__LINK;
		break;
	case MAY_UNLINK:
		av = FILE__UNLINK;
		break;
	case MAY_RMDIR:
		av = DIR__RMDIR;
		break;
	default:
		pr_warn("SELinux: %s:  unrecognized kind %d\n",
			__func__, kind);
		return 0;
	}

	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid, isec->sclass, av, &ad);
	return rc;
}

static inline int may_rename(struct inode *old_dir,
			     struct dentry *old_dentry,
			     struct inode *new_dir,
			     struct dentry *new_dentry)
{
	struct inode_security_struct *old_dsec, *new_dsec, *old_isec, *new_isec;
	struct common_audit_data ad;
	u32 sid = current_sid();
	u32 av;
	int old_is_dir, new_is_dir;
	int rc;

	old_dsec = inode_security(old_dir);
	old_isec = backing_inode_security(old_dentry);
	old_is_dir = d_is_dir(old_dentry);
	new_dsec = inode_security(new_dir);

	ad.type = LSM_AUDIT_DATA_DENTRY;

	ad.u.dentry = old_dentry;
	rc = avc_has_perm(&selinux_state,
			  sid, old_dsec->sid, SECCLASS_DIR,
			  DIR__REMOVE_NAME | DIR__SEARCH, &ad);
	if (rc)
		return rc;
	rc = avc_has_perm(&selinux_state,
			  sid, old_isec->sid,
			  old_isec->sclass, FILE__RENAME, &ad);
	if (rc)
		return rc;
	if (old_is_dir && new_dir != old_dir) {
		rc = avc_has_perm(&selinux_state,
				  sid, old_isec->sid,
				  old_isec->sclass, DIR__REPARENT, &ad);
		if (rc)
			return rc;
	}

	ad.u.dentry = new_dentry;
	av = DIR__ADD_NAME | DIR__SEARCH;
	if (d_is_positive(new_dentry))
		av |= DIR__REMOVE_NAME;
	rc = avc_has_perm(&selinux_state,
			  sid, new_dsec->sid, SECCLASS_DIR, av, &ad);
	if (rc)
		return rc;
	if (d_is_positive(new_dentry)) {
		new_isec = backing_inode_security(new_dentry);
		new_is_dir = d_is_dir(new_dentry);
		rc = avc_has_perm(&selinux_state,
				  sid, new_isec->sid,
				  new_isec->sclass,
				  (new_is_dir ? DIR__RMDIR : FILE__UNLINK), &ad);
		if (rc)
			return rc;
	}

	return 0;
}

/* Check whether a task can perform a filesystem operation. */
static int superblock_has_perm(const struct cred *cred,
			       struct super_block *sb,
			       u32 perms,
			       struct common_audit_data *ad)
{
	struct superblock_security_struct *sbsec;
	u32 sid = cred_sid(cred);

	sbsec = selinux_superblock(sb);
	return avc_has_perm(&selinux_state,
			    sid, sbsec->sid, SECCLASS_FILESYSTEM, perms, ad);
}

/* Convert a Linux mode and permission mask to an access vector. */
static inline u32 file_mask_to_av(int mode, int mask)
{
	u32 av = 0;

	if (!S_ISDIR(mode)) {
		if (mask & MAY_EXEC)
			av |= FILE__EXECUTE;
		if (mask & MAY_READ)
			av |= FILE__READ;

		if (mask & MAY_APPEND)
			av |= FILE__APPEND;
		else if (mask & MAY_WRITE)
			av |= FILE__WRITE;

	} else {
		if (mask & MAY_EXEC)
			av |= DIR__SEARCH;
		if (mask & MAY_WRITE)
			av |= DIR__WRITE;
		if (mask & MAY_READ)
			av |= DIR__READ;
	}

	return av;
}

/* Convert a Linux file to an access vector. */
static inline u32 file_to_av(struct file *file)
{
	u32 av = 0;

	if (file->f_mode & FMODE_READ)
		av |= FILE__READ;
	if (file->f_mode & FMODE_WRITE) {
		if (file->f_flags & O_APPEND)
			av |= FILE__APPEND;
		else
			av |= FILE__WRITE;
	}
	if (!av) {
		/*
		 * Special file opened with flags 3 for ioctl-only use.
		 */
		av = FILE__IOCTL;
	}

	return av;
}

/*
 * Convert a file to an access vector and include the correct
 * open permission.
 */
static inline u32 open_file_to_av(struct file *file)
{
	u32 av = file_to_av(file);
	struct inode *inode = file_inode(file);

	if (selinux_policycap_openperm() &&
	    inode->i_sb->s_magic != SOCKFS_MAGIC)
		av |= FILE__OPEN;

	return av;
}

/* Hook functions begin here. */

static int selinux_binder_set_context_mgr(struct task_struct *mgr)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_binder(mgr), SECCLASS_BINDER,
			    BINDER__SET_CONTEXT_MGR, NULL);
}

static int selinux_binder_transaction(struct task_struct *from,
				      struct task_struct *to)
{
	u32 mysid = current_sid();
	u32 fromsid = task_sid_binder(from);
	int rc;

	if (mysid != fromsid) {
		rc = avc_has_perm(&selinux_state,
				  mysid, fromsid, SECCLASS_BINDER,
				  BINDER__IMPERSONATE, NULL);
		if (rc)
			return rc;
	}

	return avc_has_perm(&selinux_state, fromsid, task_sid_binder(to),
			    SECCLASS_BINDER, BINDER__CALL, NULL);
}

static int selinux_binder_transfer_binder(struct task_struct *from,
					  struct task_struct *to)
{
	return avc_has_perm(&selinux_state,
			    task_sid_binder(from), task_sid_binder(to),
			    SECCLASS_BINDER, BINDER__TRANSFER,
			    NULL);
}

static int selinux_binder_transfer_file(struct task_struct *from,
					struct task_struct *to,
					struct file *file)
{
	u32 sid = task_sid_binder(to);
	struct file_security_struct *fsec = selinux_file(file);
	struct dentry *dentry = file->f_path.dentry;
	struct inode_security_struct *isec;
	struct common_audit_data ad;
	int rc;

	ad.type = LSM_AUDIT_DATA_PATH;
	ad.u.path = file->f_path;

	if (sid != fsec->sid) {
		rc = avc_has_perm(&selinux_state,
				  sid, fsec->sid,
				  SECCLASS_FD,
				  FD__USE,
				  &ad);
		if (rc)
			return rc;
	}

#ifdef CONFIG_BPF_SYSCALL
	rc = bpf_fd_pass(file, sid);
	if (rc)
		return rc;
#endif

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;

	isec = backing_inode_security(dentry);
	return avc_has_perm(&selinux_state,
			    sid, isec->sid, isec->sclass, file_to_av(file),
			    &ad);
}

static int selinux_ptrace_access_check(struct task_struct *child,
				       unsigned int mode)
{
	u32 sid = current_sid();
	u32 csid = task_sid_obj(child);

	if (mode & PTRACE_MODE_READ)
		return avc_has_perm(&selinux_state,
				    sid, csid, SECCLASS_FILE, FILE__READ, NULL);

	return avc_has_perm(&selinux_state,
			    sid, csid, SECCLASS_PROCESS, PROCESS__PTRACE, NULL);
}

static int selinux_ptrace_traceme(struct task_struct *parent)
{
	return avc_has_perm(&selinux_state,
			    task_sid_obj(parent), task_sid_obj(current),
			    SECCLASS_PROCESS, PROCESS__PTRACE, NULL);
}

static int selinux_capget(struct task_struct *target, kernel_cap_t *effective,
			  kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(target), SECCLASS_PROCESS,
			    PROCESS__GETCAP, NULL);
}

static int selinux_capset(struct cred *new, const struct cred *old,
			  const kernel_cap_t *effective,
			  const kernel_cap_t *inheritable,
			  const kernel_cap_t *permitted)
{
	return avc_has_perm(&selinux_state,
			    cred_sid(old), cred_sid(new), SECCLASS_PROCESS,
			    PROCESS__SETCAP, NULL);
}

/*
 * (This comment used to live with the selinux_task_setuid hook,
 * which was removed).
 *
 * Since setuid only affects the current process, and since the SELinux
 * controls are not based on the Linux identity attributes, SELinux does not
 * need to control this operation.  However, SELinux does control the use of
 * the CAP_SETUID and CAP_SETGID capabilities using the capable hook.
 */

static int selinux_capable(const struct cred *cred, struct user_namespace *ns,
			   int cap, unsigned int opts)
{
	return cred_has_capability(cred, cap, opts, ns == &init_user_ns);
}

static int selinux_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	const struct cred *cred = current_cred();
	int rc = 0;

	if (!sb)
		return 0;

	switch (cmds) {
	case Q_SYNC:
	case Q_QUOTAON:
	case Q_QUOTAOFF:
	case Q_SETINFO:
	case Q_SETQUOTA:
	case Q_XQUOTAOFF:
	case Q_XQUOTAON:
	case Q_XSETQLIM:
		rc = superblock_has_perm(cred, sb, FILESYSTEM__QUOTAMOD, NULL);
		break;
	case Q_GETFMT:
	case Q_GETINFO:
	case Q_GETQUOTA:
	case Q_XGETQUOTA:
	case Q_XGETQSTAT:
	case Q_XGETQSTATV:
	case Q_XGETNEXTQUOTA:
		rc = superblock_has_perm(cred, sb, FILESYSTEM__QUOTAGET, NULL);
		break;
	default:
		rc = 0;  /* let the kernel handle invalid cmds */
		break;
	}
	return rc;
}

static int selinux_quota_on(struct dentry *dentry)
{
	const struct cred *cred = current_cred();

	return dentry_has_perm(cred, dentry, FILE__QUOTAON);
}

static int selinux_syslog(int type)
{
	switch (type) {
	case SYSLOG_ACTION_READ_ALL:	/* Read last kernel messages */
	case SYSLOG_ACTION_SIZE_BUFFER:	/* Return size of the log buffer */
		return avc_has_perm(&selinux_state,
				    current_sid(), SECINITSID_KERNEL,
				    SECCLASS_SYSTEM, SYSTEM__SYSLOG_READ, NULL);
	case SYSLOG_ACTION_CONSOLE_OFF:	/* Disable logging to console */
	case SYSLOG_ACTION_CONSOLE_ON:	/* Enable logging to console */
	/* Set level of messages printed to console */
	case SYSLOG_ACTION_CONSOLE_LEVEL:
		return avc_has_perm(&selinux_state,
				    current_sid(), SECINITSID_KERNEL,
				    SECCLASS_SYSTEM, SYSTEM__SYSLOG_CONSOLE,
				    NULL);
	}
	/* All other syslog types */
	return avc_has_perm(&selinux_state,
			    current_sid(), SECINITSID_KERNEL,
			    SECCLASS_SYSTEM, SYSTEM__SYSLOG_MOD, NULL);
}

/*
 * Check that a process has enough memory to allocate a new virtual
 * mapping. 0 means there is enough memory for the allocation to
 * succeed and -ENOMEM implies there is not.
 *
 * Do not audit the selinux permission check, as this is applied to all
 * processes that allocate mappings.
 */
static int selinux_vm_enough_memory(struct mm_struct *mm, long pages)
{
	int rc, cap_sys_admin = 0;

	rc = cred_has_capability(current_cred(), CAP_SYS_ADMIN,
				 CAP_OPT_NOAUDIT, true);
	if (rc == 0)
		cap_sys_admin = 1;

	return cap_sys_admin;
}

/* binprm security operations */

static u32 ptrace_parent_sid(void)
{
	u32 sid = 0;
	struct task_struct *tracer;

	rcu_read_lock();
	tracer = ptrace_parent(current);
	if (tracer)
		sid = task_sid_obj(tracer);
	rcu_read_unlock();

	return sid;
}

static int check_nnp_nosuid(const struct linux_binprm *bprm,
			    const struct task_security_struct *old_tsec,
			    const struct task_security_struct *new_tsec)
{
	int nnp = (bprm->unsafe & LSM_UNSAFE_NO_NEW_PRIVS);
	int nosuid = !mnt_may_suid(bprm->file->f_path.mnt);
	int rc;
	u32 av;

	if (!nnp && !nosuid)
		return 0; /* neither NNP nor nosuid */

	if (new_tsec->sid == old_tsec->sid)
		return 0; /* No change in credentials */

	/*
	 * If the policy enables the nnp_nosuid_transition policy capability,
	 * then we permit transitions under NNP or nosuid if the
	 * policy allows the corresponding permission between
	 * the old and new contexts.
	 */
	if (selinux_policycap_nnp_nosuid_transition()) {
		av = 0;
		if (nnp)
			av |= PROCESS2__NNP_TRANSITION;
		if (nosuid)
			av |= PROCESS2__NOSUID_TRANSITION;
		rc = avc_has_perm(&selinux_state,
				  old_tsec->sid, new_tsec->sid,
				  SECCLASS_PROCESS2, av, NULL);
		if (!rc)
			return 0;
	}

	/*
	 * We also permit NNP or nosuid transitions to bounded SIDs,
	 * i.e. SIDs that are guaranteed to only be allowed a subset
	 * of the permissions of the current SID.
	 */
	rc = security_bounded_transition(&selinux_state, old_tsec->sid,
					 new_tsec->sid);
	if (!rc)
		return 0;

	/*
	 * On failure, preserve the errno values for NNP vs nosuid.
	 * NNP:  Operation not permitted for caller.
	 * nosuid:  Permission denied to file.
	 */
	if (nnp)
		return -EPERM;
	return -EACCES;
}

static int selinux_bprm_creds_for_exec(struct linux_binprm *bprm)
{
	const struct task_security_struct *old_tsec;
	struct task_security_struct *new_tsec;
	struct inode_security_struct *isec;
	struct common_audit_data ad;
	struct inode *inode = file_inode(bprm->file);
	int rc;

	/* SELinux context only depends on initial program or script and not
	 * the script interpreter */

	old_tsec = selinux_cred(current_cred());
	new_tsec = selinux_cred(bprm->cred);
	isec = inode_security(inode);

	/* Default to the current task SID. */
	new_tsec->sid = old_tsec->sid;
	new_tsec->osid = old_tsec->sid;

	/* Reset fs, key, and sock SIDs on execve. */
	new_tsec->create_sid = 0;
	new_tsec->keycreate_sid = 0;
	new_tsec->sockcreate_sid = 0;

	if (old_tsec->exec_sid) {
		new_tsec->sid = old_tsec->exec_sid;
		/* Reset exec SID on execve. */
		new_tsec->exec_sid = 0;

		/* Fail on NNP or nosuid if not an allowed transition. */
		rc = check_nnp_nosuid(bprm, old_tsec, new_tsec);
		if (rc)
			return rc;
	} else {
		/* Check for a default transition on this program. */
		rc = security_transition_sid(&selinux_state, old_tsec->sid,
					     isec->sid, SECCLASS_PROCESS, NULL,
					     &new_tsec->sid);
		if (rc)
			return rc;

		/*
		 * Fallback to old SID on NNP or nosuid if not an allowed
		 * transition.
		 */
		rc = check_nnp_nosuid(bprm, old_tsec, new_tsec);
		if (rc)
			new_tsec->sid = old_tsec->sid;
	}

	ad.type = LSM_AUDIT_DATA_FILE;
	ad.u.file = bprm->file;

	if (new_tsec->sid == old_tsec->sid) {
		rc = avc_has_perm(&selinux_state,
				  old_tsec->sid, isec->sid,
				  SECCLASS_FILE, FILE__EXECUTE_NO_TRANS, &ad);
		if (rc)
			return rc;
	} else {
		/* Check permissions for the transition. */
		rc = avc_has_perm(&selinux_state,
				  old_tsec->sid, new_tsec->sid,
				  SECCLASS_PROCESS, PROCESS__TRANSITION, &ad);
		if (rc)
			return rc;

		rc = avc_has_perm(&selinux_state,
				  new_tsec->sid, isec->sid,
				  SECCLASS_FILE, FILE__ENTRYPOINT, &ad);
		if (rc)
			return rc;

		/* Check for shared state */
		if (bprm->unsafe & LSM_UNSAFE_SHARE) {
			rc = avc_has_perm(&selinux_state,
					  old_tsec->sid, new_tsec->sid,
					  SECCLASS_PROCESS, PROCESS__SHARE,
					  NULL);
			if (rc)
				return -EPERM;
		}

		/* Make sure that anyone attempting to ptrace over a task that
		 * changes its SID has the appropriate permit */
		if (bprm->unsafe & LSM_UNSAFE_PTRACE) {
			u32 ptsid = ptrace_parent_sid();
			if (ptsid != 0) {
				rc = avc_has_perm(&selinux_state,
						  ptsid, new_tsec->sid,
						  SECCLASS_PROCESS,
						  PROCESS__PTRACE, NULL);
				if (rc)
					return -EPERM;
			}
		}

		/* Clear any possibly unsafe personality bits on exec: */
		bprm->per_clear |= PER_CLEAR_ON_SETID;

		/* Enable secure mode for SIDs transitions unless
		   the noatsecure permission is granted between
		   the two SIDs, i.e. ahp returns 0. */
		rc = avc_has_perm(&selinux_state,
				  old_tsec->sid, new_tsec->sid,
				  SECCLASS_PROCESS, PROCESS__NOATSECURE,
				  NULL);
		bprm->secureexec |= !!rc;
	}

	return 0;
}

static int match_file(const void *p, struct file *file, unsigned fd)
{
	return file_has_perm(p, file, file_to_av(file)) ? fd + 1 : 0;
}

/* Derived from fs/exec.c:flush_old_files. */
static inline void flush_unauthorized_files(const struct cred *cred,
					    struct files_struct *files)
{
	struct file *file, *devnull = NULL;
	struct tty_struct *tty;
	int drop_tty = 0;
	unsigned n;

	tty = get_current_tty();
	if (tty) {
		spin_lock(&tty->files_lock);
		if (!list_empty(&tty->tty_files)) {
			struct tty_file_private *file_priv;

			/* Revalidate access to controlling tty.
			   Use file_path_has_perm on the tty path directly
			   rather than using file_has_perm, as this particular
			   open file may belong to another process and we are
			   only interested in the inode-based check here. */
			file_priv = list_first_entry(&tty->tty_files,
						struct tty_file_private, list);
			file = file_priv->file;
			if (file_path_has_perm(cred, file, FILE__READ | FILE__WRITE))
				drop_tty = 1;
		}
		spin_unlock(&tty->files_lock);
		tty_kref_put(tty);
	}
	/* Reset controlling tty. */
	if (drop_tty)
		no_tty();

	/* Revalidate access to inherited open files. */
	n = iterate_fd(files, 0, match_file, cred);
	if (!n) /* none found? */
		return;

	devnull = dentry_open(&selinux_null, O_RDWR, cred);
	if (IS_ERR(devnull))
		devnull = NULL;
	/* replace all the matching ones with this */
	do {
		replace_fd(n - 1, devnull, 0);
	} while ((n = iterate_fd(files, n, match_file, cred)) != 0);
	if (devnull)
		fput(devnull);
}

/*
 * Prepare a process for imminent new credential changes due to exec
 */
static void selinux_bprm_committing_creds(struct linux_binprm *bprm)
{
	struct task_security_struct *new_tsec;
	struct rlimit *rlim, *initrlim;
	int rc, i;

	new_tsec = selinux_cred(bprm->cred);
	if (new_tsec->sid == new_tsec->osid)
		return;

	/* Close files for which the new task SID is not authorized. */
	flush_unauthorized_files(bprm->cred, current->files);

	/* Always clear parent death signal on SID transitions. */
	current->pdeath_signal = 0;

	/* Check whether the new SID can inherit resource limits from the old
	 * SID.  If not, reset all soft limits to the lower of the current
	 * task's hard limit and the init task's soft limit.
	 *
	 * Note that the setting of hard limits (even to lower them) can be
	 * controlled by the setrlimit check.  The inclusion of the init task's
	 * soft limit into the computation is to avoid resetting soft limits
	 * higher than the default soft limit for cases where the default is
	 * lower than the hard limit, e.g. RLIMIT_CORE or RLIMIT_STACK.
	 */
	rc = avc_has_perm(&selinux_state,
			  new_tsec->osid, new_tsec->sid, SECCLASS_PROCESS,
			  PROCESS__RLIMITINH, NULL);
	if (rc) {
		/* protect against do_prlimit() */
		task_lock(current);
		for (i = 0; i < RLIM_NLIMITS; i++) {
			rlim = current->signal->rlim + i;
			initrlim = init_task.signal->rlim + i;
			rlim->rlim_cur = min(rlim->rlim_max, initrlim->rlim_cur);
		}
		task_unlock(current);
		if (IS_ENABLED(CONFIG_POSIX_TIMERS))
			update_rlimit_cpu(current, rlimit(RLIMIT_CPU));
	}
}

/*
 * Clean up the process immediately after the installation of new credentials
 * due to exec
 */
static void selinux_bprm_committed_creds(struct linux_binprm *bprm)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	u32 osid, sid;
	int rc;

	osid = tsec->osid;
	sid = tsec->sid;

	if (sid == osid)
		return;

	/* Check whether the new SID can inherit signal state from the old SID.
	 * If not, clear itimers to avoid subsequent signal generation and
	 * flush and unblock signals.
	 *
	 * This must occur _after_ the task SID has been updated so that any
	 * kill done after the flush will be checked against the new SID.
	 */
	rc = avc_has_perm(&selinux_state,
			  osid, sid, SECCLASS_PROCESS, PROCESS__SIGINH, NULL);
	if (rc) {
		clear_itimer();

		spin_lock_irq(&current->sighand->siglock);
		if (!fatal_signal_pending(current)) {
			flush_sigqueue(&current->pending);
			flush_sigqueue(&current->signal->shared_pending);
			flush_signal_handlers(current, 1);
			sigemptyset(&current->blocked);
			recalc_sigpending();
		}
		spin_unlock_irq(&current->sighand->siglock);
	}

	/* Wake up the parent if it is waiting so that it can recheck
	 * wait permission to the new task SID. */
	read_lock(&tasklist_lock);
	__wake_up_parent(current, current->real_parent);
	read_unlock(&tasklist_lock);
}

/* superblock security operations */

static int selinux_sb_alloc_security(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = selinux_superblock(sb);

	mutex_init(&sbsec->lock);
	INIT_LIST_HEAD(&sbsec->isec_head);
	spin_lock_init(&sbsec->isec_lock);
	sbsec->sid = SECINITSID_UNLABELED;
	sbsec->def_sid = SECINITSID_FILE;
	sbsec->mntpoint_sid = SECINITSID_UNLABELED;

	return 0;
}

static inline int opt_len(const char *s)
{
	bool open_quote = false;
	int len;
	char c;

	for (len = 0; (c = s[len]) != '\0'; len++) {
		if (c == '"')
			open_quote = !open_quote;
		if (c == ',' && !open_quote)
			break;
	}
	return len;
}

static int selinux_sb_eat_lsm_opts(char *options, void **mnt_opts)
{
	char *from = options;
	char *to = options;
	bool first = true;
	int rc;

	while (1) {
		int len = opt_len(from);
		int token;
		char *arg = NULL;

		token = match_opt_prefix(from, len, &arg);

		if (token != Opt_error) {
			char *p, *q;

			/* strip quotes */
			if (arg) {
				for (p = q = arg; p < from + len; p++) {
					char c = *p;
					if (c != '"')
						*q++ = c;
				}
				arg = kmemdup_nul(arg, q - arg, GFP_KERNEL);
				if (!arg) {
					rc = -ENOMEM;
					goto free_opt;
				}
			}
			rc = selinux_add_opt(token, arg, mnt_opts);
			if (unlikely(rc)) {
				kfree(arg);
				goto free_opt;
			}
		} else {
			if (!first) {	// copy with preceding comma
				from--;
				len++;
			}
			if (to != from)
				memmove(to, from, len);
			to += len;
			first = false;
		}
		if (!from[len])
			break;
		from += len + 1;
	}
	*to = '\0';
	return 0;

free_opt:
	if (*mnt_opts) {
		selinux_free_mnt_opts(*mnt_opts);
		*mnt_opts = NULL;
	}
	return rc;
}

static int selinux_sb_mnt_opts_compat(struct super_block *sb, void *mnt_opts)
{
	struct selinux_mnt_opts *opts = mnt_opts;
	struct superblock_security_struct *sbsec = sb->s_security;
	u32 sid;
	int rc;

	/*
	 * Superblock not initialized (i.e. no options) - reject if any
	 * options specified, otherwise accept.
	 */
	if (!(sbsec->flags & SE_SBINITIALIZED))
		return opts ? 1 : 0;

	/*
	 * Superblock initialized and no options specified - reject if
	 * superblock has any options set, otherwise accept.
	 */
	if (!opts)
		return (sbsec->flags & SE_MNTMASK) ? 1 : 0;

	if (opts->fscontext) {
		rc = parse_sid(sb, opts->fscontext, &sid);
		if (rc)
			return 1;
		if (bad_option(sbsec, FSCONTEXT_MNT, sbsec->sid, sid))
			return 1;
	}
	if (opts->context) {
		rc = parse_sid(sb, opts->context, &sid);
		if (rc)
			return 1;
		if (bad_option(sbsec, CONTEXT_MNT, sbsec->mntpoint_sid, sid))
			return 1;
	}
	if (opts->rootcontext) {
		struct inode_security_struct *root_isec;

		root_isec = backing_inode_security(sb->s_root);
		rc = parse_sid(sb, opts->rootcontext, &sid);
		if (rc)
			return 1;
		if (bad_option(sbsec, ROOTCONTEXT_MNT, root_isec->sid, sid))
			return 1;
	}
	if (opts->defcontext) {
		rc = parse_sid(sb, opts->defcontext, &sid);
		if (rc)
			return 1;
		if (bad_option(sbsec, DEFCONTEXT_MNT, sbsec->def_sid, sid))
			return 1;
	}
	return 0;
}

static int selinux_sb_remount(struct super_block *sb, void *mnt_opts)
{
	struct selinux_mnt_opts *opts = mnt_opts;
	struct superblock_security_struct *sbsec = selinux_superblock(sb);
	u32 sid;
	int rc;

	if (!(sbsec->flags & SE_SBINITIALIZED))
		return 0;

	if (!opts)
		return 0;

	if (opts->fscontext) {
		rc = parse_sid(sb, opts->fscontext, &sid);
		if (rc)
			return rc;
		if (bad_option(sbsec, FSCONTEXT_MNT, sbsec->sid, sid))
			goto out_bad_option;
	}
	if (opts->context) {
		rc = parse_sid(sb, opts->context, &sid);
		if (rc)
			return rc;
		if (bad_option(sbsec, CONTEXT_MNT, sbsec->mntpoint_sid, sid))
			goto out_bad_option;
	}
	if (opts->rootcontext) {
		struct inode_security_struct *root_isec;
		root_isec = backing_inode_security(sb->s_root);
		rc = parse_sid(sb, opts->rootcontext, &sid);
		if (rc)
			return rc;
		if (bad_option(sbsec, ROOTCONTEXT_MNT, root_isec->sid, sid))
			goto out_bad_option;
	}
	if (opts->defcontext) {
		rc = parse_sid(sb, opts->defcontext, &sid);
		if (rc)
			return rc;
		if (bad_option(sbsec, DEFCONTEXT_MNT, sbsec->def_sid, sid))
			goto out_bad_option;
	}
	return 0;

out_bad_option:
	pr_warn("SELinux: unable to change security options "
	       "during remount (dev %s, type=%s)\n", sb->s_id,
	       sb->s_type->name);
	return -EINVAL;
}

static int selinux_sb_kern_mount(struct super_block *sb)
{
	const struct cred *cred = current_cred();
	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = sb->s_root;
	return superblock_has_perm(cred, sb, FILESYSTEM__MOUNT, &ad);
}

static int selinux_sb_statfs(struct dentry *dentry)
{
	const struct cred *cred = current_cred();
	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = dentry->d_sb->s_root;
	return superblock_has_perm(cred, dentry->d_sb, FILESYSTEM__GETATTR, &ad);
}

static int selinux_mount(const char *dev_name,
			 const struct path *path,
			 const char *type,
			 unsigned long flags,
			 void *data)
{
	const struct cred *cred = current_cred();

	if (flags & MS_REMOUNT)
		return superblock_has_perm(cred, path->dentry->d_sb,
					   FILESYSTEM__REMOUNT, NULL);
	else
		return path_has_perm(cred, path, FILE__MOUNTON);
}

static int selinux_move_mount(const struct path *from_path,
			      const struct path *to_path)
{
	const struct cred *cred = current_cred();

	return path_has_perm(cred, to_path, FILE__MOUNTON);
}

static int selinux_umount(struct vfsmount *mnt, int flags)
{
	const struct cred *cred = current_cred();

	return superblock_has_perm(cred, mnt->mnt_sb,
				   FILESYSTEM__UNMOUNT, NULL);
}

static int selinux_fs_context_dup(struct fs_context *fc,
				  struct fs_context *src_fc)
{
	const struct selinux_mnt_opts *src = src_fc->security;
	struct selinux_mnt_opts *opts;

	if (!src)
		return 0;

	fc->security = kzalloc(sizeof(struct selinux_mnt_opts), GFP_KERNEL);
	if (!fc->security)
		return -ENOMEM;

	opts = fc->security;

	if (src->fscontext) {
		opts->fscontext = kstrdup(src->fscontext, GFP_KERNEL);
		if (!opts->fscontext)
			return -ENOMEM;
	}
	if (src->context) {
		opts->context = kstrdup(src->context, GFP_KERNEL);
		if (!opts->context)
			return -ENOMEM;
	}
	if (src->rootcontext) {
		opts->rootcontext = kstrdup(src->rootcontext, GFP_KERNEL);
		if (!opts->rootcontext)
			return -ENOMEM;
	}
	if (src->defcontext) {
		opts->defcontext = kstrdup(src->defcontext, GFP_KERNEL);
		if (!opts->defcontext)
			return -ENOMEM;
	}
	return 0;
}

static const struct fs_parameter_spec selinux_fs_parameters[] = {
	fsparam_string(CONTEXT_STR,	Opt_context),
	fsparam_string(DEFCONTEXT_STR,	Opt_defcontext),
	fsparam_string(FSCONTEXT_STR,	Opt_fscontext),
	fsparam_string(ROOTCONTEXT_STR,	Opt_rootcontext),
	fsparam_flag  (SECLABEL_STR,	Opt_seclabel),
	{}
};

static int selinux_fs_context_parse_param(struct fs_context *fc,
					  struct fs_parameter *param)
{
	struct fs_parse_result result;
	int opt, rc;

	opt = fs_parse(fc, selinux_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	rc = selinux_add_opt(opt, param->string, &fc->security);
	if (!rc) {
		param->string = NULL;
		rc = 1;
	}
	return rc;
}

/* inode security operations */

static int selinux_inode_alloc_security(struct inode *inode)
{
	struct inode_security_struct *isec = selinux_inode(inode);
	u32 sid = current_sid();

	spin_lock_init(&isec->lock);
	INIT_LIST_HEAD(&isec->list);
	isec->inode = inode;
	isec->sid = SECINITSID_UNLABELED;
	isec->sclass = SECCLASS_FILE;
	isec->task_sid = sid;
	isec->initialized = LABEL_INVALID;

	return 0;
}

static void selinux_inode_free_security(struct inode *inode)
{
	inode_free_security(inode);
}

static int selinux_dentry_init_security(struct dentry *dentry, int mode,
					const struct qstr *name, void **ctx,
					u32 *ctxlen)
{
	u32 newsid;
	int rc;

	rc = selinux_determine_inode_label(selinux_cred(current_cred()),
					   d_inode(dentry->d_parent), name,
					   inode_mode_to_security_class(mode),
					   &newsid);
	if (rc)
		return rc;

	return security_sid_to_context(&selinux_state, newsid, (char **)ctx,
				       ctxlen);
}

static int selinux_dentry_create_files_as(struct dentry *dentry, int mode,
					  struct qstr *name,
					  const struct cred *old,
					  struct cred *new)
{
	u32 newsid;
	int rc;
	struct task_security_struct *tsec;

	rc = selinux_determine_inode_label(selinux_cred(old),
					   d_inode(dentry->d_parent), name,
					   inode_mode_to_security_class(mode),
					   &newsid);
	if (rc)
		return rc;

	tsec = selinux_cred(new);
	tsec->create_sid = newsid;
	return 0;
}

static int selinux_inode_init_security(struct inode *inode, struct inode *dir,
				       const struct qstr *qstr,
				       const char **name,
				       void **value, size_t *len)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	struct superblock_security_struct *sbsec;
	u32 newsid, clen;
	int rc;
	char *context;

	sbsec = selinux_superblock(dir->i_sb);

	newsid = tsec->create_sid;

	rc = selinux_determine_inode_label(tsec, dir, qstr,
		inode_mode_to_security_class(inode->i_mode),
		&newsid);
	if (rc)
		return rc;

	/* Possibly defer initialization to selinux_complete_init. */
	if (sbsec->flags & SE_SBINITIALIZED) {
		struct inode_security_struct *isec = selinux_inode(inode);
		isec->sclass = inode_mode_to_security_class(inode->i_mode);
		isec->sid = newsid;
		isec->initialized = LABEL_INITIALIZED;
	}

	if (!selinux_initialized(&selinux_state) ||
	    !(sbsec->flags & SBLABEL_MNT))
		return -EOPNOTSUPP;

	if (name)
		*name = XATTR_SELINUX_SUFFIX;

	if (value && len) {
		rc = security_sid_to_context_force(&selinux_state, newsid,
						   &context, &clen);
		if (rc)
			return rc;
		*value = context;
		*len = clen;
	}

	return 0;
}

static int selinux_inode_init_security_anon(struct inode *inode,
					    const struct qstr *name,
					    const struct inode *context_inode)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	struct common_audit_data ad;
	struct inode_security_struct *isec;
	int rc;

	if (unlikely(!selinux_initialized(&selinux_state)))
		return 0;

	isec = selinux_inode(inode);

	/*
	 * We only get here once per ephemeral inode.  The inode has
	 * been initialized via inode_alloc_security but is otherwise
	 * untouched.
	 */

	if (context_inode) {
		struct inode_security_struct *context_isec =
			selinux_inode(context_inode);
		if (context_isec->initialized != LABEL_INITIALIZED) {
			pr_err("SELinux:  context_inode is not initialized");
			return -EACCES;
		}

		isec->sclass = context_isec->sclass;
		isec->sid = context_isec->sid;
	} else {
		isec->sclass = SECCLASS_ANON_INODE;
		rc = security_transition_sid(
			&selinux_state, tsec->sid, tsec->sid,
			isec->sclass, name, &isec->sid);
		if (rc)
			return rc;
	}

	isec->initialized = LABEL_INITIALIZED;
	/*
	 * Now that we've initialized security, check whether we're
	 * allowed to actually create this type of anonymous inode.
	 */

	ad.type = LSM_AUDIT_DATA_INODE;
	ad.u.inode = inode;

	return avc_has_perm(&selinux_state,
			    tsec->sid,
			    isec->sid,
			    isec->sclass,
			    FILE__CREATE,
			    &ad);
}

static int selinux_inode_create(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	return may_create(dir, dentry, SECCLASS_FILE);
}

static int selinux_inode_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry)
{
	return may_link(dir, old_dentry, MAY_LINK);
}

static int selinux_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	return may_link(dir, dentry, MAY_UNLINK);
}

static int selinux_inode_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	return may_create(dir, dentry, SECCLASS_LNK_FILE);
}

static int selinux_inode_mkdir(struct inode *dir, struct dentry *dentry, umode_t mask)
{
	return may_create(dir, dentry, SECCLASS_DIR);
}

static int selinux_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	return may_link(dir, dentry, MAY_RMDIR);
}

static int selinux_inode_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	return may_create(dir, dentry, inode_mode_to_security_class(mode));
}

static int selinux_inode_rename(struct inode *old_inode, struct dentry *old_dentry,
				struct inode *new_inode, struct dentry *new_dentry)
{
	return may_rename(old_inode, old_dentry, new_inode, new_dentry);
}

static int selinux_inode_readlink(struct dentry *dentry)
{
	const struct cred *cred = current_cred();

	return dentry_has_perm(cred, dentry, FILE__READ);
}

static int selinux_inode_follow_link(struct dentry *dentry, struct inode *inode,
				     bool rcu)
{
	const struct cred *cred = current_cred();
	struct common_audit_data ad;
	struct inode_security_struct *isec;
	u32 sid;

	validate_creds(cred);

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = dentry;
	sid = cred_sid(cred);
	isec = inode_security_rcu(inode, rcu);
	if (IS_ERR(isec))
		return PTR_ERR(isec);

	return avc_has_perm(&selinux_state,
				  sid, isec->sid, isec->sclass, FILE__READ, &ad);
}

static noinline int audit_inode_permission(struct inode *inode,
					   u32 perms, u32 audited, u32 denied,
					   int result)
{
	struct common_audit_data ad;
	struct inode_security_struct *isec = selinux_inode(inode);

	ad.type = LSM_AUDIT_DATA_INODE;
	ad.u.inode = inode;

	return slow_avc_audit(&selinux_state,
			    current_sid(), isec->sid, isec->sclass, perms,
			    audited, denied, result, &ad);
}

static int selinux_inode_permission(struct inode *inode, int mask)
{
	const struct cred *cred = current_cred();
	u32 perms;
	bool from_access;
	bool no_block = mask & MAY_NOT_BLOCK;
	struct inode_security_struct *isec;
	u32 sid;
	struct av_decision avd;
	int rc, rc2;
	u32 audited, denied;

	from_access = mask & MAY_ACCESS;
	mask &= (MAY_READ|MAY_WRITE|MAY_EXEC|MAY_APPEND);

	/* No permission to check.  Existence test. */
	if (!mask)
		return 0;

	validate_creds(cred);

	if (unlikely(IS_PRIVATE(inode)))
		return 0;

	perms = file_mask_to_av(inode->i_mode, mask);

	sid = cred_sid(cred);
	isec = inode_security_rcu(inode, no_block);
	if (IS_ERR(isec))
		return PTR_ERR(isec);

	rc = avc_has_perm_noaudit(&selinux_state,
				  sid, isec->sid, isec->sclass, perms, 0,
				  &avd);
	audited = avc_audit_required(perms, &avd, rc,
				     from_access ? FILE__AUDIT_ACCESS : 0,
				     &denied);
	if (likely(!audited))
		return rc;

	rc2 = audit_inode_permission(inode, perms, audited, denied, rc);
	if (rc2)
		return rc2;
	return rc;
}

static int selinux_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	const struct cred *cred = current_cred();
	struct inode *inode = d_backing_inode(dentry);
	unsigned int ia_valid = iattr->ia_valid;
	__u32 av = FILE__WRITE;

	/* ATTR_FORCE is just used for ATTR_KILL_S[UG]ID. */
	if (ia_valid & ATTR_FORCE) {
		ia_valid &= ~(ATTR_KILL_SUID | ATTR_KILL_SGID | ATTR_MODE |
			      ATTR_FORCE);
		if (!ia_valid)
			return 0;
	}

	if (ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID |
			ATTR_ATIME_SET | ATTR_MTIME_SET | ATTR_TIMES_SET))
		return dentry_has_perm(cred, dentry, FILE__SETATTR);

	if (selinux_policycap_openperm() &&
	    inode->i_sb->s_magic != SOCKFS_MAGIC &&
	    (ia_valid & ATTR_SIZE) &&
	    !(ia_valid & ATTR_FILE))
		av |= FILE__OPEN;

	return dentry_has_perm(cred, dentry, av);
}

static int selinux_inode_getattr(const struct path *path)
{
	return path_has_perm(current_cred(), path, FILE__GETATTR);
}

static bool has_cap_mac_admin(bool audit)
{
	const struct cred *cred = current_cred();
	unsigned int opts = audit ? CAP_OPT_NONE : CAP_OPT_NOAUDIT;

	if (cap_capable(cred, &init_user_ns, CAP_MAC_ADMIN, opts))
		return false;
	if (cred_has_capability(cred, CAP_MAC_ADMIN, opts, true))
		return false;
	return true;
}

static int selinux_inode_setxattr(struct user_namespace *mnt_userns,
				  struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags)
{
	struct inode *inode = d_backing_inode(dentry);
	struct inode_security_struct *isec;
	struct superblock_security_struct *sbsec;
	struct common_audit_data ad;
	u32 newsid, sid = current_sid();
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SELINUX)) {
		rc = cap_inode_setxattr(dentry, name, value, size, flags);
		if (rc)
			return rc;

		/* Not an attribute we recognize, so just check the
		   ordinary setattr permission. */
		return dentry_has_perm(current_cred(), dentry, FILE__SETATTR);
	}

	if (!selinux_initialized(&selinux_state))
		return (inode_owner_or_capable(mnt_userns, inode) ? 0 : -EPERM);

	sbsec = selinux_superblock(inode->i_sb);
	if (!(sbsec->flags & SBLABEL_MNT))
		return -EOPNOTSUPP;

	if (!inode_owner_or_capable(mnt_userns, inode))
		return -EPERM;

	ad.type = LSM_AUDIT_DATA_DENTRY;
	ad.u.dentry = dentry;

	isec = backing_inode_security(dentry);
	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid, isec->sclass,
			  FILE__RELABELFROM, &ad);
	if (rc)
		return rc;

	rc = security_context_to_sid(&selinux_state, value, size, &newsid,
				     GFP_KERNEL);
	if (rc == -EINVAL) {
		if (!has_cap_mac_admin(true)) {
			struct audit_buffer *ab;
			size_t audit_size;

			/* We strip a nul only if it is at the end, otherwise the
			 * context contains a nul and we should audit that */
			if (value) {
				const char *str = value;

				if (str[size - 1] == '\0')
					audit_size = size - 1;
				else
					audit_size = size;
			} else {
				audit_size = 0;
			}
			ab = audit_log_start(audit_context(),
					     GFP_ATOMIC, AUDIT_SELINUX_ERR);
			if (!ab)
				return rc;
			audit_log_format(ab, "op=setxattr invalid_context=");
			audit_log_n_untrustedstring(ab, value, audit_size);
			audit_log_end(ab);

			return rc;
		}
		rc = security_context_to_sid_force(&selinux_state, value,
						   size, &newsid);
	}
	if (rc)
		return rc;

	rc = avc_has_perm(&selinux_state,
			  sid, newsid, isec->sclass,
			  FILE__RELABELTO, &ad);
	if (rc)
		return rc;

	rc = security_validate_transition(&selinux_state, isec->sid, newsid,
					  sid, isec->sclass);
	if (rc)
		return rc;

	return avc_has_perm(&selinux_state,
			    newsid,
			    sbsec->sid,
			    SECCLASS_FILESYSTEM,
			    FILESYSTEM__ASSOCIATE,
			    &ad);
}

static void selinux_inode_post_setxattr(struct dentry *dentry, const char *name,
					const void *value, size_t size,
					int flags)
{
	struct inode *inode = d_backing_inode(dentry);
	struct inode_security_struct *isec;
	u32 newsid;
	int rc;

	if (strcmp(name, XATTR_NAME_SELINUX)) {
		/* Not an attribute we recognize, so nothing to do. */
		return;
	}

	if (!selinux_initialized(&selinux_state)) {
		/* If we haven't even been initialized, then we can't validate
		 * against a policy, so leave the label as invalid. It may
		 * resolve to a valid label on the next revalidation try if
		 * we've since initialized.
		 */
		return;
	}

	rc = security_context_to_sid_force(&selinux_state, value, size,
					   &newsid);
	if (rc) {
		pr_err("SELinux:  unable to map context to SID"
		       "for (%s, %lu), rc=%d\n",
		       inode->i_sb->s_id, inode->i_ino, -rc);
		return;
	}

	isec = backing_inode_security(dentry);
	spin_lock(&isec->lock);
	isec->sclass = inode_mode_to_security_class(inode->i_mode);
	isec->sid = newsid;
	isec->initialized = LABEL_INITIALIZED;
	spin_unlock(&isec->lock);

	return;
}

static int selinux_inode_getxattr(struct dentry *dentry, const char *name)
{
	const struct cred *cred = current_cred();

	return dentry_has_perm(cred, dentry, FILE__GETATTR);
}

static int selinux_inode_listxattr(struct dentry *dentry)
{
	const struct cred *cred = current_cred();

	return dentry_has_perm(cred, dentry, FILE__GETATTR);
}

static int selinux_inode_removexattr(struct user_namespace *mnt_userns,
				     struct dentry *dentry, const char *name)
{
	if (strcmp(name, XATTR_NAME_SELINUX)) {
		int rc = cap_inode_removexattr(mnt_userns, dentry, name);
		if (rc)
			return rc;

		/* Not an attribute we recognize, so just check the
		   ordinary setattr permission. */
		return dentry_has_perm(current_cred(), dentry, FILE__SETATTR);
	}

	if (!selinux_initialized(&selinux_state))
		return 0;

	/* No one is allowed to remove a SELinux security label.
	   You can change the label, but all data must be labeled. */
	return -EACCES;
}

static int selinux_path_notify(const struct path *path, u64 mask,
						unsigned int obj_type)
{
	int ret;
	u32 perm;

	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_PATH;
	ad.u.path = *path;

	/*
	 * Set permission needed based on the type of mark being set.
	 * Performs an additional check for sb watches.
	 */
	switch (obj_type) {
	case FSNOTIFY_OBJ_TYPE_VFSMOUNT:
		perm = FILE__WATCH_MOUNT;
		break;
	case FSNOTIFY_OBJ_TYPE_SB:
		perm = FILE__WATCH_SB;
		ret = superblock_has_perm(current_cred(), path->dentry->d_sb,
						FILESYSTEM__WATCH, &ad);
		if (ret)
			return ret;
		break;
	case FSNOTIFY_OBJ_TYPE_INODE:
		perm = FILE__WATCH;
		break;
	default:
		return -EINVAL;
	}

	/* blocking watches require the file:watch_with_perm permission */
	if (mask & (ALL_FSNOTIFY_PERM_EVENTS))
		perm |= FILE__WATCH_WITH_PERM;

	/* watches on read-like events need the file:watch_reads permission */
	if (mask & (FS_ACCESS | FS_ACCESS_PERM | FS_CLOSE_NOWRITE))
		perm |= FILE__WATCH_READS;

	return path_has_perm(current_cred(), path, perm);
}

/*
 * Copy the inode security context value to the user.
 *
 * Permission check is handled by selinux_inode_getxattr hook.
 */
static int selinux_inode_getsecurity(struct user_namespace *mnt_userns,
				     struct inode *inode, const char *name,
				     void **buffer, bool alloc)
{
	u32 size;
	int error;
	char *context = NULL;
	struct inode_security_struct *isec;

	/*
	 * If we're not initialized yet, then we can't validate contexts, so
	 * just let vfs_getxattr fall back to using the on-disk xattr.
	 */
	if (!selinux_initialized(&selinux_state) ||
	    strcmp(name, XATTR_SELINUX_SUFFIX))
		return -EOPNOTSUPP;

	/*
	 * If the caller has CAP_MAC_ADMIN, then get the raw context
	 * value even if it is not defined by current policy; otherwise,
	 * use the in-core value under current policy.
	 * Use the non-auditing forms of the permission checks since
	 * getxattr may be called by unprivileged processes commonly
	 * and lack of permission just means that we fall back to the
	 * in-core context value, not a denial.
	 */
	isec = inode_security(inode);
	if (has_cap_mac_admin(false))
		error = security_sid_to_context_force(&selinux_state,
						      isec->sid, &context,
						      &size);
	else
		error = security_sid_to_context(&selinux_state, isec->sid,
						&context, &size);
	if (error)
		return error;
	error = size;
	if (alloc) {
		*buffer = context;
		goto out_nofree;
	}
	kfree(context);
out_nofree:
	return error;
}

static int selinux_inode_setsecurity(struct inode *inode, const char *name,
				     const void *value, size_t size, int flags)
{
	struct inode_security_struct *isec = inode_security_novalidate(inode);
	struct superblock_security_struct *sbsec;
	u32 newsid;
	int rc;

	if (strcmp(name, XATTR_SELINUX_SUFFIX))
		return -EOPNOTSUPP;

	sbsec = selinux_superblock(inode->i_sb);
	if (!(sbsec->flags & SBLABEL_MNT))
		return -EOPNOTSUPP;

	if (!value || !size)
		return -EACCES;

	rc = security_context_to_sid(&selinux_state, value, size, &newsid,
				     GFP_KERNEL);
	if (rc)
		return rc;

	spin_lock(&isec->lock);
	isec->sclass = inode_mode_to_security_class(inode->i_mode);
	isec->sid = newsid;
	isec->initialized = LABEL_INITIALIZED;
	spin_unlock(&isec->lock);
	return 0;
}

static int selinux_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	const int len = sizeof(XATTR_NAME_SELINUX);

	if (!selinux_initialized(&selinux_state))
		return 0;

	if (buffer && len <= buffer_size)
		memcpy(buffer, XATTR_NAME_SELINUX, len);
	return len;
}

static void selinux_inode_getsecid(struct inode *inode, u32 *secid)
{
	struct inode_security_struct *isec = inode_security_novalidate(inode);
	*secid = isec->sid;
}

static int selinux_inode_copy_up(struct dentry *src, struct cred **new)
{
	u32 sid;
	struct task_security_struct *tsec;
	struct cred *new_creds = *new;

	if (new_creds == NULL) {
		new_creds = prepare_creds();
		if (!new_creds)
			return -ENOMEM;
	}

	tsec = selinux_cred(new_creds);
	/* Get label from overlay inode and set it in create_sid */
	selinux_inode_getsecid(d_inode(src), &sid);
	tsec->create_sid = sid;
	*new = new_creds;
	return 0;
}

static int selinux_inode_copy_up_xattr(const char *name)
{
	/* The copy_up hook above sets the initial context on an inode, but we
	 * don't then want to overwrite it by blindly copying all the lower
	 * xattrs up.  Instead, we have to filter out SELinux-related xattrs.
	 */
	if (strcmp(name, XATTR_NAME_SELINUX) == 0)
		return 1; /* Discard */
	/*
	 * Any other attribute apart from SELINUX is not claimed, supported
	 * by selinux.
	 */
	return -EOPNOTSUPP;
}

/* kernfs node operations */

static int selinux_kernfs_init_security(struct kernfs_node *kn_dir,
					struct kernfs_node *kn)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	u32 parent_sid, newsid, clen;
	int rc;
	char *context;

	rc = kernfs_xattr_get(kn_dir, XATTR_NAME_SELINUX, NULL, 0);
	if (rc == -ENODATA)
		return 0;
	else if (rc < 0)
		return rc;

	clen = (u32)rc;
	context = kmalloc(clen, GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	rc = kernfs_xattr_get(kn_dir, XATTR_NAME_SELINUX, context, clen);
	if (rc < 0) {
		kfree(context);
		return rc;
	}

	rc = security_context_to_sid(&selinux_state, context, clen, &parent_sid,
				     GFP_KERNEL);
	kfree(context);
	if (rc)
		return rc;

	if (tsec->create_sid) {
		newsid = tsec->create_sid;
	} else {
		u16 secclass = inode_mode_to_security_class(kn->mode);
		struct qstr q;

		q.name = kn->name;
		q.hash_len = hashlen_string(kn_dir, kn->name);

		rc = security_transition_sid(&selinux_state, tsec->sid,
					     parent_sid, secclass, &q,
					     &newsid);
		if (rc)
			return rc;
	}

	rc = security_sid_to_context_force(&selinux_state, newsid,
					   &context, &clen);
	if (rc)
		return rc;

	rc = kernfs_xattr_set(kn, XATTR_NAME_SELINUX, context, clen,
			      XATTR_CREATE);
	kfree(context);
	return rc;
}


/* file security operations */

static int selinux_revalidate_file_permission(struct file *file, int mask)
{
	const struct cred *cred = current_cred();
	struct inode *inode = file_inode(file);

	/* file_mask_to_av won't add FILE__WRITE if MAY_APPEND is set */
	if ((file->f_flags & O_APPEND) && (mask & MAY_WRITE))
		mask |= MAY_APPEND;

	return file_has_perm(cred, file,
			     file_mask_to_av(inode->i_mode, mask));
}

static int selinux_file_permission(struct file *file, int mask)
{
	struct inode *inode = file_inode(file);
	struct file_security_struct *fsec = selinux_file(file);
	struct inode_security_struct *isec;
	u32 sid = current_sid();

	if (!mask)
		/* No permission to check.  Existence test. */
		return 0;

	isec = inode_security(inode);
	if (sid == fsec->sid && fsec->isid == isec->sid &&
	    fsec->pseqno == avc_policy_seqno(&selinux_state))
		/* No change since file_open check. */
		return 0;

	return selinux_revalidate_file_permission(file, mask);
}

static int selinux_file_alloc_security(struct file *file)
{
	struct file_security_struct *fsec = selinux_file(file);
	u32 sid = current_sid();

	fsec->sid = sid;
	fsec->fown_sid = sid;

	return 0;
}

/*
 * Check whether a task has the ioctl permission and cmd
 * operation to an inode.
 */
static int ioctl_has_perm(const struct cred *cred, struct file *file,
		u32 requested, u16 cmd)
{
	struct common_audit_data ad;
	struct file_security_struct *fsec = selinux_file(file);
	struct inode *inode = file_inode(file);
	struct inode_security_struct *isec;
	struct lsm_ioctlop_audit ioctl;
	u32 ssid = cred_sid(cred);
	int rc;
	u8 driver = cmd >> 8;
	u8 xperm = cmd & 0xff;

	ad.type = LSM_AUDIT_DATA_IOCTL_OP;
	ad.u.op = &ioctl;
	ad.u.op->cmd = cmd;
	ad.u.op->path = file->f_path;

	if (ssid != fsec->sid) {
		rc = avc_has_perm(&selinux_state,
				  ssid, fsec->sid,
				SECCLASS_FD,
				FD__USE,
				&ad);
		if (rc)
			goto out;
	}

	if (unlikely(IS_PRIVATE(inode)))
		return 0;

	isec = inode_security(inode);
	rc = avc_has_extended_perms(&selinux_state,
				    ssid, isec->sid, isec->sclass,
				    requested, driver, xperm, &ad);
out:
	return rc;
}

static int selinux_file_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	const struct cred *cred = current_cred();
	int error = 0;

	switch (cmd) {
	case FIONREAD:
	case FIBMAP:
	case FIGETBSZ:
	case FS_IOC_GETFLAGS:
	case FS_IOC_GETVERSION:
		error = file_has_perm(cred, file, FILE__GETATTR);
		break;

	case FS_IOC_SETFLAGS:
	case FS_IOC_SETVERSION:
		error = file_has_perm(cred, file, FILE__SETATTR);
		break;

	/* sys_ioctl() checks */
	case FIONBIO:
	case FIOASYNC:
		error = file_has_perm(cred, file, 0);
		break;

	case KDSKBENT:
	case KDSKBSENT:
		error = cred_has_capability(cred, CAP_SYS_TTY_CONFIG,
					    CAP_OPT_NONE, true);
		break;

	/* default case assumes that the command will go
	 * to the file's ioctl() function.
	 */
	default:
		error = ioctl_has_perm(cred, file, FILE__IOCTL, (u16) cmd);
	}
	return error;
}

static int default_noexec __ro_after_init;

static int file_map_prot_check(struct file *file, unsigned long prot, int shared)
{
	const struct cred *cred = current_cred();
	u32 sid = cred_sid(cred);
	int rc = 0;

	if (default_noexec &&
	    (prot & PROT_EXEC) && (!file || IS_PRIVATE(file_inode(file)) ||
				   (!shared && (prot & PROT_WRITE)))) {
		/*
		 * We are making executable an anonymous mapping or a
		 * private file mapping that will also be writable.
		 * This has an additional check.
		 */
		rc = avc_has_perm(&selinux_state,
				  sid, sid, SECCLASS_PROCESS,
				  PROCESS__EXECMEM, NULL);
		if (rc)
			goto error;
	}

	if (file) {
		/* read access is always possible with a mapping */
		u32 av = FILE__READ;

		/* write access only matters if the mapping is shared */
		if (shared && (prot & PROT_WRITE))
			av |= FILE__WRITE;

		if (prot & PROT_EXEC)
			av |= FILE__EXECUTE;

		return file_has_perm(cred, file, av);
	}

error:
	return rc;
}

static int selinux_mmap_addr(unsigned long addr)
{
	int rc = 0;

	if (addr < CONFIG_LSM_MMAP_MIN_ADDR) {
		u32 sid = current_sid();
		rc = avc_has_perm(&selinux_state,
				  sid, sid, SECCLASS_MEMPROTECT,
				  MEMPROTECT__MMAP_ZERO, NULL);
	}

	return rc;
}

static int selinux_mmap_file(struct file *file, unsigned long reqprot,
			     unsigned long prot, unsigned long flags)
{
	struct common_audit_data ad;
	int rc;

	if (file) {
		ad.type = LSM_AUDIT_DATA_FILE;
		ad.u.file = file;
		rc = inode_has_perm(current_cred(), file_inode(file),
				    FILE__MAP, &ad);
		if (rc)
			return rc;
	}

	if (checkreqprot_get(&selinux_state))
		prot = reqprot;

	return file_map_prot_check(file, prot,
				   (flags & MAP_TYPE) == MAP_SHARED);
}

static int selinux_file_mprotect(struct vm_area_struct *vma,
				 unsigned long reqprot,
				 unsigned long prot)
{
	const struct cred *cred = current_cred();
	u32 sid = cred_sid(cred);

	if (checkreqprot_get(&selinux_state))
		prot = reqprot;

	if (default_noexec &&
	    (prot & PROT_EXEC) && !(vma->vm_flags & VM_EXEC)) {
		int rc = 0;
		if (vma->vm_start >= vma->vm_mm->start_brk &&
		    vma->vm_end <= vma->vm_mm->brk) {
			rc = avc_has_perm(&selinux_state,
					  sid, sid, SECCLASS_PROCESS,
					  PROCESS__EXECHEAP, NULL);
		} else if (!vma->vm_file &&
			   ((vma->vm_start <= vma->vm_mm->start_stack &&
			     vma->vm_end >= vma->vm_mm->start_stack) ||
			    vma_is_stack_for_current(vma))) {
			rc = avc_has_perm(&selinux_state,
					  sid, sid, SECCLASS_PROCESS,
					  PROCESS__EXECSTACK, NULL);
		} else if (vma->vm_file && vma->anon_vma) {
			/*
			 * We are making executable a file mapping that has
			 * had some COW done. Since pages might have been
			 * written, check ability to execute the possibly
			 * modified content.  This typically should only
			 * occur for text relocations.
			 */
			rc = file_has_perm(cred, vma->vm_file, FILE__EXECMOD);
		}
		if (rc)
			return rc;
	}

	return file_map_prot_check(vma->vm_file, prot, vma->vm_flags&VM_SHARED);
}

static int selinux_file_lock(struct file *file, unsigned int cmd)
{
	const struct cred *cred = current_cred();

	return file_has_perm(cred, file, FILE__LOCK);
}

static int selinux_file_fcntl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	const struct cred *cred = current_cred();
	int err = 0;

	switch (cmd) {
	case F_SETFL:
		if ((file->f_flags & O_APPEND) && !(arg & O_APPEND)) {
			err = file_has_perm(cred, file, FILE__WRITE);
			break;
		}
		fallthrough;
	case F_SETOWN:
	case F_SETSIG:
	case F_GETFL:
	case F_GETOWN:
	case F_GETSIG:
	case F_GETOWNER_UIDS:
		/* Just check FD__USE permission */
		err = file_has_perm(cred, file, 0);
		break;
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
	case F_OFD_GETLK:
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
#if BITS_PER_LONG == 32
	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
#endif
		err = file_has_perm(cred, file, FILE__LOCK);
		break;
	}

	return err;
}

static void selinux_file_set_fowner(struct file *file)
{
	struct file_security_struct *fsec;

	fsec = selinux_file(file);
	fsec->fown_sid = current_sid();
}

static int selinux_file_send_sigiotask(struct task_struct *tsk,
				       struct fown_struct *fown, int signum)
{
	struct file *file;
	u32 sid = task_sid_obj(tsk);
	u32 perm;
	struct file_security_struct *fsec;

	/* struct fown_struct is never outside the context of a struct file */
	file = container_of(fown, struct file, f_owner);

	fsec = selinux_file(file);

	if (!signum)
		perm = signal_to_av(SIGIO); /* as per send_sigio_to_task */
	else
		perm = signal_to_av(signum);

	return avc_has_perm(&selinux_state,
			    fsec->fown_sid, sid,
			    SECCLASS_PROCESS, perm, NULL);
}

static int selinux_file_receive(struct file *file)
{
	const struct cred *cred = current_cred();

	return file_has_perm(cred, file, file_to_av(file));
}

static int selinux_file_open(struct file *file)
{
	struct file_security_struct *fsec;
	struct inode_security_struct *isec;

	fsec = selinux_file(file);
	isec = inode_security(file_inode(file));
	/*
	 * Save inode label and policy sequence number
	 * at open-time so that selinux_file_permission
	 * can determine whether revalidation is necessary.
	 * Task label is already saved in the file security
	 * struct as its SID.
	 */
	fsec->isid = isec->sid;
	fsec->pseqno = avc_policy_seqno(&selinux_state);
	/*
	 * Since the inode label or policy seqno may have changed
	 * between the selinux_inode_permission check and the saving
	 * of state above, recheck that access is still permitted.
	 * Otherwise, access might never be revalidated against the
	 * new inode label or new policy.
	 * This check is not redundant - do not remove.
	 */
	return file_path_has_perm(file->f_cred, file, open_file_to_av(file));
}

/* task security operations */

static int selinux_task_alloc(struct task_struct *task,
			      unsigned long clone_flags)
{
	u32 sid = current_sid();

	return avc_has_perm(&selinux_state,
			    sid, sid, SECCLASS_PROCESS, PROCESS__FORK, NULL);
}

/*
 * prepare a new set of credentials for modification
 */
static int selinux_cred_prepare(struct cred *new, const struct cred *old,
				gfp_t gfp)
{
	const struct task_security_struct *old_tsec = selinux_cred(old);
	struct task_security_struct *tsec = selinux_cred(new);

	*tsec = *old_tsec;
	return 0;
}

/*
 * transfer the SELinux data to a blank set of creds
 */
static void selinux_cred_transfer(struct cred *new, const struct cred *old)
{
	const struct task_security_struct *old_tsec = selinux_cred(old);
	struct task_security_struct *tsec = selinux_cred(new);

	*tsec = *old_tsec;
}

static void selinux_cred_getsecid(const struct cred *c, u32 *secid)
{
	*secid = cred_sid(c);
}

/*
 * set the security data for a kernel service
 * - all the creation contexts are set to unlabelled
 */
static int selinux_kernel_act_as(struct cred *new, u32 secid)
{
	struct task_security_struct *tsec = selinux_cred(new);
	u32 sid = current_sid();
	int ret;

	ret = avc_has_perm(&selinux_state,
			   sid, secid,
			   SECCLASS_KERNEL_SERVICE,
			   KERNEL_SERVICE__USE_AS_OVERRIDE,
			   NULL);
	if (ret == 0) {
		tsec->sid = secid;
		tsec->create_sid = 0;
		tsec->keycreate_sid = 0;
		tsec->sockcreate_sid = 0;
	}
	return ret;
}

/*
 * set the file creation context in a security record to the same as the
 * objective context of the specified inode
 */
static int selinux_kernel_create_files_as(struct cred *new, struct inode *inode)
{
	struct inode_security_struct *isec = inode_security(inode);
	struct task_security_struct *tsec = selinux_cred(new);
	u32 sid = current_sid();
	int ret;

	ret = avc_has_perm(&selinux_state,
			   sid, isec->sid,
			   SECCLASS_KERNEL_SERVICE,
			   KERNEL_SERVICE__CREATE_FILES_AS,
			   NULL);

	if (ret == 0)
		tsec->create_sid = isec->sid;
	return ret;
}

static int selinux_kernel_module_request(char *kmod_name)
{
	struct common_audit_data ad;

	ad.type = LSM_AUDIT_DATA_KMOD;
	ad.u.kmod_name = kmod_name;

	return avc_has_perm(&selinux_state,
			    current_sid(), SECINITSID_KERNEL, SECCLASS_SYSTEM,
			    SYSTEM__MODULE_REQUEST, &ad);
}

static int selinux_kernel_module_from_file(struct file *file)
{
	struct common_audit_data ad;
	struct inode_security_struct *isec;
	struct file_security_struct *fsec;
	u32 sid = current_sid();
	int rc;

	/* init_module */
	if (file == NULL)
		return avc_has_perm(&selinux_state,
				    sid, sid, SECCLASS_SYSTEM,
					SYSTEM__MODULE_LOAD, NULL);

	/* finit_module */

	ad.type = LSM_AUDIT_DATA_FILE;
	ad.u.file = file;

	fsec = selinux_file(file);
	if (sid != fsec->sid) {
		rc = avc_has_perm(&selinux_state,
				  sid, fsec->sid, SECCLASS_FD, FD__USE, &ad);
		if (rc)
			return rc;
	}

	isec = inode_security(file_inode(file));
	return avc_has_perm(&selinux_state,
			    sid, isec->sid, SECCLASS_SYSTEM,
				SYSTEM__MODULE_LOAD, &ad);
}

static int selinux_kernel_read_file(struct file *file,
				    enum kernel_read_file_id id,
				    bool contents)
{
	int rc = 0;

	switch (id) {
	case READING_MODULE:
		rc = selinux_kernel_module_from_file(contents ? file : NULL);
		break;
	default:
		break;
	}

	return rc;
}

static int selinux_kernel_load_data(enum kernel_load_data_id id, bool contents)
{
	int rc = 0;

	switch (id) {
	case LOADING_MODULE:
		rc = selinux_kernel_module_from_file(NULL);
		break;
	default:
		break;
	}

	return rc;
}

static int selinux_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__SETPGID, NULL);
}

static int selinux_task_getpgid(struct task_struct *p)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__GETPGID, NULL);
}

static int selinux_task_getsid(struct task_struct *p)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__GETSESSION, NULL);
}

static void selinux_task_getsecid_subj(struct task_struct *p, u32 *secid)
{
	*secid = task_sid_subj(p);
}

static void selinux_task_getsecid_obj(struct task_struct *p, u32 *secid)
{
	*secid = task_sid_obj(p);
}

static int selinux_task_setnice(struct task_struct *p, int nice)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__SETSCHED, NULL);
}

static int selinux_task_setioprio(struct task_struct *p, int ioprio)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__SETSCHED, NULL);
}

static int selinux_task_getioprio(struct task_struct *p)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__GETSCHED, NULL);
}

static int selinux_task_prlimit(const struct cred *cred, const struct cred *tcred,
				unsigned int flags)
{
	u32 av = 0;

	if (!flags)
		return 0;
	if (flags & LSM_PRLIMIT_WRITE)
		av |= PROCESS__SETRLIMIT;
	if (flags & LSM_PRLIMIT_READ)
		av |= PROCESS__GETRLIMIT;
	return avc_has_perm(&selinux_state,
			    cred_sid(cred), cred_sid(tcred),
			    SECCLASS_PROCESS, av, NULL);
}

static int selinux_task_setrlimit(struct task_struct *p, unsigned int resource,
		struct rlimit *new_rlim)
{
	struct rlimit *old_rlim = p->signal->rlim + resource;

	/* Control the ability to change the hard limit (whether
	   lowering or raising it), so that the hard limit can
	   later be used as a safe reset point for the soft limit
	   upon context transitions.  See selinux_bprm_committing_creds. */
	if (old_rlim->rlim_max != new_rlim->rlim_max)
		return avc_has_perm(&selinux_state,
				    current_sid(), task_sid_obj(p),
				    SECCLASS_PROCESS, PROCESS__SETRLIMIT, NULL);

	return 0;
}

static int selinux_task_setscheduler(struct task_struct *p)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__SETSCHED, NULL);
}

static int selinux_task_getscheduler(struct task_struct *p)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__GETSCHED, NULL);
}

static int selinux_task_movememory(struct task_struct *p)
{
	return avc_has_perm(&selinux_state,
			    current_sid(), task_sid_obj(p), SECCLASS_PROCESS,
			    PROCESS__SETSCHED, NULL);
}

static int selinux_task_kill(struct task_struct *p, struct kernel_siginfo *info,
				int sig, const struct cred *cred)
{
	u32 secid;
	u32 perm;

	if (!sig)
		perm = PROCESS__SIGNULL; /* null signal; existence test */
	else
		perm = signal_to_av(sig);
	if (!cred)
		secid = current_sid();
	else
		secid = cred_sid(cred);
	return avc_has_perm(&selinux_state,
			    secid, task_sid_obj(p), SECCLASS_PROCESS, perm, NULL);
}

static void selinux_task_to_inode(struct task_struct *p,
				  struct inode *inode)
{
	struct inode_security_struct *isec = selinux_inode(inode);
	u32 sid = task_sid_obj(p);

	spin_lock(&isec->lock);
	isec->sclass = inode_mode_to_security_class(inode->i_mode);
	isec->sid = sid;
	isec->initialized = LABEL_INITIALIZED;
	spin_unlock(&isec->lock);
}

/* Returns error only if unable to parse addresses */
static int selinux_parse_skb_ipv4(struct sk_buff *skb,
			struct common_audit_data *ad, u8 *proto)
{
	int offset, ihlen, ret = -EINVAL;
	struct iphdr _iph, *ih;

	offset = skb_network_offset(skb);
	ih = skb_header_pointer(skb, offset, sizeof(_iph), &_iph);
	if (ih == NULL)
		goto out;

	ihlen = ih->ihl * 4;
	if (ihlen < sizeof(_iph))
		goto out;

	ad->u.net->v4info.saddr = ih->saddr;
	ad->u.net->v4info.daddr = ih->daddr;
	ret = 0;

	if (proto)
		*proto = ih->protocol;

	switch (ih->protocol) {
	case IPPROTO_TCP: {
		struct tcphdr _tcph, *th;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		offset += ihlen;
		th = skb_header_pointer(skb, offset, sizeof(_tcph), &_tcph);
		if (th == NULL)
			break;

		ad->u.net->sport = th->source;
		ad->u.net->dport = th->dest;
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr _udph, *uh;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		offset += ihlen;
		uh = skb_header_pointer(skb, offset, sizeof(_udph), &_udph);
		if (uh == NULL)
			break;

		ad->u.net->sport = uh->source;
		ad->u.net->dport = uh->dest;
		break;
	}

	case IPPROTO_DCCP: {
		struct dccp_hdr _dccph, *dh;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		offset += ihlen;
		dh = skb_header_pointer(skb, offset, sizeof(_dccph), &_dccph);
		if (dh == NULL)
			break;

		ad->u.net->sport = dh->dccph_sport;
		ad->u.net->dport = dh->dccph_dport;
		break;
	}

#if IS_ENABLED(CONFIG_IP_SCTP)
	case IPPROTO_SCTP: {
		struct sctphdr _sctph, *sh;

		if (ntohs(ih->frag_off) & IP_OFFSET)
			break;

		offset += ihlen;
		sh = skb_header_pointer(skb, offset, sizeof(_sctph), &_sctph);
		if (sh == NULL)
			break;

		ad->u.net->sport = sh->source;
		ad->u.net->dport = sh->dest;
		break;
	}
#endif
	default:
		break;
	}
out:
	return ret;
}

#if IS_ENABLED(CONFIG_IPV6)

/* Returns error only if unable to parse addresses */
static int selinux_parse_skb_ipv6(struct sk_buff *skb,
			struct common_audit_data *ad, u8 *proto)
{
	u8 nexthdr;
	int ret = -EINVAL, offset;
	struct ipv6hdr _ipv6h, *ip6;
	__be16 frag_off;

	offset = skb_network_offset(skb);
	ip6 = skb_header_pointer(skb, offset, sizeof(_ipv6h), &_ipv6h);
	if (ip6 == NULL)
		goto out;

	ad->u.net->v6info.saddr = ip6->saddr;
	ad->u.net->v6info.daddr = ip6->daddr;
	ret = 0;

	nexthdr = ip6->nexthdr;
	offset += sizeof(_ipv6h);
	offset = ipv6_skip_exthdr(skb, offset, &nexthdr, &frag_off);
	if (offset < 0)
		goto out;

	if (proto)
		*proto = nexthdr;

	switch (nexthdr) {
	case IPPROTO_TCP: {
		struct tcphdr _tcph, *th;

		th = skb_header_pointer(skb, offset, sizeof(_tcph), &_tcph);
		if (th == NULL)
			break;

		ad->u.net->sport = th->source;
		ad->u.net->dport = th->dest;
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr _udph, *uh;

		uh = skb_header_pointer(skb, offset, sizeof(_udph), &_udph);
		if (uh == NULL)
			break;

		ad->u.net->sport = uh->source;
		ad->u.net->dport = uh->dest;
		break;
	}

	case IPPROTO_DCCP: {
		struct dccp_hdr _dccph, *dh;

		dh = skb_header_pointer(skb, offset, sizeof(_dccph), &_dccph);
		if (dh == NULL)
			break;

		ad->u.net->sport = dh->dccph_sport;
		ad->u.net->dport = dh->dccph_dport;
		break;
	}

#if IS_ENABLED(CONFIG_IP_SCTP)
	case IPPROTO_SCTP: {
		struct sctphdr _sctph, *sh;

		sh = skb_header_pointer(skb, offset, sizeof(_sctph), &_sctph);
		if (sh == NULL)
			break;

		ad->u.net->sport = sh->source;
		ad->u.net->dport = sh->dest;
		break;
	}
#endif
	/* includes fragments */
	default:
		break;
	}
out:
	return ret;
}

#endif /* IPV6 */

static int selinux_parse_skb(struct sk_buff *skb, struct common_audit_data *ad,
			     char **_addrp, int src, u8 *proto)
{
	char *addrp;
	int ret;

	switch (ad->u.net->family) {
	case PF_INET:
		ret = selinux_parse_skb_ipv4(skb, ad, proto);
		if (ret)
			goto parse_error;
		addrp = (char *)(src ? &ad->u.net->v4info.saddr :
				       &ad->u.net->v4info.daddr);
		goto okay;

#if IS_ENABLED(CONFIG_IPV6)
	case PF_INET6:
		ret = selinux_parse_skb_ipv6(skb, ad, proto);
		if (ret)
			goto parse_error;
		addrp = (char *)(src ? &ad->u.net->v6info.saddr :
				       &ad->u.net->v6info.daddr);
		goto okay;
#endif	/* IPV6 */
	default:
		addrp = NULL;
		goto okay;
	}

parse_error:
	pr_warn(
	       "SELinux: failure in selinux_parse_skb(),"
	       " unable to parse packet\n");
	return ret;

okay:
	if (_addrp)
		*_addrp = addrp;
	return 0;
}

/**
 * selinux_skb_peerlbl_sid - Determine the peer label of a packet
 * @skb: the packet
 * @family: protocol family
 * @sid: the packet's peer label SID
 *
 * Description:
 * Check the various different forms of network peer labeling and determine
 * the peer label/SID for the packet; most of the magic actually occurs in
 * the security server function security_net_peersid_cmp().  The function
 * returns zero if the value in @sid is valid (although it may be SECSID_NULL)
 * or -EACCES if @sid is invalid due to inconsistencies with the different
 * peer labels.
 *
 */
static int selinux_skb_peerlbl_sid(struct sk_buff *skb, u16 family, u32 *sid)
{
	int err;
	u32 xfrm_sid;
	u32 nlbl_sid;
	u32 nlbl_type;

	err = selinux_xfrm_skb_sid(skb, &xfrm_sid);
	if (unlikely(err))
		return -EACCES;
	err = selinux_netlbl_skbuff_getsid(skb, family, &nlbl_type, &nlbl_sid);
	if (unlikely(err))
		return -EACCES;

	err = security_net_peersid_resolve(&selinux_state, nlbl_sid,
					   nlbl_type, xfrm_sid, sid);
	if (unlikely(err)) {
		pr_warn(
		       "SELinux: failure in selinux_skb_peerlbl_sid(),"
		       " unable to determine packet's peer label\n");
		return -EACCES;
	}

	return 0;
}

/**
 * selinux_conn_sid - Determine the child socket label for a connection
 * @sk_sid: the parent socket's SID
 * @skb_sid: the packet's SID
 * @conn_sid: the resulting connection SID
 *
 * If @skb_sid is valid then the user:role:type information from @sk_sid is
 * combined with the MLS information from @skb_sid in order to create
 * @conn_sid.  If @skb_sid is not valid then @conn_sid is simply a copy
 * of @sk_sid.  Returns zero on success, negative values on failure.
 *
 */
static int selinux_conn_sid(u32 sk_sid, u32 skb_sid, u32 *conn_sid)
{
	int err = 0;

	if (skb_sid != SECSID_NULL)
		err = security_sid_mls_copy(&selinux_state, sk_sid, skb_sid,
					    conn_sid);
	else
		*conn_sid = sk_sid;

	return err;
}

/* socket security operations */

static int socket_sockcreate_sid(const struct task_security_struct *tsec,
				 u16 secclass, u32 *socksid)
{
	if (tsec->sockcreate_sid > SECSID_NULL) {
		*socksid = tsec->sockcreate_sid;
		return 0;
	}

	return security_transition_sid(&selinux_state, tsec->sid, tsec->sid,
				       secclass, NULL, socksid);
}

static int sock_has_perm(struct sock *sk, u32 perms)
{
	struct sk_security_struct *sksec = sk->sk_security;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};

	if (sksec->sid == SECINITSID_KERNEL)
		return 0;

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->sk = sk;

	return avc_has_perm(&selinux_state,
			    current_sid(), sksec->sid, sksec->sclass, perms,
			    &ad);
}

static int selinux_socket_create(int family, int type,
				 int protocol, int kern)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	u32 newsid;
	u16 secclass;
	int rc;

	if (kern)
		return 0;

	secclass = socket_type_to_security_class(family, type, protocol);
	rc = socket_sockcreate_sid(tsec, secclass, &newsid);
	if (rc)
		return rc;

	return avc_has_perm(&selinux_state,
			    tsec->sid, newsid, secclass, SOCKET__CREATE, NULL);
}

static int selinux_socket_post_create(struct socket *sock, int family,
				      int type, int protocol, int kern)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());
	struct inode_security_struct *isec = inode_security_novalidate(SOCK_INODE(sock));
	struct sk_security_struct *sksec;
	u16 sclass = socket_type_to_security_class(family, type, protocol);
	u32 sid = SECINITSID_KERNEL;
	int err = 0;

	if (!kern) {
		err = socket_sockcreate_sid(tsec, sclass, &sid);
		if (err)
			return err;
	}

	isec->sclass = sclass;
	isec->sid = sid;
	isec->initialized = LABEL_INITIALIZED;

	if (sock->sk) {
		sksec = sock->sk->sk_security;
		sksec->sclass = sclass;
		sksec->sid = sid;
		/* Allows detection of the first association on this socket */
		if (sksec->sclass == SECCLASS_SCTP_SOCKET)
			sksec->sctp_assoc_state = SCTP_ASSOC_UNSET;

		err = selinux_netlbl_socket_post_create(sock->sk, family);
	}

	return err;
}

static int selinux_socket_socketpair(struct socket *socka,
				     struct socket *sockb)
{
	struct sk_security_struct *sksec_a = socka->sk->sk_security;
	struct sk_security_struct *sksec_b = sockb->sk->sk_security;

	sksec_a->peer_sid = sksec_b->sid;
	sksec_b->peer_sid = sksec_a->sid;

	return 0;
}

/* Range of port numbers used to automatically bind.
   Need to determine whether we should perform a name_bind
   permission check between the socket and the port number. */

static int selinux_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	struct sock *sk = sock->sk;
	struct sk_security_struct *sksec = sk->sk_security;
	u16 family;
	int err;

	err = sock_has_perm(sk, SOCKET__BIND);
	if (err)
		goto out;

	/* If PF_INET or PF_INET6, check name_bind permission for the port. */
	family = sk->sk_family;
	if (family == PF_INET || family == PF_INET6) {
		char *addrp;
		struct common_audit_data ad;
		struct lsm_network_audit net = {0,};
		struct sockaddr_in *addr4 = NULL;
		struct sockaddr_in6 *addr6 = NULL;
		u16 family_sa;
		unsigned short snum;
		u32 sid, node_perm;

		/*
		 * sctp_bindx(3) calls via selinux_sctp_bind_connect()
		 * that validates multiple binding addresses. Because of this
		 * need to check address->sa_family as it is possible to have
		 * sk->sk_family = PF_INET6 with addr->sa_family = AF_INET.
		 */
		if (addrlen < offsetofend(struct sockaddr, sa_family))
			return -EINVAL;
		family_sa = address->sa_family;
		switch (family_sa) {
		case AF_UNSPEC:
		case AF_INET:
			if (addrlen < sizeof(struct sockaddr_in))
				return -EINVAL;
			addr4 = (struct sockaddr_in *)address;
			if (family_sa == AF_UNSPEC) {
				/* see __inet_bind(), we only want to allow
				 * AF_UNSPEC if the address is INADDR_ANY
				 */
				if (addr4->sin_addr.s_addr != htonl(INADDR_ANY))
					goto err_af;
				family_sa = AF_INET;
			}
			snum = ntohs(addr4->sin_port);
			addrp = (char *)&addr4->sin_addr.s_addr;
			break;
		case AF_INET6:
			if (addrlen < SIN6_LEN_RFC2133)
				return -EINVAL;
			addr6 = (struct sockaddr_in6 *)address;
			snum = ntohs(addr6->sin6_port);
			addrp = (char *)&addr6->sin6_addr.s6_addr;
			break;
		default:
			goto err_af;
		}

		ad.type = LSM_AUDIT_DATA_NET;
		ad.u.net = &net;
		ad.u.net->sport = htons(snum);
		ad.u.net->family = family_sa;

		if (snum) {
			int low, high;

			inet_get_local_port_range(sock_net(sk), &low, &high);

			if (inet_port_requires_bind_service(sock_net(sk), snum) ||
			    snum < low || snum > high) {
				err = sel_netport_sid(sk->sk_protocol,
						      snum, &sid);
				if (err)
					goto out;
				err = avc_has_perm(&selinux_state,
						   sksec->sid, sid,
						   sksec->sclass,
						   SOCKET__NAME_BIND, &ad);
				if (err)
					goto out;
			}
		}

		switch (sksec->sclass) {
		case SECCLASS_TCP_SOCKET:
			node_perm = TCP_SOCKET__NODE_BIND;
			break;

		case SECCLASS_UDP_SOCKET:
			node_perm = UDP_SOCKET__NODE_BIND;
			break;

		case SECCLASS_DCCP_SOCKET:
			node_perm = DCCP_SOCKET__NODE_BIND;
			break;

		case SECCLASS_SCTP_SOCKET:
			node_perm = SCTP_SOCKET__NODE_BIND;
			break;

		default:
			node_perm = RAWIP_SOCKET__NODE_BIND;
			break;
		}

		err = sel_netnode_sid(addrp, family_sa, &sid);
		if (err)
			goto out;

		if (family_sa == AF_INET)
			ad.u.net->v4info.saddr = addr4->sin_addr.s_addr;
		else
			ad.u.net->v6info.saddr = addr6->sin6_addr;

		err = avc_has_perm(&selinux_state,
				   sksec->sid, sid,
				   sksec->sclass, node_perm, &ad);
		if (err)
			goto out;
	}
out:
	return err;
err_af:
	/* Note that SCTP services expect -EINVAL, others -EAFNOSUPPORT. */
	if (sksec->sclass == SECCLASS_SCTP_SOCKET)
		return -EINVAL;
	return -EAFNOSUPPORT;
}

/* This supports connect(2) and SCTP connect services such as sctp_connectx(3)
 * and sctp_sendmsg(3) as described in Documentation/security/SCTP.rst
 */
static int selinux_socket_connect_helper(struct socket *sock,
					 struct sockaddr *address, int addrlen)
{
	struct sock *sk = sock->sk;
	struct sk_security_struct *sksec = sk->sk_security;
	int err;

	err = sock_has_perm(sk, SOCKET__CONNECT);
	if (err)
		return err;
	if (addrlen < offsetofend(struct sockaddr, sa_family))
		return -EINVAL;

	/* connect(AF_UNSPEC) has special handling, as it is a documented
	 * way to disconnect the socket
	 */
	if (address->sa_family == AF_UNSPEC)
		return 0;

	/*
	 * If a TCP, DCCP or SCTP socket, check name_connect permission
	 * for the port.
	 */
	if (sksec->sclass == SECCLASS_TCP_SOCKET ||
	    sksec->sclass == SECCLASS_DCCP_SOCKET ||
	    sksec->sclass == SECCLASS_SCTP_SOCKET) {
		struct common_audit_data ad;
		struct lsm_network_audit net = {0,};
		struct sockaddr_in *addr4 = NULL;
		struct sockaddr_in6 *addr6 = NULL;
		unsigned short snum;
		u32 sid, perm;

		/* sctp_connectx(3) calls via selinux_sctp_bind_connect()
		 * that validates multiple connect addresses. Because of this
		 * need to check address->sa_family as it is possible to have
		 * sk->sk_family = PF_INET6 with addr->sa_family = AF_INET.
		 */
		switch (address->sa_family) {
		case AF_INET:
			addr4 = (struct sockaddr_in *)address;
			if (addrlen < sizeof(struct sockaddr_in))
				return -EINVAL;
			snum = ntohs(addr4->sin_port);
			break;
		case AF_INET6:
			addr6 = (struct sockaddr_in6 *)address;
			if (addrlen < SIN6_LEN_RFC2133)
				return -EINVAL;
			snum = ntohs(addr6->sin6_port);
			break;
		default:
			/* Note that SCTP services expect -EINVAL, whereas
			 * others expect -EAFNOSUPPORT.
			 */
			if (sksec->sclass == SECCLASS_SCTP_SOCKET)
				return -EINVAL;
			else
				return -EAFNOSUPPORT;
		}

		err = sel_netport_sid(sk->sk_protocol, snum, &sid);
		if (err)
			return err;

		switch (sksec->sclass) {
		case SECCLASS_TCP_SOCKET:
			perm = TCP_SOCKET__NAME_CONNECT;
			break;
		case SECCLASS_DCCP_SOCKET:
			perm = DCCP_SOCKET__NAME_CONNECT;
			break;
		case SECCLASS_SCTP_SOCKET:
			perm = SCTP_SOCKET__NAME_CONNECT;
			break;
		}

		ad.type = LSM_AUDIT_DATA_NET;
		ad.u.net = &net;
		ad.u.net->dport = htons(snum);
		ad.u.net->family = address->sa_family;
		err = avc_has_perm(&selinux_state,
				   sksec->sid, sid, sksec->sclass, perm, &ad);
		if (err)
			return err;
	}

	return 0;
}

/* Supports connect(2), see comments in selinux_socket_connect_helper() */
static int selinux_socket_connect(struct socket *sock,
				  struct sockaddr *address, int addrlen)
{
	int err;
	struct sock *sk = sock->sk;

	err = selinux_socket_connect_helper(sock, address, addrlen);
	if (err)
		return err;

	return selinux_netlbl_socket_connect(sk, address);
}

static int selinux_socket_listen(struct socket *sock, int backlog)
{
	return sock_has_perm(sock->sk, SOCKET__LISTEN);
}

static int selinux_socket_accept(struct socket *sock, struct socket *newsock)
{
	int err;
	struct inode_security_struct *isec;
	struct inode_security_struct *newisec;
	u16 sclass;
	u32 sid;

	err = sock_has_perm(sock->sk, SOCKET__ACCEPT);
	if (err)
		return err;

	isec = inode_security_novalidate(SOCK_INODE(sock));
	spin_lock(&isec->lock);
	sclass = isec->sclass;
	sid = isec->sid;
	spin_unlock(&isec->lock);

	newisec = inode_security_novalidate(SOCK_INODE(newsock));
	newisec->sclass = sclass;
	newisec->sid = sid;
	newisec->initialized = LABEL_INITIALIZED;

	return 0;
}

static int selinux_socket_sendmsg(struct socket *sock, struct msghdr *msg,
				  int size)
{
	return sock_has_perm(sock->sk, SOCKET__WRITE);
}

static int selinux_socket_recvmsg(struct socket *sock, struct msghdr *msg,
				  int size, int flags)
{
	return sock_has_perm(sock->sk, SOCKET__READ);
}

static int selinux_socket_getsockname(struct socket *sock)
{
	return sock_has_perm(sock->sk, SOCKET__GETATTR);
}

static int selinux_socket_getpeername(struct socket *sock)
{
	return sock_has_perm(sock->sk, SOCKET__GETATTR);
}

static int selinux_socket_setsockopt(struct socket *sock, int level, int optname)
{
	int err;

	err = sock_has_perm(sock->sk, SOCKET__SETOPT);
	if (err)
		return err;

	return selinux_netlbl_socket_setsockopt(sock, level, optname);
}

static int selinux_socket_getsockopt(struct socket *sock, int level,
				     int optname)
{
	return sock_has_perm(sock->sk, SOCKET__GETOPT);
}

static int selinux_socket_shutdown(struct socket *sock, int how)
{
	return sock_has_perm(sock->sk, SOCKET__SHUTDOWN);
}

static int selinux_socket_unix_stream_connect(struct sock *sock,
					      struct sock *other,
					      struct sock *newsk)
{
	struct sk_security_struct *sksec_sock = sock->sk_security;
	struct sk_security_struct *sksec_other = other->sk_security;
	struct sk_security_struct *sksec_new = newsk->sk_security;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	int err;

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->sk = other;

	err = avc_has_perm(&selinux_state,
			   sksec_sock->sid, sksec_other->sid,
			   sksec_other->sclass,
			   UNIX_STREAM_SOCKET__CONNECTTO, &ad);
	if (err)
		return err;

	/* server child socket */
	sksec_new->peer_sid = sksec_sock->sid;
	err = security_sid_mls_copy(&selinux_state, sksec_other->sid,
				    sksec_sock->sid, &sksec_new->sid);
	if (err)
		return err;

	/* connecting socket */
	sksec_sock->peer_sid = sksec_new->sid;

	return 0;
}

static int selinux_socket_unix_may_send(struct socket *sock,
					struct socket *other)
{
	struct sk_security_struct *ssec = sock->sk->sk_security;
	struct sk_security_struct *osec = other->sk->sk_security;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->sk = other->sk;

	return avc_has_perm(&selinux_state,
			    ssec->sid, osec->sid, osec->sclass, SOCKET__SENDTO,
			    &ad);
}

static int selinux_inet_sys_rcv_skb(struct net *ns, int ifindex,
				    char *addrp, u16 family, u32 peer_sid,
				    struct common_audit_data *ad)
{
	int err;
	u32 if_sid;
	u32 node_sid;

	err = sel_netif_sid(ns, ifindex, &if_sid);
	if (err)
		return err;
	err = avc_has_perm(&selinux_state,
			   peer_sid, if_sid,
			   SECCLASS_NETIF, NETIF__INGRESS, ad);
	if (err)
		return err;

	err = sel_netnode_sid(addrp, family, &node_sid);
	if (err)
		return err;
	return avc_has_perm(&selinux_state,
			    peer_sid, node_sid,
			    SECCLASS_NODE, NODE__RECVFROM, ad);
}

static int selinux_sock_rcv_skb_compat(struct sock *sk, struct sk_buff *skb,
				       u16 family)
{
	int err = 0;
	struct sk_security_struct *sksec = sk->sk_security;
	u32 sk_sid = sksec->sid;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	char *addrp;

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->netif = skb->skb_iif;
	ad.u.net->family = family;
	err = selinux_parse_skb(skb, &ad, &addrp, 1, NULL);
	if (err)
		return err;

	if (selinux_secmark_enabled()) {
		err = avc_has_perm(&selinux_state,
				   sk_sid, skb->secmark, SECCLASS_PACKET,
				   PACKET__RECV, &ad);
		if (err)
			return err;
	}

	err = selinux_netlbl_sock_rcv_skb(sksec, skb, family, &ad);
	if (err)
		return err;
	err = selinux_xfrm_sock_rcv_skb(sksec->sid, skb, &ad);

	return err;
}

static int selinux_socket_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	int err;
	struct sk_security_struct *sksec = sk->sk_security;
	u16 family = sk->sk_family;
	u32 sk_sid = sksec->sid;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	char *addrp;
	u8 secmark_active;
	u8 peerlbl_active;

	if (family != PF_INET && family != PF_INET6)
		return 0;

	/* Handle mapped IPv4 packets arriving via IPv6 sockets */
	if (family == PF_INET6 && skb->protocol == htons(ETH_P_IP))
		family = PF_INET;

	/* If any sort of compatibility mode is enabled then handoff processing
	 * to the selinux_sock_rcv_skb_compat() function to deal with the
	 * special handling.  We do this in an attempt to keep this function
	 * as fast and as clean as possible. */
	if (!selinux_policycap_netpeer())
		return selinux_sock_rcv_skb_compat(sk, skb, family);

	secmark_active = selinux_secmark_enabled();
	peerlbl_active = selinux_peerlbl_enabled();
	if (!secmark_active && !peerlbl_active)
		return 0;

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->netif = skb->skb_iif;
	ad.u.net->family = family;
	err = selinux_parse_skb(skb, &ad, &addrp, 1, NULL);
	if (err)
		return err;

	if (peerlbl_active) {
		u32 peer_sid;

		err = selinux_skb_peerlbl_sid(skb, family, &peer_sid);
		if (err)
			return err;
		err = selinux_inet_sys_rcv_skb(sock_net(sk), skb->skb_iif,
					       addrp, family, peer_sid, &ad);
		if (err) {
			selinux_netlbl_err(skb, family, err, 0);
			return err;
		}
		err = avc_has_perm(&selinux_state,
				   sk_sid, peer_sid, SECCLASS_PEER,
				   PEER__RECV, &ad);
		if (err) {
			selinux_netlbl_err(skb, family, err, 0);
			return err;
		}
	}

	if (secmark_active) {
		err = avc_has_perm(&selinux_state,
				   sk_sid, skb->secmark, SECCLASS_PACKET,
				   PACKET__RECV, &ad);
		if (err)
			return err;
	}

	return err;
}

static int selinux_socket_getpeersec_stream(struct socket *sock, char __user *optval,
					    int __user *optlen, unsigned len)
{
	int err = 0;
	char *scontext;
	u32 scontext_len;
	struct sk_security_struct *sksec = sock->sk->sk_security;
	u32 peer_sid = SECSID_NULL;

	if (sksec->sclass == SECCLASS_UNIX_STREAM_SOCKET ||
	    sksec->sclass == SECCLASS_TCP_SOCKET ||
	    sksec->sclass == SECCLASS_SCTP_SOCKET)
		peer_sid = sksec->peer_sid;
	if (peer_sid == SECSID_NULL)
		return -ENOPROTOOPT;

	err = security_sid_to_context(&selinux_state, peer_sid, &scontext,
				      &scontext_len);
	if (err)
		return err;

	if (scontext_len > len) {
		err = -ERANGE;
		goto out_len;
	}

	if (copy_to_user(optval, scontext, scontext_len))
		err = -EFAULT;

out_len:
	if (put_user(scontext_len, optlen))
		err = -EFAULT;
	kfree(scontext);
	return err;
}

static int selinux_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	u32 peer_secid = SECSID_NULL;
	u16 family;
	struct inode_security_struct *isec;

	if (skb && skb->protocol == htons(ETH_P_IP))
		family = PF_INET;
	else if (skb && skb->protocol == htons(ETH_P_IPV6))
		family = PF_INET6;
	else if (sock)
		family = sock->sk->sk_family;
	else
		goto out;

	if (sock && family == PF_UNIX) {
		isec = inode_security_novalidate(SOCK_INODE(sock));
		peer_secid = isec->sid;
	} else if (skb)
		selinux_skb_peerlbl_sid(skb, family, &peer_secid);

out:
	*secid = peer_secid;
	if (peer_secid == SECSID_NULL)
		return -EINVAL;
	return 0;
}

static int selinux_sk_alloc_security(struct sock *sk, int family, gfp_t priority)
{
	struct sk_security_struct *sksec;

	sksec = kzalloc(sizeof(*sksec), priority);
	if (!sksec)
		return -ENOMEM;

	sksec->peer_sid = SECINITSID_UNLABELED;
	sksec->sid = SECINITSID_UNLABELED;
	sksec->sclass = SECCLASS_SOCKET;
	selinux_netlbl_sk_security_reset(sksec);
	sk->sk_security = sksec;

	return 0;
}

static void selinux_sk_free_security(struct sock *sk)
{
	struct sk_security_struct *sksec = sk->sk_security;

	sk->sk_security = NULL;
	selinux_netlbl_sk_security_free(sksec);
	kfree(sksec);
}

static void selinux_sk_clone_security(const struct sock *sk, struct sock *newsk)
{
	struct sk_security_struct *sksec = sk->sk_security;
	struct sk_security_struct *newsksec = newsk->sk_security;

	newsksec->sid = sksec->sid;
	newsksec->peer_sid = sksec->peer_sid;
	newsksec->sclass = sksec->sclass;

	selinux_netlbl_sk_security_reset(newsksec);
}

static void selinux_sk_getsecid(struct sock *sk, u32 *secid)
{
	if (!sk)
		*secid = SECINITSID_ANY_SOCKET;
	else {
		struct sk_security_struct *sksec = sk->sk_security;

		*secid = sksec->sid;
	}
}

static void selinux_sock_graft(struct sock *sk, struct socket *parent)
{
	struct inode_security_struct *isec =
		inode_security_novalidate(SOCK_INODE(parent));
	struct sk_security_struct *sksec = sk->sk_security;

	if (sk->sk_family == PF_INET || sk->sk_family == PF_INET6 ||
	    sk->sk_family == PF_UNIX)
		isec->sid = sksec->sid;
	sksec->sclass = isec->sclass;
}

/* Called whenever SCTP receives an INIT chunk. This happens when an incoming
 * connect(2), sctp_connectx(3) or sctp_sendmsg(3) (with no association
 * already present).
 */
static int selinux_sctp_assoc_request(struct sctp_endpoint *ep,
				      struct sk_buff *skb)
{
	struct sk_security_struct *sksec = ep->base.sk->sk_security;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	u8 peerlbl_active;
	u32 peer_sid = SECINITSID_UNLABELED;
	u32 conn_sid;
	int err = 0;

	if (!selinux_policycap_extsockclass())
		return 0;

	peerlbl_active = selinux_peerlbl_enabled();

	if (peerlbl_active) {
		/* This will return peer_sid = SECSID_NULL if there are
		 * no peer labels, see security_net_peersid_resolve().
		 */
		err = selinux_skb_peerlbl_sid(skb, ep->base.sk->sk_family,
					      &peer_sid);
		if (err)
			return err;

		if (peer_sid == SECSID_NULL)
			peer_sid = SECINITSID_UNLABELED;
	}

	if (sksec->sctp_assoc_state == SCTP_ASSOC_UNSET) {
		sksec->sctp_assoc_state = SCTP_ASSOC_SET;

		/* Here as first association on socket. As the peer SID
		 * was allowed by peer recv (and the netif/node checks),
		 * then it is approved by policy and used as the primary
		 * peer SID for getpeercon(3).
		 */
		sksec->peer_sid = peer_sid;
	} else if  (sksec->peer_sid != peer_sid) {
		/* Other association peer SIDs are checked to enforce
		 * consistency among the peer SIDs.
		 */
		ad.type = LSM_AUDIT_DATA_NET;
		ad.u.net = &net;
		ad.u.net->sk = ep->base.sk;
		err = avc_has_perm(&selinux_state,
				   sksec->peer_sid, peer_sid, sksec->sclass,
				   SCTP_SOCKET__ASSOCIATION, &ad);
		if (err)
			return err;
	}

	/* Compute the MLS component for the connection and store
	 * the information in ep. This will be used by SCTP TCP type
	 * sockets and peeled off connections as they cause a new
	 * socket to be generated. selinux_sctp_sk_clone() will then
	 * plug this into the new socket.
	 */
	err = selinux_conn_sid(sksec->sid, peer_sid, &conn_sid);
	if (err)
		return err;

	ep->secid = conn_sid;
	ep->peer_secid = peer_sid;

	/* Set any NetLabel labels including CIPSO/CALIPSO options. */
	return selinux_netlbl_sctp_assoc_request(ep, skb);
}

/* Check if sctp IPv4/IPv6 addresses are valid for binding or connecting
 * based on their @optname.
 */
static int selinux_sctp_bind_connect(struct sock *sk, int optname,
				     struct sockaddr *address,
				     int addrlen)
{
	int len, err = 0, walk_size = 0;
	void *addr_buf;
	struct sockaddr *addr;
	struct socket *sock;

	if (!selinux_policycap_extsockclass())
		return 0;

	/* Process one or more addresses that may be IPv4 or IPv6 */
	sock = sk->sk_socket;
	addr_buf = address;

	while (walk_size < addrlen) {
		if (walk_size + sizeof(sa_family_t) > addrlen)
			return -EINVAL;

		addr = addr_buf;
		switch (addr->sa_family) {
		case AF_UNSPEC:
		case AF_INET:
			len = sizeof(struct sockaddr_in);
			break;
		case AF_INET6:
			len = sizeof(struct sockaddr_in6);
			break;
		default:
			return -EINVAL;
		}

		if (walk_size + len > addrlen)
			return -EINVAL;

		err = -EINVAL;
		switch (optname) {
		/* Bind checks */
		case SCTP_PRIMARY_ADDR:
		case SCTP_SET_PEER_PRIMARY_ADDR:
		case SCTP_SOCKOPT_BINDX_ADD:
			err = selinux_socket_bind(sock, addr, len);
			break;
		/* Connect checks */
		case SCTP_SOCKOPT_CONNECTX:
		case SCTP_PARAM_SET_PRIMARY:
		case SCTP_PARAM_ADD_IP:
		case SCTP_SENDMSG_CONNECT:
			err = selinux_socket_connect_helper(sock, addr, len);
			if (err)
				return err;

			/* As selinux_sctp_bind_connect() is called by the
			 * SCTP protocol layer, the socket is already locked,
			 * therefore selinux_netlbl_socket_connect_locked()
			 * is called here. The situations handled are:
			 * sctp_connectx(3), sctp_sendmsg(3), sendmsg(2),
			 * whenever a new IP address is added or when a new
			 * primary address is selected.
			 * Note that an SCTP connect(2) call happens before
			 * the SCTP protocol layer and is handled via
			 * selinux_socket_connect().
			 */
			err = selinux_netlbl_socket_connect_locked(sk, addr);
			break;
		}

		if (err)
			return err;

		addr_buf += len;
		walk_size += len;
	}

	return 0;
}

/* Called whenever a new socket is created by accept(2) or sctp_peeloff(3). */
static void selinux_sctp_sk_clone(struct sctp_endpoint *ep, struct sock *sk,
				  struct sock *newsk)
{
	struct sk_security_struct *sksec = sk->sk_security;
	struct sk_security_struct *newsksec = newsk->sk_security;

	/* If policy does not support SECCLASS_SCTP_SOCKET then call
	 * the non-sctp clone version.
	 */
	if (!selinux_policycap_extsockclass())
		return selinux_sk_clone_security(sk, newsk);

	newsksec->sid = ep->secid;
	newsksec->peer_sid = ep->peer_secid;
	newsksec->sclass = sksec->sclass;
	selinux_netlbl_sctp_sk_clone(sk, newsk);
}

static int selinux_inet_conn_request(const struct sock *sk, struct sk_buff *skb,
				     struct request_sock *req)
{
	struct sk_security_struct *sksec = sk->sk_security;
	int err;
	u16 family = req->rsk_ops->family;
	u32 connsid;
	u32 peersid;

	err = selinux_skb_peerlbl_sid(skb, family, &peersid);
	if (err)
		return err;
	err = selinux_conn_sid(sksec->sid, peersid, &connsid);
	if (err)
		return err;
	req->secid = connsid;
	req->peer_secid = peersid;

	return selinux_netlbl_inet_conn_request(req, family);
}

static void selinux_inet_csk_clone(struct sock *newsk,
				   const struct request_sock *req)
{
	struct sk_security_struct *newsksec = newsk->sk_security;

	newsksec->sid = req->secid;
	newsksec->peer_sid = req->peer_secid;
	/* NOTE: Ideally, we should also get the isec->sid for the
	   new socket in sync, but we don't have the isec available yet.
	   So we will wait until sock_graft to do it, by which
	   time it will have been created and available. */

	/* We don't need to take any sort of lock here as we are the only
	 * thread with access to newsksec */
	selinux_netlbl_inet_csk_clone(newsk, req->rsk_ops->family);
}

static void selinux_inet_conn_established(struct sock *sk, struct sk_buff *skb)
{
	u16 family = sk->sk_family;
	struct sk_security_struct *sksec = sk->sk_security;

	/* handle mapped IPv4 packets arriving via IPv6 sockets */
	if (family == PF_INET6 && skb->protocol == htons(ETH_P_IP))
		family = PF_INET;

	selinux_skb_peerlbl_sid(skb, family, &sksec->peer_sid);
}

static int selinux_secmark_relabel_packet(u32 sid)
{
	const struct task_security_struct *__tsec;
	u32 tsid;

	__tsec = selinux_cred(current_cred());
	tsid = __tsec->sid;

	return avc_has_perm(&selinux_state,
			    tsid, sid, SECCLASS_PACKET, PACKET__RELABELTO,
			    NULL);
}

static void selinux_secmark_refcount_inc(void)
{
	atomic_inc(&selinux_secmark_refcount);
}

static void selinux_secmark_refcount_dec(void)
{
	atomic_dec(&selinux_secmark_refcount);
}

static void selinux_req_classify_flow(const struct request_sock *req,
				      struct flowi_common *flic)
{
	flic->flowic_secid = req->secid;
}

static int selinux_tun_dev_alloc_security(void **security)
{
	struct tun_security_struct *tunsec;

	tunsec = kzalloc(sizeof(*tunsec), GFP_KERNEL);
	if (!tunsec)
		return -ENOMEM;
	tunsec->sid = current_sid();

	*security = tunsec;
	return 0;
}

static void selinux_tun_dev_free_security(void *security)
{
	kfree(security);
}

static int selinux_tun_dev_create(void)
{
	u32 sid = current_sid();

	/* we aren't taking into account the "sockcreate" SID since the socket
	 * that is being created here is not a socket in the traditional sense,
	 * instead it is a private sock, accessible only to the kernel, and
	 * representing a wide range of network traffic spanning multiple
	 * connections unlike traditional sockets - check the TUN driver to
	 * get a better understanding of why this socket is special */

	return avc_has_perm(&selinux_state,
			    sid, sid, SECCLASS_TUN_SOCKET, TUN_SOCKET__CREATE,
			    NULL);
}

static int selinux_tun_dev_attach_queue(void *security)
{
	struct tun_security_struct *tunsec = security;

	return avc_has_perm(&selinux_state,
			    current_sid(), tunsec->sid, SECCLASS_TUN_SOCKET,
			    TUN_SOCKET__ATTACH_QUEUE, NULL);
}

static int selinux_tun_dev_attach(struct sock *sk, void *security)
{
	struct tun_security_struct *tunsec = security;
	struct sk_security_struct *sksec = sk->sk_security;

	/* we don't currently perform any NetLabel based labeling here and it
	 * isn't clear that we would want to do so anyway; while we could apply
	 * labeling without the support of the TUN user the resulting labeled
	 * traffic from the other end of the connection would almost certainly
	 * cause confusion to the TUN user that had no idea network labeling
	 * protocols were being used */

	sksec->sid = tunsec->sid;
	sksec->sclass = SECCLASS_TUN_SOCKET;

	return 0;
}

static int selinux_tun_dev_open(void *security)
{
	struct tun_security_struct *tunsec = security;
	u32 sid = current_sid();
	int err;

	err = avc_has_perm(&selinux_state,
			   sid, tunsec->sid, SECCLASS_TUN_SOCKET,
			   TUN_SOCKET__RELABELFROM, NULL);
	if (err)
		return err;
	err = avc_has_perm(&selinux_state,
			   sid, sid, SECCLASS_TUN_SOCKET,
			   TUN_SOCKET__RELABELTO, NULL);
	if (err)
		return err;
	tunsec->sid = sid;

	return 0;
}

#ifdef CONFIG_NETFILTER

static unsigned int selinux_ip_forward(struct sk_buff *skb,
				       const struct net_device *indev,
				       u16 family)
{
	int err;
	char *addrp;
	u32 peer_sid;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	u8 secmark_active;
	u8 netlbl_active;
	u8 peerlbl_active;

	if (!selinux_policycap_netpeer())
		return NF_ACCEPT;

	secmark_active = selinux_secmark_enabled();
	netlbl_active = netlbl_enabled();
	peerlbl_active = selinux_peerlbl_enabled();
	if (!secmark_active && !peerlbl_active)
		return NF_ACCEPT;

	if (selinux_skb_peerlbl_sid(skb, family, &peer_sid) != 0)
		return NF_DROP;

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->netif = indev->ifindex;
	ad.u.net->family = family;
	if (selinux_parse_skb(skb, &ad, &addrp, 1, NULL) != 0)
		return NF_DROP;

	if (peerlbl_active) {
		err = selinux_inet_sys_rcv_skb(dev_net(indev), indev->ifindex,
					       addrp, family, peer_sid, &ad);
		if (err) {
			selinux_netlbl_err(skb, family, err, 1);
			return NF_DROP;
		}
	}

	if (secmark_active)
		if (avc_has_perm(&selinux_state,
				 peer_sid, skb->secmark,
				 SECCLASS_PACKET, PACKET__FORWARD_IN, &ad))
			return NF_DROP;

	if (netlbl_active)
		/* we do this in the FORWARD path and not the POST_ROUTING
		 * path because we want to make sure we apply the necessary
		 * labeling before IPsec is applied so we can leverage AH
		 * protection */
		if (selinux_netlbl_skbuff_setsid(skb, family, peer_sid) != 0)
			return NF_DROP;

	return NF_ACCEPT;
}

static unsigned int selinux_ipv4_forward(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	return selinux_ip_forward(skb, state->in, PF_INET);
}

#if IS_ENABLED(CONFIG_IPV6)
static unsigned int selinux_ipv6_forward(void *priv,
					 struct sk_buff *skb,
					 const struct nf_hook_state *state)
{
	return selinux_ip_forward(skb, state->in, PF_INET6);
}
#endif	/* IPV6 */

static unsigned int selinux_ip_output(struct sk_buff *skb,
				      u16 family)
{
	struct sock *sk;
	u32 sid;

	if (!netlbl_enabled())
		return NF_ACCEPT;

	/* we do this in the LOCAL_OUT path and not the POST_ROUTING path
	 * because we want to make sure we apply the necessary labeling
	 * before IPsec is applied so we can leverage AH protection */
	sk = skb->sk;
	if (sk) {
		struct sk_security_struct *sksec;

		if (sk_listener(sk))
			/* if the socket is the listening state then this
			 * packet is a SYN-ACK packet which means it needs to
			 * be labeled based on the connection/request_sock and
			 * not the parent socket.  unfortunately, we can't
			 * lookup the request_sock yet as it isn't queued on
			 * the parent socket until after the SYN-ACK is sent.
			 * the "solution" is to simply pass the packet as-is
			 * as any IP option based labeling should be copied
			 * from the initial connection request (in the IP
			 * layer).  it is far from ideal, but until we get a
			 * security label in the packet itself this is the
			 * best we can do. */
			return NF_ACCEPT;

		/* standard practice, label using the parent socket */
		sksec = sk->sk_security;
		sid = sksec->sid;
	} else
		sid = SECINITSID_KERNEL;
	if (selinux_netlbl_skbuff_setsid(skb, family, sid) != 0)
		return NF_DROP;

	return NF_ACCEPT;
}

static unsigned int selinux_ipv4_output(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	return selinux_ip_output(skb, PF_INET);
}

#if IS_ENABLED(CONFIG_IPV6)
static unsigned int selinux_ipv6_output(void *priv,
					struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	return selinux_ip_output(skb, PF_INET6);
}
#endif	/* IPV6 */

static unsigned int selinux_ip_postroute_compat(struct sk_buff *skb,
						int ifindex,
						u16 family)
{
	struct sock *sk = skb_to_full_sk(skb);
	struct sk_security_struct *sksec;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	char *addrp;
	u8 proto;

	if (sk == NULL)
		return NF_ACCEPT;
	sksec = sk->sk_security;

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->netif = ifindex;
	ad.u.net->family = family;
	if (selinux_parse_skb(skb, &ad, &addrp, 0, &proto))
		return NF_DROP;

	if (selinux_secmark_enabled())
		if (avc_has_perm(&selinux_state,
				 sksec->sid, skb->secmark,
				 SECCLASS_PACKET, PACKET__SEND, &ad))
			return NF_DROP_ERR(-ECONNREFUSED);

	if (selinux_xfrm_postroute_last(sksec->sid, skb, &ad, proto))
		return NF_DROP_ERR(-ECONNREFUSED);

	return NF_ACCEPT;
}

static unsigned int selinux_ip_postroute(struct sk_buff *skb,
					 const struct net_device *outdev,
					 u16 family)
{
	u32 secmark_perm;
	u32 peer_sid;
	int ifindex = outdev->ifindex;
	struct sock *sk;
	struct common_audit_data ad;
	struct lsm_network_audit net = {0,};
	char *addrp;
	u8 secmark_active;
	u8 peerlbl_active;

	/* If any sort of compatibility mode is enabled then handoff processing
	 * to the selinux_ip_postroute_compat() function to deal with the
	 * special handling.  We do this in an attempt to keep this function
	 * as fast and as clean as possible. */
	if (!selinux_policycap_netpeer())
		return selinux_ip_postroute_compat(skb, ifindex, family);

	secmark_active = selinux_secmark_enabled();
	peerlbl_active = selinux_peerlbl_enabled();
	if (!secmark_active && !peerlbl_active)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);

#ifdef CONFIG_XFRM
	/* If skb->dst->xfrm is non-NULL then the packet is undergoing an IPsec
	 * packet transformation so allow the packet to pass without any checks
	 * since we'll have another chance to perform access control checks
	 * when the packet is on it's final way out.
	 * NOTE: there appear to be some IPv6 multicast cases where skb->dst
	 *       is NULL, in this case go ahead and apply access control.
	 * NOTE: if this is a local socket (skb->sk != NULL) that is in the
	 *       TCP listening state we cannot wait until the XFRM processing
	 *       is done as we will miss out on the SA label if we do;
	 *       unfortunately, this means more work, but it is only once per
	 *       connection. */
	if (skb_dst(skb) != NULL && skb_dst(skb)->xfrm != NULL &&
	    !(sk && sk_listener(sk)))
		return NF_ACCEPT;
#endif

	if (sk == NULL) {
		/* Without an associated socket the packet is either coming
		 * from the kernel or it is being forwarded; check the packet
		 * to determine which and if the packet is being forwarded
		 * query the packet directly to determine the security label. */
		if (skb->skb_iif) {
			secmark_perm = PACKET__FORWARD_OUT;
			if (selinux_skb_peerlbl_sid(skb, family, &peer_sid))
				return NF_DROP;
		} else {
			secmark_perm = PACKET__SEND;
			peer_sid = SECINITSID_KERNEL;
		}
	} else if (sk_listener(sk)) {
		/* Locally generated packet but the associated socket is in the
		 * listening state which means this is a SYN-ACK packet.  In
		 * this particular case the correct security label is assigned
		 * to the connection/request_sock but unfortunately we can't
		 * query the request_sock as it isn't queued on the parent
		 * socket until after the SYN-ACK packet is sent; the only
		 * viable choice is to regenerate the label like we do in
		 * selinux_inet_conn_request().  See also selinux_ip_output()
		 * for similar problems. */
		u32 skb_sid;
		struct sk_security_struct *sksec;

		sksec = sk->sk_security;
		if (selinux_skb_peerlbl_sid(skb, family, &skb_sid))
			return NF_DROP;
		/* At this point, if the returned skb peerlbl is SECSID_NULL
		 * and the packet has been through at least one XFRM
		 * transformation then we must be dealing with the "final"
		 * form of labeled IPsec packet; since we've already applied
		 * all of our access controls on this packet we can safely
		 * pass the packet. */
		if (skb_sid == SECSID_NULL) {
			switch (family) {
			case PF_INET:
				if (IPCB(skb)->flags & IPSKB_XFRM_TRANSFORMED)
					return NF_ACCEPT;
				break;
			case PF_INET6:
				if (IP6CB(skb)->flags & IP6SKB_XFRM_TRANSFORMED)
					return NF_ACCEPT;
				break;
			default:
				return NF_DROP_ERR(-ECONNREFUSED);
			}
		}
		if (selinux_conn_sid(sksec->sid, skb_sid, &peer_sid))
			return NF_DROP;
		secmark_perm = PACKET__SEND;
	} else {
		/* Locally generated packet, fetch the security label from the
		 * associated socket. */
		struct sk_security_struct *sksec = sk->sk_security;
		peer_sid = sksec->sid;
		secmark_perm = PACKET__SEND;
	}

	ad.type = LSM_AUDIT_DATA_NET;
	ad.u.net = &net;
	ad.u.net->netif = ifindex;
	ad.u.net->family = family;
	if (selinux_parse_skb(skb, &ad, &addrp, 0, NULL))
		return NF_DROP;

	if (secmark_active)
		if (avc_has_perm(&selinux_state,
				 peer_sid, skb->secmark,
				 SECCLASS_PACKET, secmark_perm, &ad))
			return NF_DROP_ERR(-ECONNREFUSED);

	if (peerlbl_active) {
		u32 if_sid;
		u32 node_sid;

		if (sel_netif_sid(dev_net(outdev), ifindex, &if_sid))
			return NF_DROP;
		if (avc_has_perm(&selinux_state,
				 peer_sid, if_sid,
				 SECCLASS_NETIF, NETIF__EGRESS, &ad))
			return NF_DROP_ERR(-ECONNREFUSED);

		if (sel_netnode_sid(addrp, family, &node_sid))
			return NF_DROP;
		if (avc_has_perm(&selinux_state,
				 peer_sid, node_sid,
				 SECCLASS_NODE, NODE__SENDTO, &ad))
			return NF_DROP_ERR(-ECONNREFUSED);
	}

	return NF_ACCEPT;
}

static unsigned int selinux_ipv4_postroute(void *priv,
					   struct sk_buff *skb,
					   const struct nf_hook_state *state)
{
	return selinux_ip_postroute(skb, state->out, PF_INET);
}

#if IS_ENABLED(CONFIG_IPV6)
static unsigned int selinux_ipv6_postroute(void *priv,
					   struct sk_buff *skb,
					   const struct nf_hook_state *state)
{
	return selinux_ip_postroute(skb, state->out, PF_INET6);
}
#endif	/* IPV6 */

#endif	/* CONFIG_NETFILTER */

static int selinux_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	int rc = 0;
	unsigned int msg_len;
	unsigned int data_len = skb->len;
	unsigned char *data = skb->data;
	struct nlmsghdr *nlh;
	struct sk_security_struct *sksec = sk->sk_security;
	u16 sclass = sksec->sclass;
	u32 perm;

	while (data_len >= nlmsg_total_size(0)) {
		nlh = (struct nlmsghdr *)data;

		/* NOTE: the nlmsg_len field isn't reliably set by some netlink
		 *       users which means we can't reject skb's with bogus
		 *       length fields; our solution is to follow what
		 *       netlink_rcv_skb() does and simply skip processing at
		 *       messages with length fields that are clearly junk
		 */
		if (nlh->nlmsg_len < NLMSG_HDRLEN || nlh->nlmsg_len > data_len)
			return 0;

		rc = selinux_nlmsg_lookup(sclass, nlh->nlmsg_type, &perm);
		if (rc == 0) {
			rc = sock_has_perm(sk, perm);
			if (rc)
				return rc;
		} else if (rc == -EINVAL) {
			/* -EINVAL is a missing msg/perm mapping */
			pr_warn_ratelimited("SELinux: unrecognized netlink"
				" message: protocol=%hu nlmsg_type=%hu sclass=%s"
				" pid=%d comm=%s\n",
				sk->sk_protocol, nlh->nlmsg_type,
				secclass_map[sclass - 1].name,
				task_pid_nr(current), current->comm);
			if (enforcing_enabled(&selinux_state) &&
			    !security_get_allow_unknown(&selinux_state))
				return rc;
			rc = 0;
		} else if (rc == -ENOENT) {
			/* -ENOENT is a missing socket/class mapping, ignore */
			rc = 0;
		} else {
			return rc;
		}

		/* move to the next message after applying netlink padding */
		msg_len = NLMSG_ALIGN(nlh->nlmsg_len);
		if (msg_len >= data_len)
			return 0;
		data_len -= msg_len;
		data += msg_len;
	}

	return rc;
}

static void ipc_init_security(struct ipc_security_struct *isec, u16 sclass)
{
	isec->sclass = sclass;
	isec->sid = current_sid();
}

static int ipc_has_perm(struct kern_ipc_perm *ipc_perms,
			u32 perms)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();

	isec = selinux_ipc(ipc_perms);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = ipc_perms->key;

	return avc_has_perm(&selinux_state,
			    sid, isec->sid, isec->sclass, perms, &ad);
}

static int selinux_msg_msg_alloc_security(struct msg_msg *msg)
{
	struct msg_security_struct *msec;

	msec = selinux_msg_msg(msg);
	msec->sid = SECINITSID_UNLABELED;

	return 0;
}

/* message queue security operations */
static int selinux_msg_queue_alloc_security(struct kern_ipc_perm *msq)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();
	int rc;

	isec = selinux_ipc(msq);
	ipc_init_security(isec, SECCLASS_MSGQ);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = msq->key;

	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid, SECCLASS_MSGQ,
			  MSGQ__CREATE, &ad);
	return rc;
}

static int selinux_msg_queue_associate(struct kern_ipc_perm *msq, int msqflg)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();

	isec = selinux_ipc(msq);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = msq->key;

	return avc_has_perm(&selinux_state,
			    sid, isec->sid, SECCLASS_MSGQ,
			    MSGQ__ASSOCIATE, &ad);
}

static int selinux_msg_queue_msgctl(struct kern_ipc_perm *msq, int cmd)
{
	int err;
	int perms;

	switch (cmd) {
	case IPC_INFO:
	case MSG_INFO:
		/* No specific object, just general system-wide information. */
		return avc_has_perm(&selinux_state,
				    current_sid(), SECINITSID_KERNEL,
				    SECCLASS_SYSTEM, SYSTEM__IPC_INFO, NULL);
	case IPC_STAT:
	case MSG_STAT:
	case MSG_STAT_ANY:
		perms = MSGQ__GETATTR | MSGQ__ASSOCIATE;
		break;
	case IPC_SET:
		perms = MSGQ__SETATTR;
		break;
	case IPC_RMID:
		perms = MSGQ__DESTROY;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(msq, perms);
	return err;
}

static int selinux_msg_queue_msgsnd(struct kern_ipc_perm *msq, struct msg_msg *msg, int msqflg)
{
	struct ipc_security_struct *isec;
	struct msg_security_struct *msec;
	struct common_audit_data ad;
	u32 sid = current_sid();
	int rc;

	isec = selinux_ipc(msq);
	msec = selinux_msg_msg(msg);

	/*
	 * First time through, need to assign label to the message
	 */
	if (msec->sid == SECINITSID_UNLABELED) {
		/*
		 * Compute new sid based on current process and
		 * message queue this message will be stored in
		 */
		rc = security_transition_sid(&selinux_state, sid, isec->sid,
					     SECCLASS_MSG, NULL, &msec->sid);
		if (rc)
			return rc;
	}

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = msq->key;

	/* Can this process write to the queue? */
	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid, SECCLASS_MSGQ,
			  MSGQ__WRITE, &ad);
	if (!rc)
		/* Can this process send the message */
		rc = avc_has_perm(&selinux_state,
				  sid, msec->sid, SECCLASS_MSG,
				  MSG__SEND, &ad);
	if (!rc)
		/* Can the message be put in the queue? */
		rc = avc_has_perm(&selinux_state,
				  msec->sid, isec->sid, SECCLASS_MSGQ,
				  MSGQ__ENQUEUE, &ad);

	return rc;
}

static int selinux_msg_queue_msgrcv(struct kern_ipc_perm *msq, struct msg_msg *msg,
				    struct task_struct *target,
				    long type, int mode)
{
	struct ipc_security_struct *isec;
	struct msg_security_struct *msec;
	struct common_audit_data ad;
	u32 sid = task_sid_obj(target);
	int rc;

	isec = selinux_ipc(msq);
	msec = selinux_msg_msg(msg);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = msq->key;

	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid,
			  SECCLASS_MSGQ, MSGQ__READ, &ad);
	if (!rc)
		rc = avc_has_perm(&selinux_state,
				  sid, msec->sid,
				  SECCLASS_MSG, MSG__RECEIVE, &ad);
	return rc;
}

/* Shared Memory security operations */
static int selinux_shm_alloc_security(struct kern_ipc_perm *shp)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();
	int rc;

	isec = selinux_ipc(shp);
	ipc_init_security(isec, SECCLASS_SHM);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = shp->key;

	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid, SECCLASS_SHM,
			  SHM__CREATE, &ad);
	return rc;
}

static int selinux_shm_associate(struct kern_ipc_perm *shp, int shmflg)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();

	isec = selinux_ipc(shp);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = shp->key;

	return avc_has_perm(&selinux_state,
			    sid, isec->sid, SECCLASS_SHM,
			    SHM__ASSOCIATE, &ad);
}

/* Note, at this point, shp is locked down */
static int selinux_shm_shmctl(struct kern_ipc_perm *shp, int cmd)
{
	int perms;
	int err;

	switch (cmd) {
	case IPC_INFO:
	case SHM_INFO:
		/* No specific object, just general system-wide information. */
		return avc_has_perm(&selinux_state,
				    current_sid(), SECINITSID_KERNEL,
				    SECCLASS_SYSTEM, SYSTEM__IPC_INFO, NULL);
	case IPC_STAT:
	case SHM_STAT:
	case SHM_STAT_ANY:
		perms = SHM__GETATTR | SHM__ASSOCIATE;
		break;
	case IPC_SET:
		perms = SHM__SETATTR;
		break;
	case SHM_LOCK:
	case SHM_UNLOCK:
		perms = SHM__LOCK;
		break;
	case IPC_RMID:
		perms = SHM__DESTROY;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(shp, perms);
	return err;
}

static int selinux_shm_shmat(struct kern_ipc_perm *shp,
			     char __user *shmaddr, int shmflg)
{
	u32 perms;

	if (shmflg & SHM_RDONLY)
		perms = SHM__READ;
	else
		perms = SHM__READ | SHM__WRITE;

	return ipc_has_perm(shp, perms);
}

/* Semaphore security operations */
static int selinux_sem_alloc_security(struct kern_ipc_perm *sma)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();
	int rc;

	isec = selinux_ipc(sma);
	ipc_init_security(isec, SECCLASS_SEM);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = sma->key;

	rc = avc_has_perm(&selinux_state,
			  sid, isec->sid, SECCLASS_SEM,
			  SEM__CREATE, &ad);
	return rc;
}

static int selinux_sem_associate(struct kern_ipc_perm *sma, int semflg)
{
	struct ipc_security_struct *isec;
	struct common_audit_data ad;
	u32 sid = current_sid();

	isec = selinux_ipc(sma);

	ad.type = LSM_AUDIT_DATA_IPC;
	ad.u.ipc_id = sma->key;

	return avc_has_perm(&selinux_state,
			    sid, isec->sid, SECCLASS_SEM,
			    SEM__ASSOCIATE, &ad);
}

/* Note, at this point, sma is locked down */
static int selinux_sem_semctl(struct kern_ipc_perm *sma, int cmd)
{
	int err;
	u32 perms;

	switch (cmd) {
	case IPC_INFO:
	case SEM_INFO:
		/* No specific object, just general system-wide information. */
		return avc_has_perm(&selinux_state,
				    current_sid(), SECINITSID_KERNEL,
				    SECCLASS_SYSTEM, SYSTEM__IPC_INFO, NULL);
	case GETPID:
	case GETNCNT:
	case GETZCNT:
		perms = SEM__GETATTR;
		break;
	case GETVAL:
	case GETALL:
		perms = SEM__READ;
		break;
	case SETVAL:
	case SETALL:
		perms = SEM__WRITE;
		break;
	case IPC_RMID:
		perms = SEM__DESTROY;
		break;
	case IPC_SET:
		perms = SEM__SETATTR;
		break;
	case IPC_STAT:
	case SEM_STAT:
	case SEM_STAT_ANY:
		perms = SEM__GETATTR | SEM__ASSOCIATE;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(sma, perms);
	return err;
}

static int selinux_sem_semop(struct kern_ipc_perm *sma,
			     struct sembuf *sops, unsigned nsops, int alter)
{
	u32 perms;

	if (alter)
		perms = SEM__READ | SEM__WRITE;
	else
		perms = SEM__READ;

	return ipc_has_perm(sma, perms);
}

static int selinux_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	u32 av = 0;

	av = 0;
	if (flag & S_IRUGO)
		av |= IPC__UNIX_READ;
	if (flag & S_IWUGO)
		av |= IPC__UNIX_WRITE;

	if (av == 0)
		return 0;

	return ipc_has_perm(ipcp, av);
}

static void selinux_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	struct ipc_security_struct *isec = selinux_ipc(ipcp);
	*secid = isec->sid;
}

static void selinux_d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (inode)
		inode_doinit_with_dentry(inode, dentry);
}

static int selinux_getprocattr(struct task_struct *p,
			       char *name, char **value)
{
	const struct task_security_struct *__tsec;
	u32 sid;
	int error;
	unsigned len;

	rcu_read_lock();
	__tsec = selinux_cred(__task_cred(p));

	if (current != p) {
		error = avc_has_perm(&selinux_state,
				     current_sid(), __tsec->sid,
				     SECCLASS_PROCESS, PROCESS__GETATTR, NULL);
		if (error)
			goto bad;
	}

	if (!strcmp(name, "current"))
		sid = __tsec->sid;
	else if (!strcmp(name, "prev"))
		sid = __tsec->osid;
	else if (!strcmp(name, "exec"))
		sid = __tsec->exec_sid;
	else if (!strcmp(name, "fscreate"))
		sid = __tsec->create_sid;
	else if (!strcmp(name, "keycreate"))
		sid = __tsec->keycreate_sid;
	else if (!strcmp(name, "sockcreate"))
		sid = __tsec->sockcreate_sid;
	else {
		error = -EINVAL;
		goto bad;
	}
	rcu_read_unlock();

	if (!sid)
		return 0;

	error = security_sid_to_context(&selinux_state, sid, value, &len);
	if (error)
		return error;
	return len;

bad:
	rcu_read_unlock();
	return error;
}

static int selinux_setprocattr(const char *name, void *value, size_t size)
{
	struct task_security_struct *tsec;
	struct cred *new;
	u32 mysid = current_sid(), sid = 0, ptsid;
	int error;
	char *str = value;

	/*
	 * Basic control over ability to set these attributes at all.
	 */
	if (!strcmp(name, "exec"))
		error = avc_has_perm(&selinux_state,
				     mysid, mysid, SECCLASS_PROCESS,
				     PROCESS__SETEXEC, NULL);
	else if (!strcmp(name, "fscreate"))
		error = avc_has_perm(&selinux_state,
				     mysid, mysid, SECCLASS_PROCESS,
				     PROCESS__SETFSCREATE, NULL);
	else if (!strcmp(name, "keycreate"))
		error = avc_has_perm(&selinux_state,
				     mysid, mysid, SECCLASS_PROCESS,
				     PROCESS__SETKEYCREATE, NULL);
	else if (!strcmp(name, "sockcreate"))
		error = avc_has_perm(&selinux_state,
				     mysid, mysid, SECCLASS_PROCESS,
				     PROCESS__SETSOCKCREATE, NULL);
	else if (!strcmp(name, "current"))
		error = avc_has_perm(&selinux_state,
				     mysid, mysid, SECCLASS_PROCESS,
				     PROCESS__SETCURRENT, NULL);
	else
		error = -EINVAL;
	if (error)
		return error;

	/* Obtain a SID for the context, if one was specified. */
	if (size && str[0] && str[0] != '\n') {
		if (str[size-1] == '\n') {
			str[size-1] = 0;
			size--;
		}
		error = security_context_to_sid(&selinux_state, value, size,
						&sid, GFP_KERNEL);
		if (error == -EINVAL && !strcmp(name, "fscreate")) {
			if (!has_cap_mac_admin(true)) {
				struct audit_buffer *ab;
				size_t audit_size;

				/* We strip a nul only if it is at the end, otherwise the
				 * context contains a nul and we should audit that */
				if (str[size - 1] == '\0')
					audit_size = size - 1;
				else
					audit_size = size;
				ab = audit_log_start(audit_context(),
						     GFP_ATOMIC,
						     AUDIT_SELINUX_ERR);
				if (!ab)
					return error;
				audit_log_format(ab, "op=fscreate invalid_context=");
				audit_log_n_untrustedstring(ab, value, audit_size);
				audit_log_end(ab);

				return error;
			}
			error = security_context_to_sid_force(
						      &selinux_state,
						      value, size, &sid);
		}
		if (error)
			return error;
	}

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	/* Permission checking based on the specified context is
	   performed during the actual operation (execve,
	   open/mkdir/...), when we know the full context of the
	   operation.  See selinux_bprm_creds_for_exec for the execve
	   checks and may_create for the file creation checks. The
	   operation will then fail if the context is not permitted. */
	tsec = selinux_cred(new);
	if (!strcmp(name, "exec")) {
		tsec->exec_sid = sid;
	} else if (!strcmp(name, "fscreate")) {
		tsec->create_sid = sid;
	} else if (!strcmp(name, "keycreate")) {
		if (sid) {
			error = avc_has_perm(&selinux_state, mysid, sid,
					     SECCLASS_KEY, KEY__CREATE, NULL);
			if (error)
				goto abort_change;
		}
		tsec->keycreate_sid = sid;
	} else if (!strcmp(name, "sockcreate")) {
		tsec->sockcreate_sid = sid;
	} else if (!strcmp(name, "current")) {
		error = -EINVAL;
		if (sid == 0)
			goto abort_change;

		/* Only allow single threaded processes to change context */
		error = -EPERM;
		if (!current_is_single_threaded()) {
			error = security_bounded_transition(&selinux_state,
							    tsec->sid, sid);
			if (error)
				goto abort_change;
		}

		/* Check permissions for the transition. */
		error = avc_has_perm(&selinux_state,
				     tsec->sid, sid, SECCLASS_PROCESS,
				     PROCESS__DYNTRANSITION, NULL);
		if (error)
			goto abort_change;

		/* Check for ptracing, and update the task SID if ok.
		   Otherwise, leave SID unchanged and fail. */
		ptsid = ptrace_parent_sid();
		if (ptsid != 0) {
			error = avc_has_perm(&selinux_state,
					     ptsid, sid, SECCLASS_PROCESS,
					     PROCESS__PTRACE, NULL);
			if (error)
				goto abort_change;
		}

		tsec->sid = sid;
	} else {
		error = -EINVAL;
		goto abort_change;
	}

	commit_creds(new);
	return size;

abort_change:
	abort_creds(new);
	return error;
}

static int selinux_ismaclabel(const char *name)
{
	return (strcmp(name, XATTR_SELINUX_SUFFIX) == 0);
}

static int selinux_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_sid_to_context(&selinux_state, secid,
				       secdata, seclen);
}

static int selinux_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	return security_context_to_sid(&selinux_state, secdata, seclen,
				       secid, GFP_KERNEL);
}

static void selinux_release_secctx(char *secdata, u32 seclen)
{
	kfree(secdata);
}

static void selinux_inode_invalidate_secctx(struct inode *inode)
{
	struct inode_security_struct *isec = selinux_inode(inode);

	spin_lock(&isec->lock);
	isec->initialized = LABEL_INVALID;
	spin_unlock(&isec->lock);
}

/*
 *	called with inode->i_mutex locked
 */
static int selinux_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	int rc = selinux_inode_setsecurity(inode, XATTR_SELINUX_SUFFIX,
					   ctx, ctxlen, 0);
	/* Do not return error when suppressing label (SBLABEL_MNT not set). */
	return rc == -EOPNOTSUPP ? 0 : rc;
}

/*
 *	called with inode->i_mutex locked
 */
static int selinux_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return __vfs_setxattr_noperm(&init_user_ns, dentry, XATTR_NAME_SELINUX,
				     ctx, ctxlen, 0);
}

static int selinux_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	int len = 0;
	len = selinux_inode_getsecurity(&init_user_ns, inode,
					XATTR_SELINUX_SUFFIX, ctx, true);
	if (len < 0)
		return len;
	*ctxlen = len;
	return 0;
}
#ifdef CONFIG_KEYS

static int selinux_key_alloc(struct key *k, const struct cred *cred,
			     unsigned long flags)
{
	const struct task_security_struct *tsec;
	struct key_security_struct *ksec;

	ksec = kzalloc(sizeof(struct key_security_struct), GFP_KERNEL);
	if (!ksec)
		return -ENOMEM;

	tsec = selinux_cred(cred);
	if (tsec->keycreate_sid)
		ksec->sid = tsec->keycreate_sid;
	else
		ksec->sid = tsec->sid;

	k->security = ksec;
	return 0;
}

static void selinux_key_free(struct key *k)
{
	struct key_security_struct *ksec = k->security;

	k->security = NULL;
	kfree(ksec);
}

static int selinux_key_permission(key_ref_t key_ref,
				  const struct cred *cred,
				  enum key_need_perm need_perm)
{
	struct key *key;
	struct key_security_struct *ksec;
	u32 perm, sid;

	switch (need_perm) {
	case KEY_NEED_VIEW:
		perm = KEY__VIEW;
		break;
	case KEY_NEED_READ:
		perm = KEY__READ;
		break;
	case KEY_NEED_WRITE:
		perm = KEY__WRITE;
		break;
	case KEY_NEED_SEARCH:
		perm = KEY__SEARCH;
		break;
	case KEY_NEED_LINK:
		perm = KEY__LINK;
		break;
	case KEY_NEED_SETATTR:
		perm = KEY__SETATTR;
		break;
	case KEY_NEED_UNLINK:
	case KEY_SYSADMIN_OVERRIDE:
	case KEY_AUTHTOKEN_OVERRIDE:
	case KEY_DEFER_PERM_CHECK:
		return 0;
	default:
		WARN_ON(1);
		return -EPERM;

	}

	sid = cred_sid(cred);
	key = key_ref_to_ptr(key_ref);
	ksec = key->security;

	return avc_has_perm(&selinux_state,
			    sid, ksec->sid, SECCLASS_KEY, perm, NULL);
}

static int selinux_key_getsecurity(struct key *key, char **_buffer)
{
	struct key_security_struct *ksec = key->security;
	char *context = NULL;
	unsigned len;
	int rc;

	rc = security_sid_to_context(&selinux_state, ksec->sid,
				     &context, &len);
	if (!rc)
		rc = len;
	*_buffer = context;
	return rc;
}

#ifdef CONFIG_KEY_NOTIFICATIONS
static int selinux_watch_key(struct key *key)
{
	struct key_security_struct *ksec = key->security;
	u32 sid = current_sid();

	return avc_has_perm(&selinux_state,
			    sid, ksec->sid, SECCLASS_KEY, KEY__VIEW, NULL);
}
#endif
#endif

#ifdef CONFIG_SECURITY_INFINIBAND
static int selinux_ib_pkey_access(void *ib_sec, u64 subnet_prefix, u16 pkey_val)
{
	struct common_audit_data ad;
	int err;
	u32 sid = 0;
	struct ib_security_struct *sec = ib_sec;
	struct lsm_ibpkey_audit ibpkey;

	err = sel_ib_pkey_sid(subnet_prefix, pkey_val, &sid);
	if (err)
		return err;

	ad.type = LSM_AUDIT_DATA_IBPKEY;
	ibpkey.subnet_prefix = subnet_prefix;
	ibpkey.pkey = pkey_val;
	ad.u.ibpkey = &ibpkey;
	return avc_has_perm(&selinux_state,
			    sec->sid, sid,
			    SECCLASS_INFINIBAND_PKEY,
			    INFINIBAND_PKEY__ACCESS, &ad);
}

static int selinux_ib_endport_manage_subnet(void *ib_sec, const char *dev_name,
					    u8 port_num)
{
	struct common_audit_data ad;
	int err;
	u32 sid = 0;
	struct ib_security_struct *sec = ib_sec;
	struct lsm_ibendport_audit ibendport;

	err = security_ib_endport_sid(&selinux_state, dev_name, port_num,
				      &sid);

	if (err)
		return err;

	ad.type = LSM_AUDIT_DATA_IBENDPORT;
	ibendport.dev_name = dev_name;
	ibendport.port = port_num;
	ad.u.ibendport = &ibendport;
	return avc_has_perm(&selinux_state,
			    sec->sid, sid,
			    SECCLASS_INFINIBAND_ENDPORT,
			    INFINIBAND_ENDPORT__MANAGE_SUBNET, &ad);
}

static int selinux_ib_alloc_security(void **ib_sec)
{
	struct ib_security_struct *sec;

	sec = kzalloc(sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;
	sec->sid = current_sid();

	*ib_sec = sec;
	return 0;
}

static void selinux_ib_free_security(void *ib_sec)
{
	kfree(ib_sec);
}
#endif

#ifdef CONFIG_BPF_SYSCALL
static int selinux_bpf(int cmd, union bpf_attr *attr,
				     unsigned int size)
{
	u32 sid = current_sid();
	int ret;

	switch (cmd) {
	case BPF_MAP_CREATE:
		ret = avc_has_perm(&selinux_state,
				   sid, sid, SECCLASS_BPF, BPF__MAP_CREATE,
				   NULL);
		break;
	case BPF_PROG_LOAD:
		ret = avc_has_perm(&selinux_state,
				   sid, sid, SECCLASS_BPF, BPF__PROG_LOAD,
				   NULL);
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

static u32 bpf_map_fmode_to_av(fmode_t fmode)
{
	u32 av = 0;

	if (fmode & FMODE_READ)
		av |= BPF__MAP_READ;
	if (fmode & FMODE_WRITE)
		av |= BPF__MAP_WRITE;
	return av;
}

/* This function will check the file pass through unix socket or binder to see
 * if it is a bpf related object. And apply correspinding checks on the bpf
 * object based on the type. The bpf maps and programs, not like other files and
 * socket, are using a shared anonymous inode inside the kernel as their inode.
 * So checking that inode cannot identify if the process have privilege to
 * access the bpf object and that's why we have to add this additional check in
 * selinux_file_receive and selinux_binder_transfer_files.
 */
static int bpf_fd_pass(struct file *file, u32 sid)
{
	struct bpf_security_struct *bpfsec;
	struct bpf_prog *prog;
	struct bpf_map *map;
	int ret;

	if (file->f_op == &bpf_map_fops) {
		map = file->private_data;
		bpfsec = map->security;
		ret = avc_has_perm(&selinux_state,
				   sid, bpfsec->sid, SECCLASS_BPF,
				   bpf_map_fmode_to_av(file->f_mode), NULL);
		if (ret)
			return ret;
	} else if (file->f_op == &bpf_prog_fops) {
		prog = file->private_data;
		bpfsec = prog->aux->security;
		ret = avc_has_perm(&selinux_state,
				   sid, bpfsec->sid, SECCLASS_BPF,
				   BPF__PROG_RUN, NULL);
		if (ret)
			return ret;
	}
	return 0;
}

static int selinux_bpf_map(struct bpf_map *map, fmode_t fmode)
{
	u32 sid = current_sid();
	struct bpf_security_struct *bpfsec;

	bpfsec = map->security;
	return avc_has_perm(&selinux_state,
			    sid, bpfsec->sid, SECCLASS_BPF,
			    bpf_map_fmode_to_av(fmode), NULL);
}

static int selinux_bpf_prog(struct bpf_prog *prog)
{
	u32 sid = current_sid();
	struct bpf_security_struct *bpfsec;

	bpfsec = prog->aux->security;
	return avc_has_perm(&selinux_state,
			    sid, bpfsec->sid, SECCLASS_BPF,
			    BPF__PROG_RUN, NULL);
}

static int selinux_bpf_map_alloc(struct bpf_map *map)
{
	struct bpf_security_struct *bpfsec;

	bpfsec = kzalloc(sizeof(*bpfsec), GFP_KERNEL);
	if (!bpfsec)
		return -ENOMEM;

	bpfsec->sid = current_sid();
	map->security = bpfsec;

	return 0;
}

static void selinux_bpf_map_free(struct bpf_map *map)
{
	struct bpf_security_struct *bpfsec = map->security;

	map->security = NULL;
	kfree(bpfsec);
}

static int selinux_bpf_prog_alloc(struct bpf_prog_aux *aux)
{
	struct bpf_security_struct *bpfsec;

	bpfsec = kzalloc(sizeof(*bpfsec), GFP_KERNEL);
	if (!bpfsec)
		return -ENOMEM;

	bpfsec->sid = current_sid();
	aux->security = bpfsec;

	return 0;
}

static void selinux_bpf_prog_free(struct bpf_prog_aux *aux)
{
	struct bpf_security_struct *bpfsec = aux->security;

	aux->security = NULL;
	kfree(bpfsec);
}
#endif

static int selinux_lockdown(enum lockdown_reason what)
{
	struct common_audit_data ad;
	u32 sid = current_sid();
	int invalid_reason = (what <= LOCKDOWN_NONE) ||
			     (what == LOCKDOWN_INTEGRITY_MAX) ||
			     (what >= LOCKDOWN_CONFIDENTIALITY_MAX);

	if (WARN(invalid_reason, "Invalid lockdown reason")) {
		audit_log(audit_context(),
			  GFP_ATOMIC, AUDIT_SELINUX_ERR,
			  "lockdown_reason=invalid");
		return -EINVAL;
	}

	ad.type = LSM_AUDIT_DATA_LOCKDOWN;
	ad.u.reason = what;

	if (what <= LOCKDOWN_INTEGRITY_MAX)
		return avc_has_perm(&selinux_state,
				    sid, sid, SECCLASS_LOCKDOWN,
				    LOCKDOWN__INTEGRITY, &ad);
	else
		return avc_has_perm(&selinux_state,
				    sid, sid, SECCLASS_LOCKDOWN,
				    LOCKDOWN__CONFIDENTIALITY, &ad);
}

struct lsm_blob_sizes selinux_blob_sizes __lsm_ro_after_init = {
	.lbs_cred = sizeof(struct task_security_struct),
	.lbs_file = sizeof(struct file_security_struct),
	.lbs_inode = sizeof(struct inode_security_struct),
	.lbs_ipc = sizeof(struct ipc_security_struct),
	.lbs_msg_msg = sizeof(struct msg_security_struct),
	.lbs_superblock = sizeof(struct superblock_security_struct),
};

#ifdef CONFIG_PERF_EVENTS
static int selinux_perf_event_open(struct perf_event_attr *attr, int type)
{
	u32 requested, sid = current_sid();

	if (type == PERF_SECURITY_OPEN)
		requested = PERF_EVENT__OPEN;
	else if (type == PERF_SECURITY_CPU)
		requested = PERF_EVENT__CPU;
	else if (type == PERF_SECURITY_KERNEL)
		requested = PERF_EVENT__KERNEL;
	else if (type == PERF_SECURITY_TRACEPOINT)
		requested = PERF_EVENT__TRACEPOINT;
	else
		return -EINVAL;

	return avc_has_perm(&selinux_state, sid, sid, SECCLASS_PERF_EVENT,
			    requested, NULL);
}

static int selinux_perf_event_alloc(struct perf_event *event)
{
	struct perf_event_security_struct *perfsec;

	perfsec = kzalloc(sizeof(*perfsec), GFP_KERNEL);
	if (!perfsec)
		return -ENOMEM;

	perfsec->sid = current_sid();
	event->security = perfsec;

	return 0;
}

static void selinux_perf_event_free(struct perf_event *event)
{
	struct perf_event_security_struct *perfsec = event->security;

	event->security = NULL;
	kfree(perfsec);
}

static int selinux_perf_event_read(struct perf_event *event)
{
	struct perf_event_security_struct *perfsec = event->security;
	u32 sid = current_sid();

	return avc_has_perm(&selinux_state, sid, perfsec->sid,
			    SECCLASS_PERF_EVENT, PERF_EVENT__READ, NULL);
}

static int selinux_perf_event_write(struct perf_event *event)
{
	struct perf_event_security_struct *perfsec = event->security;
	u32 sid = current_sid();

	return avc_has_perm(&selinux_state, sid, perfsec->sid,
			    SECCLASS_PERF_EVENT, PERF_EVENT__WRITE, NULL);
}
#endif

/*
 * IMPORTANT NOTE: When adding new hooks, please be careful to keep this order:
 * 1. any hooks that don't belong to (2.) or (3.) below,
 * 2. hooks that both access structures allocated by other hooks, and allocate
 *    structures that can be later accessed by other hooks (mostly "cloning"
 *    hooks),
 * 3. hooks that only allocate structures that can be later accessed by other
 *    hooks ("allocating" hooks).
 *
 * Please follow block comment delimiters in the list to keep this order.
 *
 * This ordering is needed for SELinux runtime disable to work at least somewhat
 * safely. Breaking the ordering rules above might lead to NULL pointer derefs
 * when disabling SELinux at runtime.
 */
static struct security_hook_list selinux_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(binder_set_context_mgr, selinux_binder_set_context_mgr),
	LSM_HOOK_INIT(binder_transaction, selinux_binder_transaction),
	LSM_HOOK_INIT(binder_transfer_binder, selinux_binder_transfer_binder),
	LSM_HOOK_INIT(binder_transfer_file, selinux_binder_transfer_file),

	LSM_HOOK_INIT(ptrace_access_check, selinux_ptrace_access_check),
	LSM_HOOK_INIT(ptrace_traceme, selinux_ptrace_traceme),
	LSM_HOOK_INIT(capget, selinux_capget),
	LSM_HOOK_INIT(capset, selinux_capset),
	LSM_HOOK_INIT(capable, selinux_capable),
	LSM_HOOK_INIT(quotactl, selinux_quotactl),
	LSM_HOOK_INIT(quota_on, selinux_quota_on),
	LSM_HOOK_INIT(syslog, selinux_syslog),
	LSM_HOOK_INIT(vm_enough_memory, selinux_vm_enough_memory),

	LSM_HOOK_INIT(netlink_send, selinux_netlink_send),

	LSM_HOOK_INIT(bprm_creds_for_exec, selinux_bprm_creds_for_exec),
	LSM_HOOK_INIT(bprm_committing_creds, selinux_bprm_committing_creds),
	LSM_HOOK_INIT(bprm_committed_creds, selinux_bprm_committed_creds),

	LSM_HOOK_INIT(sb_free_mnt_opts, selinux_free_mnt_opts),
	LSM_HOOK_INIT(sb_mnt_opts_compat, selinux_sb_mnt_opts_compat),
	LSM_HOOK_INIT(sb_remount, selinux_sb_remount),
	LSM_HOOK_INIT(sb_kern_mount, selinux_sb_kern_mount),
	LSM_HOOK_INIT(sb_show_options, selinux_sb_show_options),
	LSM_HOOK_INIT(sb_statfs, selinux_sb_statfs),
	LSM_HOOK_INIT(sb_mount, selinux_mount),
	LSM_HOOK_INIT(sb_umount, selinux_umount),
	LSM_HOOK_INIT(sb_set_mnt_opts, selinux_set_mnt_opts),
	LSM_HOOK_INIT(sb_clone_mnt_opts, selinux_sb_clone_mnt_opts),

	LSM_HOOK_INIT(move_mount, selinux_move_mount),

	LSM_HOOK_INIT(dentry_init_security, selinux_dentry_init_security),
	LSM_HOOK_INIT(dentry_create_files_as, selinux_dentry_create_files_as),

	LSM_HOOK_INIT(inode_free_security, selinux_inode_free_security),
	LSM_HOOK_INIT(inode_init_security, selinux_inode_init_security),
	LSM_HOOK_INIT(inode_init_security_anon, selinux_inode_init_security_anon),
	LSM_HOOK_INIT(inode_create, selinux_inode_create),
	LSM_HOOK_INIT(inode_link, selinux_inode_link),
	LSM_HOOK_INIT(inode_unlink, selinux_inode_unlink),
	LSM_HOOK_INIT(inode_symlink, selinux_inode_symlink),
	LSM_HOOK_INIT(inode_mkdir, selinux_inode_mkdir),
	LSM_HOOK_INIT(inode_rmdir, selinux_inode_rmdir),
	LSM_HOOK_INIT(inode_mknod, selinux_inode_mknod),
	LSM_HOOK_INIT(inode_rename, selinux_inode_rename),
	LSM_HOOK_INIT(inode_readlink, selinux_inode_readlink),
	LSM_HOOK_INIT(inode_follow_link, selinux_inode_follow_link),
	LSM_HOOK_INIT(inode_permission, selinux_inode_permission),
	LSM_HOOK_INIT(inode_setattr, selinux_inode_setattr),
	LSM_HOOK_INIT(inode_getattr, selinux_inode_getattr),
	LSM_HOOK_INIT(inode_setxattr, selinux_inode_setxattr),
	LSM_HOOK_INIT(inode_post_setxattr, selinux_inode_post_setxattr),
	LSM_HOOK_INIT(inode_getxattr, selinux_inode_getxattr),
	LSM_HOOK_INIT(inode_listxattr, selinux_inode_listxattr),
	LSM_HOOK_INIT(inode_removexattr, selinux_inode_removexattr),
	LSM_HOOK_INIT(inode_getsecurity, selinux_inode_getsecurity),
	LSM_HOOK_INIT(inode_setsecurity, selinux_inode_setsecurity),
	LSM_HOOK_INIT(inode_listsecurity, selinux_inode_listsecurity),
	LSM_HOOK_INIT(inode_getsecid, selinux_inode_getsecid),
	LSM_HOOK_INIT(inode_copy_up, selinux_inode_copy_up),
	LSM_HOOK_INIT(inode_copy_up_xattr, selinux_inode_copy_up_xattr),
	LSM_HOOK_INIT(path_notify, selinux_path_notify),

	LSM_HOOK_INIT(kernfs_init_security, selinux_kernfs_init_security),

	LSM_HOOK_INIT(file_permission, selinux_file_permission),
	LSM_HOOK_INIT(file_alloc_security, selinux_file_alloc_security),
	LSM_HOOK_INIT(file_ioctl, selinux_file_ioctl),
	LSM_HOOK_INIT(mmap_file, selinux_mmap_file),
	LSM_HOOK_INIT(mmap_addr, selinux_mmap_addr),
	LSM_HOOK_INIT(file_mprotect, selinux_file_mprotect),
	LSM_HOOK_INIT(file_lock, selinux_file_lock),
	LSM_HOOK_INIT(file_fcntl, selinux_file_fcntl),
	LSM_HOOK_INIT(file_set_fowner, selinux_file_set_fowner),
	LSM_HOOK_INIT(file_send_sigiotask, selinux_file_send_sigiotask),
	LSM_HOOK_INIT(file_receive, selinux_file_receive),

	LSM_HOOK_INIT(file_open, selinux_file_open),

	LSM_HOOK_INIT(task_alloc, selinux_task_alloc),
	LSM_HOOK_INIT(cred_prepare, selinux_cred_prepare),
	LSM_HOOK_INIT(cred_transfer, selinux_cred_transfer),
	LSM_HOOK_INIT(cred_getsecid, selinux_cred_getsecid),
	LSM_HOOK_INIT(kernel_act_as, selinux_kernel_act_as),
	LSM_HOOK_INIT(kernel_create_files_as, selinux_kernel_create_files_as),
	LSM_HOOK_INIT(kernel_module_request, selinux_kernel_module_request),
	LSM_HOOK_INIT(kernel_load_data, selinux_kernel_load_data),
	LSM_HOOK_INIT(kernel_read_file, selinux_kernel_read_file),
	LSM_HOOK_INIT(task_setpgid, selinux_task_setpgid),
	LSM_HOOK_INIT(task_getpgid, selinux_task_getpgid),
	LSM_HOOK_INIT(task_getsid, selinux_task_getsid),
	LSM_HOOK_INIT(task_getsecid_subj, selinux_task_getsecid_subj),
	LSM_HOOK_INIT(task_getsecid_obj, selinux_task_getsecid_obj),
	LSM_HOOK_INIT(task_setnice, selinux_task_setnice),
	LSM_HOOK_INIT(task_setioprio, selinux_task_setioprio),
	LSM_HOOK_INIT(task_getioprio, selinux_task_getioprio),
	LSM_HOOK_INIT(task_prlimit, selinux_task_prlimit),
	LSM_HOOK_INIT(task_setrlimit, selinux_task_setrlimit),
	LSM_HOOK_INIT(task_setscheduler, selinux_task_setscheduler),
	LSM_HOOK_INIT(task_getscheduler, selinux_task_getscheduler),
	LSM_HOOK_INIT(task_movememory, selinux_task_movememory),
	LSM_HOOK_INIT(task_kill, selinux_task_kill),
	LSM_HOOK_INIT(task_to_inode, selinux_task_to_inode),

	LSM_HOOK_INIT(ipc_permission, selinux_ipc_permission),
	LSM_HOOK_INIT(ipc_getsecid, selinux_ipc_getsecid),

	LSM_HOOK_INIT(msg_queue_associate, selinux_msg_queue_associate),
	LSM_HOOK_INIT(msg_queue_msgctl, selinux_msg_queue_msgctl),
	LSM_HOOK_INIT(msg_queue_msgsnd, selinux_msg_queue_msgsnd),
	LSM_HOOK_INIT(msg_queue_msgrcv, selinux_msg_queue_msgrcv),

	LSM_HOOK_INIT(shm_associate, selinux_shm_associate),
	LSM_HOOK_INIT(shm_shmctl, selinux_shm_shmctl),
	LSM_HOOK_INIT(shm_shmat, selinux_shm_shmat),

	LSM_HOOK_INIT(sem_associate, selinux_sem_associate),
	LSM_HOOK_INIT(sem_semctl, selinux_sem_semctl),
	LSM_HOOK_INIT(sem_semop, selinux_sem_semop),

	LSM_HOOK_INIT(d_instantiate, selinux_d_instantiate),

	LSM_HOOK_INIT(getprocattr, selinux_getprocattr),
	LSM_HOOK_INIT(setprocattr, selinux_setprocattr),

	LSM_HOOK_INIT(ismaclabel, selinux_ismaclabel),
	LSM_HOOK_INIT(secctx_to_secid, selinux_secctx_to_secid),
	LSM_HOOK_INIT(release_secctx, selinux_release_secctx),
	LSM_HOOK_INIT(inode_invalidate_secctx, selinux_inode_invalidate_secctx),
	LSM_HOOK_INIT(inode_notifysecctx, selinux_inode_notifysecctx),
	LSM_HOOK_INIT(inode_setsecctx, selinux_inode_setsecctx),

	LSM_HOOK_INIT(unix_stream_connect, selinux_socket_unix_stream_connect),
	LSM_HOOK_INIT(unix_may_send, selinux_socket_unix_may_send),

	LSM_HOOK_INIT(socket_create, selinux_socket_create),
	LSM_HOOK_INIT(socket_post_create, selinux_socket_post_create),
	LSM_HOOK_INIT(socket_socketpair, selinux_socket_socketpair),
	LSM_HOOK_INIT(socket_bind, selinux_socket_bind),
	LSM_HOOK_INIT(socket_connect, selinux_socket_connect),
	LSM_HOOK_INIT(socket_listen, selinux_socket_listen),
	LSM_HOOK_INIT(socket_accept, selinux_socket_accept),
	LSM_HOOK_INIT(socket_sendmsg, selinux_socket_sendmsg),
	LSM_HOOK_INIT(socket_recvmsg, selinux_socket_recvmsg),
	LSM_HOOK_INIT(socket_getsockname, selinux_socket_getsockname),
	LSM_HOOK_INIT(socket_getpeername, selinux_socket_getpeername),
	LSM_HOOK_INIT(socket_getsockopt, selinux_socket_getsockopt),
	LSM_HOOK_INIT(socket_setsockopt, selinux_socket_setsockopt),
	LSM_HOOK_INIT(socket_shutdown, selinux_socket_shutdown),
	LSM_HOOK_INIT(socket_sock_rcv_skb, selinux_socket_sock_rcv_skb),
	LSM_HOOK_INIT(socket_getpeersec_stream,
			selinux_socket_getpeersec_stream),
	LSM_HOOK_INIT(socket_getpeersec_dgram, selinux_socket_getpeersec_dgram),
	LSM_HOOK_INIT(sk_free_security, selinux_sk_free_security),
	LSM_HOOK_INIT(sk_clone_security, selinux_sk_clone_security),
	LSM_HOOK_INIT(sk_getsecid, selinux_sk_getsecid),
	LSM_HOOK_INIT(sock_graft, selinux_sock_graft),
	LSM_HOOK_INIT(sctp_assoc_request, selinux_sctp_assoc_request),
	LSM_HOOK_INIT(sctp_sk_clone, selinux_sctp_sk_clone),
	LSM_HOOK_INIT(sctp_bind_connect, selinux_sctp_bind_connect),
	LSM_HOOK_INIT(inet_conn_request, selinux_inet_conn_request),
	LSM_HOOK_INIT(inet_csk_clone, selinux_inet_csk_clone),
	LSM_HOOK_INIT(inet_conn_established, selinux_inet_conn_established),
	LSM_HOOK_INIT(secmark_relabel_packet, selinux_secmark_relabel_packet),
	LSM_HOOK_INIT(secmark_refcount_inc, selinux_secmark_refcount_inc),
	LSM_HOOK_INIT(secmark_refcount_dec, selinux_secmark_refcount_dec),
	LSM_HOOK_INIT(req_classify_flow, selinux_req_classify_flow),
	LSM_HOOK_INIT(tun_dev_free_security, selinux_tun_dev_free_security),
	LSM_HOOK_INIT(tun_dev_create, selinux_tun_dev_create),
	LSM_HOOK_INIT(tun_dev_attach_queue, selinux_tun_dev_attach_queue),
	LSM_HOOK_INIT(tun_dev_attach, selinux_tun_dev_attach),
	LSM_HOOK_INIT(tun_dev_open, selinux_tun_dev_open),
#ifdef CONFIG_SECURITY_INFINIBAND
	LSM_HOOK_INIT(ib_pkey_access, selinux_ib_pkey_access),
	LSM_HOOK_INIT(ib_endport_manage_subnet,
		      selinux_ib_endport_manage_subnet),
	LSM_HOOK_INIT(ib_free_security, selinux_ib_free_security),
#endif
#ifdef CONFIG_SECURITY_NETWORK_XFRM
	LSM_HOOK_INIT(xfrm_policy_free_security, selinux_xfrm_policy_free),
	LSM_HOOK_INIT(xfrm_policy_delete_security, selinux_xfrm_policy_delete),
	LSM_HOOK_INIT(xfrm_state_free_security, selinux_xfrm_state_free),
	LSM_HOOK_INIT(xfrm_state_delete_security, selinux_xfrm_state_delete),
	LSM_HOOK_INIT(xfrm_policy_lookup, selinux_xfrm_policy_lookup),
	LSM_HOOK_INIT(xfrm_state_pol_flow_match,
			selinux_xfrm_state_pol_flow_match),
	LSM_HOOK_INIT(xfrm_decode_session, selinux_xfrm_decode_session),
#endif

#ifdef CONFIG_KEYS
	LSM_HOOK_INIT(key_free, selinux_key_free),
	LSM_HOOK_INIT(key_permission, selinux_key_permission),
	LSM_HOOK_INIT(key_getsecurity, selinux_key_getsecurity),
#ifdef CONFIG_KEY_NOTIFICATIONS
	LSM_HOOK_INIT(watch_key, selinux_watch_key),
#endif
#endif

#ifdef CONFIG_AUDIT
	LSM_HOOK_INIT(audit_rule_known, selinux_audit_rule_known),
	LSM_HOOK_INIT(audit_rule_match, selinux_audit_rule_match),
	LSM_HOOK_INIT(audit_rule_free, selinux_audit_rule_free),
#endif

#ifdef CONFIG_BPF_SYSCALL
	LSM_HOOK_INIT(bpf, selinux_bpf),
	LSM_HOOK_INIT(bpf_map, selinux_bpf_map),
	LSM_HOOK_INIT(bpf_prog, selinux_bpf_prog),
	LSM_HOOK_INIT(bpf_map_free_security, selinux_bpf_map_free),
	LSM_HOOK_INIT(bpf_prog_free_security, selinux_bpf_prog_free),
#endif

#ifdef CONFIG_PERF_EVENTS
	LSM_HOOK_INIT(perf_event_open, selinux_perf_event_open),
	LSM_HOOK_INIT(perf_event_free, selinux_perf_event_free),
	LSM_HOOK_INIT(perf_event_read, selinux_perf_event_read),
	LSM_HOOK_INIT(perf_event_write, selinux_perf_event_write),
#endif

	LSM_HOOK_INIT(locked_down, selinux_lockdown),

	/*
	 * PUT "CLONING" (ACCESSING + ALLOCATING) HOOKS HERE
	 */
	LSM_HOOK_INIT(fs_context_dup, selinux_fs_context_dup),
	LSM_HOOK_INIT(fs_context_parse_param, selinux_fs_context_parse_param),
	LSM_HOOK_INIT(sb_eat_lsm_opts, selinux_sb_eat_lsm_opts),
	LSM_HOOK_INIT(sb_add_mnt_opt, selinux_add_mnt_opt),
#ifdef CONFIG_SECURITY_NETWORK_XFRM
	LSM_HOOK_INIT(xfrm_policy_clone_security, selinux_xfrm_policy_clone),
#endif

	/*
	 * PUT "ALLOCATING" HOOKS HERE
	 */
	LSM_HOOK_INIT(msg_msg_alloc_security, selinux_msg_msg_alloc_security),
	LSM_HOOK_INIT(msg_queue_alloc_security,
		      selinux_msg_queue_alloc_security),
	LSM_HOOK_INIT(shm_alloc_security, selinux_shm_alloc_security),
	LSM_HOOK_INIT(sb_alloc_security, selinux_sb_alloc_security),
	LSM_HOOK_INIT(inode_alloc_security, selinux_inode_alloc_security),
	LSM_HOOK_INIT(sem_alloc_security, selinux_sem_alloc_security),
	LSM_HOOK_INIT(secid_to_secctx, selinux_secid_to_secctx),
	LSM_HOOK_INIT(inode_getsecctx, selinux_inode_getsecctx),
	LSM_HOOK_INIT(sk_alloc_security, selinux_sk_alloc_security),
	LSM_HOOK_INIT(tun_dev_alloc_security, selinux_tun_dev_alloc_security),
#ifdef CONFIG_SECURITY_INFINIBAND
	LSM_HOOK_INIT(ib_alloc_security, selinux_ib_alloc_security),
#endif
#ifdef CONFIG_SECURITY_NETWORK_XFRM
	LSM_HOOK_INIT(xfrm_policy_alloc_security, selinux_xfrm_policy_alloc),
	LSM_HOOK_INIT(xfrm_state_alloc, selinux_xfrm_state_alloc),
	LSM_HOOK_INIT(xfrm_state_alloc_acquire,
		      selinux_xfrm_state_alloc_acquire),
#endif
#ifdef CONFIG_KEYS
	LSM_HOOK_INIT(key_alloc, selinux_key_alloc),
#endif
#ifdef CONFIG_AUDIT
	LSM_HOOK_INIT(audit_rule_init, selinux_audit_rule_init),
#endif
#ifdef CONFIG_BPF_SYSCALL
	LSM_HOOK_INIT(bpf_map_alloc_security, selinux_bpf_map_alloc),
	LSM_HOOK_INIT(bpf_prog_alloc_security, selinux_bpf_prog_alloc),
#endif
#ifdef CONFIG_PERF_EVENTS
	LSM_HOOK_INIT(perf_event_alloc, selinux_perf_event_alloc),
#endif
};

static __init int selinux_init(void)
{
	pr_info("SELinux:  Initializing.\n");

	memset(&selinux_state, 0, sizeof(selinux_state));
	enforcing_set(&selinux_state, selinux_enforcing_boot);
	checkreqprot_set(&selinux_state, selinux_checkreqprot_boot);
	selinux_avc_init(&selinux_state.avc);
	mutex_init(&selinux_state.status_lock);
	mutex_init(&selinux_state.policy_mutex);

	/* Set the security state for the initial task. */
	cred_init_security();

	default_noexec = !(VM_DATA_DEFAULT_FLAGS & VM_EXEC);

	avc_init();

	avtab_cache_init();

	ebitmap_cache_init();

	hashtab_cache_init();

	security_add_hooks(selinux_hooks, ARRAY_SIZE(selinux_hooks), "selinux");

	if (avc_add_callback(selinux_netcache_avc_callback, AVC_CALLBACK_RESET))
		panic("SELinux: Unable to register AVC netcache callback\n");

	if (avc_add_callback(selinux_lsm_notifier_avc_callback, AVC_CALLBACK_RESET))
		panic("SELinux: Unable to register AVC LSM notifier callback\n");

	if (selinux_enforcing_boot)
		pr_debug("SELinux:  Starting in enforcing mode\n");
	else
		pr_debug("SELinux:  Starting in permissive mode\n");

	fs_validate_description("selinux", selinux_fs_parameters);

	return 0;
}

static void delayed_superblock_init(struct super_block *sb, void *unused)
{
	selinux_set_mnt_opts(sb, NULL, 0, NULL);
}

void selinux_complete_init(void)
{
	pr_debug("SELinux:  Completing initialization.\n");

	/* Set up any superblocks initialized prior to the policy load. */
	pr_debug("SELinux:  Setting up existing superblocks.\n");
	iterate_supers(delayed_superblock_init, NULL);
}

/* SELinux requires early initialization in order to label
   all processes and objects when they are created. */
DEFINE_LSM(selinux) = {
	.name = "selinux",
	.flags = LSM_FLAG_LEGACY_MAJOR | LSM_FLAG_EXCLUSIVE,
	.enabled = &selinux_enabled_boot,
	.blobs = &selinux_blob_sizes,
	.init = selinux_init,
};

#if defined(CONFIG_NETFILTER)

static const struct nf_hook_ops selinux_nf_ops[] = {
	{
		.hook =		selinux_ipv4_postroute,
		.pf =		NFPROTO_IPV4,
		.hooknum =	NF_INET_POST_ROUTING,
		.priority =	NF_IP_PRI_SELINUX_LAST,
	},
	{
		.hook =		selinux_ipv4_forward,
		.pf =		NFPROTO_IPV4,
		.hooknum =	NF_INET_FORWARD,
		.priority =	NF_IP_PRI_SELINUX_FIRST,
	},
	{
		.hook =		selinux_ipv4_output,
		.pf =		NFPROTO_IPV4,
		.hooknum =	NF_INET_LOCAL_OUT,
		.priority =	NF_IP_PRI_SELINUX_FIRST,
	},
#if IS_ENABLED(CONFIG_IPV6)
	{
		.hook =		selinux_ipv6_postroute,
		.pf =		NFPROTO_IPV6,
		.hooknum =	NF_INET_POST_ROUTING,
		.priority =	NF_IP6_PRI_SELINUX_LAST,
	},
	{
		.hook =		selinux_ipv6_forward,
		.pf =		NFPROTO_IPV6,
		.hooknum =	NF_INET_FORWARD,
		.priority =	NF_IP6_PRI_SELINUX_FIRST,
	},
	{
		.hook =		selinux_ipv6_output,
		.pf =		NFPROTO_IPV6,
		.hooknum =	NF_INET_LOCAL_OUT,
		.priority =	NF_IP6_PRI_SELINUX_FIRST,
	},
#endif	/* IPV6 */
};

static int __net_init selinux_nf_register(struct net *net)
{
	return nf_register_net_hooks(net, selinux_nf_ops,
				     ARRAY_SIZE(selinux_nf_ops));
}

static void __net_exit selinux_nf_unregister(struct net *net)
{
	nf_unregister_net_hooks(net, selinux_nf_ops,
				ARRAY_SIZE(selinux_nf_ops));
}

static struct pernet_operations selinux_net_ops = {
	.init = selinux_nf_register,
	.exit = selinux_nf_unregister,
};

static int __init selinux_nf_ip_init(void)
{
	int err;

	if (!selinux_enabled_boot)
		return 0;

	pr_debug("SELinux:  Registering netfilter hooks\n");

	err = register_pernet_subsys(&selinux_net_ops);
	if (err)
		panic("SELinux: register_pernet_subsys: error %d\n", err);

	return 0;
}
__initcall(selinux_nf_ip_init);

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
static void selinux_nf_ip_exit(void)
{
	pr_debug("SELinux:  Unregistering netfilter hooks\n");

	unregister_pernet_subsys(&selinux_net_ops);
}
#endif

#else /* CONFIG_NETFILTER */

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
#define selinux_nf_ip_exit()
#endif

#endif /* CONFIG_NETFILTER */

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
int selinux_disable(struct selinux_state *state)
{
	if (selinux_initialized(state)) {
		/* Not permitted after initial policy load. */
		return -EINVAL;
	}

	if (selinux_disabled(state)) {
		/* Only do this once. */
		return -EINVAL;
	}

	selinux_mark_disabled(state);

	pr_info("SELinux:  Disabled at runtime.\n");

	/*
	 * Unregister netfilter hooks.
	 * Must be done before security_delete_hooks() to avoid breaking
	 * runtime disable.
	 */
	selinux_nf_ip_exit();

	security_delete_hooks(selinux_hooks, ARRAY_SIZE(selinux_hooks));

	/* Try to destroy the avc node cache */
	avc_disable();

	/* Unregister selinuxfs. */
	exit_sel_fs();

	return 0;
}
#endif
