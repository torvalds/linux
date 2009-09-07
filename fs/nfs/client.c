/* client.c: NFS client sharing and management code
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/xprtrdma.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/in6.h>
#include <net/ipv6.h>
#include <linux/nfs_xdr.h>
#include <linux/sunrpc/bc_xprt.h>

#include <asm/system.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

static DEFINE_SPINLOCK(nfs_client_lock);
static LIST_HEAD(nfs_client_list);
static LIST_HEAD(nfs_volume_list);
static DECLARE_WAIT_QUEUE_HEAD(nfs_client_active_wq);

/*
 * RPC cruft for NFS
 */
static struct rpc_version *nfs_version[5] = {
	[2]			= &nfs_version2,
#ifdef CONFIG_NFS_V3
	[3]			= &nfs_version3,
#endif
#ifdef CONFIG_NFS_V4
	[4]			= &nfs_version4,
#endif
};

struct rpc_program nfs_program = {
	.name			= "nfs",
	.number			= NFS_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfs_version),
	.version		= nfs_version,
	.stats			= &nfs_rpcstat,
	.pipe_dir_name		= "/nfs",
};

struct rpc_stat nfs_rpcstat = {
	.program		= &nfs_program
};


#ifdef CONFIG_NFS_V3_ACL
static struct rpc_stat		nfsacl_rpcstat = { &nfsacl_program };
static struct rpc_version *	nfsacl_version[] = {
	[3]			= &nfsacl_version3,
};

struct rpc_program		nfsacl_program = {
	.name			= "nfsacl",
	.number			= NFS_ACL_PROGRAM,
	.nrvers			= ARRAY_SIZE(nfsacl_version),
	.version		= nfsacl_version,
	.stats			= &nfsacl_rpcstat,
};
#endif  /* CONFIG_NFS_V3_ACL */

struct nfs_client_initdata {
	const char *hostname;
	const struct sockaddr *addr;
	size_t addrlen;
	const struct nfs_rpc_ops *rpc_ops;
	int proto;
	u32 minorversion;
};

/*
 * Allocate a shared client record
 *
 * Since these are allocated/deallocated very rarely, we don't
 * bother putting them in a slab cache...
 */
static struct nfs_client *nfs_alloc_client(const struct nfs_client_initdata *cl_init)
{
	struct nfs_client *clp;
	struct rpc_cred *cred;
	int err = -ENOMEM;

	if ((clp = kzalloc(sizeof(*clp), GFP_KERNEL)) == NULL)
		goto error_0;

	clp->rpc_ops = cl_init->rpc_ops;

	atomic_set(&clp->cl_count, 1);
	clp->cl_cons_state = NFS_CS_INITING;

	memcpy(&clp->cl_addr, cl_init->addr, cl_init->addrlen);
	clp->cl_addrlen = cl_init->addrlen;

	if (cl_init->hostname) {
		err = -ENOMEM;
		clp->cl_hostname = kstrdup(cl_init->hostname, GFP_KERNEL);
		if (!clp->cl_hostname)
			goto error_cleanup;
	}

	INIT_LIST_HEAD(&clp->cl_superblocks);
	clp->cl_rpcclient = ERR_PTR(-EINVAL);

	clp->cl_proto = cl_init->proto;

#ifdef CONFIG_NFS_V4
	INIT_LIST_HEAD(&clp->cl_delegations);
	spin_lock_init(&clp->cl_lock);
	INIT_DELAYED_WORK(&clp->cl_renewd, nfs4_renew_state);
	rpc_init_wait_queue(&clp->cl_rpcwaitq, "NFS client");
	clp->cl_boot_time = CURRENT_TIME;
	clp->cl_state = 1 << NFS4CLNT_LEASE_EXPIRED;
	clp->cl_minorversion = cl_init->minorversion;
#endif
	cred = rpc_lookup_machine_cred();
	if (!IS_ERR(cred))
		clp->cl_machine_cred = cred;

	nfs_fscache_get_client_cookie(clp);

	return clp;

error_cleanup:
	kfree(clp);
error_0:
	return ERR_PTR(err);
}

static void nfs4_shutdown_client(struct nfs_client *clp)
{
#ifdef CONFIG_NFS_V4
	if (__test_and_clear_bit(NFS_CS_RENEWD, &clp->cl_res_state))
		nfs4_kill_renewd(clp);
	BUG_ON(!RB_EMPTY_ROOT(&clp->cl_state_owners));
	if (__test_and_clear_bit(NFS_CS_IDMAP, &clp->cl_res_state))
		nfs_idmap_delete(clp);

	rpc_destroy_wait_queue(&clp->cl_rpcwaitq);
#endif
}

/*
 * Destroy the NFS4 callback service
 */
static void nfs4_destroy_callback(struct nfs_client *clp)
{
#ifdef CONFIG_NFS_V4
	if (__test_and_clear_bit(NFS_CS_CALLBACK, &clp->cl_res_state))
		nfs_callback_down(clp->cl_minorversion);
#endif /* CONFIG_NFS_V4 */
}

/*
 * Clears/puts all minor version specific parts from an nfs_client struct
 * reverting it to minorversion 0.
 */
static void nfs4_clear_client_minor_version(struct nfs_client *clp)
{
#ifdef CONFIG_NFS_V4_1
	if (nfs4_has_session(clp)) {
		nfs4_destroy_session(clp->cl_session);
		clp->cl_session = NULL;
	}

	clp->cl_call_sync = _nfs4_call_sync;
#endif /* CONFIG_NFS_V4_1 */

	nfs4_destroy_callback(clp);
}

/*
 * Destroy a shared client record
 */
static void nfs_free_client(struct nfs_client *clp)
{
	dprintk("--> nfs_free_client(%u)\n", clp->rpc_ops->version);

	nfs4_clear_client_minor_version(clp);
	nfs4_shutdown_client(clp);

	nfs_fscache_release_client_cookie(clp);

	/* -EIO all pending I/O */
	if (!IS_ERR(clp->cl_rpcclient))
		rpc_shutdown_client(clp->cl_rpcclient);

	if (clp->cl_machine_cred != NULL)
		put_rpccred(clp->cl_machine_cred);

	kfree(clp->cl_hostname);
	kfree(clp);

	dprintk("<-- nfs_free_client()\n");
}

/*
 * Release a reference to a shared client record
 */
void nfs_put_client(struct nfs_client *clp)
{
	if (!clp)
		return;

	dprintk("--> nfs_put_client({%d})\n", atomic_read(&clp->cl_count));

	if (atomic_dec_and_lock(&clp->cl_count, &nfs_client_lock)) {
		list_del(&clp->cl_share_link);
		spin_unlock(&nfs_client_lock);

		BUG_ON(!list_empty(&clp->cl_superblocks));

		nfs_free_client(clp);
	}
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
/*
 * Test if two ip6 socket addresses refer to the same socket by
 * comparing relevant fields. The padding bytes specifically, are not
 * compared. sin6_flowinfo is not compared because it only affects QoS
 * and sin6_scope_id is only compared if the address is "link local"
 * because "link local" addresses need only be unique to a specific
 * link. Conversely, ordinary unicast addresses might have different
 * sin6_scope_id.
 *
 * The caller should ensure both socket addresses are AF_INET6.
 */
static int nfs_sockaddr_match_ipaddr6(const struct sockaddr *sa1,
				      const struct sockaddr *sa2)
{
	const struct sockaddr_in6 *sin1 = (const struct sockaddr_in6 *)sa1;
	const struct sockaddr_in6 *sin2 = (const struct sockaddr_in6 *)sa2;

	if (ipv6_addr_scope(&sin1->sin6_addr) == IPV6_ADDR_SCOPE_LINKLOCAL &&
	    sin1->sin6_scope_id != sin2->sin6_scope_id)
		return 0;

	return ipv6_addr_equal(&sin1->sin6_addr, &sin1->sin6_addr);
}
#else	/* !defined(CONFIG_IPV6) && !defined(CONFIG_IPV6_MODULE) */
static int nfs_sockaddr_match_ipaddr6(const struct sockaddr *sa1,
				      const struct sockaddr *sa2)
{
	return 0;
}
#endif

/*
 * Test if two ip4 socket addresses refer to the same socket, by
 * comparing relevant fields. The padding bytes specifically, are
 * not compared.
 *
 * The caller should ensure both socket addresses are AF_INET.
 */
static int nfs_sockaddr_match_ipaddr4(const struct sockaddr *sa1,
				      const struct sockaddr *sa2)
{
	const struct sockaddr_in *sin1 = (const struct sockaddr_in *)sa1;
	const struct sockaddr_in *sin2 = (const struct sockaddr_in *)sa2;

	return sin1->sin_addr.s_addr == sin2->sin_addr.s_addr;
}

static int nfs_sockaddr_cmp_ip6(const struct sockaddr *sa1,
				const struct sockaddr *sa2)
{
	const struct sockaddr_in6 *sin1 = (const struct sockaddr_in6 *)sa1;
	const struct sockaddr_in6 *sin2 = (const struct sockaddr_in6 *)sa2;

	return nfs_sockaddr_match_ipaddr6(sa1, sa2) &&
		(sin1->sin6_port == sin2->sin6_port);
}

static int nfs_sockaddr_cmp_ip4(const struct sockaddr *sa1,
				const struct sockaddr *sa2)
{
	const struct sockaddr_in *sin1 = (const struct sockaddr_in *)sa1;
	const struct sockaddr_in *sin2 = (const struct sockaddr_in *)sa2;

	return nfs_sockaddr_match_ipaddr4(sa1, sa2) &&
		(sin1->sin_port == sin2->sin_port);
}

/*
 * Test if two socket addresses represent the same actual socket,
 * by comparing (only) relevant fields, excluding the port number.
 */
static int nfs_sockaddr_match_ipaddr(const struct sockaddr *sa1,
				     const struct sockaddr *sa2)
{
	if (sa1->sa_family != sa2->sa_family)
		return 0;

	switch (sa1->sa_family) {
	case AF_INET:
		return nfs_sockaddr_match_ipaddr4(sa1, sa2);
	case AF_INET6:
		return nfs_sockaddr_match_ipaddr6(sa1, sa2);
	}
	return 0;
}

/*
 * Test if two socket addresses represent the same actual socket,
 * by comparing (only) relevant fields, including the port number.
 */
static int nfs_sockaddr_cmp(const struct sockaddr *sa1,
			    const struct sockaddr *sa2)
{
	if (sa1->sa_family != sa2->sa_family)
		return 0;

	switch (sa1->sa_family) {
	case AF_INET:
		return nfs_sockaddr_cmp_ip4(sa1, sa2);
	case AF_INET6:
		return nfs_sockaddr_cmp_ip6(sa1, sa2);
	}
	return 0;
}

/*
 * Find a client by IP address and protocol version
 * - returns NULL if no such client
 */
struct nfs_client *nfs_find_client(const struct sockaddr *addr, u32 nfsversion)
{
	struct nfs_client *clp;

	spin_lock(&nfs_client_lock);
	list_for_each_entry(clp, &nfs_client_list, cl_share_link) {
		struct sockaddr *clap = (struct sockaddr *)&clp->cl_addr;

		/* Don't match clients that failed to initialise properly */
		if (!(clp->cl_cons_state == NFS_CS_READY ||
		      clp->cl_cons_state == NFS_CS_SESSION_INITING))
			continue;

		/* Different NFS versions cannot share the same nfs_client */
		if (clp->rpc_ops->version != nfsversion)
			continue;

		/* Match only the IP address, not the port number */
		if (!nfs_sockaddr_match_ipaddr(addr, clap))
			continue;

		atomic_inc(&clp->cl_count);
		spin_unlock(&nfs_client_lock);
		return clp;
	}
	spin_unlock(&nfs_client_lock);
	return NULL;
}

/*
 * Find a client by IP address and protocol version
 * - returns NULL if no such client
 */
struct nfs_client *nfs_find_client_next(struct nfs_client *clp)
{
	struct sockaddr *sap = (struct sockaddr *)&clp->cl_addr;
	u32 nfsvers = clp->rpc_ops->version;

	spin_lock(&nfs_client_lock);
	list_for_each_entry_continue(clp, &nfs_client_list, cl_share_link) {
		struct sockaddr *clap = (struct sockaddr *)&clp->cl_addr;

		/* Don't match clients that failed to initialise properly */
		if (clp->cl_cons_state != NFS_CS_READY)
			continue;

		/* Different NFS versions cannot share the same nfs_client */
		if (clp->rpc_ops->version != nfsvers)
			continue;

		/* Match only the IP address, not the port number */
		if (!nfs_sockaddr_match_ipaddr(sap, clap))
			continue;

		atomic_inc(&clp->cl_count);
		spin_unlock(&nfs_client_lock);
		return clp;
	}
	spin_unlock(&nfs_client_lock);
	return NULL;
}

/*
 * Find an nfs_client on the list that matches the initialisation data
 * that is supplied.
 */
static struct nfs_client *nfs_match_client(const struct nfs_client_initdata *data)
{
	struct nfs_client *clp;
	const struct sockaddr *sap = data->addr;

	list_for_each_entry(clp, &nfs_client_list, cl_share_link) {
	        const struct sockaddr *clap = (struct sockaddr *)&clp->cl_addr;
		/* Don't match clients that failed to initialise properly */
		if (clp->cl_cons_state < 0)
			continue;

		/* Different NFS versions cannot share the same nfs_client */
		if (clp->rpc_ops != data->rpc_ops)
			continue;

		if (clp->cl_proto != data->proto)
			continue;
		/* Match nfsv4 minorversion */
		if (clp->cl_minorversion != data->minorversion)
			continue;
		/* Match the full socket address */
		if (!nfs_sockaddr_cmp(sap, clap))
			continue;

		atomic_inc(&clp->cl_count);
		return clp;
	}
	return NULL;
}

/*
 * Look up a client by IP address and protocol version
 * - creates a new record if one doesn't yet exist
 */
static struct nfs_client *nfs_get_client(const struct nfs_client_initdata *cl_init)
{
	struct nfs_client *clp, *new = NULL;
	int error;

	dprintk("--> nfs_get_client(%s,v%u)\n",
		cl_init->hostname ?: "", cl_init->rpc_ops->version);

	/* see if the client already exists */
	do {
		spin_lock(&nfs_client_lock);

		clp = nfs_match_client(cl_init);
		if (clp)
			goto found_client;
		if (new)
			goto install_client;

		spin_unlock(&nfs_client_lock);

		new = nfs_alloc_client(cl_init);
	} while (!IS_ERR(new));

	dprintk("--> nfs_get_client() = %ld [failed]\n", PTR_ERR(new));
	return new;

	/* install a new client and return with it unready */
install_client:
	clp = new;
	list_add(&clp->cl_share_link, &nfs_client_list);
	spin_unlock(&nfs_client_lock);
	dprintk("--> nfs_get_client() = %p [new]\n", clp);
	return clp;

	/* found an existing client
	 * - make sure it's ready before returning
	 */
found_client:
	spin_unlock(&nfs_client_lock);

	if (new)
		nfs_free_client(new);

	error = wait_event_killable(nfs_client_active_wq,
				clp->cl_cons_state < NFS_CS_INITING);
	if (error < 0) {
		nfs_put_client(clp);
		return ERR_PTR(-ERESTARTSYS);
	}

	if (clp->cl_cons_state < NFS_CS_READY) {
		error = clp->cl_cons_state;
		nfs_put_client(clp);
		return ERR_PTR(error);
	}

	BUG_ON(clp->cl_cons_state != NFS_CS_READY);

	dprintk("--> nfs_get_client() = %p [share]\n", clp);
	return clp;
}

/*
 * Mark a server as ready or failed
 */
void nfs_mark_client_ready(struct nfs_client *clp, int state)
{
	clp->cl_cons_state = state;
	wake_up_all(&nfs_client_active_wq);
}

/*
 * With sessions, the client is not marked ready until after a
 * successful EXCHANGE_ID and CREATE_SESSION.
 *
 * Map errors cl_cons_state errors to EPROTONOSUPPORT to indicate
 * other versions of NFS can be tried.
 */
int nfs4_check_client_ready(struct nfs_client *clp)
{
	if (!nfs4_has_session(clp))
		return 0;
	if (clp->cl_cons_state < NFS_CS_READY)
		return -EPROTONOSUPPORT;
	return 0;
}

/*
 * Initialise the timeout values for a connection
 */
static void nfs_init_timeout_values(struct rpc_timeout *to, int proto,
				    unsigned int timeo, unsigned int retrans)
{
	to->to_initval = timeo * HZ / 10;
	to->to_retries = retrans;

	switch (proto) {
	case XPRT_TRANSPORT_TCP:
	case XPRT_TRANSPORT_RDMA:
		if (to->to_retries == 0)
			to->to_retries = NFS_DEF_TCP_RETRANS;
		if (to->to_initval == 0)
			to->to_initval = NFS_DEF_TCP_TIMEO * HZ / 10;
		if (to->to_initval > NFS_MAX_TCP_TIMEOUT)
			to->to_initval = NFS_MAX_TCP_TIMEOUT;
		to->to_increment = to->to_initval;
		to->to_maxval = to->to_initval + (to->to_increment * to->to_retries);
		if (to->to_maxval > NFS_MAX_TCP_TIMEOUT)
			to->to_maxval = NFS_MAX_TCP_TIMEOUT;
		if (to->to_maxval < to->to_initval)
			to->to_maxval = to->to_initval;
		to->to_exponential = 0;
		break;
	case XPRT_TRANSPORT_UDP:
		if (to->to_retries == 0)
			to->to_retries = NFS_DEF_UDP_RETRANS;
		if (!to->to_initval)
			to->to_initval = NFS_DEF_UDP_TIMEO * HZ / 10;
		if (to->to_initval > NFS_MAX_UDP_TIMEOUT)
			to->to_initval = NFS_MAX_UDP_TIMEOUT;
		to->to_maxval = NFS_MAX_UDP_TIMEOUT;
		to->to_exponential = 1;
		break;
	default:
		BUG();
	}
}

/*
 * Create an RPC client handle
 */
static int nfs_create_rpc_client(struct nfs_client *clp,
				 const struct rpc_timeout *timeparms,
				 rpc_authflavor_t flavor,
				 int discrtry, int noresvport)
{
	struct rpc_clnt		*clnt = NULL;
	struct rpc_create_args args = {
		.protocol	= clp->cl_proto,
		.address	= (struct sockaddr *)&clp->cl_addr,
		.addrsize	= clp->cl_addrlen,
		.timeout	= timeparms,
		.servername	= clp->cl_hostname,
		.program	= &nfs_program,
		.version	= clp->rpc_ops->version,
		.authflavor	= flavor,
	};

	if (discrtry)
		args.flags |= RPC_CLNT_CREATE_DISCRTRY;
	if (noresvport)
		args.flags |= RPC_CLNT_CREATE_NONPRIVPORT;

	if (!IS_ERR(clp->cl_rpcclient))
		return 0;

	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		dprintk("%s: cannot create RPC client. Error = %ld\n",
				__func__, PTR_ERR(clnt));
		return PTR_ERR(clnt);
	}

	clp->cl_rpcclient = clnt;
	return 0;
}

/*
 * Version 2 or 3 client destruction
 */
static void nfs_destroy_server(struct nfs_server *server)
{
	if (!(server->flags & NFS_MOUNT_NONLM))
		nlmclnt_done(server->nlm_host);
}

/*
 * Version 2 or 3 lockd setup
 */
static int nfs_start_lockd(struct nfs_server *server)
{
	struct nlm_host *host;
	struct nfs_client *clp = server->nfs_client;
	struct nlmclnt_initdata nlm_init = {
		.hostname	= clp->cl_hostname,
		.address	= (struct sockaddr *)&clp->cl_addr,
		.addrlen	= clp->cl_addrlen,
		.protocol	= server->flags & NFS_MOUNT_TCP ?
						IPPROTO_TCP : IPPROTO_UDP,
		.nfs_version	= clp->rpc_ops->version,
		.noresvport	= server->flags & NFS_MOUNT_NORESVPORT ?
					1 : 0,
	};

	if (nlm_init.nfs_version > 3)
		return 0;
	if (server->flags & NFS_MOUNT_NONLM)
		return 0;

	host = nlmclnt_init(&nlm_init);
	if (IS_ERR(host))
		return PTR_ERR(host);

	server->nlm_host = host;
	server->destroy = nfs_destroy_server;
	return 0;
}

/*
 * Initialise an NFSv3 ACL client connection
 */
#ifdef CONFIG_NFS_V3_ACL
static void nfs_init_server_aclclient(struct nfs_server *server)
{
	if (server->nfs_client->rpc_ops->version != 3)
		goto out_noacl;
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

/*
 * Create a general RPC client
 */
static int nfs_init_server_rpcclient(struct nfs_server *server,
		const struct rpc_timeout *timeo,
		rpc_authflavor_t pseudoflavour)
{
	struct nfs_client *clp = server->nfs_client;

	server->client = rpc_clone_client(clp->cl_rpcclient);
	if (IS_ERR(server->client)) {
		dprintk("%s: couldn't create rpc_client!\n", __func__);
		return PTR_ERR(server->client);
	}

	memcpy(&server->client->cl_timeout_default,
			timeo,
			sizeof(server->client->cl_timeout_default));
	server->client->cl_timeout = &server->client->cl_timeout_default;

	if (pseudoflavour != clp->cl_rpcclient->cl_auth->au_flavor) {
		struct rpc_auth *auth;

		auth = rpcauth_create(pseudoflavour, server->client);
		if (IS_ERR(auth)) {
			dprintk("%s: couldn't create credcache!\n", __func__);
			return PTR_ERR(auth);
		}
	}
	server->client->cl_softrtry = 0;
	if (server->flags & NFS_MOUNT_SOFT)
		server->client->cl_softrtry = 1;

	return 0;
}

/*
 * Initialise an NFS2 or NFS3 client
 */
static int nfs_init_client(struct nfs_client *clp,
			   const struct rpc_timeout *timeparms,
			   const struct nfs_parsed_mount_data *data)
{
	int error;

	if (clp->cl_cons_state == NFS_CS_READY) {
		/* the client is already initialised */
		dprintk("<-- nfs_init_client() = 0 [already %p]\n", clp);
		return 0;
	}

	/*
	 * Create a client RPC handle for doing FSSTAT with UNIX auth only
	 * - RFC 2623, sec 2.3.2
	 */
	error = nfs_create_rpc_client(clp, timeparms, RPC_AUTH_UNIX,
				      0, data->flags & NFS_MOUNT_NORESVPORT);
	if (error < 0)
		goto error;
	nfs_mark_client_ready(clp, NFS_CS_READY);
	return 0;

error:
	nfs_mark_client_ready(clp, error);
	dprintk("<-- nfs_init_client() = xerror %d\n", error);
	return error;
}

/*
 * Create a version 2 or 3 client
 */
static int nfs_init_server(struct nfs_server *server,
			   const struct nfs_parsed_mount_data *data)
{
	struct nfs_client_initdata cl_init = {
		.hostname = data->nfs_server.hostname,
		.addr = (const struct sockaddr *)&data->nfs_server.address,
		.addrlen = data->nfs_server.addrlen,
		.rpc_ops = &nfs_v2_clientops,
		.proto = data->nfs_server.protocol,
	};
	struct rpc_timeout timeparms;
	struct nfs_client *clp;
	int error;

	dprintk("--> nfs_init_server()\n");

#ifdef CONFIG_NFS_V3
	if (data->flags & NFS_MOUNT_VER3)
		cl_init.rpc_ops = &nfs_v3_clientops;
#endif

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(&cl_init);
	if (IS_ERR(clp)) {
		dprintk("<-- nfs_init_server() = error %ld\n", PTR_ERR(clp));
		return PTR_ERR(clp);
	}

	nfs_init_timeout_values(&timeparms, data->nfs_server.protocol,
			data->timeo, data->retrans);
	error = nfs_init_client(clp, &timeparms, data);
	if (error < 0)
		goto error;

	server->nfs_client = clp;

	/* Initialise the client representation from the mount data */
	server->flags = data->flags;
	server->options = data->options;

	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);

	server->acregmin = data->acregmin * HZ;
	server->acregmax = data->acregmax * HZ;
	server->acdirmin = data->acdirmin * HZ;
	server->acdirmax = data->acdirmax * HZ;

	/* Start lockd here, before we might error out */
	error = nfs_start_lockd(server);
	if (error < 0)
		goto error;

	server->port = data->nfs_server.port;

	error = nfs_init_server_rpcclient(server, &timeparms, data->auth_flavors[0]);
	if (error < 0)
		goto error;

	/* Preserve the values of mount_server-related mount options */
	if (data->mount_server.addrlen) {
		memcpy(&server->mountd_address, &data->mount_server.address,
			data->mount_server.addrlen);
		server->mountd_addrlen = data->mount_server.addrlen;
	}
	server->mountd_version = data->mount_server.version;
	server->mountd_port = data->mount_server.port;
	server->mountd_protocol = data->mount_server.protocol;

	server->namelen  = data->namlen;
	/* Create a client RPC handle for the NFSv3 ACL management interface */
	nfs_init_server_aclclient(server);
	dprintk("<-- nfs_init_server() = 0 [new %p]\n", clp);
	return 0;

error:
	server->nfs_client = NULL;
	nfs_put_client(clp);
	dprintk("<-- nfs_init_server() = xerror %d\n", error);
	return error;
}

/*
 * Load up the server record from information gained in an fsinfo record
 */
static void nfs_server_set_fsinfo(struct nfs_server *server, struct nfs_fsinfo *fsinfo)
{
	unsigned long max_rpc_payload;

	/* Work out a lot of parameters */
	if (server->rsize == 0)
		server->rsize = nfs_block_size(fsinfo->rtpref, NULL);
	if (server->wsize == 0)
		server->wsize = nfs_block_size(fsinfo->wtpref, NULL);

	if (fsinfo->rtmax >= 512 && server->rsize > fsinfo->rtmax)
		server->rsize = nfs_block_size(fsinfo->rtmax, NULL);
	if (fsinfo->wtmax >= 512 && server->wsize > fsinfo->wtmax)
		server->wsize = nfs_block_size(fsinfo->wtmax, NULL);

	max_rpc_payload = nfs_block_size(rpc_max_payload(server->client), NULL);
	if (server->rsize > max_rpc_payload)
		server->rsize = max_rpc_payload;
	if (server->rsize > NFS_MAX_FILE_IO_SIZE)
		server->rsize = NFS_MAX_FILE_IO_SIZE;
	server->rpages = (server->rsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;

	server->backing_dev_info.ra_pages = server->rpages * NFS_MAX_READAHEAD;

	if (server->wsize > max_rpc_payload)
		server->wsize = max_rpc_payload;
	if (server->wsize > NFS_MAX_FILE_IO_SIZE)
		server->wsize = NFS_MAX_FILE_IO_SIZE;
	server->wpages = (server->wsize + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	server->wtmult = nfs_block_bits(fsinfo->wtmult, NULL);

	server->dtsize = nfs_block_size(fsinfo->dtpref, NULL);
	if (server->dtsize > PAGE_CACHE_SIZE)
		server->dtsize = PAGE_CACHE_SIZE;
	if (server->dtsize > server->rsize)
		server->dtsize = server->rsize;

	if (server->flags & NFS_MOUNT_NOAC) {
		server->acregmin = server->acregmax = 0;
		server->acdirmin = server->acdirmax = 0;
	}

	server->maxfilesize = fsinfo->maxfilesize;

	/* We're airborne Set socket buffersize */
	rpc_setbufsize(server->client, server->wsize + 100, server->rsize + 100);
}

/*
 * Probe filesystem information, including the FSID on v2/v3
 */
static int nfs_probe_fsinfo(struct nfs_server *server, struct nfs_fh *mntfh, struct nfs_fattr *fattr)
{
	struct nfs_fsinfo fsinfo;
	struct nfs_client *clp = server->nfs_client;
	int error;

	dprintk("--> nfs_probe_fsinfo()\n");

	if (clp->rpc_ops->set_capabilities != NULL) {
		error = clp->rpc_ops->set_capabilities(server, mntfh);
		if (error < 0)
			goto out_error;
	}

	fsinfo.fattr = fattr;
	nfs_fattr_init(fattr);
	error = clp->rpc_ops->fsinfo(server, mntfh, &fsinfo);
	if (error < 0)
		goto out_error;

	nfs_server_set_fsinfo(server, &fsinfo);
	error = bdi_init(&server->backing_dev_info);
	if (error)
		goto out_error;


	/* Get some general file system info */
	if (server->namelen == 0) {
		struct nfs_pathconf pathinfo;

		pathinfo.fattr = fattr;
		nfs_fattr_init(fattr);

		if (clp->rpc_ops->pathconf(server, mntfh, &pathinfo) >= 0)
			server->namelen = pathinfo.max_namelen;
	}

	dprintk("<-- nfs_probe_fsinfo() = 0\n");
	return 0;

out_error:
	dprintk("nfs_probe_fsinfo: error = %d\n", -error);
	return error;
}

/*
 * Copy useful information when duplicating a server record
 */
static void nfs_server_copy_userdata(struct nfs_server *target, struct nfs_server *source)
{
	target->flags = source->flags;
	target->acregmin = source->acregmin;
	target->acregmax = source->acregmax;
	target->acdirmin = source->acdirmin;
	target->acdirmax = source->acdirmax;
	target->caps = source->caps;
}

/*
 * Allocate and initialise a server record
 */
static struct nfs_server *nfs_alloc_server(void)
{
	struct nfs_server *server;

	server = kzalloc(sizeof(struct nfs_server), GFP_KERNEL);
	if (!server)
		return NULL;

	server->client = server->client_acl = ERR_PTR(-EINVAL);

	/* Zero out the NFS state stuff */
	INIT_LIST_HEAD(&server->client_link);
	INIT_LIST_HEAD(&server->master_link);

	atomic_set(&server->active, 0);

	server->io_stats = nfs_alloc_iostats();
	if (!server->io_stats) {
		kfree(server);
		return NULL;
	}

	return server;
}

/*
 * Free up a server record
 */
void nfs_free_server(struct nfs_server *server)
{
	dprintk("--> nfs_free_server()\n");

	spin_lock(&nfs_client_lock);
	list_del(&server->client_link);
	list_del(&server->master_link);
	spin_unlock(&nfs_client_lock);

	if (server->destroy != NULL)
		server->destroy(server);

	if (!IS_ERR(server->client_acl))
		rpc_shutdown_client(server->client_acl);
	if (!IS_ERR(server->client))
		rpc_shutdown_client(server->client);

	nfs_put_client(server->nfs_client);

	nfs_free_iostats(server->io_stats);
	bdi_destroy(&server->backing_dev_info);
	kfree(server);
	nfs_release_automount_timer();
	dprintk("<-- nfs_free_server()\n");
}

/*
 * Create a version 2 or 3 volume record
 * - keyed on server and FSID
 */
struct nfs_server *nfs_create_server(const struct nfs_parsed_mount_data *data,
				     struct nfs_fh *mntfh)
{
	struct nfs_server *server;
	struct nfs_fattr fattr;
	int error;

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	/* Get a client representation */
	error = nfs_init_server(server, data);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	/* Probe the root fh to retrieve its FSID */
	error = nfs_probe_fsinfo(server, mntfh, &fattr);
	if (error < 0)
		goto error;
	if (server->nfs_client->rpc_ops->version == 3) {
		if (server->namelen == 0 || server->namelen > NFS3_MAXNAMLEN)
			server->namelen = NFS3_MAXNAMLEN;
		if (!(data->flags & NFS_MOUNT_NORDIRPLUS))
			server->caps |= NFS_CAP_READDIRPLUS;
	} else {
		if (server->namelen == 0 || server->namelen > NFS2_MAXNAMLEN)
			server->namelen = NFS2_MAXNAMLEN;
	}

	if (!(fattr.valid & NFS_ATTR_FATTR)) {
		error = server->nfs_client->rpc_ops->getattr(server, mntfh, &fattr);
		if (error < 0) {
			dprintk("nfs_create_server: getattr error = %d\n", -error);
			goto error;
		}
	}
	memcpy(&server->fsid, &fattr.fsid, sizeof(server->fsid));

	dprintk("Server FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;
	return server;

error:
	nfs_free_server(server);
	return ERR_PTR(error);
}

#ifdef CONFIG_NFS_V4
/*
 * Initialize the NFS4 callback service
 */
static int nfs4_init_callback(struct nfs_client *clp)
{
	int error;

	if (clp->rpc_ops->version == 4) {
		if (nfs4_has_session(clp)) {
			error = xprt_setup_backchannel(
						clp->cl_rpcclient->cl_xprt,
						NFS41_BC_MIN_CALLBACKS);
			if (error < 0)
				return error;
		}

		error = nfs_callback_up(clp->cl_minorversion,
					clp->cl_rpcclient->cl_xprt);
		if (error < 0) {
			dprintk("%s: failed to start callback. Error = %d\n",
				__func__, error);
			return error;
		}
		__set_bit(NFS_CS_CALLBACK, &clp->cl_res_state);
	}
	return 0;
}

/*
 * Initialize the minor version specific parts of an NFS4 client record
 */
static int nfs4_init_client_minor_version(struct nfs_client *clp)
{
	clp->cl_call_sync = _nfs4_call_sync;

#if defined(CONFIG_NFS_V4_1)
	if (clp->cl_minorversion) {
		struct nfs4_session *session = NULL;
		/*
		 * Create the session and mark it expired.
		 * When a SEQUENCE operation encounters the expired session
		 * it will do session recovery to initialize it.
		 */
		session = nfs4_alloc_session(clp);
		if (!session)
			return -ENOMEM;

		clp->cl_session = session;
		clp->cl_call_sync = _nfs4_call_sync_session;
	}
#endif /* CONFIG_NFS_V4_1 */

	return nfs4_init_callback(clp);
}

/*
 * Initialise an NFS4 client record
 */
static int nfs4_init_client(struct nfs_client *clp,
		const struct rpc_timeout *timeparms,
		const char *ip_addr,
		rpc_authflavor_t authflavour,
		int flags)
{
	int error;

	if (clp->cl_cons_state == NFS_CS_READY) {
		/* the client is initialised already */
		dprintk("<-- nfs4_init_client() = 0 [already %p]\n", clp);
		return 0;
	}

	/* Check NFS protocol revision and initialize RPC op vector */
	clp->rpc_ops = &nfs_v4_clientops;

	error = nfs_create_rpc_client(clp, timeparms, authflavour,
				      1, flags & NFS_MOUNT_NORESVPORT);
	if (error < 0)
		goto error;
	memcpy(clp->cl_ipaddr, ip_addr, sizeof(clp->cl_ipaddr));

	error = nfs_idmap_new(clp);
	if (error < 0) {
		dprintk("%s: failed to create idmapper. Error = %d\n",
			__func__, error);
		goto error;
	}
	__set_bit(NFS_CS_IDMAP, &clp->cl_res_state);

	error = nfs4_init_client_minor_version(clp);
	if (error < 0)
		goto error;

	if (!nfs4_has_session(clp))
		nfs_mark_client_ready(clp, NFS_CS_READY);
	return 0;

error:
	nfs_mark_client_ready(clp, error);
	dprintk("<-- nfs4_init_client() = xerror %d\n", error);
	return error;
}

/*
 * Set up an NFS4 client
 */
static int nfs4_set_client(struct nfs_server *server,
		const char *hostname,
		const struct sockaddr *addr,
		const size_t addrlen,
		const char *ip_addr,
		rpc_authflavor_t authflavour,
		int proto, const struct rpc_timeout *timeparms,
		u32 minorversion)
{
	struct nfs_client_initdata cl_init = {
		.hostname = hostname,
		.addr = addr,
		.addrlen = addrlen,
		.rpc_ops = &nfs_v4_clientops,
		.proto = proto,
		.minorversion = minorversion,
	};
	struct nfs_client *clp;
	int error;

	dprintk("--> nfs4_set_client()\n");

	/* Allocate or find a client reference we can use */
	clp = nfs_get_client(&cl_init);
	if (IS_ERR(clp)) {
		error = PTR_ERR(clp);
		goto error;
	}
	error = nfs4_init_client(clp, timeparms, ip_addr, authflavour,
					server->flags);
	if (error < 0)
		goto error_put;

	server->nfs_client = clp;
	dprintk("<-- nfs4_set_client() = 0 [new %p]\n", clp);
	return 0;

error_put:
	nfs_put_client(clp);
error:
	dprintk("<-- nfs4_set_client() = xerror %d\n", error);
	return error;
}


/*
 * Session has been established, and the client marked ready.
 * Set the mount rsize and wsize with negotiated fore channel
 * attributes which will be bound checked in nfs_server_set_fsinfo.
 */
static void nfs4_session_set_rwsize(struct nfs_server *server)
{
#ifdef CONFIG_NFS_V4_1
	if (!nfs4_has_session(server->nfs_client))
		return;
	server->rsize = server->nfs_client->cl_session->fc_attrs.max_resp_sz;
	server->wsize = server->nfs_client->cl_session->fc_attrs.max_rqst_sz;
#endif /* CONFIG_NFS_V4_1 */
}

/*
 * Create a version 4 volume record
 */
static int nfs4_init_server(struct nfs_server *server,
		const struct nfs_parsed_mount_data *data)
{
	struct rpc_timeout timeparms;
	int error;

	dprintk("--> nfs4_init_server()\n");

	nfs_init_timeout_values(&timeparms, data->nfs_server.protocol,
			data->timeo, data->retrans);

	/* Initialise the client representation from the mount data */
	server->flags = data->flags;
	server->caps |= NFS_CAP_ATOMIC_OPEN;
	server->options = data->options;

	/* Get a client record */
	error = nfs4_set_client(server,
			data->nfs_server.hostname,
			(const struct sockaddr *)&data->nfs_server.address,
			data->nfs_server.addrlen,
			data->client_address,
			data->auth_flavors[0],
			data->nfs_server.protocol,
			&timeparms,
			data->minorversion);
	if (error < 0)
		goto error;

	if (data->rsize)
		server->rsize = nfs_block_size(data->rsize, NULL);
	if (data->wsize)
		server->wsize = nfs_block_size(data->wsize, NULL);

	server->acregmin = data->acregmin * HZ;
	server->acregmax = data->acregmax * HZ;
	server->acdirmin = data->acdirmin * HZ;
	server->acdirmax = data->acdirmax * HZ;

	server->port = data->nfs_server.port;

	error = nfs_init_server_rpcclient(server, &timeparms, data->auth_flavors[0]);

error:
	/* Done */
	dprintk("<-- nfs4_init_server() = %d\n", error);
	return error;
}

/*
 * Create a version 4 volume record
 * - keyed on server and FSID
 */
struct nfs_server *nfs4_create_server(const struct nfs_parsed_mount_data *data,
				      struct nfs_fh *mntfh)
{
	struct nfs_fattr fattr;
	struct nfs_server *server;
	int error;

	dprintk("--> nfs4_create_server()\n");

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	/* set up the general RPC client */
	error = nfs4_init_server(server, data);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	error = nfs4_init_session(server);
	if (error < 0)
		goto error;

	/* Probe the root fh to retrieve its FSID */
	error = nfs4_path_walk(server, mntfh, data->nfs_server.export_path);
	if (error < 0)
		goto error;

	dprintk("Server FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);
	dprintk("Mount FH: %d\n", mntfh->size);

	nfs4_session_set_rwsize(server);

	error = nfs_probe_fsinfo(server, mntfh, &fattr);
	if (error < 0)
		goto error;

	if (server->namelen == 0 || server->namelen > NFS4_MAXNAMLEN)
		server->namelen = NFS4_MAXNAMLEN;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;
	dprintk("<-- nfs4_create_server() = %p\n", server);
	return server;

error:
	nfs_free_server(server);
	dprintk("<-- nfs4_create_server() = error %d\n", error);
	return ERR_PTR(error);
}

/*
 * Create an NFS4 referral server record
 */
struct nfs_server *nfs4_create_referral_server(struct nfs_clone_mount *data,
					       struct nfs_fh *mntfh)
{
	struct nfs_client *parent_client;
	struct nfs_server *server, *parent_server;
	struct nfs_fattr fattr;
	int error;

	dprintk("--> nfs4_create_referral_server()\n");

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	parent_server = NFS_SB(data->sb);
	parent_client = parent_server->nfs_client;

	/* Initialise the client representation from the parent server */
	nfs_server_copy_userdata(server, parent_server);
	server->caps |= NFS_CAP_ATOMIC_OPEN;

	/* Get a client representation.
	 * Note: NFSv4 always uses TCP, */
	error = nfs4_set_client(server, data->hostname,
				data->addr,
				data->addrlen,
				parent_client->cl_ipaddr,
				data->authflavor,
				parent_server->client->cl_xprt->prot,
				parent_server->client->cl_timeout,
				parent_client->cl_minorversion);
	if (error < 0)
		goto error;

	error = nfs_init_server_rpcclient(server, parent_server->client->cl_timeout, data->authflavor);
	if (error < 0)
		goto error;

	BUG_ON(!server->nfs_client);
	BUG_ON(!server->nfs_client->rpc_ops);
	BUG_ON(!server->nfs_client->rpc_ops->file_inode_ops);

	/* Probe the root fh to retrieve its FSID and filehandle */
	error = nfs4_path_walk(server, mntfh, data->mnt_path);
	if (error < 0)
		goto error;

	/* probe the filesystem info for this server filesystem */
	error = nfs_probe_fsinfo(server, mntfh, &fattr);
	if (error < 0)
		goto error;

	if (server->namelen == 0 || server->namelen > NFS4_MAXNAMLEN)
		server->namelen = NFS4_MAXNAMLEN;

	dprintk("Referral FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;

	dprintk("<-- nfs_create_referral_server() = %p\n", server);
	return server;

error:
	nfs_free_server(server);
	dprintk("<-- nfs4_create_referral_server() = error %d\n", error);
	return ERR_PTR(error);
}

#endif /* CONFIG_NFS_V4 */

/*
 * Clone an NFS2, NFS3 or NFS4 server record
 */
struct nfs_server *nfs_clone_server(struct nfs_server *source,
				    struct nfs_fh *fh,
				    struct nfs_fattr *fattr)
{
	struct nfs_server *server;
	struct nfs_fattr fattr_fsinfo;
	int error;

	dprintk("--> nfs_clone_server(,%llx:%llx,)\n",
		(unsigned long long) fattr->fsid.major,
		(unsigned long long) fattr->fsid.minor);

	server = nfs_alloc_server();
	if (!server)
		return ERR_PTR(-ENOMEM);

	/* Copy data from the source */
	server->nfs_client = source->nfs_client;
	atomic_inc(&server->nfs_client->cl_count);
	nfs_server_copy_userdata(server, source);

	server->fsid = fattr->fsid;

	error = nfs_init_server_rpcclient(server,
			source->client->cl_timeout,
			source->client->cl_auth->au_flavor);
	if (error < 0)
		goto out_free_server;
	if (!IS_ERR(source->client_acl))
		nfs_init_server_aclclient(server);

	/* probe the filesystem info for this server filesystem */
	error = nfs_probe_fsinfo(server, fh, &fattr_fsinfo);
	if (error < 0)
		goto out_free_server;

	if (server->namelen == 0 || server->namelen > NFS4_MAXNAMLEN)
		server->namelen = NFS4_MAXNAMLEN;

	dprintk("Cloned FSID: %llx:%llx\n",
		(unsigned long long) server->fsid.major,
		(unsigned long long) server->fsid.minor);

	error = nfs_start_lockd(server);
	if (error < 0)
		goto out_free_server;

	spin_lock(&nfs_client_lock);
	list_add_tail(&server->client_link, &server->nfs_client->cl_superblocks);
	list_add_tail(&server->master_link, &nfs_volume_list);
	spin_unlock(&nfs_client_lock);

	server->mount_time = jiffies;

	dprintk("<-- nfs_clone_server() = %p\n", server);
	return server;

out_free_server:
	nfs_free_server(server);
	dprintk("<-- nfs_clone_server() = error %d\n", error);
	return ERR_PTR(error);
}

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_fs_nfs;

static int nfs_server_list_open(struct inode *inode, struct file *file);
static void *nfs_server_list_start(struct seq_file *p, loff_t *pos);
static void *nfs_server_list_next(struct seq_file *p, void *v, loff_t *pos);
static void nfs_server_list_stop(struct seq_file *p, void *v);
static int nfs_server_list_show(struct seq_file *m, void *v);

static struct seq_operations nfs_server_list_ops = {
	.start	= nfs_server_list_start,
	.next	= nfs_server_list_next,
	.stop	= nfs_server_list_stop,
	.show	= nfs_server_list_show,
};

static const struct file_operations nfs_server_list_fops = {
	.open		= nfs_server_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.owner		= THIS_MODULE,
};

static int nfs_volume_list_open(struct inode *inode, struct file *file);
static void *nfs_volume_list_start(struct seq_file *p, loff_t *pos);
static void *nfs_volume_list_next(struct seq_file *p, void *v, loff_t *pos);
static void nfs_volume_list_stop(struct seq_file *p, void *v);
static int nfs_volume_list_show(struct seq_file *m, void *v);

static struct seq_operations nfs_volume_list_ops = {
	.start	= nfs_volume_list_start,
	.next	= nfs_volume_list_next,
	.stop	= nfs_volume_list_stop,
	.show	= nfs_volume_list_show,
};

static const struct file_operations nfs_volume_list_fops = {
	.open		= nfs_volume_list_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
	.owner		= THIS_MODULE,
};

/*
 * open "/proc/fs/nfsfs/servers" which provides a summary of servers with which
 * we're dealing
 */
static int nfs_server_list_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &nfs_server_list_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
}

/*
 * set up the iterator to start reading from the server list and return the first item
 */
static void *nfs_server_list_start(struct seq_file *m, loff_t *_pos)
{
	/* lock the list against modification */
	spin_lock(&nfs_client_lock);
	return seq_list_start_head(&nfs_client_list, *_pos);
}

/*
 * move to next server
 */
static void *nfs_server_list_next(struct seq_file *p, void *v, loff_t *pos)
{
	return seq_list_next(v, &nfs_client_list, pos);
}

/*
 * clean up after reading from the transports list
 */
static void nfs_server_list_stop(struct seq_file *p, void *v)
{
	spin_unlock(&nfs_client_lock);
}

/*
 * display a header line followed by a load of call lines
 */
static int nfs_server_list_show(struct seq_file *m, void *v)
{
	struct nfs_client *clp;

	/* display header on line 1 */
	if (v == &nfs_client_list) {
		seq_puts(m, "NV SERVER   PORT USE HOSTNAME\n");
		return 0;
	}

	/* display one transport per line on subsequent lines */
	clp = list_entry(v, struct nfs_client, cl_share_link);

	seq_printf(m, "v%u %s %s %3d %s\n",
		   clp->rpc_ops->version,
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_ADDR),
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_PORT),
		   atomic_read(&clp->cl_count),
		   clp->cl_hostname);

	return 0;
}

/*
 * open "/proc/fs/nfsfs/volumes" which provides a summary of extant volumes
 */
static int nfs_volume_list_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &nfs_volume_list_ops);
	if (ret < 0)
		return ret;

	m = file->private_data;
	m->private = PDE(inode)->data;

	return 0;
}

/*
 * set up the iterator to start reading from the volume list and return the first item
 */
static void *nfs_volume_list_start(struct seq_file *m, loff_t *_pos)
{
	/* lock the list against modification */
	spin_lock(&nfs_client_lock);
	return seq_list_start_head(&nfs_volume_list, *_pos);
}

/*
 * move to next volume
 */
static void *nfs_volume_list_next(struct seq_file *p, void *v, loff_t *pos)
{
	return seq_list_next(v, &nfs_volume_list, pos);
}

/*
 * clean up after reading from the transports list
 */
static void nfs_volume_list_stop(struct seq_file *p, void *v)
{
	spin_unlock(&nfs_client_lock);
}

/*
 * display a header line followed by a load of call lines
 */
static int nfs_volume_list_show(struct seq_file *m, void *v)
{
	struct nfs_server *server;
	struct nfs_client *clp;
	char dev[8], fsid[17];

	/* display header on line 1 */
	if (v == &nfs_volume_list) {
		seq_puts(m, "NV SERVER   PORT DEV     FSID              FSC\n");
		return 0;
	}
	/* display one transport per line on subsequent lines */
	server = list_entry(v, struct nfs_server, master_link);
	clp = server->nfs_client;

	snprintf(dev, 8, "%u:%u",
		 MAJOR(server->s_dev), MINOR(server->s_dev));

	snprintf(fsid, 17, "%llx:%llx",
		 (unsigned long long) server->fsid.major,
		 (unsigned long long) server->fsid.minor);

	seq_printf(m, "v%u %s %s %-7s %-17s %s\n",
		   clp->rpc_ops->version,
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_ADDR),
		   rpc_peeraddr2str(clp->cl_rpcclient, RPC_DISPLAY_HEX_PORT),
		   dev,
		   fsid,
		   nfs_server_fscache_state(server));

	return 0;
}

/*
 * initialise the /proc/fs/nfsfs/ directory
 */
int __init nfs_fs_proc_init(void)
{
	struct proc_dir_entry *p;

	proc_fs_nfs = proc_mkdir("fs/nfsfs", NULL);
	if (!proc_fs_nfs)
		goto error_0;

	/* a file of servers with which we're dealing */
	p = proc_create("servers", S_IFREG|S_IRUGO,
			proc_fs_nfs, &nfs_server_list_fops);
	if (!p)
		goto error_1;

	/* a file of volumes that we have mounted */
	p = proc_create("volumes", S_IFREG|S_IRUGO,
			proc_fs_nfs, &nfs_volume_list_fops);
	if (!p)
		goto error_2;
	return 0;

error_2:
	remove_proc_entry("servers", proc_fs_nfs);
error_1:
	remove_proc_entry("fs/nfsfs", NULL);
error_0:
	return -ENOMEM;
}

/*
 * clean up the /proc/fs/nfsfs/ directory
 */
void nfs_fs_proc_exit(void)
{
	remove_proc_entry("volumes", proc_fs_nfs);
	remove_proc_entry("servers", proc_fs_nfs);
	remove_proc_entry("fs/nfsfs", NULL);
}

#endif /* CONFIG_PROC_FS */
