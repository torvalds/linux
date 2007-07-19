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
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/lockd/sm_inter.h>


#define NLMDBG_FACILITY		NLMDBG_MONITOR

static struct rpc_clnt *	nsm_create(void);

static struct rpc_program	nsm_program;

/*
 * Local NSM state
 */
int				nsm_local_state;

/*
 * Common procedure for SM_MON/SM_UNMON calls
 */
static int
nsm_mon_unmon(struct nsm_handle *nsm, u32 proc, struct nsm_res *res)
{
	struct rpc_clnt	*clnt;
	int		status;
	struct nsm_args	args;
	struct rpc_message msg = {
		.rpc_argp	= &args,
		.rpc_resp	= res,
	};

	clnt = nsm_create();
	if (IS_ERR(clnt)) {
		status = PTR_ERR(clnt);
		goto out;
	}

	memset(&args, 0, sizeof(args));
	args.mon_name = nsm->sm_name;
	args.addr = nsm->sm_addr.sin_addr.s_addr;
	args.prog = NLM_PROGRAM;
	args.vers = 3;
	args.proc = NLMPROC_NSM_NOTIFY;
	memset(res, 0, sizeof(*res));

	msg.rpc_proc = &clnt->cl_procinfo[proc];
	status = rpc_call_sync(clnt, &msg, 0);
	if (status < 0)
		printk(KERN_DEBUG "nsm_mon_unmon: rpc failed, status=%d\n",
			status);
	else
		status = 0;
	rpc_shutdown_client(clnt);
 out:
	return status;
}

/*
 * Set up monitoring of a remote host
 */
int
nsm_monitor(struct nlm_host *host)
{
	struct nsm_handle *nsm = host->h_nsmhandle;
	struct nsm_res	res;
	int		status;

	dprintk("lockd: nsm_monitor(%s)\n", host->h_name);
	BUG_ON(nsm == NULL);

	if (nsm->sm_monitored)
		return 0;

	status = nsm_mon_unmon(nsm, SM_MON, &res);

	if (status < 0 || res.status != 0)
		printk(KERN_NOTICE "lockd: cannot monitor %s\n", host->h_name);
	else
		nsm->sm_monitored = 1;
	return status;
}

/*
 * Cease to monitor remote host
 */
int
nsm_unmonitor(struct nlm_host *host)
{
	struct nsm_handle *nsm = host->h_nsmhandle;
	struct nsm_res	res;
	int		status = 0;

	if (nsm == NULL)
		return 0;
	host->h_nsmhandle = NULL;

	if (atomic_read(&nsm->sm_count) == 1
	 && nsm->sm_monitored && !nsm->sm_sticky) {
		dprintk("lockd: nsm_unmonitor(%s)\n", host->h_name);

		status = nsm_mon_unmon(nsm, SM_UNMON, &res);
		if (status < 0)
			printk(KERN_NOTICE "lockd: cannot unmonitor %s\n",
					host->h_name);
		else
			nsm->sm_monitored = 0;
	}
	nsm_release(nsm);
	return status;
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
		.protocol	= IPPROTO_UDP,
		.address	= (struct sockaddr *)&sin,
		.addrsize	= sizeof(sin),
		.servername	= "localhost",
		.program	= &nsm_program,
		.version	= SM_VERSION,
		.authflavor	= RPC_AUTH_NULL,
	};

	return rpc_create(&args);
}

/*
 * XDR functions for NSM.
 */

static __be32 *
xdr_encode_common(struct rpc_rqst *rqstp, __be32 *p, struct nsm_args *argp)
{
	char	buffer[20], *name;

	/*
	 * Use the dotted-quad IP address of the remote host as
	 * identifier. Linux statd always looks up the canonical
	 * hostname first for whatever remote hostname it receives,
	 * so this works alright.
	 */
	if (nsm_use_hostnames) {
		name = argp->mon_name;
	} else {
		sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(argp->addr));
		name = buffer;
	}
	if (!(p = xdr_encode_string(p, name))
	 || !(p = xdr_encode_string(p, utsname()->nodename)))
		return ERR_PTR(-EIO);
	*p++ = htonl(argp->prog);
	*p++ = htonl(argp->vers);
	*p++ = htonl(argp->proc);

	return p;
}

static int
xdr_encode_mon(struct rpc_rqst *rqstp, __be32 *p, struct nsm_args *argp)
{
	p = xdr_encode_common(rqstp, p, argp);
	if (IS_ERR(p))
		return PTR_ERR(p);

	/* Surprise - there may even be room for an IPv6 address now */
	*p++ = argp->addr;
	*p++ = 0;
	*p++ = 0;
	*p++ = 0;
	rqstp->rq_slen = xdr_adjust_iovec(rqstp->rq_svec, p);
	return 0;
}

static int
xdr_encode_unmon(struct rpc_rqst *rqstp, __be32 *p, struct nsm_args *argp)
{
	p = xdr_encode_common(rqstp, p, argp);
	if (IS_ERR(p))
		return PTR_ERR(p);
	rqstp->rq_slen = xdr_adjust_iovec(rqstp->rq_svec, p);
	return 0;
}

static int
xdr_decode_stat_res(struct rpc_rqst *rqstp, __be32 *p, struct nsm_res *resp)
{
	resp->status = ntohl(*p++);
	resp->state = ntohl(*p++);
	dprintk("nsm: xdr_decode_stat_res status %d state %d\n",
			resp->status, resp->state);
	return 0;
}

static int
xdr_decode_stat(struct rpc_rqst *rqstp, __be32 *p, struct nsm_res *resp)
{
	resp->state = ntohl(*p++);
	return 0;
}

#define SM_my_name_sz	(1+XDR_QUADLEN(SM_MAXSTRLEN))
#define SM_my_id_sz	(3+1+SM_my_name_sz)
#define SM_mon_id_sz	(1+XDR_QUADLEN(20)+SM_my_id_sz)
#define SM_mon_sz	(SM_mon_id_sz+4)
#define SM_monres_sz	2
#define SM_unmonres_sz	1

static struct rpc_procinfo	nsm_procedures[] = {
[SM_MON] = {
		.p_proc		= SM_MON,
		.p_encode	= (kxdrproc_t) xdr_encode_mon,
		.p_decode	= (kxdrproc_t) xdr_decode_stat_res,
		.p_arglen	= SM_mon_sz,
		.p_replen	= SM_monres_sz,
		.p_statidx	= SM_MON,
		.p_name		= "MONITOR",
	},
[SM_UNMON] = {
		.p_proc		= SM_UNMON,
		.p_encode	= (kxdrproc_t) xdr_encode_unmon,
		.p_decode	= (kxdrproc_t) xdr_decode_stat,
		.p_arglen	= SM_mon_id_sz,
		.p_replen	= SM_unmonres_sz,
		.p_statidx	= SM_UNMON,
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
		.number		= SM_PROGRAM,
		.nrvers		= ARRAY_SIZE(nsm_version),
		.version	= nsm_version,
		.stats		= &nsm_stats
};
