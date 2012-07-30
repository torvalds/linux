/*
 * Copyright (c) 2012 Netapp, Inc. All rights reserved.
 *
 * Function and structures exported by the NFS module
 * for use by NFS version-specific modules.
 */
#ifndef __LINUX_INTERNAL_NFS_H
#define __LINUX_INTERNAL_NFS_H

#include <linux/fs.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_xdr.h>

struct nfs_subversion {
	struct module *owner;	/* THIS_MODULE pointer */
	struct file_system_type *nfs_fs;	/* NFS filesystem type */
	const struct rpc_version *rpc_vers;	/* NFS version information */
	const struct nfs_rpc_ops *rpc_ops;	/* NFS operations */
	const struct super_operations *sops;	/* NFS Super operations */
	const struct xattr_handler **xattr;	/* NFS xattr handlers */
	struct list_head list;		/* List of NFS versions */
};

int nfs_register_versions(void);
void nfs_unregister_versions(void);

#ifdef CONFIG_NFS_V2
int init_nfs_v2(void);
void exit_nfs_v2(void);
#else /* CONFIG_NFS_V2 */
static inline int __init init_nfs_v2(void)
{
	return 0;
}

static inline void exit_nfs_v2(void)
{
}
#endif /* CONFIG_NFS_V2 */

#ifdef CONFIG_NFS_V3
int init_nfs_v3(void);
void exit_nfs_v3(void);
#else /* CONFIG_NFS_V3 */
static inline int __init init_nfs_v3(void)
{
	return 0;
}

static inline void exit_nfs_v3(void)
{
}
#endif /* CONFIG_NFS_V3 */

#ifdef CONFIG_NFS_V4
int init_nfs_v4(void);
void exit_nfs_v4(void);
#else /* CONFIG_NFS_V4 */
static inline int __init init_nfs_v4(void)
{
	return 0;
}

static inline void exit_nfs_v4(void)
{
}
#endif /* CONFIG_NFS_V4 */

struct nfs_subversion *get_nfs_version(unsigned int);
void put_nfs_version(struct nfs_subversion *);
void register_nfs_version(struct nfs_subversion *);
void unregister_nfs_version(struct nfs_subversion *);

#endif /* __LINUX_INTERNAL_NFS_H */
