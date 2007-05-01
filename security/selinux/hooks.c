/*
 *  NSA Security-Enhanced Linux (SELinux) security module
 *
 *  This file contains the SELinux hook function implementations.
 *
 *  Authors:  Stephen Smalley, <sds@epoch.ncsc.mil>
 *            Chris Vance, <cvance@nai.com>
 *            Wayne Salamon, <wsalamon@nai.com>
 *            James Morris <jmorris@redhat.com>
 *
 *  Copyright (C) 2001,2002 Networks Associates Technology, Inc.
 *  Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *  Copyright (C) 2004-2005 Trusted Computer Solutions, Inc.
 *                          <dgoeddel@trustedcs.com>
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *                     Paul Moore, <paul.moore@hp.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *      as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/xattr.h>
#include <linux/capability.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/ext2_fs.h>
#include <linux/proc_fs.h>
#include <linux/kd.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/tty.h>
#include <net/icmp.h>
#include <net/ip.h>		/* for sysctl_local_port_range[] */
#include <net/tcp.h>		/* struct or_callable used in sock_rcv_skb */
#include <asm/uaccess.h>
#include <asm/ioctls.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>	/* for network interface checks */
#include <linux/netlink.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/dccp.h>
#include <linux/quota.h>
#include <linux/un.h>		/* for Unix socket types */
#include <net/af_unix.h>	/* for Unix socket types */
#include <linux/parser.h>
#include <linux/nfs_mount.h>
#include <net/ipv6.h>
#include <linux/hugetlb.h>
#include <linux/personality.h>
#include <linux/sysctl.h>
#include <linux/audit.h>
#include <linux/string.h>
#include <linux/selinux.h>
#include <linux/mutex.h>

#include "avc.h"
#include "objsec.h"
#include "netif.h"
#include "xfrm.h"
#include "netlabel.h"

#define XATTR_SELINUX_SUFFIX "selinux"
#define XATTR_NAME_SELINUX XATTR_SECURITY_PREFIX XATTR_SELINUX_SUFFIX

extern unsigned int policydb_loaded_version;
extern int selinux_nlmsg_lookup(u16 sclass, u16 nlmsg_type, u32 *perm);
extern int selinux_compat_net;

#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
int selinux_enforcing = 0;

static int __init enforcing_setup(char *str)
{
	selinux_enforcing = simple_strtol(str,NULL,0);
	return 1;
}
__setup("enforcing=", enforcing_setup);
#endif

#ifdef CONFIG_SECURITY_SELINUX_BOOTPARAM
int selinux_enabled = CONFIG_SECURITY_SELINUX_BOOTPARAM_VALUE;

static int __init selinux_enabled_setup(char *str)
{
	selinux_enabled = simple_strtol(str, NULL, 0);
	return 1;
}
__setup("selinux=", selinux_enabled_setup);
#else
int selinux_enabled = 1;
#endif

/* Original (dummy) security module. */
static struct security_operations *original_ops = NULL;

/* Minimal support for a secondary security module,
   just to allow the use of the dummy or capability modules.
   The owlsm module can alternatively be used as a secondary
   module as long as CONFIG_OWLSM_FD is not enabled. */
static struct security_operations *secondary_ops = NULL;

/* Lists of inode and superblock security structures initialized
   before the policy was loaded. */
static LIST_HEAD(superblock_security_head);
static DEFINE_SPINLOCK(sb_security_lock);

static struct kmem_cache *sel_inode_cache;

/* Return security context for a given sid or just the context 
   length if the buffer is null or length is 0 */
static int selinux_getsecurity(u32 sid, void *buffer, size_t size)
{
	char *context;
	unsigned len;
	int rc;

	rc = security_sid_to_context(sid, &context, &len);
	if (rc)
		return rc;

	if (!buffer || !size)
		goto getsecurity_exit;

	if (size < len) {
		len = -ERANGE;
		goto getsecurity_exit;
	}
	memcpy(buffer, context, len);

getsecurity_exit:
	kfree(context);
	return len;
}

/* Allocate and free functions for each kind of security blob. */

static int task_alloc_security(struct task_struct *task)
{
	struct task_security_struct *tsec;

	tsec = kzalloc(sizeof(struct task_security_struct), GFP_KERNEL);
	if (!tsec)
		return -ENOMEM;

	tsec->task = task;
	tsec->osid = tsec->sid = tsec->ptrace_sid = SECINITSID_UNLABELED;
	task->security = tsec;

	return 0;
}

static void task_free_security(struct task_struct *task)
{
	struct task_security_struct *tsec = task->security;
	task->security = NULL;
	kfree(tsec);
}

static int inode_alloc_security(struct inode *inode)
{
	struct task_security_struct *tsec = current->security;
	struct inode_security_struct *isec;

	isec = kmem_cache_zalloc(sel_inode_cache, GFP_KERNEL);
	if (!isec)
		return -ENOMEM;

	mutex_init(&isec->lock);
	INIT_LIST_HEAD(&isec->list);
	isec->inode = inode;
	isec->sid = SECINITSID_UNLABELED;
	isec->sclass = SECCLASS_FILE;
	isec->task_sid = tsec->sid;
	inode->i_security = isec;

	return 0;
}

static void inode_free_security(struct inode *inode)
{
	struct inode_security_struct *isec = inode->i_security;
	struct superblock_security_struct *sbsec = inode->i_sb->s_security;

	spin_lock(&sbsec->isec_lock);
	if (!list_empty(&isec->list))
		list_del_init(&isec->list);
	spin_unlock(&sbsec->isec_lock);

	inode->i_security = NULL;
	kmem_cache_free(sel_inode_cache, isec);
}

static int file_alloc_security(struct file *file)
{
	struct task_security_struct *tsec = current->security;
	struct file_security_struct *fsec;

	fsec = kzalloc(sizeof(struct file_security_struct), GFP_KERNEL);
	if (!fsec)
		return -ENOMEM;

	fsec->file = file;
	fsec->sid = tsec->sid;
	fsec->fown_sid = tsec->sid;
	file->f_security = fsec;

	return 0;
}

static void file_free_security(struct file *file)
{
	struct file_security_struct *fsec = file->f_security;
	file->f_security = NULL;
	kfree(fsec);
}

static int superblock_alloc_security(struct super_block *sb)
{
	struct superblock_security_struct *sbsec;

	sbsec = kzalloc(sizeof(struct superblock_security_struct), GFP_KERNEL);
	if (!sbsec)
		return -ENOMEM;

	mutex_init(&sbsec->lock);
	INIT_LIST_HEAD(&sbsec->list);
	INIT_LIST_HEAD(&sbsec->isec_head);
	spin_lock_init(&sbsec->isec_lock);
	sbsec->sb = sb;
	sbsec->sid = SECINITSID_UNLABELED;
	sbsec->def_sid = SECINITSID_FILE;
	sbsec->mntpoint_sid = SECINITSID_UNLABELED;
	sb->s_security = sbsec;

	return 0;
}

static void superblock_free_security(struct super_block *sb)
{
	struct superblock_security_struct *sbsec = sb->s_security;

	spin_lock(&sb_security_lock);
	if (!list_empty(&sbsec->list))
		list_del_init(&sbsec->list);
	spin_unlock(&sb_security_lock);

	sb->s_security = NULL;
	kfree(sbsec);
}

static int sk_alloc_security(struct sock *sk, int family, gfp_t priority)
{
	struct sk_security_struct *ssec;

	ssec = kzalloc(sizeof(*ssec), priority);
	if (!ssec)
		return -ENOMEM;

	ssec->sk = sk;
	ssec->peer_sid = SECINITSID_UNLABELED;
	ssec->sid = SECINITSID_UNLABELED;
	sk->sk_security = ssec;

	selinux_netlbl_sk_security_init(ssec, family);

	return 0;
}

static void sk_free_security(struct sock *sk)
{
	struct sk_security_struct *ssec = sk->sk_security;

	sk->sk_security = NULL;
	kfree(ssec);
}

/* The security server must be initialized before
   any labeling or access decisions can be provided. */
extern int ss_initialized;

/* The file system's label must be initialized prior to use. */

static char *labeling_behaviors[6] = {
	"uses xattr",
	"uses transition SIDs",
	"uses task SIDs",
	"uses genfs_contexts",
	"not configured for labeling",
	"uses mountpoint labeling",
};

static int inode_doinit_with_dentry(struct inode *inode, struct dentry *opt_dentry);

static inline int inode_doinit(struct inode *inode)
{
	return inode_doinit_with_dentry(inode, NULL);
}

enum {
	Opt_context = 1,
	Opt_fscontext = 2,
	Opt_defcontext = 4,
	Opt_rootcontext = 8,
};

static match_table_t tokens = {
	{Opt_context, "context=%s"},
	{Opt_fscontext, "fscontext=%s"},
	{Opt_defcontext, "defcontext=%s"},
	{Opt_rootcontext, "rootcontext=%s"},
};

#define SEL_MOUNT_FAIL_MSG "SELinux:  duplicate or incompatible mount options\n"

static int may_context_mount_sb_relabel(u32 sid,
			struct superblock_security_struct *sbsec,
			struct task_security_struct *tsec)
{
	int rc;

	rc = avc_has_perm(tsec->sid, sbsec->sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__RELABELFROM, NULL);
	if (rc)
		return rc;

	rc = avc_has_perm(tsec->sid, sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__RELABELTO, NULL);
	return rc;
}

static int may_context_mount_inode_relabel(u32 sid,
			struct superblock_security_struct *sbsec,
			struct task_security_struct *tsec)
{
	int rc;
	rc = avc_has_perm(tsec->sid, sbsec->sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__RELABELFROM, NULL);
	if (rc)
		return rc;

	rc = avc_has_perm(sid, sbsec->sid, SECCLASS_FILESYSTEM,
			  FILESYSTEM__ASSOCIATE, NULL);
	return rc;
}

static int try_context_mount(struct super_block *sb, void *data)
{
	char *context = NULL, *defcontext = NULL;
	char *fscontext = NULL, *rootcontext = NULL;
	const char *name;
	u32 sid;
	int alloc = 0, rc = 0, seen = 0;
	struct task_security_struct *tsec = current->security;
	struct superblock_security_struct *sbsec = sb->s_security;

	if (!data)
		goto out;

	name = sb->s_type->name;

	if (sb->s_type->fs_flags & FS_BINARY_MOUNTDATA) {

		/* NFS we understand. */
		if (!strcmp(name, "nfs")) {
			struct nfs_mount_data *d = data;

			if (d->version <  NFS_MOUNT_VERSION)
				goto out;

			if (d->context[0]) {
				context = d->context;
				seen |= Opt_context;
			}
		} else
			goto out;

	} else {
		/* Standard string-based options. */
		char *p, *options = data;

		while ((p = strsep(&options, "|")) != NULL) {
			int token;
			substring_t args[MAX_OPT_ARGS];

			if (!*p)
				continue;

			token = match_token(p, tokens, args);

			switch (token) {
			case Opt_context:
				if (seen & (Opt_context|Opt_defcontext)) {
					rc = -EINVAL;
					printk(KERN_WARNING SEL_MOUNT_FAIL_MSG);
					goto out_free;
				}
				context = match_strdup(&args[0]);
				if (!context) {
					rc = -ENOMEM;
					goto out_free;
				}
				if (!alloc)
					alloc = 1;
				seen |= Opt_context;
				break;

			case Opt_fscontext:
				if (seen & Opt_fscontext) {
					rc = -EINVAL;
					printk(KERN_WARNING SEL_MOUNT_FAIL_MSG);
					goto out_free;
				}
				fscontext = match_strdup(&args[0]);
				if (!fscontext) {
					rc = -ENOMEM;
					goto out_free;
				}
				if (!alloc)
					alloc = 1;
				seen |= Opt_fscontext;
				break;

			case Opt_rootcontext:
				if (seen & Opt_rootcontext) {
					rc = -EINVAL;
					printk(KERN_WARNING SEL_MOUNT_FAIL_MSG);
					goto out_free;
				}
				rootcontext = match_strdup(&args[0]);
				if (!rootcontext) {
					rc = -ENOMEM;
					goto out_free;
				}
				if (!alloc)
					alloc = 1;
				seen |= Opt_rootcontext;
				break;

			case Opt_defcontext:
				if (sbsec->behavior != SECURITY_FS_USE_XATTR) {
					rc = -EINVAL;
					printk(KERN_WARNING "SELinux:  "
					       "defcontext option is invalid "
					       "for this filesystem type\n");
					goto out_free;
				}
				if (seen & (Opt_context|Opt_defcontext)) {
					rc = -EINVAL;
					printk(KERN_WARNING SEL_MOUNT_FAIL_MSG);
					goto out_free;
				}
				defcontext = match_strdup(&args[0]);
				if (!defcontext) {
					rc = -ENOMEM;
					goto out_free;
				}
				if (!alloc)
					alloc = 1;
				seen |= Opt_defcontext;
				break;

			default:
				rc = -EINVAL;
				printk(KERN_WARNING "SELinux:  unknown mount "
				       "option\n");
				goto out_free;

			}
		}
	}

	if (!seen)
		goto out;

	/* sets the context of the superblock for the fs being mounted. */
	if (fscontext) {
		rc = security_context_to_sid(fscontext, strlen(fscontext), &sid);
		if (rc) {
			printk(KERN_WARNING "SELinux: security_context_to_sid"
			       "(%s) failed for (dev %s, type %s) errno=%d\n",
			       fscontext, sb->s_id, name, rc);
			goto out_free;
		}

		rc = may_context_mount_sb_relabel(sid, sbsec, tsec);
		if (rc)
			goto out_free;

		sbsec->sid = sid;
	}

	/*
	 * Switch to using mount point labeling behavior.
	 * sets the label used on all file below the mountpoint, and will set
	 * the superblock context if not already set.
	 */
	if (context) {
		rc = security_context_to_sid(context, strlen(context), &sid);
		if (rc) {
			printk(KERN_WARNING "SELinux: security_context_to_sid"
			       "(%s) failed for (dev %s, type %s) errno=%d\n",
			       context, sb->s_id, name, rc);
			goto out_free;
		}

		if (!fscontext) {
			rc = may_context_mount_sb_relabel(sid, sbsec, tsec);
			if (rc)
				goto out_free;
			sbsec->sid = sid;
		} else {
			rc = may_context_mount_inode_relabel(sid, sbsec, tsec);
			if (rc)
				goto out_free;
		}
		sbsec->mntpoint_sid = sid;

		sbsec->behavior = SECURITY_FS_USE_MNTPOINT;
	}

	if (rootcontext) {
		struct inode *inode = sb->s_root->d_inode;
		struct inode_security_struct *isec = inode->i_security;
		rc = security_context_to_sid(rootcontext, strlen(rootcontext), &sid);
		if (rc) {
			printk(KERN_WARNING "SELinux: security_context_to_sid"
			       "(%s) failed for (dev %s, type %s) errno=%d\n",
			       rootcontext, sb->s_id, name, rc);
			goto out_free;
		}

		rc = may_context_mount_inode_relabel(sid, sbsec, tsec);
		if (rc)
			goto out_free;

		isec->sid = sid;
		isec->initialized = 1;
	}

	if (defcontext) {
		rc = security_context_to_sid(defcontext, strlen(defcontext), &sid);
		if (rc) {
			printk(KERN_WARNING "SELinux: security_context_to_sid"
			       "(%s) failed for (dev %s, type %s) errno=%d\n",
			       defcontext, sb->s_id, name, rc);
			goto out_free;
		}

		if (sid == sbsec->def_sid)
			goto out_free;

		rc = may_context_mount_inode_relabel(sid, sbsec, tsec);
		if (rc)
			goto out_free;

		sbsec->def_sid = sid;
	}

out_free:
	if (alloc) {
		kfree(context);
		kfree(defcontext);
		kfree(fscontext);
		kfree(rootcontext);
	}
out:
	return rc;
}

static int superblock_doinit(struct super_block *sb, void *data)
{
	struct superblock_security_struct *sbsec = sb->s_security;
	struct dentry *root = sb->s_root;
	struct inode *inode = root->d_inode;
	int rc = 0;

	mutex_lock(&sbsec->lock);
	if (sbsec->initialized)
		goto out;

	if (!ss_initialized) {
		/* Defer initialization until selinux_complete_init,
		   after the initial policy is loaded and the security
		   server is ready to handle calls. */
		spin_lock(&sb_security_lock);
		if (list_empty(&sbsec->list))
			list_add(&sbsec->list, &superblock_security_head);
		spin_unlock(&sb_security_lock);
		goto out;
	}

	/* Determine the labeling behavior to use for this filesystem type. */
	rc = security_fs_use(sb->s_type->name, &sbsec->behavior, &sbsec->sid);
	if (rc) {
		printk(KERN_WARNING "%s:  security_fs_use(%s) returned %d\n",
		       __FUNCTION__, sb->s_type->name, rc);
		goto out;
	}

	rc = try_context_mount(sb, data);
	if (rc)
		goto out;

	if (sbsec->behavior == SECURITY_FS_USE_XATTR) {
		/* Make sure that the xattr handler exists and that no
		   error other than -ENODATA is returned by getxattr on
		   the root directory.  -ENODATA is ok, as this may be
		   the first boot of the SELinux kernel before we have
		   assigned xattr values to the filesystem. */
		if (!inode->i_op->getxattr) {
			printk(KERN_WARNING "SELinux: (dev %s, type %s) has no "
			       "xattr support\n", sb->s_id, sb->s_type->name);
			rc = -EOPNOTSUPP;
			goto out;
		}
		rc = inode->i_op->getxattr(root, XATTR_NAME_SELINUX, NULL, 0);
		if (rc < 0 && rc != -ENODATA) {
			if (rc == -EOPNOTSUPP)
				printk(KERN_WARNING "SELinux: (dev %s, type "
				       "%s) has no security xattr handler\n",
				       sb->s_id, sb->s_type->name);
			else
				printk(KERN_WARNING "SELinux: (dev %s, type "
				       "%s) getxattr errno %d\n", sb->s_id,
				       sb->s_type->name, -rc);
			goto out;
		}
	}

	if (strcmp(sb->s_type->name, "proc") == 0)
		sbsec->proc = 1;

	sbsec->initialized = 1;

	if (sbsec->behavior > ARRAY_SIZE(labeling_behaviors)) {
		printk(KERN_ERR "SELinux: initialized (dev %s, type %s), unknown behavior\n",
		       sb->s_id, sb->s_type->name);
	}
	else {
		printk(KERN_DEBUG "SELinux: initialized (dev %s, type %s), %s\n",
		       sb->s_id, sb->s_type->name,
		       labeling_behaviors[sbsec->behavior-1]);
	}

	/* Initialize the root inode. */
	rc = inode_doinit_with_dentry(sb->s_root->d_inode, sb->s_root);

	/* Initialize any other inodes associated with the superblock, e.g.
	   inodes created prior to initial policy load or inodes created
	   during get_sb by a pseudo filesystem that directly
	   populates itself. */
	spin_lock(&sbsec->isec_lock);
next_inode:
	if (!list_empty(&sbsec->isec_head)) {
		struct inode_security_struct *isec =
				list_entry(sbsec->isec_head.next,
				           struct inode_security_struct, list);
		struct inode *inode = isec->inode;
		spin_unlock(&sbsec->isec_lock);
		inode = igrab(inode);
		if (inode) {
			if (!IS_PRIVATE (inode))
				inode_doinit(inode);
			iput(inode);
		}
		spin_lock(&sbsec->isec_lock);
		list_del_init(&isec->list);
		goto next_inode;
	}
	spin_unlock(&sbsec->isec_lock);
out:
	mutex_unlock(&sbsec->lock);
	return rc;
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
	return (protocol == IPPROTO_IP || protocol == IPPROTO_TCP);
}

static inline int default_protocol_dgram(int protocol)
{
	return (protocol == IPPROTO_IP || protocol == IPPROTO_UDP);
}

static inline u16 socket_type_to_security_class(int family, int type, int protocol)
{
	switch (family) {
	case PF_UNIX:
		switch (type) {
		case SOCK_STREAM:
		case SOCK_SEQPACKET:
			return SECCLASS_UNIX_STREAM_SOCKET;
		case SOCK_DGRAM:
			return SECCLASS_UNIX_DGRAM_SOCKET;
		}
		break;
	case PF_INET:
	case PF_INET6:
		switch (type) {
		case SOCK_STREAM:
			if (default_protocol_stream(protocol))
				return SECCLASS_TCP_SOCKET;
			else
				return SECCLASS_RAWIP_SOCKET;
		case SOCK_DGRAM:
			if (default_protocol_dgram(protocol))
				return SECCLASS_UDP_SOCKET;
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
		case NETLINK_FIREWALL:
			return SECCLASS_NETLINK_FIREWALL_SOCKET;
		case NETLINK_INET_DIAG:
			return SECCLASS_NETLINK_TCPDIAG_SOCKET;
		case NETLINK_NFLOG:
			return SECCLASS_NETLINK_NFLOG_SOCKET;
		case NETLINK_XFRM:
			return SECCLASS_NETLINK_XFRM_SOCKET;
		case NETLINK_SELINUX:
			return SECCLASS_NETLINK_SELINUX_SOCKET;
		case NETLINK_AUDIT:
			return SECCLASS_NETLINK_AUDIT_SOCKET;
		case NETLINK_IP6_FW:
			return SECCLASS_NETLINK_IP6FW_SOCKET;
		case NETLINK_DNRTMSG:
			return SECCLASS_NETLINK_DNRT_SOCKET;
		case NETLINK_KOBJECT_UEVENT:
			return SECCLASS_NETLINK_KOBJECT_UEVENT_SOCKET;
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

	return SECCLASS_SOCKET;
}

#ifdef CONFIG_PROC_FS
static int selinux_proc_get_sid(struct proc_dir_entry *de,
				u16 tclass,
				u32 *sid)
{
	int buflen, rc;
	char *buffer, *path, *end;

	buffer = (char*)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	buflen = PAGE_SIZE;
	end = buffer+buflen;
	*--end = '\0';
	buflen--;
	path = end-1;
	*path = '/';
	while (de && de != de->parent) {
		buflen -= de->namelen + 1;
		if (buflen < 0)
			break;
		end -= de->namelen;
		memcpy(end, de->name, de->namelen);
		*--end = '/';
		path = end;
		de = de->parent;
	}
	rc = security_genfs_sid("proc", path, tclass, sid);
	free_page((unsigned long)buffer);
	return rc;
}
#else
static int selinux_proc_get_sid(struct proc_dir_entry *de,
				u16 tclass,
				u32 *sid)
{
	return -EINVAL;
}
#endif

/* The inode's security attributes must be initialized before first use. */
static int inode_doinit_with_dentry(struct inode *inode, struct dentry *opt_dentry)
{
	struct superblock_security_struct *sbsec = NULL;
	struct inode_security_struct *isec = inode->i_security;
	u32 sid;
	struct dentry *dentry;
#define INITCONTEXTLEN 255
	char *context = NULL;
	unsigned len = 0;
	int rc = 0;

	if (isec->initialized)
		goto out;

	mutex_lock(&isec->lock);
	if (isec->initialized)
		goto out_unlock;

	sbsec = inode->i_sb->s_security;
	if (!sbsec->initialized) {
		/* Defer initialization until selinux_complete_init,
		   after the initial policy is loaded and the security
		   server is ready to handle calls. */
		spin_lock(&sbsec->isec_lock);
		if (list_empty(&isec->list))
			list_add(&isec->list, &sbsec->isec_head);
		spin_unlock(&sbsec->isec_lock);
		goto out_unlock;
	}

	switch (sbsec->behavior) {
	case SECURITY_FS_USE_XATTR:
		if (!inode->i_op->getxattr) {
			isec->sid = sbsec->def_sid;
			break;
		}

		/* Need a dentry, since the xattr API requires one.
		   Life would be simpler if we could just pass the inode. */
		if (opt_dentry) {
			/* Called from d_instantiate or d_splice_alias. */
			dentry = dget(opt_dentry);
		} else {
			/* Called from selinux_complete_init, try to find a dentry. */
			dentry = d_find_alias(inode);
		}
		if (!dentry) {
			printk(KERN_WARNING "%s:  no dentry for dev=%s "
			       "ino=%ld\n", __FUNCTION__, inode->i_sb->s_id,
			       inode->i_ino);
			goto out_unlock;
		}

		len = INITCONTEXTLEN;
		context = kmalloc(len, GFP_KERNEL);
		if (!context) {
			rc = -ENOMEM;
			dput(dentry);
			goto out_unlock;
		}
		rc = inode->i_op->getxattr(dentry, XATTR_NAME_SELINUX,
					   context, len);
		if (rc == -ERANGE) {
			/* Need a larger buffer.  Query for the right size. */
			rc = inode->i_op->getxattr(dentry, XATTR_NAME_SELINUX,
						   NULL, 0);
			if (rc < 0) {
				dput(dentry);
				goto out_unlock;
			}
			kfree(context);
			len = rc;
			context = kmalloc(len, GFP_KERNEL);
			if (!context) {
				rc = -ENOMEM;
				dput(dentry);
				goto out_unlock;
			}
			rc = inode->i_op->getxattr(dentry,
						   XATTR_NAME_SELINUX,
						   context, len);
		}
		dput(dentry);
		if (rc < 0) {
			if (rc != -ENODATA) {
				printk(KERN_WARNING "%s:  getxattr returned "
				       "%d for dev=%s ino=%ld\n", __FUNCTION__,
				       -rc, inode->i_sb->s_id, inode->i_ino);
				kfree(context);
				goto out_unlock;
			}
			/* Map ENODATA to the default file SID */
			sid = sbsec->def_sid;
			rc = 0;
		} else {
			rc = security_context_to_sid_default(context, rc, &sid,
			                                     sbsec->def_sid);
			if (rc) {
				printk(KERN_WARNING "%s:  context_to_sid(%s) "
				       "returned %d for dev=%s ino=%ld\n",
				       __FUNCTION__, context, -rc,
				       inode->i_sb->s_id, inode->i_ino);
				kfree(context);
				/* Leave with the unlabeled SID */
				rc = 0;
				break;
			}
		}
		kfree(context);
		isec->sid = sid;
		break;
	case SECURITY_FS_USE_TASK:
		isec->sid = isec->task_sid;
		break;
	case SECURITY_FS_USE_TRANS:
		/* Default to the fs SID. */
		isec->sid = sbsec->sid;

		/* Try to obtain a transition SID. */
		isec->sclass = inode_mode_to_security_class(inode->i_mode);
		rc = security_transition_sid(isec->task_sid,
					     sbsec->sid,
					     isec->sclass,
					     &sid);
		if (rc)
			goto out_unlock;
		isec->sid = sid;
		break;
	case SECURITY_FS_USE_MNTPOINT:
		isec->sid = sbsec->mntpoint_sid;
		break;
	default:
		/* Default to the fs superblock SID. */
		isec->sid = sbsec->sid;

		if (sbsec->proc) {
			struct proc_inode *proci = PROC_I(inode);
			if (proci->pde) {
				isec->sclass = inode_mode_to_security_class(inode->i_mode);
				rc = selinux_proc_get_sid(proci->pde,
							  isec->sclass,
							  &sid);
				if (rc)
					goto out_unlock;
				isec->sid = sid;
			}
		}
		break;
	}

	isec->initialized = 1;

out_unlock:
	mutex_unlock(&isec->lock);
out:
	if (isec->sclass == SECCLASS_FILE)
		isec->sclass = inode_mode_to_security_class(inode->i_mode);
	return rc;
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

/* Check permission betweeen a pair of tasks, e.g. signal checks,
   fork check, ptrace check, etc. */
static int task_has_perm(struct task_struct *tsk1,
			 struct task_struct *tsk2,
			 u32 perms)
{
	struct task_security_struct *tsec1, *tsec2;

	tsec1 = tsk1->security;
	tsec2 = tsk2->security;
	return avc_has_perm(tsec1->sid, tsec2->sid,
			    SECCLASS_PROCESS, perms, NULL);
}

/* Check whether a task is allowed to use a capability. */
static int task_has_capability(struct task_struct *tsk,
			       int cap)
{
	struct task_security_struct *tsec;
	struct avc_audit_data ad;

	tsec = tsk->security;

	AVC_AUDIT_DATA_INIT(&ad,CAP);
	ad.tsk = tsk;
	ad.u.cap = cap;

	return avc_has_perm(tsec->sid, tsec->sid,
			    SECCLASS_CAPABILITY, CAP_TO_MASK(cap), &ad);
}

/* Check whether a task is allowed to use a system operation. */
static int task_has_system(struct task_struct *tsk,
			   u32 perms)
{
	struct task_security_struct *tsec;

	tsec = tsk->security;

	return avc_has_perm(tsec->sid, SECINITSID_KERNEL,
			    SECCLASS_SYSTEM, perms, NULL);
}

/* Check whether a task has a particular permission to an inode.
   The 'adp' parameter is optional and allows other audit
   data to be passed (e.g. the dentry). */
static int inode_has_perm(struct task_struct *tsk,
			  struct inode *inode,
			  u32 perms,
			  struct avc_audit_data *adp)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *isec;
	struct avc_audit_data ad;

	if (unlikely (IS_PRIVATE (inode)))
		return 0;

	tsec = tsk->security;
	isec = inode->i_security;

	if (!adp) {
		adp = &ad;
		AVC_AUDIT_DATA_INIT(&ad, FS);
		ad.u.fs.inode = inode;
	}

	return avc_has_perm(tsec->sid, isec->sid, isec->sclass, perms, adp);
}

/* Same as inode_has_perm, but pass explicit audit data containing
   the dentry to help the auditing code to more easily generate the
   pathname if needed. */
static inline int dentry_has_perm(struct task_struct *tsk,
				  struct vfsmount *mnt,
				  struct dentry *dentry,
				  u32 av)
{
	struct inode *inode = dentry->d_inode;
	struct avc_audit_data ad;
	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.mnt = mnt;
	ad.u.fs.dentry = dentry;
	return inode_has_perm(tsk, inode, av, &ad);
}

/* Check whether a task can use an open file descriptor to
   access an inode in a given way.  Check access to the
   descriptor itself, and then use dentry_has_perm to
   check a particular permission to the file.
   Access to the descriptor is implicitly granted if it
   has the same SID as the process.  If av is zero, then
   access to the file is not checked, e.g. for cases
   where only the descriptor is affected like seek. */
static int file_has_perm(struct task_struct *tsk,
				struct file *file,
				u32 av)
{
	struct task_security_struct *tsec = tsk->security;
	struct file_security_struct *fsec = file->f_security;
	struct vfsmount *mnt = file->f_path.mnt;
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct avc_audit_data ad;
	int rc;

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.mnt = mnt;
	ad.u.fs.dentry = dentry;

	if (tsec->sid != fsec->sid) {
		rc = avc_has_perm(tsec->sid, fsec->sid,
				  SECCLASS_FD,
				  FD__USE,
				  &ad);
		if (rc)
			return rc;
	}

	/* av is zero if only checking access to the descriptor. */
	if (av)
		return inode_has_perm(tsk, inode, av, &ad);

	return 0;
}

/* Check whether a task can create a file. */
static int may_create(struct inode *dir,
		      struct dentry *dentry,
		      u16 tclass)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *dsec;
	struct superblock_security_struct *sbsec;
	u32 newsid;
	struct avc_audit_data ad;
	int rc;

	tsec = current->security;
	dsec = dir->i_security;
	sbsec = dir->i_sb->s_security;

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.dentry = dentry;

	rc = avc_has_perm(tsec->sid, dsec->sid, SECCLASS_DIR,
			  DIR__ADD_NAME | DIR__SEARCH,
			  &ad);
	if (rc)
		return rc;

	if (tsec->create_sid && sbsec->behavior != SECURITY_FS_USE_MNTPOINT) {
		newsid = tsec->create_sid;
	} else {
		rc = security_transition_sid(tsec->sid, dsec->sid, tclass,
					     &newsid);
		if (rc)
			return rc;
	}

	rc = avc_has_perm(tsec->sid, newsid, tclass, FILE__CREATE, &ad);
	if (rc)
		return rc;

	return avc_has_perm(newsid, sbsec->sid,
			    SECCLASS_FILESYSTEM,
			    FILESYSTEM__ASSOCIATE, &ad);
}

/* Check whether a task can create a key. */
static int may_create_key(u32 ksid,
			  struct task_struct *ctx)
{
	struct task_security_struct *tsec;

	tsec = ctx->security;

	return avc_has_perm(tsec->sid, ksid, SECCLASS_KEY, KEY__CREATE, NULL);
}

#define MAY_LINK   0
#define MAY_UNLINK 1
#define MAY_RMDIR  2

/* Check whether a task can link, unlink, or rmdir a file/directory. */
static int may_link(struct inode *dir,
		    struct dentry *dentry,
		    int kind)

{
	struct task_security_struct *tsec;
	struct inode_security_struct *dsec, *isec;
	struct avc_audit_data ad;
	u32 av;
	int rc;

	tsec = current->security;
	dsec = dir->i_security;
	isec = dentry->d_inode->i_security;

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.dentry = dentry;

	av = DIR__SEARCH;
	av |= (kind ? DIR__REMOVE_NAME : DIR__ADD_NAME);
	rc = avc_has_perm(tsec->sid, dsec->sid, SECCLASS_DIR, av, &ad);
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
		printk(KERN_WARNING "may_link:  unrecognized kind %d\n", kind);
		return 0;
	}

	rc = avc_has_perm(tsec->sid, isec->sid, isec->sclass, av, &ad);
	return rc;
}

static inline int may_rename(struct inode *old_dir,
			     struct dentry *old_dentry,
			     struct inode *new_dir,
			     struct dentry *new_dentry)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *old_dsec, *new_dsec, *old_isec, *new_isec;
	struct avc_audit_data ad;
	u32 av;
	int old_is_dir, new_is_dir;
	int rc;

	tsec = current->security;
	old_dsec = old_dir->i_security;
	old_isec = old_dentry->d_inode->i_security;
	old_is_dir = S_ISDIR(old_dentry->d_inode->i_mode);
	new_dsec = new_dir->i_security;

	AVC_AUDIT_DATA_INIT(&ad, FS);

	ad.u.fs.dentry = old_dentry;
	rc = avc_has_perm(tsec->sid, old_dsec->sid, SECCLASS_DIR,
			  DIR__REMOVE_NAME | DIR__SEARCH, &ad);
	if (rc)
		return rc;
	rc = avc_has_perm(tsec->sid, old_isec->sid,
			  old_isec->sclass, FILE__RENAME, &ad);
	if (rc)
		return rc;
	if (old_is_dir && new_dir != old_dir) {
		rc = avc_has_perm(tsec->sid, old_isec->sid,
				  old_isec->sclass, DIR__REPARENT, &ad);
		if (rc)
			return rc;
	}

	ad.u.fs.dentry = new_dentry;
	av = DIR__ADD_NAME | DIR__SEARCH;
	if (new_dentry->d_inode)
		av |= DIR__REMOVE_NAME;
	rc = avc_has_perm(tsec->sid, new_dsec->sid, SECCLASS_DIR, av, &ad);
	if (rc)
		return rc;
	if (new_dentry->d_inode) {
		new_isec = new_dentry->d_inode->i_security;
		new_is_dir = S_ISDIR(new_dentry->d_inode->i_mode);
		rc = avc_has_perm(tsec->sid, new_isec->sid,
				  new_isec->sclass,
				  (new_is_dir ? DIR__RMDIR : FILE__UNLINK), &ad);
		if (rc)
			return rc;
	}

	return 0;
}

/* Check whether a task can perform a filesystem operation. */
static int superblock_has_perm(struct task_struct *tsk,
			       struct super_block *sb,
			       u32 perms,
			       struct avc_audit_data *ad)
{
	struct task_security_struct *tsec;
	struct superblock_security_struct *sbsec;

	tsec = tsk->security;
	sbsec = sb->s_security;
	return avc_has_perm(tsec->sid, sbsec->sid, SECCLASS_FILESYSTEM,
			    perms, ad);
}

/* Convert a Linux mode and permission mask to an access vector. */
static inline u32 file_mask_to_av(int mode, int mask)
{
	u32 av = 0;

	if ((mode & S_IFMT) != S_IFDIR) {
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

	return av;
}

/* Hook functions begin here. */

static int selinux_ptrace(struct task_struct *parent, struct task_struct *child)
{
	struct task_security_struct *psec = parent->security;
	struct task_security_struct *csec = child->security;
	int rc;

	rc = secondary_ops->ptrace(parent,child);
	if (rc)
		return rc;

	rc = task_has_perm(parent, child, PROCESS__PTRACE);
	/* Save the SID of the tracing process for later use in apply_creds. */
	if (!(child->ptrace & PT_PTRACED) && !rc)
		csec->ptrace_sid = psec->sid;
	return rc;
}

static int selinux_capget(struct task_struct *target, kernel_cap_t *effective,
                          kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	int error;

	error = task_has_perm(current, target, PROCESS__GETCAP);
	if (error)
		return error;

	return secondary_ops->capget(target, effective, inheritable, permitted);
}

static int selinux_capset_check(struct task_struct *target, kernel_cap_t *effective,
                                kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	int error;

	error = secondary_ops->capset_check(target, effective, inheritable, permitted);
	if (error)
		return error;

	return task_has_perm(current, target, PROCESS__SETCAP);
}

static void selinux_capset_set(struct task_struct *target, kernel_cap_t *effective,
                               kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	secondary_ops->capset_set(target, effective, inheritable, permitted);
}

static int selinux_capable(struct task_struct *tsk, int cap)
{
	int rc;

	rc = secondary_ops->capable(tsk, cap);
	if (rc)
		return rc;

	return task_has_capability(tsk,cap);
}

static int selinux_sysctl_get_sid(ctl_table *table, u16 tclass, u32 *sid)
{
	int buflen, rc;
	char *buffer, *path, *end;

	rc = -ENOMEM;
	buffer = (char*)__get_free_page(GFP_KERNEL);
	if (!buffer)
		goto out;

	buflen = PAGE_SIZE;
	end = buffer+buflen;
	*--end = '\0';
	buflen--;
	path = end-1;
	*path = '/';
	while (table) {
		const char *name = table->procname;
		size_t namelen = strlen(name);
		buflen -= namelen + 1;
		if (buflen < 0)
			goto out_free;
		end -= namelen;
		memcpy(end, name, namelen);
		*--end = '/';
		path = end;
		table = table->parent;
	}
	buflen -= 4;
	if (buflen < 0)
		goto out_free;
	end -= 4;
	memcpy(end, "/sys", 4);
	path = end;
	rc = security_genfs_sid("proc", path, tclass, sid);
out_free:
	free_page((unsigned long)buffer);
out:
	return rc;
}

static int selinux_sysctl(ctl_table *table, int op)
{
	int error = 0;
	u32 av;
	struct task_security_struct *tsec;
	u32 tsid;
	int rc;

	rc = secondary_ops->sysctl(table, op);
	if (rc)
		return rc;

	tsec = current->security;

	rc = selinux_sysctl_get_sid(table, (op == 0001) ?
				    SECCLASS_DIR : SECCLASS_FILE, &tsid);
	if (rc) {
		/* Default to the well-defined sysctl SID. */
		tsid = SECINITSID_SYSCTL;
	}

	/* The op values are "defined" in sysctl.c, thereby creating
	 * a bad coupling between this module and sysctl.c */
	if(op == 001) {
		error = avc_has_perm(tsec->sid, tsid,
				     SECCLASS_DIR, DIR__SEARCH, NULL);
	} else {
		av = 0;
		if (op & 004)
			av |= FILE__READ;
		if (op & 002)
			av |= FILE__WRITE;
		if (av)
			error = avc_has_perm(tsec->sid, tsid,
					     SECCLASS_FILE, av, NULL);
        }

	return error;
}

static int selinux_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	int rc = 0;

	if (!sb)
		return 0;

	switch (cmds) {
		case Q_SYNC:
		case Q_QUOTAON:
		case Q_QUOTAOFF:
	        case Q_SETINFO:
		case Q_SETQUOTA:
			rc = superblock_has_perm(current,
						 sb,
						 FILESYSTEM__QUOTAMOD, NULL);
			break;
	        case Q_GETFMT:
	        case Q_GETINFO:
		case Q_GETQUOTA:
			rc = superblock_has_perm(current,
						 sb,
						 FILESYSTEM__QUOTAGET, NULL);
			break;
		default:
			rc = 0;  /* let the kernel handle invalid cmds */
			break;
	}
	return rc;
}

static int selinux_quota_on(struct dentry *dentry)
{
	return dentry_has_perm(current, NULL, dentry, FILE__QUOTAON);
}

static int selinux_syslog(int type)
{
	int rc;

	rc = secondary_ops->syslog(type);
	if (rc)
		return rc;

	switch (type) {
		case 3:         /* Read last kernel messages */
		case 10:        /* Return size of the log buffer */
			rc = task_has_system(current, SYSTEM__SYSLOG_READ);
			break;
		case 6:         /* Disable logging to console */
		case 7:         /* Enable logging to console */
		case 8:		/* Set level of messages printed to console */
			rc = task_has_system(current, SYSTEM__SYSLOG_CONSOLE);
			break;
		case 0:         /* Close log */
		case 1:         /* Open log */
		case 2:         /* Read from log */
		case 4:         /* Read/clear last kernel messages */
		case 5:         /* Clear ring buffer */
		default:
			rc = task_has_system(current, SYSTEM__SYSLOG_MOD);
			break;
	}
	return rc;
}

/*
 * Check that a process has enough memory to allocate a new virtual
 * mapping. 0 means there is enough memory for the allocation to
 * succeed and -ENOMEM implies there is not.
 *
 * Note that secondary_ops->capable and task_has_perm_noaudit return 0
 * if the capability is granted, but __vm_enough_memory requires 1 if
 * the capability is granted.
 *
 * Do not audit the selinux permission check, as this is applied to all
 * processes that allocate mappings.
 */
static int selinux_vm_enough_memory(long pages)
{
	int rc, cap_sys_admin = 0;
	struct task_security_struct *tsec = current->security;

	rc = secondary_ops->capable(current, CAP_SYS_ADMIN);
	if (rc == 0)
		rc = avc_has_perm_noaudit(tsec->sid, tsec->sid,
					SECCLASS_CAPABILITY,
					CAP_TO_MASK(CAP_SYS_ADMIN),
					NULL);

	if (rc == 0)
		cap_sys_admin = 1;

	return __vm_enough_memory(pages, cap_sys_admin);
}

/* binprm security operations */

static int selinux_bprm_alloc_security(struct linux_binprm *bprm)
{
	struct bprm_security_struct *bsec;

	bsec = kzalloc(sizeof(struct bprm_security_struct), GFP_KERNEL);
	if (!bsec)
		return -ENOMEM;

	bsec->bprm = bprm;
	bsec->sid = SECINITSID_UNLABELED;
	bsec->set = 0;

	bprm->security = bsec;
	return 0;
}

static int selinux_bprm_set_security(struct linux_binprm *bprm)
{
	struct task_security_struct *tsec;
	struct inode *inode = bprm->file->f_path.dentry->d_inode;
	struct inode_security_struct *isec;
	struct bprm_security_struct *bsec;
	u32 newsid;
	struct avc_audit_data ad;
	int rc;

	rc = secondary_ops->bprm_set_security(bprm);
	if (rc)
		return rc;

	bsec = bprm->security;

	if (bsec->set)
		return 0;

	tsec = current->security;
	isec = inode->i_security;

	/* Default to the current task SID. */
	bsec->sid = tsec->sid;

	/* Reset fs, key, and sock SIDs on execve. */
	tsec->create_sid = 0;
	tsec->keycreate_sid = 0;
	tsec->sockcreate_sid = 0;

	if (tsec->exec_sid) {
		newsid = tsec->exec_sid;
		/* Reset exec SID on execve. */
		tsec->exec_sid = 0;
	} else {
		/* Check for a default transition on this program. */
		rc = security_transition_sid(tsec->sid, isec->sid,
		                             SECCLASS_PROCESS, &newsid);
		if (rc)
			return rc;
	}

	AVC_AUDIT_DATA_INIT(&ad, FS);
	ad.u.fs.mnt = bprm->file->f_path.mnt;
	ad.u.fs.dentry = bprm->file->f_path.dentry;

	if (bprm->file->f_path.mnt->mnt_flags & MNT_NOSUID)
		newsid = tsec->sid;

        if (tsec->sid == newsid) {
		rc = avc_has_perm(tsec->sid, isec->sid,
				  SECCLASS_FILE, FILE__EXECUTE_NO_TRANS, &ad);
		if (rc)
			return rc;
	} else {
		/* Check permissions for the transition. */
		rc = avc_has_perm(tsec->sid, newsid,
				  SECCLASS_PROCESS, PROCESS__TRANSITION, &ad);
		if (rc)
			return rc;

		rc = avc_has_perm(newsid, isec->sid,
				  SECCLASS_FILE, FILE__ENTRYPOINT, &ad);
		if (rc)
			return rc;

		/* Clear any possibly unsafe personality bits on exec: */
		current->personality &= ~PER_CLEAR_ON_SETID;

		/* Set the security field to the new SID. */
		bsec->sid = newsid;
	}

	bsec->set = 1;
	return 0;
}

static int selinux_bprm_check_security (struct linux_binprm *bprm)
{
	return secondary_ops->bprm_check_security(bprm);
}


static int selinux_bprm_secureexec (struct linux_binprm *bprm)
{
	struct task_security_struct *tsec = current->security;
	int atsecure = 0;

	if (tsec->osid != tsec->sid) {
		/* Enable secure mode for SIDs transitions unless
		   the noatsecure permission is granted between
		   the two SIDs, i.e. ahp returns 0. */
		atsecure = avc_has_perm(tsec->osid, tsec->sid,
					 SECCLASS_PROCESS,
					 PROCESS__NOATSECURE, NULL);
	}

	return (atsecure || secondary_ops->bprm_secureexec(bprm));
}

static void selinux_bprm_free_security(struct linux_binprm *bprm)
{
	kfree(bprm->security);
	bprm->security = NULL;
}

extern struct vfsmount *selinuxfs_mount;
extern struct dentry *selinux_null;

/* Derived from fs/exec.c:flush_old_files. */
static inline void flush_unauthorized_files(struct files_struct * files)
{
	struct avc_audit_data ad;
	struct file *file, *devnull = NULL;
	struct tty_struct *tty;
	struct fdtable *fdt;
	long j = -1;
	int drop_tty = 0;

	mutex_lock(&tty_mutex);
	tty = get_current_tty();
	if (tty) {
		file_list_lock();
		file = list_entry(tty->tty_files.next, typeof(*file), f_u.fu_list);
		if (file) {
			/* Revalidate access to controlling tty.
			   Use inode_has_perm on the tty inode directly rather
			   than using file_has_perm, as this particular open
			   file may belong to another process and we are only
			   interested in the inode-based check here. */
			struct inode *inode = file->f_path.dentry->d_inode;
			if (inode_has_perm(current, inode,
					   FILE__READ | FILE__WRITE, NULL)) {
				drop_tty = 1;
			}
		}
		file_list_unlock();

		/* Reset controlling tty. */
		if (drop_tty)
			proc_set_tty(current, NULL);
	}
	mutex_unlock(&tty_mutex);

	/* Revalidate access to inherited open files. */

	AVC_AUDIT_DATA_INIT(&ad,FS);

	spin_lock(&files->file_lock);
	for (;;) {
		unsigned long set, i;
		int fd;

		j++;
		i = j * __NFDBITS;
		fdt = files_fdtable(files);
		if (i >= fdt->max_fds)
			break;
		set = fdt->open_fds->fds_bits[j];
		if (!set)
			continue;
		spin_unlock(&files->file_lock);
		for ( ; set ; i++,set >>= 1) {
			if (set & 1) {
				file = fget(i);
				if (!file)
					continue;
				if (file_has_perm(current,
						  file,
						  file_to_av(file))) {
					sys_close(i);
					fd = get_unused_fd();
					if (fd != i) {
						if (fd >= 0)
							put_unused_fd(fd);
						fput(file);
						continue;
					}
					if (devnull) {
						get_file(devnull);
					} else {
						devnull = dentry_open(dget(selinux_null), mntget(selinuxfs_mount), O_RDWR);
						if (IS_ERR(devnull)) {
							devnull = NULL;
							put_unused_fd(fd);
							fput(file);
							continue;
						}
					}
					fd_install(fd, devnull);
				}
				fput(file);
			}
		}
		spin_lock(&files->file_lock);

	}
	spin_unlock(&files->file_lock);
}

static void selinux_bprm_apply_creds(struct linux_binprm *bprm, int unsafe)
{
	struct task_security_struct *tsec;
	struct bprm_security_struct *bsec;
	u32 sid;
	int rc;

	secondary_ops->bprm_apply_creds(bprm, unsafe);

	tsec = current->security;

	bsec = bprm->security;
	sid = bsec->sid;

	tsec->osid = tsec->sid;
	bsec->unsafe = 0;
	if (tsec->sid != sid) {
		/* Check for shared state.  If not ok, leave SID
		   unchanged and kill. */
		if (unsafe & LSM_UNSAFE_SHARE) {
			rc = avc_has_perm(tsec->sid, sid, SECCLASS_PROCESS,
					PROCESS__SHARE, NULL);
			if (rc) {
				bsec->unsafe = 1;
				return;
			}
		}

		/* Check for ptracing, and update the task SID if ok.
		   Otherwise, leave SID unchanged and kill. */
		if (unsafe & (LSM_UNSAFE_PTRACE | LSM_UNSAFE_PTRACE_CAP)) {
			rc = avc_has_perm(tsec->ptrace_sid, sid,
					  SECCLASS_PROCESS, PROCESS__PTRACE,
					  NULL);
			if (rc) {
				bsec->unsafe = 1;
				return;
			}
		}
		tsec->sid = sid;
	}
}

/*
 * called after apply_creds without the task lock held
 */
static void selinux_bprm_post_apply_creds(struct linux_binprm *bprm)
{
	struct task_security_struct *tsec;
	struct rlimit *rlim, *initrlim;
	struct itimerval itimer;
	struct bprm_security_struct *bsec;
	int rc, i;

	tsec = current->security;
	bsec = bprm->security;

	if (bsec->unsafe) {
		force_sig_specific(SIGKILL, current);
		return;
	}
	if (tsec->osid == tsec->sid)
		return;

	/* Close files for which the new task SID is not authorized. */
	flush_unauthorized_files(current->files);

	/* Check whether the new SID can inherit signal state
	   from the old SID.  If not, clear itimers to avoid
	   subsequent signal generation and flush and unblock
	   signals. This must occur _after_ the task SID has
	  been updated so that any kill done after the flush
	  will be checked against the new SID. */
	rc = avc_has_perm(tsec->osid, tsec->sid, SECCLASS_PROCESS,
			  PROCESS__SIGINH, NULL);
	if (rc) {
		memset(&itimer, 0, sizeof itimer);
		for (i = 0; i < 3; i++)
			do_setitimer(i, &itimer, NULL);
		flush_signals(current);
		spin_lock_irq(&current->sighand->siglock);
		flush_signal_handlers(current, 1);
		sigemptyset(&current->blocked);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}

	/* Check whether the new SID can inherit resource limits
	   from the old SID.  If not, reset all soft limits to
	   the lower of the current task's hard limit and the init
	   task's soft limit.  Note that the setting of hard limits
	   (even to lower them) can be controlled by the setrlimit
	   check. The inclusion of the init task's soft limit into
	   the computation is to avoid resetting soft limits higher
	   than the default soft limit for cases where the default
	   is lower than the hard limit, e.g. RLIMIT_CORE or
	   RLIMIT_STACK.*/
	rc = avc_has_perm(tsec->osid, tsec->sid, SECCLASS_PROCESS,
			  PROCESS__RLIMITINH, NULL);
	if (rc) {
		for (i = 0; i < RLIM_NLIMITS; i++) {
			rlim = current->signal->rlim + i;
			initrlim = init_task.signal->rlim+i;
			rlim->rlim_cur = min(rlim->rlim_max,initrlim->rlim_cur);
		}
		if (current->signal->rlim[RLIMIT_CPU].rlim_cur != RLIM_INFINITY) {
			/*
			 * This will cause RLIMIT_CPU calculations
			 * to be refigured.
			 */
			current->it_prof_expires = jiffies_to_cputime(1);
		}
	}

	/* Wake up the parent if it is waiting so that it can
	   recheck wait permission to the new task SID. */
	wake_up_interruptible(&current->parent->signal->wait_chldexit);
}

/* superblock security operations */

static int selinux_sb_alloc_security(struct super_block *sb)
{
	return superblock_alloc_security(sb);
}

static void selinux_sb_free_security(struct super_block *sb)
{
	superblock_free_security(sb);
}

static inline int match_prefix(char *prefix, int plen, char *option, int olen)
{
	if (plen > olen)
		return 0;

	return !memcmp(prefix, option, plen);
}

static inline int selinux_option(char *option, int len)
{
	return (match_prefix("context=", sizeof("context=")-1, option, len) ||
	        match_prefix("fscontext=", sizeof("fscontext=")-1, option, len) ||
	        match_prefix("defcontext=", sizeof("defcontext=")-1, option, len) ||
		match_prefix("rootcontext=", sizeof("rootcontext=")-1, option, len));
}

static inline void take_option(char **to, char *from, int *first, int len)
{
	if (!*first) {
		**to = ',';
		*to += 1;
	} else
		*first = 0;
	memcpy(*to, from, len);
	*to += len;
}

static inline void take_selinux_option(char **to, char *from, int *first, 
		                       int len)
{
	int current_size = 0;

	if (!*first) {
		**to = '|';
		*to += 1;
	}
	else
		*first = 0;

	while (current_size < len) {
		if (*from != '"') {
			**to = *from;
			*to += 1;
		}
		from += 1;
		current_size += 1;
	}
}

static int selinux_sb_copy_data(struct file_system_type *type, void *orig, void *copy)
{
	int fnosec, fsec, rc = 0;
	char *in_save, *in_curr, *in_end;
	char *sec_curr, *nosec_save, *nosec;
	int open_quote = 0;

	in_curr = orig;
	sec_curr = copy;

	/* Binary mount data: just copy */
	if (type->fs_flags & FS_BINARY_MOUNTDATA) {
		copy_page(sec_curr, in_curr);
		goto out;
	}

	nosec = (char *)get_zeroed_page(GFP_KERNEL);
	if (!nosec) {
		rc = -ENOMEM;
		goto out;
	}

	nosec_save = nosec;
	fnosec = fsec = 1;
	in_save = in_end = orig;

	do {
		if (*in_end == '"')
			open_quote = !open_quote;
		if ((*in_end == ',' && open_quote == 0) ||
				*in_end == '\0') {
			int len = in_end - in_curr;

			if (selinux_option(in_curr, len))
				take_selinux_option(&sec_curr, in_curr, &fsec, len);
			else
				take_option(&nosec, in_curr, &fnosec, len);

			in_curr = in_end + 1;
		}
	} while (*in_end++);

	strcpy(in_save, nosec_save);
	free_page((unsigned long)nosec_save);
out:
	return rc;
}

static int selinux_sb_kern_mount(struct super_block *sb, void *data)
{
	struct avc_audit_data ad;
	int rc;

	rc = superblock_doinit(sb, data);
	if (rc)
		return rc;

	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.dentry = sb->s_root;
	return superblock_has_perm(current, sb, FILESYSTEM__MOUNT, &ad);
}

static int selinux_sb_statfs(struct dentry *dentry)
{
	struct avc_audit_data ad;

	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.dentry = dentry->d_sb->s_root;
	return superblock_has_perm(current, dentry->d_sb, FILESYSTEM__GETATTR, &ad);
}

static int selinux_mount(char * dev_name,
                         struct nameidata *nd,
                         char * type,
                         unsigned long flags,
                         void * data)
{
	int rc;

	rc = secondary_ops->sb_mount(dev_name, nd, type, flags, data);
	if (rc)
		return rc;

	if (flags & MS_REMOUNT)
		return superblock_has_perm(current, nd->mnt->mnt_sb,
		                           FILESYSTEM__REMOUNT, NULL);
	else
		return dentry_has_perm(current, nd->mnt, nd->dentry,
		                       FILE__MOUNTON);
}

static int selinux_umount(struct vfsmount *mnt, int flags)
{
	int rc;

	rc = secondary_ops->sb_umount(mnt, flags);
	if (rc)
		return rc;

	return superblock_has_perm(current,mnt->mnt_sb,
	                           FILESYSTEM__UNMOUNT,NULL);
}

/* inode security operations */

static int selinux_inode_alloc_security(struct inode *inode)
{
	return inode_alloc_security(inode);
}

static void selinux_inode_free_security(struct inode *inode)
{
	inode_free_security(inode);
}

static int selinux_inode_init_security(struct inode *inode, struct inode *dir,
				       char **name, void **value,
				       size_t *len)
{
	struct task_security_struct *tsec;
	struct inode_security_struct *dsec;
	struct superblock_security_struct *sbsec;
	u32 newsid, clen;
	int rc;
	char *namep = NULL, *context;

	tsec = current->security;
	dsec = dir->i_security;
	sbsec = dir->i_sb->s_security;

	if (tsec->create_sid && sbsec->behavior != SECURITY_FS_USE_MNTPOINT) {
		newsid = tsec->create_sid;
	} else {
		rc = security_transition_sid(tsec->sid, dsec->sid,
					     inode_mode_to_security_class(inode->i_mode),
					     &newsid);
		if (rc) {
			printk(KERN_WARNING "%s:  "
			       "security_transition_sid failed, rc=%d (dev=%s "
			       "ino=%ld)\n",
			       __FUNCTION__,
			       -rc, inode->i_sb->s_id, inode->i_ino);
			return rc;
		}
	}

	/* Possibly defer initialization to selinux_complete_init. */
	if (sbsec->initialized) {
		struct inode_security_struct *isec = inode->i_security;
		isec->sclass = inode_mode_to_security_class(inode->i_mode);
		isec->sid = newsid;
		isec->initialized = 1;
	}

	if (!ss_initialized || sbsec->behavior == SECURITY_FS_USE_MNTPOINT)
		return -EOPNOTSUPP;

	if (name) {
		namep = kstrdup(XATTR_SELINUX_SUFFIX, GFP_KERNEL);
		if (!namep)
			return -ENOMEM;
		*name = namep;
	}

	if (value && len) {
		rc = security_sid_to_context(newsid, &context, &clen);
		if (rc) {
			kfree(namep);
			return rc;
		}
		*value = context;
		*len = clen;
	}

	return 0;
}

static int selinux_inode_create(struct inode *dir, struct dentry *dentry, int mask)
{
	return may_create(dir, dentry, SECCLASS_FILE);
}

static int selinux_inode_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry)
{
	int rc;

	rc = secondary_ops->inode_link(old_dentry,dir,new_dentry);
	if (rc)
		return rc;
	return may_link(dir, old_dentry, MAY_LINK);
}

static int selinux_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	int rc;

	rc = secondary_ops->inode_unlink(dir, dentry);
	if (rc)
		return rc;
	return may_link(dir, dentry, MAY_UNLINK);
}

static int selinux_inode_symlink(struct inode *dir, struct dentry *dentry, const char *name)
{
	return may_create(dir, dentry, SECCLASS_LNK_FILE);
}

static int selinux_inode_mkdir(struct inode *dir, struct dentry *dentry, int mask)
{
	return may_create(dir, dentry, SECCLASS_DIR);
}

static int selinux_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	return may_link(dir, dentry, MAY_RMDIR);
}

static int selinux_inode_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	int rc;

	rc = secondary_ops->inode_mknod(dir, dentry, mode, dev);
	if (rc)
		return rc;

	return may_create(dir, dentry, inode_mode_to_security_class(mode));
}

static int selinux_inode_rename(struct inode *old_inode, struct dentry *old_dentry,
                                struct inode *new_inode, struct dentry *new_dentry)
{
	return may_rename(old_inode, old_dentry, new_inode, new_dentry);
}

static int selinux_inode_readlink(struct dentry *dentry)
{
	return dentry_has_perm(current, NULL, dentry, FILE__READ);
}

static int selinux_inode_follow_link(struct dentry *dentry, struct nameidata *nameidata)
{
	int rc;

	rc = secondary_ops->inode_follow_link(dentry,nameidata);
	if (rc)
		return rc;
	return dentry_has_perm(current, NULL, dentry, FILE__READ);
}

static int selinux_inode_permission(struct inode *inode, int mask,
				    struct nameidata *nd)
{
	int rc;

	rc = secondary_ops->inode_permission(inode, mask, nd);
	if (rc)
		return rc;

	if (!mask) {
		/* No permission to check.  Existence test. */
		return 0;
	}

	return inode_has_perm(current, inode,
			       file_mask_to_av(inode->i_mode, mask), NULL);
}

static int selinux_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int rc;

	rc = secondary_ops->inode_setattr(dentry, iattr);
	if (rc)
		return rc;

	if (iattr->ia_valid & ATTR_FORCE)
		return 0;

	if (iattr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID |
			       ATTR_ATIME_SET | ATTR_MTIME_SET))
		return dentry_has_perm(current, NULL, dentry, FILE__SETATTR);

	return dentry_has_perm(current, NULL, dentry, FILE__WRITE);
}

static int selinux_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	return dentry_has_perm(current, mnt, dentry, FILE__GETATTR);
}

static int selinux_inode_setxattr(struct dentry *dentry, char *name, void *value, size_t size, int flags)
{
	struct task_security_struct *tsec = current->security;
	struct inode *inode = dentry->d_inode;
	struct inode_security_struct *isec = inode->i_security;
	struct superblock_security_struct *sbsec;
	struct avc_audit_data ad;
	u32 newsid;
	int rc = 0;

	if (strcmp(name, XATTR_NAME_SELINUX)) {
		if (!strncmp(name, XATTR_SECURITY_PREFIX,
			     sizeof XATTR_SECURITY_PREFIX - 1) &&
		    !capable(CAP_SYS_ADMIN)) {
			/* A different attribute in the security namespace.
			   Restrict to administrator. */
			return -EPERM;
		}

		/* Not an attribute we recognize, so just check the
		   ordinary setattr permission. */
		return dentry_has_perm(current, NULL, dentry, FILE__SETATTR);
	}

	sbsec = inode->i_sb->s_security;
	if (sbsec->behavior == SECURITY_FS_USE_MNTPOINT)
		return -EOPNOTSUPP;

	if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
		return -EPERM;

	AVC_AUDIT_DATA_INIT(&ad,FS);
	ad.u.fs.dentry = dentry;

	rc = avc_has_perm(tsec->sid, isec->sid, isec->sclass,
			  FILE__RELABELFROM, &ad);
	if (rc)
		return rc;

	rc = security_context_to_sid(value, size, &newsid);
	if (rc)
		return rc;

	rc = avc_has_perm(tsec->sid, newsid, isec->sclass,
			  FILE__RELABELTO, &ad);
	if (rc)
		return rc;

	rc = security_validate_transition(isec->sid, newsid, tsec->sid,
	                                  isec->sclass);
	if (rc)
		return rc;

	return avc_has_perm(newsid,
			    sbsec->sid,
			    SECCLASS_FILESYSTEM,
			    FILESYSTEM__ASSOCIATE,
			    &ad);
}

static void selinux_inode_post_setxattr(struct dentry *dentry, char *name,
                                        void *value, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct inode_security_struct *isec = inode->i_security;
	u32 newsid;
	int rc;

	if (strcmp(name, XATTR_NAME_SELINUX)) {
		/* Not an attribute we recognize, so nothing to do. */
		return;
	}

	rc = security_context_to_sid(value, size, &newsid);
	if (rc) {
		printk(KERN_WARNING "%s:  unable to obtain SID for context "
		       "%s, rc=%d\n", __FUNCTION__, (char*)value, -rc);
		return;
	}

	isec->sid = newsid;
	return;
}

static int selinux_inode_getxattr (struct dentry *dentry, char *name)
{
	return dentry_has_perm(current, NULL, dentry, FILE__GETATTR);
}

static int selinux_inode_listxattr (struct dentry *dentry)
{
	return dentry_has_perm(current, NULL, dentry, FILE__GETATTR);
}

static int selinux_inode_removexattr (struct dentry *dentry, char *name)
{
	if (strcmp(name, XATTR_NAME_SELINUX)) {
		if (!strncmp(name, XATTR_SECURITY_PREFIX,
			     sizeof XATTR_SECURITY_PREFIX - 1) &&
		    !capable(CAP_SYS_ADMIN)) {
			/* A different attribute in the security namespace.
			   Restrict to administrator. */
			return -EPERM;
		}

		/* Not an attribute we recognize, so just check the
		   ordinary setattr permission. Might want a separate
		   permission for removexattr. */
		return dentry_has_perm(current, NULL, dentry, FILE__SETATTR);
	}

	/* No one is allowed to remove a SELinux security label.
	   You can change the label, but all data must be labeled. */
	return -EACCES;
}

static const char *selinux_inode_xattr_getsuffix(void)
{
      return XATTR_SELINUX_SUFFIX;
}

/*
 * Copy the in-core inode security context value to the user.  If the
 * getxattr() prior to this succeeded, check to see if we need to
 * canonicalize the value to be finally returned to the user.
 *
 * Permission check is handled by selinux_inode_getxattr hook.
 */
static int selinux_inode_getsecurity(const struct inode *inode, const char *name, void *buffer, size_t size, int err)
{
	struct inode_security_struct *isec = inode->i_security;

	if (strcmp(name, XATTR_SELINUX_SUFFIX))
		return -EOPNOTSUPP;

	return selinux_getsecurity(isec->sid, buffer, size);
}

static int selinux_inode_setsecurity(struct inode *inode, const char *name,
                                     const void *value, size_t size, int flags)
{
	struct inode_security_struct *isec = inode->i_security;
	u32 newsid;
	int rc;

	if (strcmp(name, XATTR_SELINUX_SUFFIX))
		return -EOPNOTSUPP;

	if (!value || !size)
		return -EACCES;

	rc = security_context_to_sid((void*)value, size, &newsid);
	if (rc)
		return rc;

	isec->sid = newsid;
	return 0;
}

static int selinux_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	const int len = sizeof(XATTR_NAME_SELINUX);
	if (buffer && len <= buffer_size)
		memcpy(buffer, XATTR_NAME_SELINUX, len);
	return len;
}

/* file security operations */

static int selinux_file_permission(struct file *file, int mask)
{
	int rc;
	struct inode *inode = file->f_path.dentry->d_inode;

	if (!mask) {
		/* No permission to check.  Existence test. */
		return 0;
	}

	/* file_mask_to_av won't add FILE__WRITE if MAY_APPEND is set */
	if ((file->f_flags & O_APPEND) && (mask & MAY_WRITE))
		mask |= MAY_APPEND;

	rc = file_has_perm(current, file,
			   file_mask_to_av(inode->i_mode, mask));
	if (rc)
		return rc;

	return selinux_netlbl_inode_permission(inode, mask);
}

static int selinux_file_alloc_security(struct file *file)
{
	return file_alloc_security(file);
}

static void selinux_file_free_security(struct file *file)
{
	file_free_security(file);
}

static int selinux_file_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	int error = 0;

	switch (cmd) {
		case FIONREAD:
		/* fall through */
		case FIBMAP:
		/* fall through */
		case FIGETBSZ:
		/* fall through */
		case EXT2_IOC_GETFLAGS:
		/* fall through */
		case EXT2_IOC_GETVERSION:
			error = file_has_perm(current, file, FILE__GETATTR);
			break;

		case EXT2_IOC_SETFLAGS:
		/* fall through */
		case EXT2_IOC_SETVERSION:
			error = file_has_perm(current, file, FILE__SETATTR);
			break;

		/* sys_ioctl() checks */
		case FIONBIO:
		/* fall through */
		case FIOASYNC:
			error = file_has_perm(current, file, 0);
			break;

	        case KDSKBENT:
	        case KDSKBSENT:
			error = task_has_capability(current,CAP_SYS_TTY_CONFIG);
			break;

		/* default case assumes that the command will go
		 * to the file's ioctl() function.
		 */
		default:
			error = file_has_perm(current, file, FILE__IOCTL);

	}
	return error;
}

static int file_map_prot_check(struct file *file, unsigned long prot, int shared)
{
#ifndef CONFIG_PPC32
	if ((prot & PROT_EXEC) && (!file || (!shared && (prot & PROT_WRITE)))) {
		/*
		 * We are making executable an anonymous mapping or a
		 * private file mapping that will also be writable.
		 * This has an additional check.
		 */
		int rc = task_has_perm(current, current, PROCESS__EXECMEM);
		if (rc)
			return rc;
	}
#endif

	if (file) {
		/* read access is always possible with a mapping */
		u32 av = FILE__READ;

		/* write access only matters if the mapping is shared */
		if (shared && (prot & PROT_WRITE))
			av |= FILE__WRITE;

		if (prot & PROT_EXEC)
			av |= FILE__EXECUTE;

		return file_has_perm(current, file, av);
	}
	return 0;
}

static int selinux_file_mmap(struct file *file, unsigned long reqprot,
			     unsigned long prot, unsigned long flags)
{
	int rc;

	rc = secondary_ops->file_mmap(file, reqprot, prot, flags);
	if (rc)
		return rc;

	if (selinux_checkreqprot)
		prot = reqprot;

	return file_map_prot_check(file, prot,
				   (flags & MAP_TYPE) == MAP_SHARED);
}

static int selinux_file_mprotect(struct vm_area_struct *vma,
				 unsigned long reqprot,
				 unsigned long prot)
{
	int rc;

	rc = secondary_ops->file_mprotect(vma, reqprot, prot);
	if (rc)
		return rc;

	if (selinux_checkreqprot)
		prot = reqprot;

#ifndef CONFIG_PPC32
	if ((prot & PROT_EXEC) && !(vma->vm_flags & VM_EXEC)) {
		rc = 0;
		if (vma->vm_start >= vma->vm_mm->start_brk &&
		    vma->vm_end <= vma->vm_mm->brk) {
			rc = task_has_perm(current, current,
					   PROCESS__EXECHEAP);
		} else if (!vma->vm_file &&
			   vma->vm_start <= vma->vm_mm->start_stack &&
			   vma->vm_end >= vma->vm_mm->start_stack) {
			rc = task_has_perm(current, current, PROCESS__EXECSTACK);
		} else if (vma->vm_file && vma->anon_vma) {
			/*
			 * We are making executable a file mapping that has
			 * had some COW done. Since pages might have been
			 * written, check ability to execute the possibly
			 * modified content.  This typically should only
			 * occur for text relocations.
			 */
			rc = file_has_perm(current, vma->vm_file,
					   FILE__EXECMOD);
		}
		if (rc)
			return rc;
	}
#endif

	return file_map_prot_check(vma->vm_file, prot, vma->vm_flags&VM_SHARED);
}

static int selinux_file_lock(struct file *file, unsigned int cmd)
{
	return file_has_perm(current, file, FILE__LOCK);
}

static int selinux_file_fcntl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	int err = 0;

	switch (cmd) {
	        case F_SETFL:
			if (!file->f_path.dentry || !file->f_path.dentry->d_inode) {
				err = -EINVAL;
				break;
			}

			if ((file->f_flags & O_APPEND) && !(arg & O_APPEND)) {
				err = file_has_perm(current, file,FILE__WRITE);
				break;
			}
			/* fall through */
	        case F_SETOWN:
	        case F_SETSIG:
	        case F_GETFL:
	        case F_GETOWN:
	        case F_GETSIG:
			/* Just check FD__USE permission */
			err = file_has_perm(current, file, 0);
			break;
		case F_GETLK:
		case F_SETLK:
	        case F_SETLKW:
#if BITS_PER_LONG == 32
	        case F_GETLK64:
		case F_SETLK64:
	        case F_SETLKW64:
#endif
			if (!file->f_path.dentry || !file->f_path.dentry->d_inode) {
				err = -EINVAL;
				break;
			}
			err = file_has_perm(current, file, FILE__LOCK);
			break;
	}

	return err;
}

static int selinux_file_set_fowner(struct file *file)
{
	struct task_security_struct *tsec;
	struct file_security_struct *fsec;

	tsec = current->security;
	fsec = file->f_security;
	fsec->fown_sid = tsec->sid;

	return 0;
}

static int selinux_file_send_sigiotask(struct task_struct *tsk,
				       struct fown_struct *fown, int signum)
{
        struct file *file;
	u32 perm;
	struct task_security_struct *tsec;
	struct file_security_struct *fsec;

	/* struct fown_struct is never outside the context of a struct file */
        file = container_of(fown, struct file, f_owner);

	tsec = tsk->security;
	fsec = file->f_security;

	if (!signum)
		perm = signal_to_av(SIGIO); /* as per send_sigio_to_task */
	else
		perm = signal_to_av(signum);

	return avc_has_perm(fsec->fown_sid, tsec->sid,
			    SECCLASS_PROCESS, perm, NULL);
}

static int selinux_file_receive(struct file *file)
{
	return file_has_perm(current, file, file_to_av(file));
}

/* task security operations */

static int selinux_task_create(unsigned long clone_flags)
{
	int rc;

	rc = secondary_ops->task_create(clone_flags);
	if (rc)
		return rc;

	return task_has_perm(current, current, PROCESS__FORK);
}

static int selinux_task_alloc_security(struct task_struct *tsk)
{
	struct task_security_struct *tsec1, *tsec2;
	int rc;

	tsec1 = current->security;

	rc = task_alloc_security(tsk);
	if (rc)
		return rc;
	tsec2 = tsk->security;

	tsec2->osid = tsec1->osid;
	tsec2->sid = tsec1->sid;

	/* Retain the exec, fs, key, and sock SIDs across fork */
	tsec2->exec_sid = tsec1->exec_sid;
	tsec2->create_sid = tsec1->create_sid;
	tsec2->keycreate_sid = tsec1->keycreate_sid;
	tsec2->sockcreate_sid = tsec1->sockcreate_sid;

	/* Retain ptracer SID across fork, if any.
	   This will be reset by the ptrace hook upon any
	   subsequent ptrace_attach operations. */
	tsec2->ptrace_sid = tsec1->ptrace_sid;

	return 0;
}

static void selinux_task_free_security(struct task_struct *tsk)
{
	task_free_security(tsk);
}

static int selinux_task_setuid(uid_t id0, uid_t id1, uid_t id2, int flags)
{
	/* Since setuid only affects the current process, and
	   since the SELinux controls are not based on the Linux
	   identity attributes, SELinux does not need to control
	   this operation.  However, SELinux does control the use
	   of the CAP_SETUID and CAP_SETGID capabilities using the
	   capable hook. */
	return 0;
}

static int selinux_task_post_setuid(uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return secondary_ops->task_post_setuid(id0,id1,id2,flags);
}

static int selinux_task_setgid(gid_t id0, gid_t id1, gid_t id2, int flags)
{
	/* See the comment for setuid above. */
	return 0;
}

static int selinux_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return task_has_perm(current, p, PROCESS__SETPGID);
}

static int selinux_task_getpgid(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETPGID);
}

static int selinux_task_getsid(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETSESSION);
}

static void selinux_task_getsecid(struct task_struct *p, u32 *secid)
{
	selinux_get_task_sid(p, secid);
}

static int selinux_task_setgroups(struct group_info *group_info)
{
	/* See the comment for setuid above. */
	return 0;
}

static int selinux_task_setnice(struct task_struct *p, int nice)
{
	int rc;

	rc = secondary_ops->task_setnice(p, nice);
	if (rc)
		return rc;

	return task_has_perm(current,p, PROCESS__SETSCHED);
}

static int selinux_task_setioprio(struct task_struct *p, int ioprio)
{
	return task_has_perm(current, p, PROCESS__SETSCHED);
}

static int selinux_task_getioprio(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETSCHED);
}

static int selinux_task_setrlimit(unsigned int resource, struct rlimit *new_rlim)
{
	struct rlimit *old_rlim = current->signal->rlim + resource;
	int rc;

	rc = secondary_ops->task_setrlimit(resource, new_rlim);
	if (rc)
		return rc;

	/* Control the ability to change the hard limit (whether
	   lowering or raising it), so that the hard limit can
	   later be used as a safe reset point for the soft limit
	   upon context transitions. See selinux_bprm_apply_creds. */
	if (old_rlim->rlim_max != new_rlim->rlim_max)
		return task_has_perm(current, current, PROCESS__SETRLIMIT);

	return 0;
}

static int selinux_task_setscheduler(struct task_struct *p, int policy, struct sched_param *lp)
{
	return task_has_perm(current, p, PROCESS__SETSCHED);
}

static int selinux_task_getscheduler(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__GETSCHED);
}

static int selinux_task_movememory(struct task_struct *p)
{
	return task_has_perm(current, p, PROCESS__SETSCHED);
}

static int selinux_task_kill(struct task_struct *p, struct siginfo *info,
				int sig, u32 secid)
{
	u32 perm;
	int rc;
	struct task_security_struct *tsec;

	rc = secondary_ops->task_kill(p, info, sig, secid);
	if (rc)
		return rc;

	if (info != SEND_SIG_NOINFO && (is_si_special(info) || SI_FROMKERNEL(info)))
		return 0;

	if (!sig)
		perm = PROCESS__SIGNULL; /* null signal; existence test */
	else
		perm = signal_to_av(sig);
	tsec = p->security;
	if (secid)
		rc = avc_has_perm(secid, tsec->sid, SECCLASS_PROCESS, perm, NULL);
	else
		rc = task_has_perm(current, p, perm);
	return rc;
}

static int selinux_task_prctl(int option,
			      unsigned long arg2,
			      unsigned long arg3,
			      unsigned long arg4,
			      unsigned long arg5)
{
	/* The current prctl operations do not appear to require
	   any SELinux controls since they merely observe or modify
	   the state of the current process. */
	return 0;
}

static int selinux_task_wait(struct task_struct *p)
{
	u32 perm;

	perm = signal_to_av(p->exit_signal);

	return task_has_perm(p, current, perm);
}

static void selinux_task_reparent_to_init(struct task_struct *p)
{
  	struct task_security_struct *tsec;

	secondary_ops->task_reparent_to_init(p);

	tsec = p->security;
	tsec->osid = tsec->sid;
	tsec->sid = SECINITSID_KERNEL;
	return;
}

static void selinux_task_to_inode(struct task_struct *p,
				  struct inode *inode)
{
	struct task_security_struct *tsec = p->security;
	struct inode_security_struct *isec = inode->i_security;

	isec->sid = tsec->sid;
	isec->initialized = 1;
	return;
}

/* Returns error only if unable to parse addresses */
static int selinux_parse_skb_ipv4(struct sk_buff *skb,
			struct avc_audit_data *ad, u8 *proto)
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

	ad->u.net.v4info.saddr = ih->saddr;
	ad->u.net.v4info.daddr = ih->daddr;
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

		ad->u.net.sport = th->source;
		ad->u.net.dport = th->dest;
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

        	ad->u.net.sport = uh->source;
        	ad->u.net.dport = uh->dest;
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

		ad->u.net.sport = dh->dccph_sport;
		ad->u.net.dport = dh->dccph_dport;
		break;
        }

        default:
        	break;
        }
out:
	return ret;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

/* Returns error only if unable to parse addresses */
static int selinux_parse_skb_ipv6(struct sk_buff *skb,
			struct avc_audit_data *ad, u8 *proto)
{
	u8 nexthdr;
	int ret = -EINVAL, offset;
	struct ipv6hdr _ipv6h, *ip6;

	offset = skb_network_offset(skb);
	ip6 = skb_header_pointer(skb, offset, sizeof(_ipv6h), &_ipv6h);
	if (ip6 == NULL)
		goto out;

	ipv6_addr_copy(&ad->u.net.v6info.saddr, &ip6->saddr);
	ipv6_addr_copy(&ad->u.net.v6info.daddr, &ip6->daddr);
	ret = 0;

	nexthdr = ip6->nexthdr;
	offset += sizeof(_ipv6h);
	offset = ipv6_skip_exthdr(skb, offset, &nexthdr);
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

		ad->u.net.sport = th->source;
		ad->u.net.dport = th->dest;
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr _udph, *uh;

		uh = skb_header_pointer(skb, offset, sizeof(_udph), &_udph);
		if (uh == NULL)
			break;

		ad->u.net.sport = uh->source;
		ad->u.net.dport = uh->dest;
		break;
	}

	case IPPROTO_DCCP: {
		struct dccp_hdr _dccph, *dh;

		dh = skb_header_pointer(skb, offset, sizeof(_dccph), &_dccph);
		if (dh == NULL)
			break;

		ad->u.net.sport = dh->dccph_sport;
		ad->u.net.dport = dh->dccph_dport;
		break;
        }

	/* includes fragments */
	default:
		break;
	}
out:
	return ret;
}

#endif /* IPV6 */

static int selinux_parse_skb(struct sk_buff *skb, struct avc_audit_data *ad,
			     char **addrp, int *len, int src, u8 *proto)
{
	int ret = 0;

	switch (ad->u.net.family) {
	case PF_INET:
		ret = selinux_parse_skb_ipv4(skb, ad, proto);
		if (ret || !addrp)
			break;
		*len = 4;
		*addrp = (char *)(src ? &ad->u.net.v4info.saddr :
					&ad->u.net.v4info.daddr);
		break;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	case PF_INET6:
		ret = selinux_parse_skb_ipv6(skb, ad, proto);
		if (ret || !addrp)
			break;
		*len = 16;
		*addrp = (char *)(src ? &ad->u.net.v6info.saddr :
					&ad->u.net.v6info.daddr);
		break;
#endif	/* IPV6 */
	default:
		break;
	}

	return ret;
}

/**
 * selinux_skb_extlbl_sid - Determine the external label of a packet
 * @skb: the packet
 * @base_sid: the SELinux SID to use as a context for MLS only external labels
 * @sid: the packet's SID
 *
 * Description:
 * Check the various different forms of external packet labeling and determine
 * the external SID for the packet.
 *
 */
static void selinux_skb_extlbl_sid(struct sk_buff *skb,
				   u32 base_sid,
				   u32 *sid)
{
	u32 xfrm_sid;
	u32 nlbl_sid;

	selinux_skb_xfrm_sid(skb, &xfrm_sid);
	if (selinux_netlbl_skbuff_getsid(skb,
					 (xfrm_sid == SECSID_NULL ?
					  base_sid : xfrm_sid),
					 &nlbl_sid) != 0)
		nlbl_sid = SECSID_NULL;

	*sid = (nlbl_sid == SECSID_NULL ? xfrm_sid : nlbl_sid);
}

/* socket security operations */
static int socket_has_perm(struct task_struct *task, struct socket *sock,
			   u32 perms)
{
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;
	struct avc_audit_data ad;
	int err = 0;

	tsec = task->security;
	isec = SOCK_INODE(sock)->i_security;

	if (isec->sid == SECINITSID_KERNEL)
		goto out;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = sock->sk;
	err = avc_has_perm(tsec->sid, isec->sid, isec->sclass, perms, &ad);

out:
	return err;
}

static int selinux_socket_create(int family, int type,
				 int protocol, int kern)
{
	int err = 0;
	struct task_security_struct *tsec;
	u32 newsid;

	if (kern)
		goto out;

	tsec = current->security;
	newsid = tsec->sockcreate_sid ? : tsec->sid;
	err = avc_has_perm(tsec->sid, newsid,
			   socket_type_to_security_class(family, type,
			   protocol), SOCKET__CREATE, NULL);

out:
	return err;
}

static int selinux_socket_post_create(struct socket *sock, int family,
				      int type, int protocol, int kern)
{
	int err = 0;
	struct inode_security_struct *isec;
	struct task_security_struct *tsec;
	struct sk_security_struct *sksec;
	u32 newsid;

	isec = SOCK_INODE(sock)->i_security;

	tsec = current->security;
	newsid = tsec->sockcreate_sid ? : tsec->sid;
	isec->sclass = socket_type_to_security_class(family, type, protocol);
	isec->sid = kern ? SECINITSID_KERNEL : newsid;
	isec->initialized = 1;

	if (sock->sk) {
		sksec = sock->sk->sk_security;
		sksec->sid = isec->sid;
		err = selinux_netlbl_socket_post_create(sock);
	}

	return err;
}

/* Range of port numbers used to automatically bind.
   Need to determine whether we should perform a name_bind
   permission check between the socket and the port number. */
#define ip_local_port_range_0 sysctl_local_port_range[0]
#define ip_local_port_range_1 sysctl_local_port_range[1]

static int selinux_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	u16 family;
	int err;

	err = socket_has_perm(current, sock, SOCKET__BIND);
	if (err)
		goto out;

	/*
	 * If PF_INET or PF_INET6, check name_bind permission for the port.
	 * Multiple address binding for SCTP is not supported yet: we just
	 * check the first address now.
	 */
	family = sock->sk->sk_family;
	if (family == PF_INET || family == PF_INET6) {
		char *addrp;
		struct inode_security_struct *isec;
		struct task_security_struct *tsec;
		struct avc_audit_data ad;
		struct sockaddr_in *addr4 = NULL;
		struct sockaddr_in6 *addr6 = NULL;
		unsigned short snum;
		struct sock *sk = sock->sk;
		u32 sid, node_perm, addrlen;

		tsec = current->security;
		isec = SOCK_INODE(sock)->i_security;

		if (family == PF_INET) {
			addr4 = (struct sockaddr_in *)address;
			snum = ntohs(addr4->sin_port);
			addrlen = sizeof(addr4->sin_addr.s_addr);
			addrp = (char *)&addr4->sin_addr.s_addr;
		} else {
			addr6 = (struct sockaddr_in6 *)address;
			snum = ntohs(addr6->sin6_port);
			addrlen = sizeof(addr6->sin6_addr.s6_addr);
			addrp = (char *)&addr6->sin6_addr.s6_addr;
		}

		if (snum&&(snum < max(PROT_SOCK,ip_local_port_range_0) ||
			   snum > ip_local_port_range_1)) {
			err = security_port_sid(sk->sk_family, sk->sk_type,
						sk->sk_protocol, snum, &sid);
			if (err)
				goto out;
			AVC_AUDIT_DATA_INIT(&ad,NET);
			ad.u.net.sport = htons(snum);
			ad.u.net.family = family;
			err = avc_has_perm(isec->sid, sid,
					   isec->sclass,
					   SOCKET__NAME_BIND, &ad);
			if (err)
				goto out;
		}
		
		switch(isec->sclass) {
		case SECCLASS_TCP_SOCKET:
			node_perm = TCP_SOCKET__NODE_BIND;
			break;
			
		case SECCLASS_UDP_SOCKET:
			node_perm = UDP_SOCKET__NODE_BIND;
			break;

		case SECCLASS_DCCP_SOCKET:
			node_perm = DCCP_SOCKET__NODE_BIND;
			break;

		default:
			node_perm = RAWIP_SOCKET__NODE_BIND;
			break;
		}
		
		err = security_node_sid(family, addrp, addrlen, &sid);
		if (err)
			goto out;
		
		AVC_AUDIT_DATA_INIT(&ad,NET);
		ad.u.net.sport = htons(snum);
		ad.u.net.family = family;

		if (family == PF_INET)
			ad.u.net.v4info.saddr = addr4->sin_addr.s_addr;
		else
			ipv6_addr_copy(&ad.u.net.v6info.saddr, &addr6->sin6_addr);

		err = avc_has_perm(isec->sid, sid,
		                   isec->sclass, node_perm, &ad);
		if (err)
			goto out;
	}
out:
	return err;
}

static int selinux_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen)
{
	struct inode_security_struct *isec;
	int err;

	err = socket_has_perm(current, sock, SOCKET__CONNECT);
	if (err)
		return err;

	/*
	 * If a TCP or DCCP socket, check name_connect permission for the port.
	 */
	isec = SOCK_INODE(sock)->i_security;
	if (isec->sclass == SECCLASS_TCP_SOCKET ||
	    isec->sclass == SECCLASS_DCCP_SOCKET) {
		struct sock *sk = sock->sk;
		struct avc_audit_data ad;
		struct sockaddr_in *addr4 = NULL;
		struct sockaddr_in6 *addr6 = NULL;
		unsigned short snum;
		u32 sid, perm;

		if (sk->sk_family == PF_INET) {
			addr4 = (struct sockaddr_in *)address;
			if (addrlen < sizeof(struct sockaddr_in))
				return -EINVAL;
			snum = ntohs(addr4->sin_port);
		} else {
			addr6 = (struct sockaddr_in6 *)address;
			if (addrlen < SIN6_LEN_RFC2133)
				return -EINVAL;
			snum = ntohs(addr6->sin6_port);
		}

		err = security_port_sid(sk->sk_family, sk->sk_type,
					sk->sk_protocol, snum, &sid);
		if (err)
			goto out;

		perm = (isec->sclass == SECCLASS_TCP_SOCKET) ?
		       TCP_SOCKET__NAME_CONNECT : DCCP_SOCKET__NAME_CONNECT;

		AVC_AUDIT_DATA_INIT(&ad,NET);
		ad.u.net.dport = htons(snum);
		ad.u.net.family = sk->sk_family;
		err = avc_has_perm(isec->sid, sid, isec->sclass, perm, &ad);
		if (err)
			goto out;
	}

out:
	return err;
}

static int selinux_socket_listen(struct socket *sock, int backlog)
{
	return socket_has_perm(current, sock, SOCKET__LISTEN);
}

static int selinux_socket_accept(struct socket *sock, struct socket *newsock)
{
	int err;
	struct inode_security_struct *isec;
	struct inode_security_struct *newisec;

	err = socket_has_perm(current, sock, SOCKET__ACCEPT);
	if (err)
		return err;

	newisec = SOCK_INODE(newsock)->i_security;

	isec = SOCK_INODE(sock)->i_security;
	newisec->sclass = isec->sclass;
	newisec->sid = isec->sid;
	newisec->initialized = 1;

	return 0;
}

static int selinux_socket_sendmsg(struct socket *sock, struct msghdr *msg,
 				  int size)
{
	int rc;

	rc = socket_has_perm(current, sock, SOCKET__WRITE);
	if (rc)
		return rc;

	return selinux_netlbl_inode_permission(SOCK_INODE(sock), MAY_WRITE);
}

static int selinux_socket_recvmsg(struct socket *sock, struct msghdr *msg,
				  int size, int flags)
{
	return socket_has_perm(current, sock, SOCKET__READ);
}

static int selinux_socket_getsockname(struct socket *sock)
{
	return socket_has_perm(current, sock, SOCKET__GETATTR);
}

static int selinux_socket_getpeername(struct socket *sock)
{
	return socket_has_perm(current, sock, SOCKET__GETATTR);
}

static int selinux_socket_setsockopt(struct socket *sock,int level,int optname)
{
	int err;

	err = socket_has_perm(current, sock, SOCKET__SETOPT);
	if (err)
		return err;

	return selinux_netlbl_socket_setsockopt(sock, level, optname);
}

static int selinux_socket_getsockopt(struct socket *sock, int level,
				     int optname)
{
	return socket_has_perm(current, sock, SOCKET__GETOPT);
}

static int selinux_socket_shutdown(struct socket *sock, int how)
{
	return socket_has_perm(current, sock, SOCKET__SHUTDOWN);
}

static int selinux_socket_unix_stream_connect(struct socket *sock,
					      struct socket *other,
					      struct sock *newsk)
{
	struct sk_security_struct *ssec;
	struct inode_security_struct *isec;
	struct inode_security_struct *other_isec;
	struct avc_audit_data ad;
	int err;

	err = secondary_ops->unix_stream_connect(sock, other, newsk);
	if (err)
		return err;

	isec = SOCK_INODE(sock)->i_security;
	other_isec = SOCK_INODE(other)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = other->sk;

	err = avc_has_perm(isec->sid, other_isec->sid,
			   isec->sclass,
			   UNIX_STREAM_SOCKET__CONNECTTO, &ad);
	if (err)
		return err;

	/* connecting socket */
	ssec = sock->sk->sk_security;
	ssec->peer_sid = other_isec->sid;
	
	/* server child socket */
	ssec = newsk->sk_security;
	ssec->peer_sid = isec->sid;
	err = security_sid_mls_copy(other_isec->sid, ssec->peer_sid, &ssec->sid);

	return err;
}

static int selinux_socket_unix_may_send(struct socket *sock,
					struct socket *other)
{
	struct inode_security_struct *isec;
	struct inode_security_struct *other_isec;
	struct avc_audit_data ad;
	int err;

	isec = SOCK_INODE(sock)->i_security;
	other_isec = SOCK_INODE(other)->i_security;

	AVC_AUDIT_DATA_INIT(&ad,NET);
	ad.u.net.sk = other->sk;

	err = avc_has_perm(isec->sid, other_isec->sid,
			   isec->sclass, SOCKET__SENDTO, &ad);
	if (err)
		return err;

	return 0;
}

static int selinux_sock_rcv_skb_compat(struct sock *sk, struct sk_buff *skb,
		struct avc_audit_data *ad, u16 family, char *addrp, int len)
{
	int err = 0;
	u32 netif_perm, node_perm, node_sid, if_sid, recv_perm = 0;
	struct socket *sock;
	u16 sock_class = 0;
	u32 sock_sid = 0;

 	read_lock_bh(&sk->sk_callback_lock);
 	sock = sk->sk_socket;
 	if (sock) {
 		struct inode *inode;
 		inode = SOCK_INODE(sock);
 		if (inode) {
 			struct inode_security_struct *isec;
 			isec = inode->i_security;
 			sock_sid = isec->sid;
 			sock_class = isec->sclass;
 		}
 	}
 	read_unlock_bh(&sk->sk_callback_lock);
 	if (!sock_sid)
  		goto out;

	if (!skb->dev)
		goto out;

	err = sel_netif_sids(skb->dev, &if_sid, NULL);
	if (err)
		goto out;

	switch (sock_class) {
	case SECCLASS_UDP_SOCKET:
		netif_perm = NETIF__UDP_RECV;
		node_perm = NODE__UDP_RECV;
		recv_perm = UDP_SOCKET__RECV_MSG;
		break;
	
	case SECCLASS_TCP_SOCKET:
		netif_perm = NETIF__TCP_RECV;
		node_perm = NODE__TCP_RECV;
		recv_perm = TCP_SOCKET__RECV_MSG;
		break;

	case SECCLASS_DCCP_SOCKET:
		netif_perm = NETIF__DCCP_RECV;
		node_perm = NODE__DCCP_RECV;
		recv_perm = DCCP_SOCKET__RECV_MSG;
		break;

	default:
		netif_perm = NETIF__RAWIP_RECV;
		node_perm = NODE__RAWIP_RECV;
		break;
	}

	err = avc_has_perm(sock_sid, if_sid, SECCLASS_NETIF, netif_perm, ad);
	if (err)
		goto out;
	
	err = security_node_sid(family, addrp, len, &node_sid);
	if (err)
		goto out;
	
	err = avc_has_perm(sock_sid, node_sid, SECCLASS_NODE, node_perm, ad);
	if (err)
		goto out;

	if (recv_perm) {
		u32 port_sid;

		err = security_port_sid(sk->sk_family, sk->sk_type,
		                        sk->sk_protocol, ntohs(ad->u.net.sport),
		                        &port_sid);
		if (err)
			goto out;

		err = avc_has_perm(sock_sid, port_sid,
				   sock_class, recv_perm, ad);
	}

out:
	return err;
}

static int selinux_socket_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	u16 family;
	char *addrp;
	int len, err = 0;
	struct avc_audit_data ad;
	struct sk_security_struct *sksec = sk->sk_security;

	family = sk->sk_family;
	if (family != PF_INET && family != PF_INET6)
		goto out;

	/* Handle mapped IPv4 packets arriving via IPv6 sockets */
	if (family == PF_INET6 && skb->protocol == htons(ETH_P_IP))
		family = PF_INET;

	AVC_AUDIT_DATA_INIT(&ad, NET);
	ad.u.net.netif = skb->dev ? skb->dev->name : "[unknown]";
	ad.u.net.family = family;

	err = selinux_parse_skb(skb, &ad, &addrp, &len, 1, NULL);
	if (err)
		goto out;

	if (selinux_compat_net)
		err = selinux_sock_rcv_skb_compat(sk, skb, &ad, family,
						  addrp, len);
	else
		err = avc_has_perm(sksec->sid, skb->secmark, SECCLASS_PACKET,
				   PACKET__RECV, &ad);
	if (err)
		goto out;

	err = selinux_netlbl_sock_rcv_skb(sksec, skb, &ad);
	if (err)
		goto out;

	err = selinux_xfrm_sock_rcv_skb(sksec->sid, skb, &ad);
out:	
	return err;
}

static int selinux_socket_getpeersec_stream(struct socket *sock, char __user *optval,
					    int __user *optlen, unsigned len)
{
	int err = 0;
	char *scontext;
	u32 scontext_len;
	struct sk_security_struct *ssec;
	struct inode_security_struct *isec;
	u32 peer_sid = SECSID_NULL;

	isec = SOCK_INODE(sock)->i_security;

	if (isec->sclass == SECCLASS_UNIX_STREAM_SOCKET ||
	    isec->sclass == SECCLASS_TCP_SOCKET) {
		ssec = sock->sk->sk_security;
		peer_sid = ssec->peer_sid;
	}
	if (peer_sid == SECSID_NULL) {
		err = -ENOPROTOOPT;
		goto out;
	}

	err = security_sid_to_context(peer_sid, &scontext, &scontext_len);

	if (err)
		goto out;

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
out:	
	return err;
}

static int selinux_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	u32 peer_secid = SECSID_NULL;
	int err = 0;

	if (sock && sock->sk->sk_family == PF_UNIX)
		selinux_get_inode_sid(SOCK_INODE(sock), &peer_secid);
	else if (skb)
		selinux_skb_extlbl_sid(skb, SECINITSID_UNLABELED, &peer_secid);

	if (peer_secid == SECSID_NULL)
		err = -EINVAL;
	*secid = peer_secid;

	return err;
}

static int selinux_sk_alloc_security(struct sock *sk, int family, gfp_t priority)
{
	return sk_alloc_security(sk, family, priority);
}

static void selinux_sk_free_security(struct sock *sk)
{
	sk_free_security(sk);
}

static void selinux_sk_clone_security(const struct sock *sk, struct sock *newsk)
{
	struct sk_security_struct *ssec = sk->sk_security;
	struct sk_security_struct *newssec = newsk->sk_security;

	newssec->sid = ssec->sid;
	newssec->peer_sid = ssec->peer_sid;

	selinux_netlbl_sk_security_clone(ssec, newssec);
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

static void selinux_sock_graft(struct sock* sk, struct socket *parent)
{
	struct inode_security_struct *isec = SOCK_INODE(parent)->i_security;
	struct sk_security_struct *sksec = sk->sk_security;

	if (sk->sk_family == PF_INET || sk->sk_family == PF_INET6 ||
	    sk->sk_family == PF_UNIX)
		isec->sid = sksec->sid;

	selinux_netlbl_sock_graft(sk, parent);
}

static int selinux_inet_conn_request(struct sock *sk, struct sk_buff *skb,
				     struct request_sock *req)
{
	struct sk_security_struct *sksec = sk->sk_security;
	int err;
	u32 newsid;
	u32 peersid;

	selinux_skb_extlbl_sid(skb, SECINITSID_UNLABELED, &peersid);
	if (peersid == SECSID_NULL) {
		req->secid = sksec->sid;
		req->peer_secid = SECSID_NULL;
		return 0;
	}

	err = security_sid_mls_copy(sksec->sid, peersid, &newsid);
	if (err)
		return err;

	req->secid = newsid;
	req->peer_secid = peersid;
	return 0;
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
	selinux_netlbl_sk_security_reset(newsksec, req->rsk_ops->family);
}

static void selinux_inet_conn_established(struct sock *sk,
				struct sk_buff *skb)
{
	struct sk_security_struct *sksec = sk->sk_security;

	selinux_skb_extlbl_sid(skb, SECINITSID_UNLABELED, &sksec->peer_sid);
}

static void selinux_req_classify_flow(const struct request_sock *req,
				      struct flowi *fl)
{
	fl->secid = req->secid;
}

static int selinux_nlmsg_perm(struct sock *sk, struct sk_buff *skb)
{
	int err = 0;
	u32 perm;
	struct nlmsghdr *nlh;
	struct socket *sock = sk->sk_socket;
	struct inode_security_struct *isec = SOCK_INODE(sock)->i_security;
	
	if (skb->len < NLMSG_SPACE(0)) {
		err = -EINVAL;
		goto out;
	}
	nlh = nlmsg_hdr(skb);
	
	err = selinux_nlmsg_lookup(isec->sclass, nlh->nlmsg_type, &perm);
	if (err) {
		if (err == -EINVAL) {
			audit_log(current->audit_context, GFP_KERNEL, AUDIT_SELINUX_ERR,
				  "SELinux:  unrecognized netlink message"
				  " type=%hu for sclass=%hu\n",
				  nlh->nlmsg_type, isec->sclass);
			if (!selinux_enforcing)
				err = 0;
		}

		/* Ignore */
		if (err == -ENOENT)
			err = 0;
		goto out;
	}

	err = socket_has_perm(current, sock, perm);
out:
	return err;
}

#ifdef CONFIG_NETFILTER

static int selinux_ip_postroute_last_compat(struct sock *sk, struct net_device *dev,
					    struct avc_audit_data *ad,
					    u16 family, char *addrp, int len)
{
	int err = 0;
	u32 netif_perm, node_perm, node_sid, if_sid, send_perm = 0;
	struct socket *sock;
	struct inode *inode;
	struct inode_security_struct *isec;

	sock = sk->sk_socket;
	if (!sock)
		goto out;

	inode = SOCK_INODE(sock);
	if (!inode)
		goto out;

	isec = inode->i_security;
	
	err = sel_netif_sids(dev, &if_sid, NULL);
	if (err)
		goto out;

	switch (isec->sclass) {
	case SECCLASS_UDP_SOCKET:
		netif_perm = NETIF__UDP_SEND;
		node_perm = NODE__UDP_SEND;
		send_perm = UDP_SOCKET__SEND_MSG;
		break;
	
	case SECCLASS_TCP_SOCKET:
		netif_perm = NETIF__TCP_SEND;
		node_perm = NODE__TCP_SEND;
		send_perm = TCP_SOCKET__SEND_MSG;
		break;

	case SECCLASS_DCCP_SOCKET:
		netif_perm = NETIF__DCCP_SEND;
		node_perm = NODE__DCCP_SEND;
		send_perm = DCCP_SOCKET__SEND_MSG;
		break;

	default:
		netif_perm = NETIF__RAWIP_SEND;
		node_perm = NODE__RAWIP_SEND;
		break;
	}

	err = avc_has_perm(isec->sid, if_sid, SECCLASS_NETIF, netif_perm, ad);
	if (err)
		goto out;
		
	err = security_node_sid(family, addrp, len, &node_sid);
	if (err)
		goto out;
	
	err = avc_has_perm(isec->sid, node_sid, SECCLASS_NODE, node_perm, ad);
	if (err)
		goto out;

	if (send_perm) {
		u32 port_sid;
		
		err = security_port_sid(sk->sk_family,
		                        sk->sk_type,
		                        sk->sk_protocol,
		                        ntohs(ad->u.net.dport),
		                        &port_sid);
		if (err)
			goto out;

		err = avc_has_perm(isec->sid, port_sid, isec->sclass,
				   send_perm, ad);
	}
out:
	return err;
}

static unsigned int selinux_ip_postroute_last(unsigned int hooknum,
                                              struct sk_buff **pskb,
                                              const struct net_device *in,
                                              const struct net_device *out,
                                              int (*okfn)(struct sk_buff *),
                                              u16 family)
{
	char *addrp;
	int len, err = 0;
	struct sock *sk;
	struct sk_buff *skb = *pskb;
	struct avc_audit_data ad;
	struct net_device *dev = (struct net_device *)out;
	struct sk_security_struct *sksec;
	u8 proto;

	sk = skb->sk;
	if (!sk)
		goto out;

	sksec = sk->sk_security;

	AVC_AUDIT_DATA_INIT(&ad, NET);
	ad.u.net.netif = dev->name;
	ad.u.net.family = family;

	err = selinux_parse_skb(skb, &ad, &addrp, &len, 0, &proto);
	if (err)
		goto out;

	if (selinux_compat_net)
		err = selinux_ip_postroute_last_compat(sk, dev, &ad,
						       family, addrp, len);
	else
		err = avc_has_perm(sksec->sid, skb->secmark, SECCLASS_PACKET,
				   PACKET__SEND, &ad);

	if (err)
		goto out;

	err = selinux_xfrm_postroute_last(sksec->sid, skb, &ad, proto);
out:
	return err ? NF_DROP : NF_ACCEPT;
}

static unsigned int selinux_ipv4_postroute_last(unsigned int hooknum,
						struct sk_buff **pskb,
						const struct net_device *in,
						const struct net_device *out,
						int (*okfn)(struct sk_buff *))
{
	return selinux_ip_postroute_last(hooknum, pskb, in, out, okfn, PF_INET);
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

static unsigned int selinux_ipv6_postroute_last(unsigned int hooknum,
						struct sk_buff **pskb,
						const struct net_device *in,
						const struct net_device *out,
						int (*okfn)(struct sk_buff *))
{
	return selinux_ip_postroute_last(hooknum, pskb, in, out, okfn, PF_INET6);
}

#endif	/* IPV6 */

#endif	/* CONFIG_NETFILTER */

static int selinux_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	int err;

	err = secondary_ops->netlink_send(sk, skb);
	if (err)
		return err;

	if (policydb_loaded_version >= POLICYDB_VERSION_NLCLASS)
		err = selinux_nlmsg_perm(sk, skb);

	return err;
}

static int selinux_netlink_recv(struct sk_buff *skb, int capability)
{
	int err;
	struct avc_audit_data ad;

	err = secondary_ops->netlink_recv(skb, capability);
	if (err)
		return err;

	AVC_AUDIT_DATA_INIT(&ad, CAP);
	ad.u.cap = capability;

	return avc_has_perm(NETLINK_CB(skb).sid, NETLINK_CB(skb).sid,
	                    SECCLASS_CAPABILITY, CAP_TO_MASK(capability), &ad);
}

static int ipc_alloc_security(struct task_struct *task,
			      struct kern_ipc_perm *perm,
			      u16 sclass)
{
	struct task_security_struct *tsec = task->security;
	struct ipc_security_struct *isec;

	isec = kzalloc(sizeof(struct ipc_security_struct), GFP_KERNEL);
	if (!isec)
		return -ENOMEM;

	isec->sclass = sclass;
	isec->ipc_perm = perm;
	isec->sid = tsec->sid;
	perm->security = isec;

	return 0;
}

static void ipc_free_security(struct kern_ipc_perm *perm)
{
	struct ipc_security_struct *isec = perm->security;
	perm->security = NULL;
	kfree(isec);
}

static int msg_msg_alloc_security(struct msg_msg *msg)
{
	struct msg_security_struct *msec;

	msec = kzalloc(sizeof(struct msg_security_struct), GFP_KERNEL);
	if (!msec)
		return -ENOMEM;

	msec->msg = msg;
	msec->sid = SECINITSID_UNLABELED;
	msg->security = msec;

	return 0;
}

static void msg_msg_free_security(struct msg_msg *msg)
{
	struct msg_security_struct *msec = msg->security;

	msg->security = NULL;
	kfree(msec);
}

static int ipc_has_perm(struct kern_ipc_perm *ipc_perms,
			u32 perms)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = ipc_perms->security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = ipc_perms->key;

	return avc_has_perm(tsec->sid, isec->sid, isec->sclass, perms, &ad);
}

static int selinux_msg_msg_alloc_security(struct msg_msg *msg)
{
	return msg_msg_alloc_security(msg);
}

static void selinux_msg_msg_free_security(struct msg_msg *msg)
{
	msg_msg_free_security(msg);
}

/* message queue security operations */
static int selinux_msg_queue_alloc_security(struct msg_queue *msq)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;
	int rc;

	rc = ipc_alloc_security(current, &msq->q_perm, SECCLASS_MSGQ);
	if (rc)
		return rc;

	tsec = current->security;
	isec = msq->q_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = msq->q_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_MSGQ,
			  MSGQ__CREATE, &ad);
	if (rc) {
		ipc_free_security(&msq->q_perm);
		return rc;
	}
	return 0;
}

static void selinux_msg_queue_free_security(struct msg_queue *msq)
{
	ipc_free_security(&msq->q_perm);
}

static int selinux_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = msq->q_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = msq->q_perm.key;

	return avc_has_perm(tsec->sid, isec->sid, SECCLASS_MSGQ,
			    MSGQ__ASSOCIATE, &ad);
}

static int selinux_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	int err;
	int perms;

	switch(cmd) {
	case IPC_INFO:
	case MSG_INFO:
		/* No specific object, just general system-wide information. */
		return task_has_system(current, SYSTEM__IPC_INFO);
	case IPC_STAT:
	case MSG_STAT:
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

	err = ipc_has_perm(&msq->q_perm, perms);
	return err;
}

static int selinux_msg_queue_msgsnd(struct msg_queue *msq, struct msg_msg *msg, int msqflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct msg_security_struct *msec;
	struct avc_audit_data ad;
	int rc;

	tsec = current->security;
	isec = msq->q_perm.security;
	msec = msg->security;

	/*
	 * First time through, need to assign label to the message
	 */
	if (msec->sid == SECINITSID_UNLABELED) {
		/*
		 * Compute new sid based on current process and
		 * message queue this message will be stored in
		 */
		rc = security_transition_sid(tsec->sid,
					     isec->sid,
					     SECCLASS_MSG,
					     &msec->sid);
		if (rc)
			return rc;
	}

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = msq->q_perm.key;

	/* Can this process write to the queue? */
	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_MSGQ,
			  MSGQ__WRITE, &ad);
	if (!rc)
		/* Can this process send the message */
		rc = avc_has_perm(tsec->sid, msec->sid,
				  SECCLASS_MSG, MSG__SEND, &ad);
	if (!rc)
		/* Can the message be put in the queue? */
		rc = avc_has_perm(msec->sid, isec->sid,
				  SECCLASS_MSGQ, MSGQ__ENQUEUE, &ad);

	return rc;
}

static int selinux_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
				    struct task_struct *target,
				    long type, int mode)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct msg_security_struct *msec;
	struct avc_audit_data ad;
	int rc;

	tsec = target->security;
	isec = msq->q_perm.security;
	msec = msg->security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = msq->q_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid,
			  SECCLASS_MSGQ, MSGQ__READ, &ad);
	if (!rc)
		rc = avc_has_perm(tsec->sid, msec->sid,
				  SECCLASS_MSG, MSG__RECEIVE, &ad);
	return rc;
}

/* Shared Memory security operations */
static int selinux_shm_alloc_security(struct shmid_kernel *shp)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;
	int rc;

	rc = ipc_alloc_security(current, &shp->shm_perm, SECCLASS_SHM);
	if (rc)
		return rc;

	tsec = current->security;
	isec = shp->shm_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = shp->shm_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_SHM,
			  SHM__CREATE, &ad);
	if (rc) {
		ipc_free_security(&shp->shm_perm);
		return rc;
	}
	return 0;
}

static void selinux_shm_free_security(struct shmid_kernel *shp)
{
	ipc_free_security(&shp->shm_perm);
}

static int selinux_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = shp->shm_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = shp->shm_perm.key;

	return avc_has_perm(tsec->sid, isec->sid, SECCLASS_SHM,
			    SHM__ASSOCIATE, &ad);
}

/* Note, at this point, shp is locked down */
static int selinux_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	int perms;
	int err;

	switch(cmd) {
	case IPC_INFO:
	case SHM_INFO:
		/* No specific object, just general system-wide information. */
		return task_has_system(current, SYSTEM__IPC_INFO);
	case IPC_STAT:
	case SHM_STAT:
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

	err = ipc_has_perm(&shp->shm_perm, perms);
	return err;
}

static int selinux_shm_shmat(struct shmid_kernel *shp,
			     char __user *shmaddr, int shmflg)
{
	u32 perms;
	int rc;

	rc = secondary_ops->shm_shmat(shp, shmaddr, shmflg);
	if (rc)
		return rc;

	if (shmflg & SHM_RDONLY)
		perms = SHM__READ;
	else
		perms = SHM__READ | SHM__WRITE;

	return ipc_has_perm(&shp->shm_perm, perms);
}

/* Semaphore security operations */
static int selinux_sem_alloc_security(struct sem_array *sma)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;
	int rc;

	rc = ipc_alloc_security(current, &sma->sem_perm, SECCLASS_SEM);
	if (rc)
		return rc;

	tsec = current->security;
	isec = sma->sem_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
 	ad.u.ipc_id = sma->sem_perm.key;

	rc = avc_has_perm(tsec->sid, isec->sid, SECCLASS_SEM,
			  SEM__CREATE, &ad);
	if (rc) {
		ipc_free_security(&sma->sem_perm);
		return rc;
	}
	return 0;
}

static void selinux_sem_free_security(struct sem_array *sma)
{
	ipc_free_security(&sma->sem_perm);
}

static int selinux_sem_associate(struct sem_array *sma, int semflg)
{
	struct task_security_struct *tsec;
	struct ipc_security_struct *isec;
	struct avc_audit_data ad;

	tsec = current->security;
	isec = sma->sem_perm.security;

	AVC_AUDIT_DATA_INIT(&ad, IPC);
	ad.u.ipc_id = sma->sem_perm.key;

	return avc_has_perm(tsec->sid, isec->sid, SECCLASS_SEM,
			    SEM__ASSOCIATE, &ad);
}

/* Note, at this point, sma is locked down */
static int selinux_sem_semctl(struct sem_array *sma, int cmd)
{
	int err;
	u32 perms;

	switch(cmd) {
	case IPC_INFO:
	case SEM_INFO:
		/* No specific object, just general system-wide information. */
		return task_has_system(current, SYSTEM__IPC_INFO);
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
		perms = SEM__GETATTR | SEM__ASSOCIATE;
		break;
	default:
		return 0;
	}

	err = ipc_has_perm(&sma->sem_perm, perms);
	return err;
}

static int selinux_sem_semop(struct sem_array *sma,
			     struct sembuf *sops, unsigned nsops, int alter)
{
	u32 perms;

	if (alter)
		perms = SEM__READ | SEM__WRITE;
	else
		perms = SEM__READ;

	return ipc_has_perm(&sma->sem_perm, perms);
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

/* module stacking operations */
static int selinux_register_security (const char *name, struct security_operations *ops)
{
	if (secondary_ops != original_ops) {
		printk(KERN_ERR "%s:  There is already a secondary security "
		       "module registered.\n", __FUNCTION__);
		return -EINVAL;
 	}

	secondary_ops = ops;

	printk(KERN_INFO "%s:  Registering secondary module %s\n",
	       __FUNCTION__,
	       name);

	return 0;
}

static int selinux_unregister_security (const char *name, struct security_operations *ops)
{
	if (ops != secondary_ops) {
		printk(KERN_ERR "%s:  trying to unregister a security module "
		        "that is not registered.\n", __FUNCTION__);
		return -EINVAL;
	}

	secondary_ops = original_ops;

	return 0;
}

static void selinux_d_instantiate (struct dentry *dentry, struct inode *inode)
{
	if (inode)
		inode_doinit_with_dentry(inode, dentry);
}

static int selinux_getprocattr(struct task_struct *p,
			       char *name, char **value)
{
	struct task_security_struct *tsec;
	u32 sid;
	int error;
	unsigned len;

	if (current != p) {
		error = task_has_perm(current, p, PROCESS__GETATTR);
		if (error)
			return error;
	}

	tsec = p->security;

	if (!strcmp(name, "current"))
		sid = tsec->sid;
	else if (!strcmp(name, "prev"))
		sid = tsec->osid;
	else if (!strcmp(name, "exec"))
		sid = tsec->exec_sid;
	else if (!strcmp(name, "fscreate"))
		sid = tsec->create_sid;
	else if (!strcmp(name, "keycreate"))
		sid = tsec->keycreate_sid;
	else if (!strcmp(name, "sockcreate"))
		sid = tsec->sockcreate_sid;
	else
		return -EINVAL;

	if (!sid)
		return 0;

	error = security_sid_to_context(sid, value, &len);
	if (error)
		return error;
	return len;
}

static int selinux_setprocattr(struct task_struct *p,
			       char *name, void *value, size_t size)
{
	struct task_security_struct *tsec;
	u32 sid = 0;
	int error;
	char *str = value;

	if (current != p) {
		/* SELinux only allows a process to change its own
		   security attributes. */
		return -EACCES;
	}

	/*
	 * Basic control over ability to set these attributes at all.
	 * current == p, but we'll pass them separately in case the
	 * above restriction is ever removed.
	 */
	if (!strcmp(name, "exec"))
		error = task_has_perm(current, p, PROCESS__SETEXEC);
	else if (!strcmp(name, "fscreate"))
		error = task_has_perm(current, p, PROCESS__SETFSCREATE);
	else if (!strcmp(name, "keycreate"))
		error = task_has_perm(current, p, PROCESS__SETKEYCREATE);
	else if (!strcmp(name, "sockcreate"))
		error = task_has_perm(current, p, PROCESS__SETSOCKCREATE);
	else if (!strcmp(name, "current"))
		error = task_has_perm(current, p, PROCESS__SETCURRENT);
	else
		error = -EINVAL;
	if (error)
		return error;

	/* Obtain a SID for the context, if one was specified. */
	if (size && str[1] && str[1] != '\n') {
		if (str[size-1] == '\n') {
			str[size-1] = 0;
			size--;
		}
		error = security_context_to_sid(value, size, &sid);
		if (error)
			return error;
	}

	/* Permission checking based on the specified context is
	   performed during the actual operation (execve,
	   open/mkdir/...), when we know the full context of the
	   operation.  See selinux_bprm_set_security for the execve
	   checks and may_create for the file creation checks. The
	   operation will then fail if the context is not permitted. */
	tsec = p->security;
	if (!strcmp(name, "exec"))
		tsec->exec_sid = sid;
	else if (!strcmp(name, "fscreate"))
		tsec->create_sid = sid;
	else if (!strcmp(name, "keycreate")) {
		error = may_create_key(sid, p);
		if (error)
			return error;
		tsec->keycreate_sid = sid;
	} else if (!strcmp(name, "sockcreate"))
		tsec->sockcreate_sid = sid;
	else if (!strcmp(name, "current")) {
		struct av_decision avd;

		if (sid == 0)
			return -EINVAL;

		/* Only allow single threaded processes to change context */
		if (atomic_read(&p->mm->mm_users) != 1) {
			struct task_struct *g, *t;
			struct mm_struct *mm = p->mm;
			read_lock(&tasklist_lock);
			do_each_thread(g, t)
				if (t->mm == mm && t != p) {
					read_unlock(&tasklist_lock);
					return -EPERM;
				}
			while_each_thread(g, t);
			read_unlock(&tasklist_lock);
                }

		/* Check permissions for the transition. */
		error = avc_has_perm(tsec->sid, sid, SECCLASS_PROCESS,
		                     PROCESS__DYNTRANSITION, NULL);
		if (error)
			return error;

		/* Check for ptracing, and update the task SID if ok.
		   Otherwise, leave SID unchanged and fail. */
		task_lock(p);
		if (p->ptrace & PT_PTRACED) {
			error = avc_has_perm_noaudit(tsec->ptrace_sid, sid,
						     SECCLASS_PROCESS,
						     PROCESS__PTRACE, &avd);
			if (!error)
				tsec->sid = sid;
			task_unlock(p);
			avc_audit(tsec->ptrace_sid, sid, SECCLASS_PROCESS,
				  PROCESS__PTRACE, &avd, error, NULL);
			if (error)
				return error;
		} else {
			tsec->sid = sid;
			task_unlock(p);
		}
	}
	else
		return -EINVAL;

	return size;
}

static int selinux_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_sid_to_context(secid, secdata, seclen);
}

static void selinux_release_secctx(char *secdata, u32 seclen)
{
	if (secdata)
		kfree(secdata);
}

#ifdef CONFIG_KEYS

static int selinux_key_alloc(struct key *k, struct task_struct *tsk,
			     unsigned long flags)
{
	struct task_security_struct *tsec = tsk->security;
	struct key_security_struct *ksec;

	ksec = kzalloc(sizeof(struct key_security_struct), GFP_KERNEL);
	if (!ksec)
		return -ENOMEM;

	ksec->obj = k;
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
			    struct task_struct *ctx,
			    key_perm_t perm)
{
	struct key *key;
	struct task_security_struct *tsec;
	struct key_security_struct *ksec;

	key = key_ref_to_ptr(key_ref);

	tsec = ctx->security;
	ksec = key->security;

	/* if no specific permissions are requested, we skip the
	   permission check. No serious, additional covert channels
	   appear to be created. */
	if (perm == 0)
		return 0;

	return avc_has_perm(tsec->sid, ksec->sid,
			    SECCLASS_KEY, perm, NULL);
}

#endif

static struct security_operations selinux_ops = {
	.ptrace =			selinux_ptrace,
	.capget =			selinux_capget,
	.capset_check =			selinux_capset_check,
	.capset_set =			selinux_capset_set,
	.sysctl =			selinux_sysctl,
	.capable =			selinux_capable,
	.quotactl =			selinux_quotactl,
	.quota_on =			selinux_quota_on,
	.syslog =			selinux_syslog,
	.vm_enough_memory =		selinux_vm_enough_memory,

	.netlink_send =			selinux_netlink_send,
        .netlink_recv =			selinux_netlink_recv,

	.bprm_alloc_security =		selinux_bprm_alloc_security,
	.bprm_free_security =		selinux_bprm_free_security,
	.bprm_apply_creds =		selinux_bprm_apply_creds,
	.bprm_post_apply_creds =	selinux_bprm_post_apply_creds,
	.bprm_set_security =		selinux_bprm_set_security,
	.bprm_check_security =		selinux_bprm_check_security,
	.bprm_secureexec =		selinux_bprm_secureexec,

	.sb_alloc_security =		selinux_sb_alloc_security,
	.sb_free_security =		selinux_sb_free_security,
	.sb_copy_data =			selinux_sb_copy_data,
	.sb_kern_mount =	        selinux_sb_kern_mount,
	.sb_statfs =			selinux_sb_statfs,
	.sb_mount =			selinux_mount,
	.sb_umount =			selinux_umount,

	.inode_alloc_security =		selinux_inode_alloc_security,
	.inode_free_security =		selinux_inode_free_security,
	.inode_init_security =		selinux_inode_init_security,
	.inode_create =			selinux_inode_create,
	.inode_link =			selinux_inode_link,
	.inode_unlink =			selinux_inode_unlink,
	.inode_symlink =		selinux_inode_symlink,
	.inode_mkdir =			selinux_inode_mkdir,
	.inode_rmdir =			selinux_inode_rmdir,
	.inode_mknod =			selinux_inode_mknod,
	.inode_rename =			selinux_inode_rename,
	.inode_readlink =		selinux_inode_readlink,
	.inode_follow_link =		selinux_inode_follow_link,
	.inode_permission =		selinux_inode_permission,
	.inode_setattr =		selinux_inode_setattr,
	.inode_getattr =		selinux_inode_getattr,
	.inode_setxattr =		selinux_inode_setxattr,
	.inode_post_setxattr =		selinux_inode_post_setxattr,
	.inode_getxattr =		selinux_inode_getxattr,
	.inode_listxattr =		selinux_inode_listxattr,
	.inode_removexattr =		selinux_inode_removexattr,
	.inode_xattr_getsuffix =        selinux_inode_xattr_getsuffix,
	.inode_getsecurity =            selinux_inode_getsecurity,
	.inode_setsecurity =            selinux_inode_setsecurity,
	.inode_listsecurity =           selinux_inode_listsecurity,

	.file_permission =		selinux_file_permission,
	.file_alloc_security =		selinux_file_alloc_security,
	.file_free_security =		selinux_file_free_security,
	.file_ioctl =			selinux_file_ioctl,
	.file_mmap =			selinux_file_mmap,
	.file_mprotect =		selinux_file_mprotect,
	.file_lock =			selinux_file_lock,
	.file_fcntl =			selinux_file_fcntl,
	.file_set_fowner =		selinux_file_set_fowner,
	.file_send_sigiotask =		selinux_file_send_sigiotask,
	.file_receive =			selinux_file_receive,

	.task_create =			selinux_task_create,
	.task_alloc_security =		selinux_task_alloc_security,
	.task_free_security =		selinux_task_free_security,
	.task_setuid =			selinux_task_setuid,
	.task_post_setuid =		selinux_task_post_setuid,
	.task_setgid =			selinux_task_setgid,
	.task_setpgid =			selinux_task_setpgid,
	.task_getpgid =			selinux_task_getpgid,
	.task_getsid =		        selinux_task_getsid,
	.task_getsecid =		selinux_task_getsecid,
	.task_setgroups =		selinux_task_setgroups,
	.task_setnice =			selinux_task_setnice,
	.task_setioprio =		selinux_task_setioprio,
	.task_getioprio =		selinux_task_getioprio,
	.task_setrlimit =		selinux_task_setrlimit,
	.task_setscheduler =		selinux_task_setscheduler,
	.task_getscheduler =		selinux_task_getscheduler,
	.task_movememory =		selinux_task_movememory,
	.task_kill =			selinux_task_kill,
	.task_wait =			selinux_task_wait,
	.task_prctl =			selinux_task_prctl,
	.task_reparent_to_init =	selinux_task_reparent_to_init,
	.task_to_inode =                selinux_task_to_inode,

	.ipc_permission =		selinux_ipc_permission,

	.msg_msg_alloc_security =	selinux_msg_msg_alloc_security,
	.msg_msg_free_security =	selinux_msg_msg_free_security,

	.msg_queue_alloc_security =	selinux_msg_queue_alloc_security,
	.msg_queue_free_security =	selinux_msg_queue_free_security,
	.msg_queue_associate =		selinux_msg_queue_associate,
	.msg_queue_msgctl =		selinux_msg_queue_msgctl,
	.msg_queue_msgsnd =		selinux_msg_queue_msgsnd,
	.msg_queue_msgrcv =		selinux_msg_queue_msgrcv,

	.shm_alloc_security =		selinux_shm_alloc_security,
	.shm_free_security =		selinux_shm_free_security,
	.shm_associate =		selinux_shm_associate,
	.shm_shmctl =			selinux_shm_shmctl,
	.shm_shmat =			selinux_shm_shmat,

	.sem_alloc_security = 		selinux_sem_alloc_security,
	.sem_free_security =  		selinux_sem_free_security,
	.sem_associate =		selinux_sem_associate,
	.sem_semctl =			selinux_sem_semctl,
	.sem_semop =			selinux_sem_semop,

	.register_security =		selinux_register_security,
	.unregister_security =		selinux_unregister_security,

	.d_instantiate =                selinux_d_instantiate,

	.getprocattr =                  selinux_getprocattr,
	.setprocattr =                  selinux_setprocattr,

	.secid_to_secctx =		selinux_secid_to_secctx,
	.release_secctx =		selinux_release_secctx,

        .unix_stream_connect =		selinux_socket_unix_stream_connect,
	.unix_may_send =		selinux_socket_unix_may_send,

	.socket_create =		selinux_socket_create,
	.socket_post_create =		selinux_socket_post_create,
	.socket_bind =			selinux_socket_bind,
	.socket_connect =		selinux_socket_connect,
	.socket_listen =		selinux_socket_listen,
	.socket_accept =		selinux_socket_accept,
	.socket_sendmsg =		selinux_socket_sendmsg,
	.socket_recvmsg =		selinux_socket_recvmsg,
	.socket_getsockname =		selinux_socket_getsockname,
	.socket_getpeername =		selinux_socket_getpeername,
	.socket_getsockopt =		selinux_socket_getsockopt,
	.socket_setsockopt =		selinux_socket_setsockopt,
	.socket_shutdown =		selinux_socket_shutdown,
	.socket_sock_rcv_skb =		selinux_socket_sock_rcv_skb,
	.socket_getpeersec_stream =	selinux_socket_getpeersec_stream,
	.socket_getpeersec_dgram =	selinux_socket_getpeersec_dgram,
	.sk_alloc_security =		selinux_sk_alloc_security,
	.sk_free_security =		selinux_sk_free_security,
	.sk_clone_security =		selinux_sk_clone_security,
	.sk_getsecid = 			selinux_sk_getsecid,
	.sock_graft =			selinux_sock_graft,
	.inet_conn_request =		selinux_inet_conn_request,
	.inet_csk_clone =		selinux_inet_csk_clone,
	.inet_conn_established =	selinux_inet_conn_established,
	.req_classify_flow =		selinux_req_classify_flow,

#ifdef CONFIG_SECURITY_NETWORK_XFRM
	.xfrm_policy_alloc_security =	selinux_xfrm_policy_alloc,
	.xfrm_policy_clone_security =	selinux_xfrm_policy_clone,
	.xfrm_policy_free_security =	selinux_xfrm_policy_free,
	.xfrm_policy_delete_security =	selinux_xfrm_policy_delete,
	.xfrm_state_alloc_security =	selinux_xfrm_state_alloc,
	.xfrm_state_free_security =	selinux_xfrm_state_free,
	.xfrm_state_delete_security =	selinux_xfrm_state_delete,
	.xfrm_policy_lookup = 		selinux_xfrm_policy_lookup,
	.xfrm_state_pol_flow_match =	selinux_xfrm_state_pol_flow_match,
	.xfrm_decode_session =		selinux_xfrm_decode_session,
#endif

#ifdef CONFIG_KEYS
	.key_alloc =                    selinux_key_alloc,
	.key_free =                     selinux_key_free,
	.key_permission =               selinux_key_permission,
#endif
};

static __init int selinux_init(void)
{
	struct task_security_struct *tsec;

	if (!selinux_enabled) {
		printk(KERN_INFO "SELinux:  Disabled at boot.\n");
		return 0;
	}

	printk(KERN_INFO "SELinux:  Initializing.\n");

	/* Set the security state for the initial task. */
	if (task_alloc_security(current))
		panic("SELinux:  Failed to initialize initial task.\n");
	tsec = current->security;
	tsec->osid = tsec->sid = SECINITSID_KERNEL;

	sel_inode_cache = kmem_cache_create("selinux_inode_security",
					    sizeof(struct inode_security_struct),
					    0, SLAB_PANIC, NULL, NULL);
	avc_init();

	original_ops = secondary_ops = security_ops;
	if (!secondary_ops)
		panic ("SELinux: No initial security operations\n");
	if (register_security (&selinux_ops))
		panic("SELinux: Unable to register with kernel.\n");

	if (selinux_enforcing) {
		printk(KERN_DEBUG "SELinux:  Starting in enforcing mode\n");
	} else {
		printk(KERN_DEBUG "SELinux:  Starting in permissive mode\n");
	}

#ifdef CONFIG_KEYS
	/* Add security information to initial keyrings */
	selinux_key_alloc(&root_user_keyring, current,
			  KEY_ALLOC_NOT_IN_QUOTA);
	selinux_key_alloc(&root_session_keyring, current,
			  KEY_ALLOC_NOT_IN_QUOTA);
#endif

	return 0;
}

void selinux_complete_init(void)
{
	printk(KERN_DEBUG "SELinux:  Completing initialization.\n");

	/* Set up any superblocks initialized prior to the policy load. */
	printk(KERN_DEBUG "SELinux:  Setting up existing superblocks.\n");
	spin_lock(&sb_lock);
	spin_lock(&sb_security_lock);
next_sb:
	if (!list_empty(&superblock_security_head)) {
		struct superblock_security_struct *sbsec =
				list_entry(superblock_security_head.next,
				           struct superblock_security_struct,
				           list);
		struct super_block *sb = sbsec->sb;
		sb->s_count++;
		spin_unlock(&sb_security_lock);
		spin_unlock(&sb_lock);
		down_read(&sb->s_umount);
		if (sb->s_root)
			superblock_doinit(sb, NULL);
		drop_super(sb);
		spin_lock(&sb_lock);
		spin_lock(&sb_security_lock);
		list_del_init(&sbsec->list);
		goto next_sb;
	}
	spin_unlock(&sb_security_lock);
	spin_unlock(&sb_lock);
}

/* SELinux requires early initialization in order to label
   all processes and objects when they are created. */
security_initcall(selinux_init);

#if defined(CONFIG_NETFILTER)

static struct nf_hook_ops selinux_ipv4_op = {
	.hook =		selinux_ipv4_postroute_last,
	.owner =	THIS_MODULE,
	.pf =		PF_INET,
	.hooknum =	NF_IP_POST_ROUTING,
	.priority =	NF_IP_PRI_SELINUX_LAST,
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

static struct nf_hook_ops selinux_ipv6_op = {
	.hook =		selinux_ipv6_postroute_last,
	.owner =	THIS_MODULE,
	.pf =		PF_INET6,
	.hooknum =	NF_IP6_POST_ROUTING,
	.priority =	NF_IP6_PRI_SELINUX_LAST,
};

#endif	/* IPV6 */

static int __init selinux_nf_ip_init(void)
{
	int err = 0;

	if (!selinux_enabled)
		goto out;

	printk(KERN_DEBUG "SELinux:  Registering netfilter hooks\n");

	err = nf_register_hook(&selinux_ipv4_op);
	if (err)
		panic("SELinux: nf_register_hook for IPv4: error %d\n", err);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

	err = nf_register_hook(&selinux_ipv6_op);
	if (err)
		panic("SELinux: nf_register_hook for IPv6: error %d\n", err);

#endif	/* IPV6 */

out:
	return err;
}

__initcall(selinux_nf_ip_init);

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
static void selinux_nf_ip_exit(void)
{
	printk(KERN_DEBUG "SELinux:  Unregistering netfilter hooks\n");

	nf_unregister_hook(&selinux_ipv4_op);
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	nf_unregister_hook(&selinux_ipv6_op);
#endif	/* IPV6 */
}
#endif

#else /* CONFIG_NETFILTER */

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
#define selinux_nf_ip_exit()
#endif

#endif /* CONFIG_NETFILTER */

#ifdef CONFIG_SECURITY_SELINUX_DISABLE
int selinux_disable(void)
{
	extern void exit_sel_fs(void);
	static int selinux_disabled = 0;

	if (ss_initialized) {
		/* Not permitted after initial policy load. */
		return -EINVAL;
	}

	if (selinux_disabled) {
		/* Only do this once. */
		return -EINVAL;
	}

	printk(KERN_INFO "SELinux:  Disabled at runtime.\n");

	selinux_disabled = 1;
	selinux_enabled = 0;

	/* Reset security_ops to the secondary module, dummy or capability. */
	security_ops = secondary_ops;

	/* Unregister netfilter hooks. */
	selinux_nf_ip_exit();

	/* Unregister selinuxfs. */
	exit_sel_fs();

	return 0;
}
#endif


