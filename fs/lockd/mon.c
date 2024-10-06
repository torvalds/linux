// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/lockd/mon.c
 *
 * The kernel statd client.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/slab.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/addr.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>

#include <linux/unaligned.h>

#include "netns.h"

#define NLMDBG_FACILITY		NLMDBG_MONITOR
#define NSM_PROGRAM		100024
#define NSM_VERSION		1

enum {
	NSMPROC_NULL,
	NSMPROC_STAT,
	NSMPROC_MON,
	NSMPROC_UNMON,
	NSMPROC_UNMON_ALL,
	NSMPROC_SIMU_CRASH,
	NSMPROC_NOTIFY,
};

struct nsm_args {
	struct nsm_private	*priv;
	u32			prog;		/* RPC callback info */
	u32			vers;
	u32			proc;

	char			*mon_name;
	const char		*nodename;
};

struct nsm_res {
	u32			status;
	u32			state;
};

static const struct rpc_program	nsm_program;
static				DEFINE_SPINLOCK(nsm_lock);

/*
 * Local NSM state
 */
u32	__read_mostly		nsm_local_state;
bool	__read_mostly		nsm_use_hostnames;

static inline struct sockaddr *nsm_addr(const struct nsm_handle *nsm)
{
	return (struct sockaddr *)&nsm->sm_addr;
}

static struct rpc_clnt *nsm_create(struct net *net, const char *nodename)
{
	struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= htonl(INADDR_LOOPBACK),
	};
	struct rpc_create_args args = {
		.net			= net,
		.protocol		= XPRT_TRANSPORT_TCP,
		.address		= (struct sockaddr *)&sin,
		.addrsize		= sizeof(sin),
		.servername		= "rpc.statd",
		.nodename		= nodename,
		.program		= &nsm_program,
		.version		= NSM_VERSION,
		.authflavor		= RPC_AUTH_NULL,
		.flags			= RPC_CLNT_CREATE_NOPING,
		.cred			= current_cred(),
	};

	return rpc_create(&args);
}

static int nsm_mon_unmon(struct nsm_handle *nsm, u32 proc, struct nsm_res *res,
			 const struct nlm_host *host)
{
	int		status;
	struct rpc_clnt *clnt;
	struct nsm_args args = {
		.priv		= &nsm->sm_priv,
		.prog		= NLM_PROGRAM,
		.vers		= 3,
		.proc		= NLMPROC_NSM_NOTIFY,
		.mon_name	= nsm->sm_mon_name,
		.nodename	= host->nodename,
	};
	struct rpc_message msg = {
		.rpc_argp	= &args,
		.rpc_resp	= res,
	};

	memset(res, 0, sizeof(*res));

	clnt = nsm_create(host->net, host->nodename);
	if (IS_ERR(clnt)) {
		dprintk("lockd: failed to create NSM upcall transport, "
			"status=%ld, net=%x\n", PTR_ERR(clnt),
			host->net->ns.inum);
		return PTR_ERR(clnt);
	}

	msg.rpc_proc = &clnt->cl_procinfo[proc];
	status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFTCONN);
	if (status == -ECONNREFUSED) {
		dprintk("lockd:	NSM upcall RPC failed, status=%d, forcing rebind\n",
				status);
		rpc_force_rebind(clnt);
		status = rpc_call_sync(clnt, &msg, RPC_TASK_SOFTCONN);
	}
	if (status < 0)
		dprintk("lockd: NSM upcall RPC failed, status=%d\n",
				status);
	else
		status = 0;

	rpc_shutdown_client(clnt);
	return status;
}

/**
 * nsm_monitor - Notify a peer in case we reboot
 * @host: pointer to nlm_host of peer to notify
 *
 * If this peer is not already monitored, this function sends an
 * upcall to the local rpc.statd to record the name/address of
 * the peer to notify in case we reboot.
 *
 * Returns zero if the peer is monitored by the local rpc.statd;
 * otherwise a negative errno value is returned.
 */
int nsm_monitor(const struct nlm_host *host)
{
	struct nsm_handle *nsm = host->h_nsmhandle;
	struct nsm_res	res;
	int		status;

	dprintk("lockd: nsm_monitor(%s)\n", nsm->sm_name);

	if (nsm->sm_monitored)
		return 0;

	/*
	 * Choose whether to record the caller_name or IP address of
	 * this peer in the local rpc.statd's database.
	 */
	nsm->sm_mon_name = nsm_use_hostnames ? nsm->sm_name : nsm->sm_addrbuf;

	status = nsm_mon_unmon(nsm, NSMPROC_MON, &res, host);
	if (unlikely(res.status != 0))
		status = -EIO;
	if (unlikely(status < 0)) {
		pr_notice_ratelimited("lockd: cannot monitor %s\n", nsm->sm_name);
		return status;
	}

	nsm->sm_monitored = 1;
	if (unlikely(nsm_local_state != res.state)) {
		nsm_local_state = res.state;
		dprintk("lockd: NSM state changed to %d\n", nsm_local_state);
	}
	return 0;
}

/**
 * nsm_unmonitor - Unregister peer notification
 * @host: pointer to nlm_host of peer to stop monitoring
 *
 * If this peer is monitored, this function sends an upcall to
 * tell the local rpc.statd not to send this peer a notification
 * when we reboot.
 */
void nsm_unmonitor(const struct nlm_host *host)
{
	struct nsm_handle *nsm = host->h_nsmhandle;
	struct nsm_res	res;
	int status;

	if (refcount_read(&nsm->sm_count) == 1
	 && nsm->sm_monitored && !nsm->sm_sticky) {
		dprintk("lockd: nsm_unmonitor(%s)\n", nsm->sm_name);

		status = nsm_mon_unmon(nsm, NSMPROC_UNMON, &res, host);
		if (res.status != 0)
			status = -EIO;
		if (status < 0)
			printk(KERN_NOTICE "lockd: cannot unmonitor %s\n",
					nsm->sm_name);
		else
			nsm->sm_monitored = 0;
	}
}

static struct nsm_handle *nsm_lookup_hostname(const struct list_head *nsm_handles,
					const char *hostname, const size_t len)
{
	struct nsm_handle *nsm;

	list_for_each_entry(nsm, nsm_handles, sm_link)
		if (strlen(nsm->sm_name) == len &&
		    memcmp(nsm->sm_name, hostname, len) == 0)
			return nsm;
	return NULL;
}

static struct nsm_handle *nsm_lookup_addr(const struct list_head *nsm_handles,
					const struct sockaddr *sap)
{
	struct nsm_handle *nsm;

	list_for_each_entry(nsm, nsm_handles, sm_link)
		if (rpc_cmp_addr(nsm_addr(nsm), sap))
			return nsm;
	return NULL;
}

static struct nsm_handle *nsm_lookup_priv(const struct list_head *nsm_handles,
					const struct nsm_private *priv)
{
	struct nsm_handle *nsm;

	list_for_each_entry(nsm, nsm_handles, sm_link)
		if (memcmp(nsm->sm_priv.data, priv->data,
					sizeof(priv->data)) == 0)
			return nsm;
	return NULL;
}

/*
 * Construct a unique cookie to match this nsm_handle to this monitored
 * host.  It is passed to the local rpc.statd via NSMPROC_MON, and
 * returned via NLMPROC_SM_NOTIFY, in the "priv" field of these
 * requests.
 *
 * The NSM protocol requires that these cookies be unique while the
 * system is running.  We prefer a stronger requirement of making them
 * unique across reboots.  If user space bugs cause a stale cookie to
 * be sent to the kernel, it could cause the wrong host to lose its
 * lock state if cookies were not unique across reboots.
 *
 * The cookies are exposed only to local user space via loopback.  They
 * do not appear on the physical network.  If we want greater security
 * for some reason, nsm_init_private() could perform a one-way hash to
 * obscure the contents of the cookie.
 */
static void nsm_init_private(struct nsm_handle *nsm)
{
	u64 *p = (u64 *)&nsm->sm_priv.data;
	s64 ns;

	ns = ktime_get_ns();
	put_unaligned(ns, p);
	put_unaligned((unsigned long)nsm, p + 1);
}

static struct nsm_handle *nsm_create_handle(const struct sockaddr *sap,
					    const size_t salen,
					    const char *hostname,
					    const size_t hostname_len)
{
	struct nsm_handle *new;

	if (!hostname)
		return NULL;

	new = kzalloc(sizeof(*new) + hostname_len + 1, GFP_KERNEL);
	if (unlikely(new == NULL))
		return NULL;

	refcount_set(&new->sm_count, 1);
	new->sm_name = (char *)(new + 1);
	memcpy(nsm_addr(new), sap, salen);
	new->sm_addrlen = salen;
	nsm_init_private(new);

	if (rpc_ntop(nsm_addr(new), new->sm_addrbuf,
					sizeof(new->sm_addrbuf)) == 0)
		(void)snprintf(new->sm_addrbuf, sizeof(new->sm_addrbuf),
				"unsupported address family");
	memcpy(new->sm_name, hostname, hostname_len);
	new->sm_name[hostname_len] = '\0';

	return new;
}

/**
 * nsm_get_handle - Find or create a cached nsm_handle
 * @net: network namespace
 * @sap: pointer to socket address of handle to find
 * @salen: length of socket address
 * @hostname: pointer to C string containing hostname to find
 * @hostname_len: length of C string
 *
 * Behavior is modulated by the global nsm_use_hostnames variable.
 *
 * Returns a cached nsm_handle after bumping its ref count, or
 * returns a fresh nsm_handle if a handle that matches @sap and/or
 * @hostname cannot be found in the handle cache.  Returns NULL if
 * an error occurs.
 */
struct nsm_handle *nsm_get_handle(const struct net *net,
				  const struct sockaddr *sap,
				  const size_t salen, const char *hostname,
				  const size_t hostname_len)
{
	struct nsm_handle *cached, *new = NULL;
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	if (hostname && memchr(hostname, '/', hostname_len) != NULL) {
		if (printk_ratelimit()) {
			printk(KERN_WARNING "Invalid hostname \"%.*s\" "
					    "in NFS lock request\n",
				(int)hostname_len, hostname);
		}
		return NULL;
	}

retry:
	spin_lock(&nsm_lock);

	if (nsm_use_hostnames && hostname != NULL)
		cached = nsm_lookup_hostname(&ln->nsm_handles,
					hostname, hostname_len);
	else
		cached = nsm_lookup_addr(&ln->nsm_handles, sap);

	if (cached != NULL) {
		refcount_inc(&cached->sm_count);
		spin_unlock(&nsm_lock);
		kfree(new);
		dprintk("lockd: found nsm_handle for %s (%s), "
				"cnt %d\n", cached->sm_name,
				cached->sm_addrbuf,
				refcount_read(&cached->sm_count));
		return cached;
	}

	if (new != NULL) {
		list_add(&new->sm_link, &ln->nsm_handles);
		spin_unlock(&nsm_lock);
		dprintk("lockd: created nsm_handle for %s (%s)\n",
				new->sm_name, new->sm_addrbuf);
		return new;
	}

	spin_unlock(&nsm_lock);

	new = nsm_create_handle(sap, salen, hostname, hostname_len);
	if (unlikely(new == NULL))
		return NULL;
	goto retry;
}

/**
 * nsm_reboot_lookup - match NLMPROC_SM_NOTIFY arguments to an nsm_handle
 * @net:  network namespace
 * @info: pointer to NLMPROC_SM_NOTIFY arguments
 *
 * Returns a matching nsm_handle if found in the nsm cache. The returned
 * nsm_handle's reference count is bumped. Otherwise returns NULL if some
 * error occurred.
 */
struct nsm_handle *nsm_reboot_lookup(const struct net *net,
				const struct nlm_reboot *info)
{
	struct nsm_handle *cached;
	struct lockd_net *ln = net_generic(net, lockd_net_id);

	spin_lock(&nsm_lock);

	cached = nsm_lookup_priv(&ln->nsm_handles, &info->priv);
	if (unlikely(cached == NULL)) {
		spin_unlock(&nsm_lock);
		dprintk("lockd: never saw rebooted peer '%.*s' before\n",
				info->len, info->mon);
		return cached;
	}

	refcount_inc(&cached->sm_count);
	spin_unlock(&nsm_lock);

	dprintk("lockd: host %s (%s) rebooted, cnt %d\n",
			cached->sm_name, cached->sm_addrbuf,
			refcount_read(&cached->sm_count));
	return cached;
}

/**
 * nsm_release - Release an NSM handle
 * @nsm: pointer to handle to be released
 *
 */
void nsm_release(struct nsm_handle *nsm)
{
	if (refcount_dec_and_lock(&nsm->sm_count, &nsm_lock)) {
		list_del(&nsm->sm_link);
		spin_unlock(&nsm_lock);
		dprintk("lockd: destroyed nsm_handle for %s (%s)\n",
				nsm->sm_name, nsm->sm_addrbuf);
		kfree(nsm);
	}
}

/*
 * XDR functions for NSM.
 *
 * See https://www.opengroup.org/ for details on the Network
 * Status Monitor wire protocol.
 */

static void encode_nsm_string(struct xdr_stream *xdr, const char *string)
{
	const u32 len = strlen(string);
	__be32 *p;

	p = xdr_reserve_space(xdr, 4 + len);
	xdr_encode_opaque(p, string, len);
}

/*
 * "mon_name" specifies the host to be monitored.
 */
static void encode_mon_name(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	encode_nsm_string(xdr, argp->mon_name);
}

/*
 * The "my_id" argument specifies the hostname and RPC procedure
 * to be called when the status manager receives notification
 * (via the NLMPROC_SM_NOTIFY call) that the state of host "mon_name"
 * has changed.
 */
static void encode_my_id(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	__be32 *p;

	encode_nsm_string(xdr, argp->nodename);
	p = xdr_reserve_space(xdr, 4 + 4 + 4);
	*p++ = cpu_to_be32(argp->prog);
	*p++ = cpu_to_be32(argp->vers);
	*p = cpu_to_be32(argp->proc);
}

/*
 * The "mon_id" argument specifies the non-private arguments
 * of an NSMPROC_MON or NSMPROC_UNMON call.
 */
static void encode_mon_id(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	encode_mon_name(xdr, argp);
	encode_my_id(xdr, argp);
}

/*
 * The "priv" argument may contain private information required
 * by the NSMPROC_MON call. This information will be supplied in the
 * NLMPROC_SM_NOTIFY call.
 */
static void encode_priv(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, SM_PRIV_SIZE);
	xdr_encode_opaque_fixed(p, argp->priv->data, SM_PRIV_SIZE);
}

static void nsm_xdr_enc_mon(struct rpc_rqst *req, struct xdr_stream *xdr,
			    const void *argp)
{
	encode_mon_id(xdr, argp);
	encode_priv(xdr, argp);
}

static void nsm_xdr_enc_unmon(struct rpc_rqst *req, struct xdr_stream *xdr,
			      const void *argp)
{
	encode_mon_id(xdr, argp);
}

static int nsm_xdr_dec_stat_res(struct rpc_rqst *rqstp,
				struct xdr_stream *xdr,
				void *data)
{
	struct nsm_res *resp = data;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4 + 4);
	if (unlikely(p == NULL))
		return -EIO;
	resp->status = be32_to_cpup(p++);
	resp->state = be32_to_cpup(p);

	dprintk("lockd: %s status %d state %d\n",
		__func__, resp->status, resp->state);
	return 0;
}

static int nsm_xdr_dec_stat(struct rpc_rqst *rqstp,
			    struct xdr_stream *xdr,
			    void *data)
{
	struct nsm_res *resp = data;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -EIO;
	resp->state = be32_to_cpup(p);

	dprintk("lockd: %s state %d\n", __func__, resp->state);
	return 0;
}

#define SM_my_name_sz	(1+XDR_QUADLEN(SM_MAXSTRLEN))
#define SM_my_id_sz	(SM_my_name_sz+3)
#define SM_mon_name_sz	(1+XDR_QUADLEN(SM_MAXSTRLEN))
#define SM_mon_id_sz	(SM_mon_name_sz+SM_my_id_sz)
#define SM_priv_sz	(XDR_QUADLEN(SM_PRIV_SIZE))
#define SM_mon_sz	(SM_mon_id_sz+SM_priv_sz)
#define SM_monres_sz	2
#define SM_unmonres_sz	1

static const struct rpc_procinfo nsm_procedures[] = {
[NSMPROC_MON] = {
		.p_proc		= NSMPROC_MON,
		.p_encode	= nsm_xdr_enc_mon,
		.p_decode	= nsm_xdr_dec_stat_res,
		.p_arglen	= SM_mon_sz,
		.p_replen	= SM_monres_sz,
		.p_statidx	= NSMPROC_MON,
		.p_name		= "MONITOR",
	},
[NSMPROC_UNMON] = {
		.p_proc		= NSMPROC_UNMON,
		.p_encode	= nsm_xdr_enc_unmon,
		.p_decode	= nsm_xdr_dec_stat,
		.p_arglen	= SM_mon_id_sz,
		.p_replen	= SM_unmonres_sz,
		.p_statidx	= NSMPROC_UNMON,
		.p_name		= "UNMONITOR",
	},
};

static unsigned int nsm_version1_counts[ARRAY_SIZE(nsm_procedures)];
static const struct rpc_version nsm_version1 = {
	.number		= 1,
	.nrprocs	= ARRAY_SIZE(nsm_procedures),
	.procs		= nsm_procedures,
	.counts		= nsm_version1_counts,
};

static const struct rpc_version *nsm_version[] = {
	[1] = &nsm_version1,
};

static struct rpc_stat		nsm_stats;

static const struct rpc_program nsm_program = {
	.name		= "statd",
	.number		= NSM_PROGRAM,
	.nrvers		= ARRAY_SIZE(nsm_version),
	.version	= nsm_version,
	.stats		= &nsm_stats
};
