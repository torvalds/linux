/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_FS_NFS_NFS4_0_H
#define __LINUX_FS_NFS_NFS4_0_H


/* nfs40proc.c */
extern const struct rpc_call_ops nfs40_call_sync_ops;
extern const struct nfs4_state_recovery_ops nfs40_reboot_recovery_ops;
extern const struct nfs4_state_recovery_ops nfs40_nograce_recovery_ops;
extern const struct nfs4_state_maintenance_ops nfs40_state_renewal_ops;
extern const struct nfs4_mig_recovery_ops nfs40_mig_recovery_ops;
extern const struct nfs4_minor_version_ops nfs_v4_0_minor_ops;

/* nfs40state.c */
int nfs40_discover_server_trunking(struct nfs_client *clp,
				   struct nfs_client **result,
				   const struct cred *cred);

#endif /* __LINUX_FS_NFS_NFS4_0_H */
