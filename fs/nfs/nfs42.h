/*
 * Copyright (c) 2014 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */

#ifndef __LINUX_FS_NFS_NFS4_2_H
#define __LINUX_FS_NFS_NFS4_2_H

/* nfs4.2proc.c */
int nfs42_proc_allocate(struct file *, loff_t, loff_t);
int nfs42_proc_deallocate(struct file *, loff_t, loff_t);
loff_t nfs42_proc_llseek(struct file *, loff_t, int);

/* nfs4.2xdr.h */
extern struct rpc_procinfo nfs4_2_procedures[];

#endif /* __LINUX_FS_NFS_NFS4_2_H */
