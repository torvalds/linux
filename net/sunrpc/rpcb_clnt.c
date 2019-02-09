/*
 * In-kernel rpcbind client supporting versions 2, 3, and 4 of the rpcbind
 * protocol
 *
 * Based on RFC 1833: "Binding Protocols for ONC RPC Version 2" and
 * RFC 3530: "Network File System (NFS) version 4 Protocol"
 *
 * Original: Gilles Quillard, Bull Open Source, 2005 <gilles.quillard@bull.net>
 * Updated: Chuck Lever, Oracle Corporation, 2007 <chuck.lever@oracle.com>
 *
 * Descended from net/sunrpc/pmap_clnt.c,
 *  Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/module.h>

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <net/ipv6.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xprtsock.h>

#include "netns.h"

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_BIND
#endif

#define RPCBIND_SOCK_PATHNAME	"/var/run/rpcbind.sock"

#define RPCBIND_PROGRAM		(100000u)
#define RPCBIND_PORT		(111u)

#define RPCBVERS_2		(2u)
#define RPCBVERS_3		(3u)
#define RPCBVERS_4		(4u)

enum {
	RPCBPROC_NULL,
	RPCBPROC_SET,
	RPCBPROC_UNSET,
	RPCBPROC_GETPORT,
	RPCBPROC_GETADDR = 3,		/* alias for GETPORT */
	RPCBPROC_DUMP,
	RPCBPROC_CALLIT,
	RPCBPROC_BCAST = 5,		/* alias for CALLIT */
	RPCBPROC_GETTIME,
	RPCBPROC_UADDR2TADDR,
	RPCBPROC_TADDR2UADDR,
	RPCBPROC_GETVERSADDR,
	RPCBPROC_INDIRECT,
	RPCBPROC_GETADDRLIST,
	RPCBPROC_GETSTAT,
};

/*
 * r_owner
 *
 * The "owner" is allowed to unset a service in the rpcbind database.
 *
 * For AF_LOCAL SET/UNSET requests, rpcbind treats this string as a
 * UID which it maps to a local user name via a password lookup.
 * In all other cases it is ignored.
 *
 * For SET/UNSET requests, user space provides a value, even for
 * network requests, and GETADDR uses an empty string.  We follow
 * those precedents here.
 */
#define RPCB_OWNER_STRING	"0"
#define RPCB_MAXOWNERLEN	sizeof(RPCB_OWNER_STRING)

/*
 * XDR data type sizes
 */
#define RPCB_program_sz		(1)
#define RPCB_version_sz		(1)
#define RPCB_protocol_sz	(1)
#define RPCB_port_sz		(1)
#define RPCB_boolean_sz		(1)

#define RPCB_netid_sz		(1 + XDR_QUADLEN(RPCBIND_MAXNETIDLEN))
#define RPCB_addr_sz		(1 + XDR_QUADLEN(RPCBIND_MAXUADDRLEN))
#define RPCB_ownerstring_sz	(1 + XDR_QUADLEN(RPCB_MAXOWNERLEN))

/*
 * XDR argument and result sizes
 */
#define RPCB_mappingargs_sz	(RPCB_program_sz + RPCB_version_sz + \
				RPCB_protocol_sz + RPCB_port_sz)
#define RPCB_getaddrargs_sz	(RPCB_program_sz + RPCB_version_sz + \
				RPCB_netid_sz + RPCB_addr_sz + \
				RPCB_ownerstring_sz)

#define RPCB_getportres_sz	RPCB_port_sz
#define RPCB_setres_sz		RPCB_boolean_sz

/*
 * Note that RFC 1833 does not put any size restrictions on the
 * address string returned by the remote rpcbind database.
 */
#define RPCB_getaddrres_sz	RPCB_addr_sz

static void			rpcb_getport_done(struct rpc_task *, void *);
static void			rpcb_map_release(void *data);
static const struct rpc_program	rpcb_program;

struct rpcbind_args {
	struct rpc_xprt *	r_xprt;

	u32			r_prog;
	u32			r_vers;
	u32			r_prot;
	unsigned short		r_port;
	const char *		r_netid;
	const char *		r_addr;
	const char *		r_owner;

	int			r_status;
};

static const struct rpc_procinfo rpcb_procedures2[];
static const struct rpc_procinfo rpcb_procedures3[];
static const struct rpc_procinfo rpcb_procedures4[];

struct rpcb_info {
	u32			rpc_vers;
	const struct rpc_procinfo *rpc_proc;
};

static const struct rpcb_info rpcb_next_version[];
static const struct rpcb_info rpcb_next_version6[];

static const struct rpc_call_ops rpcb_getport_ops = {
	.rpc_call_done		= rpcb_getport_done,
	.rpc_release		= rpcb_map_release,
};

static void rpcb_wake_rpcbind_waiters(struct rpc_xprt *xprt, int status)
{
	xprt_clear_binding(xprt);
	rpc_wake_up_status(&xprt->binding, status);
}

static void rpcb_map_release(void *data)
{
	struct rpcbind_args *map = data;

	rpcb_wake_rpcbind_waiters(map->r_xprt, map->r_status);
	xprt_put(map->r_xprt);
	kfree(map->r_addr);
	kfree(map);
}

static int rpcb_get_local(struct net *net)
{
	int cnt;
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	spin_lock(&sn->rpcb_clnt_lock);
	if (sn->rpcb_users)
		sn->rpcb_users++;
	cnt = sn->rpcb_users;
	spin_unlock(&sn->rpcb_clnt_lock);

	return cnt;
}

void rpcb_put_local(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct rpc_clnt *clnt = sn->rpcb_local_clnt;
	struct rpc_clnt *clnt4 = sn->rpcb_local_clnt4;
	int shutdown = 0;

	spin_lock(&sn->rpcb_clnt_lock);
	if (sn->rpcb_users) {
		if (--sn->rpcb_users == 0) {
			sn->rpcb_local_clnt = NULL;
			sn->rpcb_local_clnt4 = NULL;
		}
		shutdown = !sn->rpcb_users;
	}
	spin_unlock(&sn->rpcb_clnt_lock);

	if (shutdown) {
		/*
		 * cleanup_rpcb_clnt - remove xprtsock's sysctls, unregister
		 */
		if (clnt4)
			rpc_shutdown_client(clnt4);
		if (clnt)
			rpc_shutdown_client(clnt);
	}
}

static void rpcb_set_local(struct net *net, struct rpc_clnt *clnt,
			struct rpc_clnt *clnt4,
			bool is_af_local)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	/* Protected by rpcb_create_local_mutex */
	sn->rpcb_local_clnt = clnt;
	sn->rpcb_local_clnt4 = clnt4;
	sn->rpcb_is_af_local = is_af_local ? 1 : 0;
	smp_wmb();
	sn->rpcb_users = 1;
	dprintk("RPC:       created new rpcb local clients (rpcb_local_clnt: "
		"%p, rpcb_local_clnt4: %p) for net %x%s\n",
		sn->rpcb_local_clnt, sn->rpcb_local_clnt4,
		net->ns.inum, (net == &init_net) ? " (init_net)" : "");
}

/*
 * Returns zero on success, otherwise a negative errno value
 * is returned.
 */
static int rpcb_create_local_unix(struct net *net)
{
	static const struct sockaddr_un rpcb_localaddr_rpcbind = {
		.sun_family		= AF_LOCAL,
		.sun_path		= RPCBIND_SOCK_PATHNAME,
	};
	struct rpc_create_args args = {
		.net		= net,
		.protocol	= XPRT_TRANSPORT_LOCAL,
		.address	= (struct sockaddr *)&rpcb_localaddr_rpcbind,
		.addrsize	= sizeof(rpcb_localaddr_rpcbind),
		.servername	= "localhost",
		.program	= &rpcb_program,
		.version	= RPCBVERS_2,
		.authflavor	= RPC_AUTH_NULL,
		/*
		 * We turn off the idle timeout to prevent the kernel
		 * from automatically disconnecting the socket.
		 * Otherwise, we'd have to cache the mount namespace
		 * of the caller and somehow pass that to the socket
		 * reconnect code.
		 */
		.flags		= RPC_CLNT_CREATE_NO_IDLE_TIMEOUT,
	};
	struct rpc_clnt *clnt, *clnt4;
	int result = 0;

	/*
	 * Because we requested an RPC PING at transport creation time,
	 * this works only if the user space portmapper is rpcbind, and
	 * it's listening on AF_LOCAL on the named socket.
	 */
	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		dprintk("RPC:       failed to create AF_LOCAL rpcbind "
				"client (errno %ld).\n", PTR_ERR(clnt));
		result = PTR_ERR(clnt);
		goto out;
	}

	clnt4 = rpc_bind_new_program(clnt, &rpcb_program, RPCBVERS_4);
	if (IS_ERR(clnt4)) {
		dprintk("RPC:       failed to bind second program to "
				"rpcbind v4 client (errno %ld).\n",
				PTR_ERR(clnt4));
		clnt4 = NULL;
	}

	rpcb_set_local(net, clnt, clnt4, true);

out:
	return result;
}

/*
 * Returns zero on success, otherwise a negative errno value
 * is returned.
 */
static int rpcb_create_local_net(struct net *net)
{
	static const struct sockaddr_in rpcb_inaddr_loopback = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= htonl(INADDR_LOOPBACK),
		.sin_port		= htons(RPCBIND_PORT),
	};
	struct rpc_create_args args = {
		.net		= net,
		.protocol	= XPRT_TRANSPORT_TCP,
		.address	= (struct sockaddr *)&rpcb_inaddr_loopback,
		.addrsize	= sizeof(rpcb_inaddr_loopback),
		.servername	= "localhost",
		.program	= &rpcb_program,
		.version	= RPCBVERS_2,
		.authflavor	= RPC_AUTH_UNIX,
		.flags		= RPC_CLNT_CREATE_NOPING,
	};
	struct rpc_clnt *clnt, *clnt4;
	int result = 0;

	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		dprintk("RPC:       failed to create local rpcbind "
				"client (errno %ld).\n", PTR_ERR(clnt));
		result = PTR_ERR(clnt);
		goto out;
	}

	/*
	 * This results in an RPC ping.  On systems running portmapper,
	 * the v4 ping will fail.  Proceed anyway, but disallow rpcb
	 * v4 upcalls.
	 */
	clnt4 = rpc_bind_new_program(clnt, &rpcb_program, RPCBVERS_4);
	if (IS_ERR(clnt4)) {
		dprintk("RPC:       failed to bind second program to "
				"rpcbind v4 client (errno %ld).\n",
				PTR_ERR(clnt4));
		clnt4 = NULL;
	}

	rpcb_set_local(net, clnt, clnt4, false);

out:
	return result;
}

/*
 * Returns zero on success, otherwise a negative errno value
 * is returned.
 */
int rpcb_create_local(struct net *net)
{
	static DEFINE_MUTEX(rpcb_create_local_mutex);
	int result = 0;

	if (rpcb_get_local(net))
		return result;

	mutex_lock(&rpcb_create_local_mutex);
	if (rpcb_get_local(net))
		goto out;

	if (rpcb_create_local_unix(net) != 0)
		result = rpcb_create_local_net(net);

out:
	mutex_unlock(&rpcb_create_local_mutex);
	return result;
}

static struct rpc_clnt *rpcb_create(struct net *net, const char *nodename,
				    const char *hostname,
				    struct sockaddr *srvaddr, size_t salen,
				    int proto, u32 version)
{
	struct rpc_create_args args = {
		.net		= net,
		.protocol	= proto,
		.address	= srvaddr,
		.addrsize	= salen,
		.servername	= hostname,
		.nodename	= nodename,
		.program	= &rpcb_program,
		.version	= version,
		.authflavor	= RPC_AUTH_UNIX,
		.flags		= (RPC_CLNT_CREATE_NOPING |
					RPC_CLNT_CREATE_NONPRIVPORT),
	};

	switch (srvaddr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)srvaddr)->sin_port = htons(RPCBIND_PORT);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)srvaddr)->sin6_port = htons(RPCBIND_PORT);
		break;
	default:
		return ERR_PTR(-EAFNOSUPPORT);
	}

	return rpc_create(&args);
}

static int rpcb_register_call(struct sunrpc_net *sn, struct rpc_clnt *clnt, struct rpc_message *msg, bool is_set)
{
	int flags = RPC_TASK_NOCONNECT;
	int error, result = 0;

	if (is_set || !sn->rpcb_is_af_local)
		flags = RPC_TASK_SOFTCONN;
	msg->rpc_resp = &result;

	error = rpc_call_sync(clnt, msg, flags);
	if (error < 0) {
		dprintk("RPC:       failed to contact local rpcbind "
				"server (errno %d).\n", -error);
		return error;
	}

	if (!result)
		return -EACCES;
	return 0;
}

/**
 * rpcb_register - set or unset a port registration with the local rpcbind svc
 * @net: target network namespace
 * @prog: RPC program number to bind
 * @vers: RPC version number to bind
 * @prot: transport protocol to register
 * @port: port value to register
 *
 * Returns zero if the registration request was dispatched successfully
 * and the rpcbind daemon returned success.  Otherwise, returns an errno
 * value that reflects the nature of the error (request could not be
 * dispatched, timed out, or rpcbind returned an error).
 *
 * RPC services invoke this function to advertise their contact
 * information via the system's rpcbind daemon.  RPC services
 * invoke this function once for each [program, version, transport]
 * tuple they wish to advertise.
 *
 * Callers may also unregister RPC services that are no longer
 * available by setting the passed-in port to zero.  This removes
 * all registered transports for [program, version] from the local
 * rpcbind database.
 *
 * This function uses rpcbind protocol version 2 to contact the
 * local rpcbind daemon.
 *
 * Registration works over both AF_INET and AF_INET6, and services
 * registered via this function are advertised as available for any
 * address.  If the local rpcbind daemon is listening on AF_INET6,
 * services registered via this function will be advertised on
 * IN6ADDR_ANY (ie available for all AF_INET and AF_INET6
 * addresses).
 */
int rpcb_register(struct net *net, u32 prog, u32 vers, int prot, unsigned short port)
{
	struct rpcbind_args map = {
		.r_prog		= prog,
		.r_vers		= vers,
		.r_prot		= prot,
		.r_port		= port,
	};
	struct rpc_message msg = {
		.rpc_argp	= &map,
	};
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	bool is_set = false;

	dprintk("RPC:       %sregistering (%u, %u, %d, %u) with local "
			"rpcbind\n", (port ? "" : "un"),
			prog, vers, prot, port);

	msg.rpc_proc = &rpcb_procedures2[RPCBPROC_UNSET];
	if (port != 0) {
		msg.rpc_proc = &rpcb_procedures2[RPCBPROC_SET];
		is_set = true;
	}

	return rpcb_register_call(sn, sn->rpcb_local_clnt, &msg, is_set);
}

/*
 * Fill in AF_INET family-specific arguments to register
 */
static int rpcb_register_inet4(struct sunrpc_net *sn,
			       const struct sockaddr *sap,
			       struct rpc_message *msg)
{
	const struct sockaddr_in *sin = (const struct sockaddr_in *)sap;
	struct rpcbind_args *map = msg->rpc_argp;
	unsigned short port = ntohs(sin->sin_port);
	bool is_set = false;
	int result;

	map->r_addr = rpc_sockaddr2uaddr(sap, GFP_KERNEL);

	dprintk("RPC:       %sregistering [%u, %u, %s, '%s'] with "
		"local rpcbind\n", (port ? "" : "un"),
			map->r_prog, map->r_vers,
			map->r_addr, map->r_netid);

	msg->rpc_proc = &rpcb_procedures4[RPCBPROC_UNSET];
	if (port != 0) {
		msg->rpc_proc = &rpcb_procedures4[RPCBPROC_SET];
		is_set = true;
	}

	result = rpcb_register_call(sn, sn->rpcb_local_clnt4, msg, is_set);
	kfree(map->r_addr);
	return result;
}

/*
 * Fill in AF_INET6 family-specific arguments to register
 */
static int rpcb_register_inet6(struct sunrpc_net *sn,
			       const struct sockaddr *sap,
			       struct rpc_message *msg)
{
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sap;
	struct rpcbind_args *map = msg->rpc_argp;
	unsigned short port = ntohs(sin6->sin6_port);
	bool is_set = false;
	int result;

	map->r_addr = rpc_sockaddr2uaddr(sap, GFP_KERNEL);

	dprintk("RPC:       %sregistering [%u, %u, %s, '%s'] with "
		"local rpcbind\n", (port ? "" : "un"),
			map->r_prog, map->r_vers,
			map->r_addr, map->r_netid);

	msg->rpc_proc = &rpcb_procedures4[RPCBPROC_UNSET];
	if (port != 0) {
		msg->rpc_proc = &rpcb_procedures4[RPCBPROC_SET];
		is_set = true;
	}

	result = rpcb_register_call(sn, sn->rpcb_local_clnt4, msg, is_set);
	kfree(map->r_addr);
	return result;
}

static int rpcb_unregister_all_protofamilies(struct sunrpc_net *sn,
					     struct rpc_message *msg)
{
	struct rpcbind_args *map = msg->rpc_argp;

	dprintk("RPC:       unregistering [%u, %u, '%s'] with "
		"local rpcbind\n",
			map->r_prog, map->r_vers, map->r_netid);

	map->r_addr = "";
	msg->rpc_proc = &rpcb_procedures4[RPCBPROC_UNSET];

	return rpcb_register_call(sn, sn->rpcb_local_clnt4, msg, false);
}

/**
 * rpcb_v4_register - set or unset a port registration with the local rpcbind
 * @net: target network namespace
 * @program: RPC program number of service to (un)register
 * @version: RPC version number of service to (un)register
 * @address: address family, IP address, and port to (un)register
 * @netid: netid of transport protocol to (un)register
 *
 * Returns zero if the registration request was dispatched successfully
 * and the rpcbind daemon returned success.  Otherwise, returns an errno
 * value that reflects the nature of the error (request could not be
 * dispatched, timed out, or rpcbind returned an error).
 *
 * RPC services invoke this function to advertise their contact
 * information via the system's rpcbind daemon.  RPC services
 * invoke this function once for each [program, version, address,
 * netid] tuple they wish to advertise.
 *
 * Callers may also unregister RPC services that are registered at a
 * specific address by setting the port number in @address to zero.
 * They may unregister all registered protocol families at once for
 * a service by passing a NULL @address argument.  If @netid is ""
 * then all netids for [program, version, address] are unregistered.
 *
 * This function uses rpcbind protocol version 4 to contact the
 * local rpcbind daemon.  The local rpcbind daemon must support
 * version 4 of the rpcbind protocol in order for these functions
 * to register a service successfully.
 *
 * Supported netids include "udp" and "tcp" for UDP and TCP over
 * IPv4, and "udp6" and "tcp6" for UDP and TCP over IPv6,
 * respectively.
 *
 * The contents of @address determine the address family and the
 * port to be registered.  The usual practice is to pass INADDR_ANY
 * as the raw address, but specifying a non-zero address is also
 * supported by this API if the caller wishes to advertise an RPC
 * service on a specific network interface.
 *
 * Note that passing in INADDR_ANY does not create the same service
 * registration as IN6ADDR_ANY.  The former advertises an RPC
 * service on any IPv4 address, but not on IPv6.  The latter
 * advertises the service on all IPv4 and IPv6 addresses.
 */
int rpcb_v4_register(struct net *net, const u32 program, const u32 version,
		     const struct sockaddr *address, const char *netid)
{
	struct rpcbind_args map = {
		.r_prog		= program,
		.r_vers		= version,
		.r_netid	= netid,
		.r_owner	= RPCB_OWNER_STRING,
	};
	struct rpc_message msg = {
		.rpc_argp	= &map,
	};
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);

	if (sn->rpcb_local_clnt4 == NULL)
		return -EPROTONOSUPPORT;

	if (address == NULL)
		return rpcb_unregister_all_protofamilies(sn, &msg);

	switch (address->sa_family) {
	case AF_INET:
		return rpcb_register_inet4(sn, address, &msg);
	case AF_INET6:
		return rpcb_register_inet6(sn, address, &msg);
	}

	return -EAFNOSUPPORT;
}

static struct rpc_task *rpcb_call_async(struct rpc_clnt *rpcb_clnt,
		struct rpcbind_args *map, const struct rpc_procinfo *proc)
{
	struct rpc_message msg = {
		.rpc_proc = proc,
		.rpc_argp = map,
		.rpc_resp = map,
	};
	struct rpc_task_setup task_setup_data = {
		.rpc_client = rpcb_clnt,
		.rpc_message = &msg,
		.callback_ops = &rpcb_getport_ops,
		.callback_data = map,
		.flags = RPC_TASK_ASYNC | RPC_TASK_SOFTCONN,
	};

	return rpc_run_task(&task_setup_data);
}

/*
 * In the case where rpc clients have been cloned, we want to make
 * sure that we use the program number/version etc of the actual
 * owner of the xprt. To do so, we walk back up the tree of parents
 * to find whoever created the transport and/or whoever has the
 * autobind flag set.
 */
static struct rpc_clnt *rpcb_find_transport_owner(struct rpc_clnt *clnt)
{
	struct rpc_clnt *parent = clnt->cl_parent;
	struct rpc_xprt_switch *xps = rcu_access_pointer(clnt->cl_xpi.xpi_xpswitch);

	while (parent != clnt) {
		if (rcu_access_pointer(parent->cl_xpi.xpi_xpswitch) != xps)
			break;
		if (clnt->cl_autobind)
			break;
		clnt = parent;
		parent = parent->cl_parent;
	}
	return clnt;
}

/**
 * rpcb_getport_async - obtain the port for a given RPC service on a given host
 * @task: task that is waiting for portmapper request
 *
 * This one can be called for an ongoing RPC request, and can be used in
 * an async (rpciod) context.
 */
void rpcb_getport_async(struct rpc_task *task)
{
	struct rpc_clnt *clnt;
	const struct rpc_procinfo *proc;
	u32 bind_version;
	struct rpc_xprt *xprt;
	struct rpc_clnt	*rpcb_clnt;
	struct rpcbind_args *map;
	struct rpc_task	*child;
	struct sockaddr_storage addr;
	struct sockaddr *sap = (struct sockaddr *)&addr;
	size_t salen;
	int status;

	rcu_read_lock();
	clnt = rpcb_find_transport_owner(task->tk_client);
	rcu_read_unlock();
	xprt = xprt_get(task->tk_xprt);

	dprintk("RPC: %5u %s(%s, %u, %u, %d)\n",
		task->tk_pid, __func__,
		xprt->servername, clnt->cl_prog, clnt->cl_vers, xprt->prot);

	/* Put self on the wait queue to ensure we get notified if
	 * some other task is already attempting to bind the port */
	rpc_sleep_on(&xprt->binding, task, NULL);

	if (xprt_test_and_set_binding(xprt)) {
		dprintk("RPC: %5u %s: waiting for another binder\n",
			task->tk_pid, __func__);
		xprt_put(xprt);
		return;
	}

	/* Someone else may have bound if we slept */
	if (xprt_bound(xprt)) {
		status = 0;
		dprintk("RPC: %5u %s: already bound\n",
			task->tk_pid, __func__);
		goto bailout_nofree;
	}

	/* Parent transport's destination address */
	salen = rpc_peeraddr(clnt, sap, sizeof(addr));

	/* Don't ever use rpcbind v2 for AF_INET6 requests */
	switch (sap->sa_family) {
	case AF_INET:
		proc = rpcb_next_version[xprt->bind_index].rpc_proc;
		bind_version = rpcb_next_version[xprt->bind_index].rpc_vers;
		break;
	case AF_INET6:
		proc = rpcb_next_version6[xprt->bind_index].rpc_proc;
		bind_version = rpcb_next_version6[xprt->bind_index].rpc_vers;
		break;
	default:
		status = -EAFNOSUPPORT;
		dprintk("RPC: %5u %s: bad address family\n",
				task->tk_pid, __func__);
		goto bailout_nofree;
	}
	if (proc == NULL) {
		xprt->bind_index = 0;
		status = -EPFNOSUPPORT;
		dprintk("RPC: %5u %s: no more getport versions available\n",
			task->tk_pid, __func__);
		goto bailout_nofree;
	}

	dprintk("RPC: %5u %s: trying rpcbind version %u\n",
		task->tk_pid, __func__, bind_version);

	rpcb_clnt = rpcb_create(xprt->xprt_net,
				clnt->cl_nodename,
				xprt->servername, sap, salen,
				xprt->prot, bind_version);
	if (IS_ERR(rpcb_clnt)) {
		status = PTR_ERR(rpcb_clnt);
		dprintk("RPC: %5u %s: rpcb_create failed, error %ld\n",
			task->tk_pid, __func__, PTR_ERR(rpcb_clnt));
		goto bailout_nofree;
	}

	map = kzalloc(sizeof(struct rpcbind_args), GFP_ATOMIC);
	if (!map) {
		status = -ENOMEM;
		dprintk("RPC: %5u %s: no memory available\n",
			task->tk_pid, __func__);
		goto bailout_release_client;
	}
	map->r_prog = clnt->cl_prog;
	map->r_vers = clnt->cl_vers;
	map->r_prot = xprt->prot;
	map->r_port = 0;
	map->r_xprt = xprt;
	map->r_status = -EIO;

	switch (bind_version) {
	case RPCBVERS_4:
	case RPCBVERS_3:
		map->r_netid = xprt->address_strings[RPC_DISPLAY_NETID];
		map->r_addr = rpc_sockaddr2uaddr(sap, GFP_ATOMIC);
		if (!map->r_addr) {
			status = -ENOMEM;
			dprintk("RPC: %5u %s: no memory available\n",
				task->tk_pid, __func__);
			goto bailout_free_args;
		}
		map->r_owner = "";
		break;
	case RPCBVERS_2:
		map->r_addr = NULL;
		break;
	default:
		BUG();
	}

	child = rpcb_call_async(rpcb_clnt, map, proc);
	rpc_release_client(rpcb_clnt);
	if (IS_ERR(child)) {
		/* rpcb_map_release() has freed the arguments */
		dprintk("RPC: %5u %s: rpc_run_task failed\n",
			task->tk_pid, __func__);
		return;
	}

	xprt->stat.bind_count++;
	rpc_put_task(child);
	return;

bailout_free_args:
	kfree(map);
bailout_release_client:
	rpc_release_client(rpcb_clnt);
bailout_nofree:
	rpcb_wake_rpcbind_waiters(xprt, status);
	task->tk_status = status;
	xprt_put(xprt);
}
EXPORT_SYMBOL_GPL(rpcb_getport_async);

/*
 * Rpcbind child task calls this callback via tk_exit.
 */
static void rpcb_getport_done(struct rpc_task *child, void *data)
{
	struct rpcbind_args *map = data;
	struct rpc_xprt *xprt = map->r_xprt;
	int status = child->tk_status;

	/* Garbage reply: retry with a lesser rpcbind version */
	if (status == -EIO)
		status = -EPROTONOSUPPORT;

	/* rpcbind server doesn't support this rpcbind protocol version */
	if (status == -EPROTONOSUPPORT)
		xprt->bind_index++;

	if (status < 0) {
		/* rpcbind server not available on remote host? */
		xprt->ops->set_port(xprt, 0);
	} else if (map->r_port == 0) {
		/* Requested RPC service wasn't registered on remote host */
		xprt->ops->set_port(xprt, 0);
		status = -EACCES;
	} else {
		/* Succeeded */
		xprt->ops->set_port(xprt, map->r_port);
		xprt_set_bound(xprt);
		status = 0;
	}

	dprintk("RPC: %5u rpcb_getport_done(status %d, port %u)\n",
			child->tk_pid, status, map->r_port);

	map->r_status = status;
}

/*
 * XDR functions for rpcbind
 */

static void rpcb_enc_mapping(struct rpc_rqst *req, struct xdr_stream *xdr,
			     const void *data)
{
	const struct rpcbind_args *rpcb = data;
	__be32 *p;

	dprintk("RPC: %5u encoding PMAP_%s call (%u, %u, %d, %u)\n",
			req->rq_task->tk_pid,
			req->rq_task->tk_msg.rpc_proc->p_name,
			rpcb->r_prog, rpcb->r_vers, rpcb->r_prot, rpcb->r_port);

	p = xdr_reserve_space(xdr, RPCB_mappingargs_sz << 2);
	*p++ = cpu_to_be32(rpcb->r_prog);
	*p++ = cpu_to_be32(rpcb->r_vers);
	*p++ = cpu_to_be32(rpcb->r_prot);
	*p   = cpu_to_be32(rpcb->r_port);
}

static int rpcb_dec_getport(struct rpc_rqst *req, struct xdr_stream *xdr,
			    void *data)
{
	struct rpcbind_args *rpcb = data;
	unsigned long port;
	__be32 *p;

	rpcb->r_port = 0;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -EIO;

	port = be32_to_cpup(p);
	dprintk("RPC: %5u PMAP_%s result: %lu\n", req->rq_task->tk_pid,
			req->rq_task->tk_msg.rpc_proc->p_name, port);
	if (unlikely(port > USHRT_MAX))
		return -EIO;

	rpcb->r_port = port;
	return 0;
}

static int rpcb_dec_set(struct rpc_rqst *req, struct xdr_stream *xdr,
			void *data)
{
	unsigned int *boolp = data;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -EIO;

	*boolp = 0;
	if (*p != xdr_zero)
		*boolp = 1;

	dprintk("RPC: %5u RPCB_%s call %s\n",
			req->rq_task->tk_pid,
			req->rq_task->tk_msg.rpc_proc->p_name,
			(*boolp ? "succeeded" : "failed"));
	return 0;
}

static void encode_rpcb_string(struct xdr_stream *xdr, const char *string,
			       const u32 maxstrlen)
{
	__be32 *p;
	u32 len;

	len = strlen(string);
	WARN_ON_ONCE(len > maxstrlen);
	if (len > maxstrlen)
		/* truncate and hope for the best */
		len = maxstrlen;
	p = xdr_reserve_space(xdr, 4 + len);
	xdr_encode_opaque(p, string, len);
}

static void rpcb_enc_getaddr(struct rpc_rqst *req, struct xdr_stream *xdr,
			     const void *data)
{
	const struct rpcbind_args *rpcb = data;
	__be32 *p;

	dprintk("RPC: %5u encoding RPCB_%s call (%u, %u, '%s', '%s')\n",
			req->rq_task->tk_pid,
			req->rq_task->tk_msg.rpc_proc->p_name,
			rpcb->r_prog, rpcb->r_vers,
			rpcb->r_netid, rpcb->r_addr);

	p = xdr_reserve_space(xdr, (RPCB_program_sz + RPCB_version_sz) << 2);
	*p++ = cpu_to_be32(rpcb->r_prog);
	*p = cpu_to_be32(rpcb->r_vers);

	encode_rpcb_string(xdr, rpcb->r_netid, RPCBIND_MAXNETIDLEN);
	encode_rpcb_string(xdr, rpcb->r_addr, RPCBIND_MAXUADDRLEN);
	encode_rpcb_string(xdr, rpcb->r_owner, RPCB_MAXOWNERLEN);
}

static int rpcb_dec_getaddr(struct rpc_rqst *req, struct xdr_stream *xdr,
			    void *data)
{
	struct rpcbind_args *rpcb = data;
	struct sockaddr_storage address;
	struct sockaddr *sap = (struct sockaddr *)&address;
	__be32 *p;
	u32 len;

	rpcb->r_port = 0;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		goto out_fail;
	len = be32_to_cpup(p);

	/*
	 * If the returned universal address is a null string,
	 * the requested RPC service was not registered.
	 */
	if (len == 0) {
		dprintk("RPC: %5u RPCB reply: program not registered\n",
				req->rq_task->tk_pid);
		return 0;
	}

	if (unlikely(len > RPCBIND_MAXUADDRLEN))
		goto out_fail;

	p = xdr_inline_decode(xdr, len);
	if (unlikely(p == NULL))
		goto out_fail;
	dprintk("RPC: %5u RPCB_%s reply: %s\n", req->rq_task->tk_pid,
			req->rq_task->tk_msg.rpc_proc->p_name, (char *)p);

	if (rpc_uaddr2sockaddr(req->rq_xprt->xprt_net, (char *)p, len,
				sap, sizeof(address)) == 0)
		goto out_fail;
	rpcb->r_port = rpc_get_port(sap);

	return 0;

out_fail:
	dprintk("RPC: %5u malformed RPCB_%s reply\n",
			req->rq_task->tk_pid,
			req->rq_task->tk_msg.rpc_proc->p_name);
	return -EIO;
}

/*
 * Not all rpcbind procedures described in RFC 1833 are implemented
 * since the Linux kernel RPC code requires only these.
 */

static const struct rpc_procinfo rpcb_procedures2[] = {
	[RPCBPROC_SET] = {
		.p_proc		= RPCBPROC_SET,
		.p_encode	= rpcb_enc_mapping,
		.p_decode	= rpcb_dec_set,
		.p_arglen	= RPCB_mappingargs_sz,
		.p_replen	= RPCB_setres_sz,
		.p_statidx	= RPCBPROC_SET,
		.p_timer	= 0,
		.p_name		= "SET",
	},
	[RPCBPROC_UNSET] = {
		.p_proc		= RPCBPROC_UNSET,
		.p_encode	= rpcb_enc_mapping,
		.p_decode	= rpcb_dec_set,
		.p_arglen	= RPCB_mappingargs_sz,
		.p_replen	= RPCB_setres_sz,
		.p_statidx	= RPCBPROC_UNSET,
		.p_timer	= 0,
		.p_name		= "UNSET",
	},
	[RPCBPROC_GETPORT] = {
		.p_proc		= RPCBPROC_GETPORT,
		.p_encode	= rpcb_enc_mapping,
		.p_decode	= rpcb_dec_getport,
		.p_arglen	= RPCB_mappingargs_sz,
		.p_replen	= RPCB_getportres_sz,
		.p_statidx	= RPCBPROC_GETPORT,
		.p_timer	= 0,
		.p_name		= "GETPORT",
	},
};

static const struct rpc_procinfo rpcb_procedures3[] = {
	[RPCBPROC_SET] = {
		.p_proc		= RPCBPROC_SET,
		.p_encode	= rpcb_enc_getaddr,
		.p_decode	= rpcb_dec_set,
		.p_arglen	= RPCB_getaddrargs_sz,
		.p_replen	= RPCB_setres_sz,
		.p_statidx	= RPCBPROC_SET,
		.p_timer	= 0,
		.p_name		= "SET",
	},
	[RPCBPROC_UNSET] = {
		.p_proc		= RPCBPROC_UNSET,
		.p_encode	= rpcb_enc_getaddr,
		.p_decode	= rpcb_dec_set,
		.p_arglen	= RPCB_getaddrargs_sz,
		.p_replen	= RPCB_setres_sz,
		.p_statidx	= RPCBPROC_UNSET,
		.p_timer	= 0,
		.p_name		= "UNSET",
	},
	[RPCBPROC_GETADDR] = {
		.p_proc		= RPCBPROC_GETADDR,
		.p_encode	= rpcb_enc_getaddr,
		.p_decode	= rpcb_dec_getaddr,
		.p_arglen	= RPCB_getaddrargs_sz,
		.p_replen	= RPCB_getaddrres_sz,
		.p_statidx	= RPCBPROC_GETADDR,
		.p_timer	= 0,
		.p_name		= "GETADDR",
	},
};

static const struct rpc_procinfo rpcb_procedures4[] = {
	[RPCBPROC_SET] = {
		.p_proc		= RPCBPROC_SET,
		.p_encode	= rpcb_enc_getaddr,
		.p_decode	= rpcb_dec_set,
		.p_arglen	= RPCB_getaddrargs_sz,
		.p_replen	= RPCB_setres_sz,
		.p_statidx	= RPCBPROC_SET,
		.p_timer	= 0,
		.p_name		= "SET",
	},
	[RPCBPROC_UNSET] = {
		.p_proc		= RPCBPROC_UNSET,
		.p_encode	= rpcb_enc_getaddr,
		.p_decode	= rpcb_dec_set,
		.p_arglen	= RPCB_getaddrargs_sz,
		.p_replen	= RPCB_setres_sz,
		.p_statidx	= RPCBPROC_UNSET,
		.p_timer	= 0,
		.p_name		= "UNSET",
	},
	[RPCBPROC_GETADDR] = {
		.p_proc		= RPCBPROC_GETADDR,
		.p_encode	= rpcb_enc_getaddr,
		.p_decode	= rpcb_dec_getaddr,
		.p_arglen	= RPCB_getaddrargs_sz,
		.p_replen	= RPCB_getaddrres_sz,
		.p_statidx	= RPCBPROC_GETADDR,
		.p_timer	= 0,
		.p_name		= "GETADDR",
	},
};

static const struct rpcb_info rpcb_next_version[] = {
	{
		.rpc_vers	= RPCBVERS_2,
		.rpc_proc	= &rpcb_procedures2[RPCBPROC_GETPORT],
	},
	{
		.rpc_proc	= NULL,
	},
};

static const struct rpcb_info rpcb_next_version6[] = {
	{
		.rpc_vers	= RPCBVERS_4,
		.rpc_proc	= &rpcb_procedures4[RPCBPROC_GETADDR],
	},
	{
		.rpc_vers	= RPCBVERS_3,
		.rpc_proc	= &rpcb_procedures3[RPCBPROC_GETADDR],
	},
	{
		.rpc_proc	= NULL,
	},
};

static unsigned int rpcb_version2_counts[ARRAY_SIZE(rpcb_procedures2)];
static const struct rpc_version rpcb_version2 = {
	.number		= RPCBVERS_2,
	.nrprocs	= ARRAY_SIZE(rpcb_procedures2),
	.procs		= rpcb_procedures2,
	.counts		= rpcb_version2_counts,
};

static unsigned int rpcb_version3_counts[ARRAY_SIZE(rpcb_procedures3)];
static const struct rpc_version rpcb_version3 = {
	.number		= RPCBVERS_3,
	.nrprocs	= ARRAY_SIZE(rpcb_procedures3),
	.procs		= rpcb_procedures3,
	.counts		= rpcb_version3_counts,
};

static unsigned int rpcb_version4_counts[ARRAY_SIZE(rpcb_procedures4)];
static const struct rpc_version rpcb_version4 = {
	.number		= RPCBVERS_4,
	.nrprocs	= ARRAY_SIZE(rpcb_procedures4),
	.procs		= rpcb_procedures4,
	.counts		= rpcb_version4_counts,
};

static const struct rpc_version *rpcb_version[] = {
	NULL,
	NULL,
	&rpcb_version2,
	&rpcb_version3,
	&rpcb_version4
};

static struct rpc_stat rpcb_stats;

static const struct rpc_program rpcb_program = {
	.name		= "rpcbind",
	.number		= RPCBIND_PROGRAM,
	.nrvers		= ARRAY_SIZE(rpcb_version),
	.version	= rpcb_version,
	.stats		= &rpcb_stats,
};
