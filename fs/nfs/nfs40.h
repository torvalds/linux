/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_FS_NFS_NFS4_0_H
#define __LINUX_FS_NFS_NFS4_0_H


/* nfs40client.c */
void nfs40_shutdown_client(struct nfs_client *);
int nfs40_init_client(struct nfs_client *);
void nfs40_handle_cb_pathdown(struct nfs_client *clp);

/* nfs40proc.c */
extern const struct nfs4_minor_version_ops nfs_v4_0_minor_ops;

/* nfs40state.c */
int nfs40_discover_server_trunking(struct nfs_client *clp,
				   struct nfs_client **result,
				   const struct cred *cred);

#endif /* __LINUX_FS_NFS_NFS4_0_H */
