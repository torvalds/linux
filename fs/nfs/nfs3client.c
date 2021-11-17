// SPDX-License-Identifier: GPL-2.0-only
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/sunrpc/addr.h>
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

struct nfs_server *nfs3_create_server(struct fs_context *fc)
{
	struct nfs_server *server = nfs_create_server(fc);

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

/*
 * Set up a pNFS Data Server client over NFSv3.
 *
 * Return any existing nfs_client that matches server address,port,version
 * and minorversion.
 *
 * For a new nfs_client, use a soft mount (default), a low retrans and a
 * low timeout interval so that if a connection is lost, we retry through
 * the MDS.
 */
struct nfs_client *nfs3_set_ds_client(struct nfs_server *mds_srv,
		const struct sockaddr *ds_addr, int ds_addrlen,
		int ds_proto, unsigned int ds_timeo, unsigned int ds_retrans)
{
	struct rpc_timeout ds_timeout;
	struct nfs_client *mds_clp = mds_srv->nfs_client;
	struct nfs_client_initdata cl_init = {
		.addr = ds_addr,
		.addrlen = ds_addrlen,
		.nodename = mds_clp->cl_rpcclient->cl_nodename,
		.ip_addr = mds_clp->cl_ipaddr,
		.nfs_mod = &nfs_v3,
		.proto = ds_proto,
		.net = mds_clp->cl_net,
		.timeparms = &ds_timeout,
		.cred = mds_srv->cred,
	};
	struct nfs_client *clp;
	char buf[INET6_ADDRSTRLEN + 1];

	/* fake a hostname because lockd wants it */
	if (rpc_ntop(ds_addr, buf, sizeof(buf)) <= 0)
		return ERR_PTR(-EINVAL);
	cl_init.hostname = buf;

	if (mds_clp->cl_nconnect > 1 && ds_proto == XPRT_TRANSPORT_TCP)
		cl_init.nconnect = mds_clp->cl_nconnect;

	if (mds_srv->flags & NFS_MOUNT_NORESVPORT)
		__set_bit(NFS_CS_NORESVPORT, &cl_init.init_flags);

	__set_bit(NFS_CS_NOPING, &cl_init.init_flags);
	__set_bit(NFS_CS_DS, &cl_init.init_flags);

	/* Use the MDS nfs_client cl_ipaddr. */
	nfs_init_timeout_values(&ds_timeout, ds_proto, ds_timeo, ds_retrans);
	clp = nfs_get_client(&cl_init);

	return clp;
}
EXPORT_SYMBOL_GPL(nfs3_set_ds_client);
