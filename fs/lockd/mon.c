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
u32				nsm_local_state;

/*
 * Common procedure for SM_MON/SM_UNMON calls
 */
static int
nsm_mon_unmon(struct nlm_host *host, u32 proc, struct nsm_res *res)
{
	struct rpc_clnt	*clnt;
	int		status;
	struct nsm_args	args;

	clnt = nsm_create();
	if (IS_ERR(clnt)) {
		status = PTR_ERR(clnt);
		goto out;
	}

	args.addr = host->h_addr.sin_addr.s_addr;
	args.proto= (host->h_proto<<1) | host->h_server;
	args.prog = NLM_PROGRAM;
	args.vers = host->h_version;
	args.proc = NLMPROC_NSM_NOTIFY;
	memset(res, 0, sizeof(*res));

	status = rpc_call(clnt, proc, &args, res, 0);
	if (status < 0)
		printk(KERN_DEBUG "nsm_mon_unmon: rpc failed, status=%d\n",
			status);
	else
		status = 0;
 out:
	return status;
}

/*
 * Set up monitoring of a remote host
 */
int
nsm_monitor(struct nlm_host *host)
{
	struct nsm_res	res;
	int		status;

	dprintk("lockd: nsm_monitor(%s)\n", host->h_name);

	status = nsm_mon_unmon(host, SM_MON, &res);

	if (status < 0 || res.status != 0)
		printk(KERN_NOTICE "lockd: cannot monitor %s\n", host->h_name);
	else
		host->h_monitored = 1;
	return status;
}

/*
 * Cease to monitor remote host
 */
int
nsm_unmonitor(struct nlm_host *host)
{
	struct nsm_res	res;
	int		status;

	dprintk("lockd: nsm_unmonitor(%s)\n", host->h_name);

	status = nsm_mon_unmon(host, SM_UNMON, &res);
	if (status < 0)
		printk(KERN_NOTICE "lockd: cannot unmonitor %s\n", host->h_name);
	else
		host->h_monitored = 0;
	return status;
}

/*
 * Create NSM client for the local host
 */
static struct rpc_clnt *
nsm_create(void)
{
	struct rpc_xprt		*xprt;
	struct rpc_clnt		*clnt;
	struct sockaddr_in	sin;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = 0;

	xprt = xprt_create_proto(IPPROTO_UDP, &sin, NULL);
	if (IS_ERR(xprt))
		return (struct rpc_clnt *)xprt;
	xprt->resvport = 1;	/* NSM requires a reserved port */

	clnt = rpc_create_client(xprt, "localhost",
				&nsm_program, SM_VERSION,
				RPC_AUTH_NULL);
	if (IS_ERR(clnt))
		goto out_err;
	clnt->cl_softrtry = 1;
	clnt->cl_oneshot  = 1;
	return clnt;

out_err:
	return clnt;
}

/*
 * XDR functions for NSM.
 */

static u32 *
xdr_encode_common(struct rpc_rqst *rqstp, u32 *p, struct nsm_args *argp)
{
	char	buffer[20];

	/*
	 * Use the dotted-quad IP address of the remote host as
	 * identifier. Linux statd always looks up the canonical
	 * hostname first for whatever remote hostname it receives,
	 * so this works alright.
	 */
	sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(argp->addr));
	if (!(p = xdr_encode_string(p, buffer))
	 || !(p = xdr_encode_string(p, system_utsname.nodename)))
		return ERR_PTR(-EIO);
	*p++ = htonl(argp->prog);
	*p++ = htonl(argp->vers);
	*p++ = htonl(argp->proc);

	return p;
}

static int
xdr_encode_mon(struct rpc_rqst *rqstp, u32 *p, struct nsm_args *argp)
{
	p = xdr_encode_common(rqstp, p, argp);
	if (IS_ERR(p))
		return PTR_ERR(p);
	*p++ = argp->addr;
	*p++ = argp->vers;
	*p++ = argp->proto;
	*p++ = 0;
	rqstp->rq_slen = xdr_adjust_iovec(rqstp->rq_svec, p);
	return 0;
}

static int
xdr_encode_unmon(struct rpc_rqst *rqstp, u32 *p, struct nsm_args *argp)
{
	p = xdr_encode_common(rqstp, p, argp);
	if (IS_ERR(p))
		return PTR_ERR(p);
	rqstp->rq_slen = xdr_adjust_iovec(rqstp->rq_svec, p);
	return 0;
}

static int
xdr_decode_stat_res(struct rpc_rqst *rqstp, u32 *p, struct nsm_res *resp)
{
	resp->status = ntohl(*p++);
	resp->state = ntohl(*p++);
	dprintk("nsm: xdr_decode_stat_res status %d state %d\n",
			resp->status, resp->state);
	return 0;
}

static int
xdr_decode_stat(struct rpc_rqst *rqstp, u32 *p, struct nsm_res *resp)
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

#ifndef MAX
# define MAX(a, b)	(((a) > (b))? (a) : (b))
#endif

static struct rpc_procinfo	nsm_procedures[] = {
[SM_MON] = {
		.p_proc		= SM_MON,
		.p_encode	= (kxdrproc_t) xdr_encode_mon,
		.p_decode	= (kxdrproc_t) xdr_decode_stat_res,
		.p_bufsiz	= MAX(SM_mon_sz, SM_monres_sz) << 2,
	},
[SM_UNMON] = {
		.p_proc		= SM_UNMON,
		.p_encode	= (kxdrproc_t) xdr_encode_unmon,
		.p_decode	= (kxdrproc_t) xdr_decode_stat,
		.p_bufsiz	= MAX(SM_mon_id_sz, SM_unmonres_sz) << 2,
	},
};

static struct rpc_version	nsm_version1 = {
		.number		= 1, 
		.nrprocs	= sizeof(nsm_procedures)/sizeof(nsm_procedures[0]),
		.procs		= nsm_procedures
};

static struct rpc_version *	nsm_version[] = {
	[1] = &nsm_version1,
};

static struct rpc_stat		nsm_stats;

static struct rpc_program	nsm_program = {
		.name		= "statd",
		.number		= SM_PROGRAM,
		.nrvers		= sizeof(nsm_version)/sizeof(nsm_version[0]),
		.version	= nsm_version,
		.stats		= &nsm_stats
};
