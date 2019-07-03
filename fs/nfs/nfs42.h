/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */

#ifndef __LINUX_FS_NFS_NFS4_2_H
#define __LINUX_FS_NFS_NFS4_2_H

/*
 * FIXME:  four LAYOUTSTATS calls per compound at most! Do we need to support
 * more? Need to consider not to pre-alloc too much for a compound.
 */
#define PNFS_LAYOUTSTATS_MAXDEV (4)

/* nfs4.2proc.c */
#ifdef CONFIG_NFS_V4_2
int nfs42_proc_allocate(struct file *, loff_t, loff_t);
ssize_t nfs42_proc_copy(struct file *, loff_t, struct file *, loff_t, size_t,
			struct nl4_server *, nfs4_stateid *, bool);
int nfs42_proc_deallocate(struct file *, loff_t, loff_t);
loff_t nfs42_proc_llseek(struct file *, loff_t, int);
int nfs42_proc_layoutstats_generic(struct nfs_server *,
				   struct nfs42_layoutstat_data *);
int nfs42_proc_clone(struct file *, struct file *, loff_t, loff_t, loff_t);
int nfs42_proc_layouterror(struct pnfs_layout_segment *lseg,
			   const struct nfs42_layout_error *errors,
			   size_t n);
int nfs42_proc_copy_notify(struct file *, struct file *,
			   struct nfs42_copy_notify_res *);
static inline bool nfs42_files_from_same_server(struct file *in,
						struct file *out)
{
	struct nfs_client *c_in = (NFS_SERVER(file_inode(in)))->nfs_client;
	struct nfs_client *c_out = (NFS_SERVER(file_inode(out)))->nfs_client;

	return nfs4_check_serverowner_major_id(c_in->cl_serverowner,
					       c_out->cl_serverowner);
}
#endif /* CONFIG_NFS_V4_2 */
#endif /* __LINUX_FS_NFS_NFS4_2_H */
