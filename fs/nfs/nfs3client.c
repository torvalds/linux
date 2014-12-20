#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include "internal.h"
#include "nfs3_fs.h"

#ifdef CONFIG_NFS_V3_ACL
static struct rpc_stat		nfsacl_rpcstat = { &nfsacl_program };
static const struct rpc_version *nfsacl_version[] = {
	[3]			= &nfsacl_version3,
};

const struct rpc_program nfsacl_program = {
	.name			= "nfsacl",
	.number			= NFS_ACL_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfsacl_version),
	.version		= nfsacl_version,
	.stats			= &nfsacl_rpcstat,
};

/*
 * Initialise an NFSv3 ACL client connection
 */
static void nfs_init_server_aclclient(struct nfs_server *server)
{
	if (server->flags & NFS_MOUNT_NOACL)
		goto out_noacl;

	server->client_acl = rpc_bind_new_program(server->client, &nfsacl_program, 3);
	if (IS_ERR(server->client_acl))
		goto out_noacl;

	/* No errors! Assume that Sun nfsacls are supported */
	server->caps |= NFS_CAP_ACLS;
	return;

out_noacl:
	server->caps &= ~NFS_CAP_ACLS;
}
#else
static inline void nfs_init_server_aclclient(struct nfs_server *server)
{
	server->flags &= ~NFS_MOUNT_NOACL;
	server->caps &= ~NFS_CAP_ACLS;
}
#endif

struct nfs_server *nfs3_create_server(struct nfs_mount_info *mount_info,
				      struct nfs_subversion *nfs_mod)
{
	struct nfs_server *server = nfs_create_server(mount_info, nfs_mod);
	/* Create a client RPC handle for the NFS v3 ACL management interface */
	if (!IS_ERR(server))
		nfs_init_server_aclclient(server);
	return server;
}

struct nfs_server *nfs3_clone_server(struct nfs_server *source,
				     struct nfs_fh *fh,
				     struct nfs_fattr *fattr,
				     rpc_authflavor_t flavor)
{
	struct nfs_server *server = nfs_clone_server(source, fh, fattr, flavor);
	if (!IS_ERR(server) && !IS_ERR(source->client_acl))
		nfs_init_server_aclclient(server);
	return server;
}
