/*
 * linux/fs/lockd/mon.c
 *
 * The kernel statd client.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/utsname.h>
#include <linux/kernel.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtsock.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/sm_inter.h>


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
	__be32			addr;		/* remote address */
	u32			prog;		/* RPC callback info */
	u32			vers;
	u32			proc;

	char			*mon_name;
};

struct nsm_res {
	u32			status;
	u32			state;
};

static struct rpc_clnt *	nsm_create(void);

static struct rpc_program	nsm_program;

/*
 * Local NSM state
 */
int				nsm_local_state;

/*
 * Common procedure for NSMPROC_MON/NSMPROC_UNMON calls
 */
static int
nsm_mon_unmon(struct nsm_handle *nsm, u32 proc, struct nsm_res *res)
{
	struct rpc_clnt	*clnt;
	int		status;
	struct nsm_args args = {
		.addr		= nsm_addr_in(nsm)->sin_addr.s_addr,
		.prog		= NLM_PROGRAM,
		.vers		= 3,
		.proc		= NLMPROC_NSM_NOTIFY,
		.mon_name	= nsm->sm_mon_name,
	};
	struct rpc_message msg = {
		.rpc_argp	= &args,
		.rpc_resp	= res,
	};

	clnt = nsm_create();
	if (IS_ERR(clnt)) {
		status = PTR_ERR(clnt);
		dprintk("lockd: failed to create NSM upcall transport, "
				"status=%d\n", status);
		goto out;
	}

	memset(res, 0, sizeof(*res));

	msg.rpc_proc = &clnt->cl_procinfo[proc];
	status = rpc_call_sync(clnt, &msg, 0);
	if (status < 0)
		dprintk("lockd: NSM upcall RPC failed, status=%d\n",
				status);
	else
		status = 0;
	rpc_shutdown_client(clnt);
 out:
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

	status = nsm_mon_unmon(nsm, NSMPROC_MON, &res);
	if (res.status != 0)
		status = -EIO;
	if (status < 0)
		printk(KERN_NOTICE "lockd: cannot monitor %s\n", nsm->sm_name);
	else
		nsm->sm_monitored = 1;
	return status;
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

	if (atomic_read(&nsm->sm_count) == 1
	 && nsm->sm_monitored && !nsm->sm_sticky) {
		dprintk("lockd: nsm_unmonitor(%s)\n", nsm->sm_name);

		status = nsm_mon_unmon(nsm, NSMPROC_UNMON, &res);
		if (res.status != 0)
			status = -EIO;
		if (status < 0)
			printk(KERN_NOTICE "lockd: cannot unmonitor %s\n",
					nsm->sm_name);
		else
			nsm->sm_monitored = 0;
	}
}

/*
 * Create NSM client for the local host
 */
static struct rpc_clnt *
nsm_create(void)
{
	struct sockaddr_in	sin = {
		.sin_family	= AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port	= 0,
	};
	struct rpc_create_args args = {
		.protocol	= XPRT_TRANSPORT_UDP,
		.address	= (struct sockaddr *)&sin,
		.addrsize	= sizeof(sin),
		.servername	= "localhost",
		.program	= &nsm_program,
		.version	= NSM_VERSION,
		.authflavor	= RPC_AUTH_NULL,
	};

	return rpc_create(&args);
}

/*
 * XDR functions for NSM.
 *
 * See http://www.opengroup.org/ for details on the Network
 * Status Monitor wire protocol.
 */

static int encode_nsm_string(struct xdr_stream *xdr, const char *string)
{
	const u32 len = strlen(string);
	__be32 *p;

	if (unlikely(len > SM_MAXSTRLEN))
		return -EIO;
	p = xdr_reserve_space(xdr, sizeof(u32) + len);
	if (unlikely(p == NULL))
		return -EIO;
	xdr_encode_opaque(p, string, len);
	return 0;
}

/*
 * "mon_name" specifies the host to be monitored.
 */
static int encode_mon_name(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	return encode_nsm_string(xdr, argp->mon_name);
}

/*
 * The "my_id" argument specifies the hostname and RPC procedure
 * to be called when the status manager receives notification
 * (via the NLMPROC_SM_NOTIFY call) that the state of host "mon_name"
 * has changed.
 */
static int encode_my_id(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	int status;
	__be32 *p;

	status = encode_nsm_string(xdr, utsname()->nodename);
	if (unlikely(status != 0))
		return status;
	p = xdr_reserve_space(xdr, 3 * sizeof(u32));
	if (unlikely(p == NULL))
		return -EIO;
	*p++ = htonl(argp->prog);
	*p++ = htonl(argp->vers);
	*p++ = htonl(argp->proc);
	return 0;
}

/*
 * The "mon_id" argument specifies the non-private arguments
 * of an NSMPROC_MON or NSMPROC_UNMON call.
 */
static int encode_mon_id(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	int status;

	status = encode_mon_name(xdr, argp);
	if (unlikely(status != 0))
		return status;
	return encode_my_id(xdr, argp);
}

/*
 * The "priv" argument may contain private information required
 * by the NSMPROC_MON call. This information will be supplied in the
 * NLMPROC_SM_NOTIFY call.
 *
 * Linux provides the raw IP address of the monitored host,
 * left in network byte order.
 */
static int encode_priv(struct xdr_stream *xdr, const struct nsm_args *argp)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, SM_PRIV_SIZE);
	if (unlikely(p == NULL))
		return -EIO;
	*p++ = argp->addr;
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;
	return 0;
}

static int xdr_enc_mon(struct rpc_rqst *req, __be32 *p,
		       const struct nsm_args *argp)
{
	struct xdr_stream xdr;
	int status;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	status = encode_mon_id(&xdr, argp);
	if (unlikely(status))
		return status;
	return encode_priv(&xdr, argp);
}

static int xdr_enc_unmon(struct rpc_rqst *req, __be32 *p,
			 const struct nsm_args *argp)
{
	struct xdr_stream xdr;

	xdr_init_encode(&xdr, &req->rq_snd_buf, p);
	return encode_mon_id(&xdr, argp);
}

static int xdr_dec_stat_res(struct rpc_rqst *rqstp, __be32 *p,
			    struct nsm_res *resp)
{
	struct xdr_stream xdr;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	p = xdr_inline_decode(&xdr, 2 * sizeof(u32));
	if (unlikely(p == NULL))
		return -EIO;
	resp->status = ntohl(*p++);
	resp->state = ntohl(*p);

	dprintk("lockd: xdr_dec_stat_res status %d state %d\n",
			resp->status, resp->state);
	return 0;
}

static int xdr_dec_stat(struct rpc_rqst *rqstp, __be32 *p,
			struct nsm_res *resp)
{
	struct xdr_stream xdr;

	xdr_init_decode(&xdr, &rqstp->rq_rcv_buf, p);
	p = xdr_inline_decode(&xdr, sizeof(u32));
	if (unlikely(p == NULL))
		return -EIO;
	resp->state = ntohl(*p);

	dprintk("lockd: xdr_dec_stat state %d\n", resp->state);
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

static struct rpc_procinfo	nsm_procedures[] = {
[NSMPROC_MON] = {
		.p_proc		= NSMPROC_MON,
		.p_encode	= (kxdrproc_t)xdr_enc_mon,
		.p_decode	= (kxdrproc_t)xdr_dec_stat_res,
		.p_arglen	= SM_mon_sz,
		.p_replen	= SM_monres_sz,
		.p_statidx	= NSMPROC_MON,
		.p_name		= "MONITOR",
	},
[NSMPROC_UNMON] = {
		.p_proc		= NSMPROC_UNMON,
		.p_encode	= (kxdrproc_t)xdr_enc_unmon,
		.p_decode	= (kxdrproc_t)xdr_dec_stat,
		.p_arglen	= SM_mon_id_sz,
		.p_replen	= SM_unmonres_sz,
		.p_statidx	= NSMPROC_UNMON,
		.p_name		= "UNMONITOR",
	},
};

static struct rpc_version	nsm_version1 = {
		.number		= 1,
		.nrprocs	= ARRAY_SIZE(nsm_procedures),
		.procs		= nsm_procedures
};

static struct rpc_version *	nsm_version[] = {
	[1] = &nsm_version1,
};

static struct rpc_stat		nsm_stats;

static struct rpc_program	nsm_program = {
		.name		= "statd",
		.number		= NSM_PROGRAM,
		.nrvers		= ARRAY_SIZE(nsm_version),
		.version	= nsm_version,
		.stats		= &nsm_stats
};
