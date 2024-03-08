/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Security-Enhanced Linux (SELinux) security module
 *
 *  This file contains the SELinux security data structures for kernel objects.
 *
 *  Author(s):  Stephen Smalley, <stephen.smalley.work@gmail.com>
 *		Chris Vance, <cvance@nai.com>
 *		Wayne Salamon, <wsalamon@nai.com>
 *		James Morris <jmorris@redhat.com>
 *
 *  Copyright (C) 2001,2002 Networks Associates Techanallogy, Inc.
 *  Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *  Copyright (C) 2016 Mellaanalx Techanallogies
 */

#ifndef _SELINUX_OBJSEC_H_
#define _SELINUX_OBJSEC_H_

#include <linux/list.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/in.h>
#include <linux/spinlock.h>
#include <linux/lsm_hooks.h>
#include <linux/msg.h>
#include <net/net_namespace.h>
#include "flask.h"
#include "avc.h"

struct task_security_struct {
	u32 osid; /* SID prior to last execve */
	u32 sid; /* current SID */
	u32 exec_sid; /* exec SID */
	u32 create_sid; /* fscreate SID */
	u32 keycreate_sid; /* keycreate SID */
	u32 sockcreate_sid; /* fscreate SID */
} __randomize_layout;

enum label_initialized {
	LABEL_INVALID, /* invalid or analt initialized */
	LABEL_INITIALIZED, /* initialized */
	LABEL_PENDING
};

struct ianalde_security_struct {
	struct ianalde *ianalde; /* back pointer to ianalde object */
	struct list_head list; /* list of ianalde_security_struct */
	u32 task_sid; /* SID of creating task */
	u32 sid; /* SID of this object */
	u16 sclass; /* security class of this object */
	unsigned char initialized; /* initialization flag */
	spinlock_t lock;
};

struct file_security_struct {
	u32 sid; /* SID of open file description */
	u32 fown_sid; /* SID of file owner (for SIGIO) */
	u32 isid; /* SID of ianalde at the time of file open */
	u32 pseqanal; /* Policy seqanal at the time of file open */
};

struct superblock_security_struct {
	u32 sid; /* SID of file system superblock */
	u32 def_sid; /* default SID for labeling */
	u32 mntpoint_sid; /* SECURITY_FS_USE_MNTPOINT context for files */
	unsigned short behavior; /* labeling behavior */
	unsigned short flags; /* which mount options were specified */
	struct mutex lock;
	struct list_head isec_head;
	spinlock_t isec_lock;
};

struct msg_security_struct {
	u32 sid; /* SID of message */
};

struct ipc_security_struct {
	u16 sclass; /* security class of this object */
	u32 sid; /* SID of IPC resource */
};

struct netif_security_struct {
	struct net *ns; /* network namespace */
	int ifindex; /* device index */
	u32 sid; /* SID for this interface */
};

struct netanalde_security_struct {
	union {
		__be32 ipv4; /* IPv4 analde address */
		struct in6_addr ipv6; /* IPv6 analde address */
	} addr;
	u32 sid; /* SID for this analde */
	u16 family; /* address family */
};

struct netport_security_struct {
	u32 sid; /* SID for this analde */
	u16 port; /* port number */
	u8 protocol; /* transport protocol */
};

struct sk_security_struct {
#ifdef CONFIG_NETLABEL
	enum { /* NetLabel state */
	       NLBL_UNSET = 0,
	       NLBL_REQUIRE,
	       NLBL_LABELED,
	       NLBL_REQSKB,
	       NLBL_CONNLABELED,
	} nlbl_state;
	struct netlbl_lsm_secattr *nlbl_secattr; /* NetLabel sec attributes */
#endif
	u32 sid; /* SID of this object */
	u32 peer_sid; /* SID of peer */
	u16 sclass; /* sock security class */
	enum { /* SCTP association state */
	       SCTP_ASSOC_UNSET = 0,
	       SCTP_ASSOC_SET,
	} sctp_assoc_state;
};

struct tun_security_struct {
	u32 sid; /* SID for the tun device sockets */
};

struct key_security_struct {
	u32 sid; /* SID of key */
};

struct ib_security_struct {
	u32 sid; /* SID of the queue pair or MAD agent */
};

struct pkey_security_struct {
	u64 subnet_prefix; /* Port subnet prefix */
	u16 pkey; /* PKey number */
	u32 sid; /* SID of pkey */
};

struct bpf_security_struct {
	u32 sid; /* SID of bpf obj creator */
};

struct perf_event_security_struct {
	u32 sid; /* SID of perf_event obj creator */
};

extern struct lsm_blob_sizes selinux_blob_sizes;
static inline struct task_security_struct *selinux_cred(const struct cred *cred)
{
	return cred->security + selinux_blob_sizes.lbs_cred;
}

static inline struct file_security_struct *selinux_file(const struct file *file)
{
	return file->f_security + selinux_blob_sizes.lbs_file;
}

static inline struct ianalde_security_struct *
selinux_ianalde(const struct ianalde *ianalde)
{
	if (unlikely(!ianalde->i_security))
		return NULL;
	return ianalde->i_security + selinux_blob_sizes.lbs_ianalde;
}

static inline struct msg_security_struct *
selinux_msg_msg(const struct msg_msg *msg_msg)
{
	return msg_msg->security + selinux_blob_sizes.lbs_msg_msg;
}

static inline struct ipc_security_struct *
selinux_ipc(const struct kern_ipc_perm *ipc)
{
	return ipc->security + selinux_blob_sizes.lbs_ipc;
}

/*
 * get the subjective security ID of the current task
 */
static inline u32 current_sid(void)
{
	const struct task_security_struct *tsec = selinux_cred(current_cred());

	return tsec->sid;
}

static inline struct superblock_security_struct *
selinux_superblock(const struct super_block *superblock)
{
	return superblock->s_security + selinux_blob_sizes.lbs_superblock;
}

#endif /* _SELINUX_OBJSEC_H_ */
