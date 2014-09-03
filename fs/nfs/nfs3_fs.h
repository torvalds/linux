/*
 * Copyright (C) 2014 Anna Schumaker.
 *
 * NFSv3-specific filesystem definitions and declarations
 */
#ifndef __LINUX_FS_NFS_NFS3_FS_H
#define __LINUX_FS_NFS_NFS3_FS_H

/* nfs3client.c */
struct nfs_server *nfs3_create_server(struct nfs_mount_info *, struct nfs_subversion *);
struct nfs_server *nfs3_clone_server(struct nfs_server *, struct nfs_fh *,
				     struct nfs_fattr *, rpc_authflavor_t);


#endif /* __LINUX_FS_NFS_NFS3_FS_H */
