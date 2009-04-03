/* NFS filesystem cache interface definitions
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _NFS_FSCACHE_H
#define _NFS_FSCACHE_H

#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/fscache.h>

#ifdef CONFIG_NFS_FSCACHE

/*
 * set of NFS FS-Cache objects that form a superblock key
 */
struct nfs_fscache_key {
	struct rb_node		node;
	struct nfs_client	*nfs_client;	/* the server */

	/* the elements of the unique key - as used by nfs_compare_super() and
	 * nfs_compare_mount_options() to distinguish superblocks */
	struct {
		struct {
			unsigned long	s_flags;	/* various flags
							 * (& NFS_MS_MASK) */
		} super;

		struct {
			struct nfs_fsid fsid;
			int		flags;
			unsigned int	rsize;		/* read size */
			unsigned int	wsize;		/* write size */
			unsigned int	acregmin;	/* attr cache timeouts */
			unsigned int	acregmax;
			unsigned int	acdirmin;
			unsigned int	acdirmax;
		} nfs_server;

		struct {
			rpc_authflavor_t au_flavor;
		} rpc_auth;

		/* uniquifier - can be used if nfs_server.flags includes
		 * NFS_MOUNT_UNSHARED  */
		u8 uniq_len;
		char uniquifier[0];
	} key;
};

/*
 * fscache-index.c
 */
extern struct fscache_netfs nfs_fscache_netfs;
extern const struct fscache_cookie_def nfs_fscache_server_index_def;
extern const struct fscache_cookie_def nfs_fscache_super_index_def;
extern const struct fscache_cookie_def nfs_fscache_inode_object_def;

extern int nfs_fscache_register(void);
extern void nfs_fscache_unregister(void);

/*
 * fscache.c
 */
extern void nfs_fscache_get_client_cookie(struct nfs_client *);
extern void nfs_fscache_release_client_cookie(struct nfs_client *);

extern void nfs_fscache_get_super_cookie(struct super_block *,
					 struct nfs_parsed_mount_data *);
extern void nfs_fscache_release_super_cookie(struct super_block *);

extern void nfs_fscache_init_inode_cookie(struct inode *);
extern void nfs_fscache_release_inode_cookie(struct inode *);
extern void nfs_fscache_zap_inode_cookie(struct inode *);
extern void nfs_fscache_set_inode_cookie(struct inode *, struct file *);
extern void nfs_fscache_reset_inode_cookie(struct inode *);

#else /* CONFIG_NFS_FSCACHE */
static inline int nfs_fscache_register(void) { return 0; }
static inline void nfs_fscache_unregister(void) {}

static inline void nfs_fscache_get_client_cookie(struct nfs_client *clp) {}
static inline void nfs_fscache_release_client_cookie(struct nfs_client *clp) {}

static inline void nfs_fscache_get_super_cookie(
	struct super_block *sb,
	struct nfs_parsed_mount_data *data)
{
}
static inline void nfs_fscache_release_super_cookie(struct super_block *sb) {}

static inline void nfs_fscache_init_inode_cookie(struct inode *inode) {}
static inline void nfs_fscache_release_inode_cookie(struct inode *inode) {}
static inline void nfs_fscache_zap_inode_cookie(struct inode *inode) {}
static inline void nfs_fscache_set_inode_cookie(struct inode *inode,
						struct file *filp) {}
static inline void nfs_fscache_reset_inode_cookie(struct inode *inode) {}

#endif /* CONFIG_NFS_FSCACHE */
#endif /* _NFS_FSCACHE_H */
