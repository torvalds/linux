/*
 *  NSA Security-Enhanced Linux (SELinux) security module
 *
 *  This file contains the SELinux security data structures for kernel objects.
 *
 *  Author(s):  Stephen Smalley, <sds@epoch.ncsc.mil>
 *		Chris Vance, <cvance@nai.com>
 *		Wayne Salamon, <wsalamon@nai.com>
 *		James Morris <jmorris@redhat.com>
 *
 *  Copyright (C) 2001,2002 Networks Associates Technology, Inc.
 *  Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2,
 *	as published by the Free Software Foundation.
 */
#ifndef _SELINUX_OBJSEC_H_
#define _SELINUX_OBJSEC_H_

#include <linux/list.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/binfmts.h>
#include <linux/in.h>
#include <linux/spinlock.h>
#include <net/net_namespace.h>
#include "flask.h"
#include "avc.h"

struct task_security_struct {
	u32 osid;		/* SID prior to last execve */
	u32 sid;		/* current SID */
	u32 exec_sid;		/* exec SID */
	u32 create_sid;		/* fscreate SID */
	u32 keycreate_sid;	/* keycreate SID */
	u32 sockcreate_sid;	/* fscreate SID */
};

enum label_initialized {
	LABEL_MISSING,		/* not initialized */
	LABEL_INITIALIZED,	/* inizialized */
	LABEL_INVALID		/* invalid */
};

struct inode_security_struct {
	struct inode *inode;	/* back pointer to inode object */
	union {
		struct list_head list;	/* list of inode_security_struct */
		struct rcu_head rcu;	/* for freeing the inode_security_struct */
	};
	u32 task_sid;		/* SID of creating task */
	u32 sid;		/* SID of this object */
	u16 sclass;		/* security class of this object */
	unsigned char initialized;	/* initialization flag */
	struct mutex lock;
};

struct file_security_struct {
	u32 sid;		/* SID of open file description */
	u32 fown_sid;		/* SID of file owner (for SIGIO) */
	u32 isid;		/* SID of inode at the time of file open */
	u32 pseqno;		/* Policy seqno at the time of file open */
};

struct superblock_security_struct {
	struct super_block *sb;		/* back pointer to sb object */
	u32 sid;			/* SID of file system superblock */
	u32 def_sid;			/* default SID for labeling */
	u32 mntpoint_sid;		/* SECURITY_FS_USE_MNTPOINT context for files */
	unsigned short behavior;	/* labeling behavior */
	unsigned short flags;		/* which mount options were specified */
	struct mutex lock;
	struct list_head isec_head;
	spinlock_t isec_lock;
};

struct msg_security_struct {
	u32 sid;	/* SID of message */
};

struct ipc_security_struct {
	u16 sclass;	/* security class of this object */
	u32 sid;	/* SID of IPC resource */
};

struct netif_security_struct {
	struct net *ns;			/* network namespace */
	int ifindex;			/* device index */
	u32 sid;			/* SID for this interface */
};

struct netnode_security_struct {
	union {
		__be32 ipv4;		/* IPv4 node address */
		struct in6_addr ipv6;	/* IPv6 node address */
	} addr;
	u32 sid;			/* SID for this node */
	u16 family;			/* address family */
};

struct netport_security_struct {
	u32 sid;			/* SID for this node */
	u16 port;			/* port number */
	u8 protocol;			/* transport protocol */
};

struct sk_security_struct {
#ifdef CONFIG_NETLABEL
	enum {				/* NetLabel state */
		NLBL_UNSET = 0,
		NLBL_REQUIRE,
		NLBL_LABELED,
		NLBL_REQSKB,
		NLBL_CONNLABELED,
	} nlbl_state;
	struct netlbl_lsm_secattr *nlbl_secattr; /* NetLabel sec attributes */
#endif
	u32 sid;			/* SID of this object */
	u32 peer_sid;			/* SID of peer */
	u16 sclass;			/* sock security class */
};

struct tun_security_struct {
	u32 sid;			/* SID for the tun device sockets */
};

struct key_security_struct {
	u32 sid;	/* SID of key */
};

extern unsigned int selinux_checkreqprot;

#endif /* _SELINUX_OBJSEC_H_ */
