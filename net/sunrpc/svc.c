/*
 * linux/net/sunrpc/svc.c
 *
 * High-level RPC service routines
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/linkage.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/mm.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/clnt.h>

#define RPCDBG_FACILITY	RPCDBG_SVCDSP
#define RPC_PARANOIA 1

/*
 * Create an RPC service
 */
struct svc_serv *
svc_create(struct svc_program *prog, unsigned int bufsize,
	   void (*shutdown)(struct svc_serv *serv))
{
	struct svc_serv	*serv;
	int vers;
	unsigned int xdrsize;
	unsigned int i;

	if (!(serv = kzalloc(sizeof(*serv), GFP_KERNEL)))
		return NULL;
	serv->sv_name      = prog->pg_name;
	serv->sv_program   = prog;
	serv->sv_nrthreads = 1;
	serv->sv_stats     = prog->pg_stats;
	serv->sv_bufsz	   = bufsize? bufsize : 4096;
	serv->sv_shutdown  = shutdown;
	xdrsize = 0;
	while (prog) {
		prog->pg_lovers = prog->pg_nvers-1;
		for (vers=0; vers<prog->pg_nvers ; vers++)
			if (prog->pg_vers[vers]) {
				prog->pg_hivers = vers;
				if (prog->pg_lovers > vers)
					prog->pg_lovers = vers;
				if (prog->pg_vers[vers]->vs_xdrsize > xdrsize)
					xdrsize = prog->pg_vers[vers]->vs_xdrsize;
			}
		prog = prog->pg_next;
	}
	serv->sv_xdrsize   = xdrsize;
	INIT_LIST_HEAD(&serv->sv_tempsocks);
	INIT_LIST_HEAD(&serv->sv_permsocks);
	init_timer(&serv->sv_temptimer);
	spin_lock_init(&serv->sv_lock);

	serv->sv_nrpools = 1;
	serv->sv_pools =
		kcalloc(sizeof(struct svc_pool), serv->sv_nrpools,
			GFP_KERNEL);
	if (!serv->sv_pools) {
		kfree(serv);
		return NULL;
	}

	for (i = 0; i < serv->sv_nrpools; i++) {
		struct svc_pool *pool = &serv->sv_pools[i];

		dprintk("initialising pool %u for %s\n",
				i, serv->sv_name);

		pool->sp_id = i;
		INIT_LIST_HEAD(&pool->sp_threads);
		INIT_LIST_HEAD(&pool->sp_sockets);
		spin_lock_init(&pool->sp_lock);
	}


	/* Remove any stale portmap registrations */
	svc_register(serv, 0, 0);

	return serv;
}

/*
 * Destroy an RPC service.  Should be called with the BKL held
 */
void
svc_destroy(struct svc_serv *serv)
{
	struct svc_sock	*svsk;

	dprintk("RPC: svc_destroy(%s, %d)\n",
				serv->sv_program->pg_name,
				serv->sv_nrthreads);

	if (serv->sv_nrthreads) {
		if (--(serv->sv_nrthreads) != 0) {
			svc_sock_update_bufs(serv);
			return;
		}
	} else
		printk("svc_destroy: no threads for serv=%p!\n", serv);

	del_timer_sync(&serv->sv_temptimer);

	while (!list_empty(&serv->sv_tempsocks)) {
		svsk = list_entry(serv->sv_tempsocks.next,
				  struct svc_sock,
				  sk_list);
		svc_delete_socket(svsk);
	}
	if (serv->sv_shutdown)
		serv->sv_shutdown(serv);

	while (!list_empty(&serv->sv_permsocks)) {
		svsk = list_entry(serv->sv_permsocks.next,
				  struct svc_sock,
				  sk_list);
		svc_delete_socket(svsk);
	}
	
	cache_clean_deferred(serv);

	/* Unregister service with the portmapper */
	svc_register(serv, 0, 0);
	kfree(serv->sv_pools);
	kfree(serv);
}

/*
 * Allocate an RPC server's buffer space.
 * We allocate pages and place them in rq_argpages.
 */
static int
svc_init_buffer(struct svc_rqst *rqstp, unsigned int size)
{
	int pages;
	int arghi;
	
	if (size > RPCSVC_MAXPAYLOAD)
		size = RPCSVC_MAXPAYLOAD;
	pages = 2 + (size+ PAGE_SIZE -1) / PAGE_SIZE;
	rqstp->rq_argused = 0;
	rqstp->rq_resused = 0;
	arghi = 0;
	BUG_ON(pages > RPCSVC_MAXPAGES);
	while (pages) {
		struct page *p = alloc_page(GFP_KERNEL);
		if (!p)
			break;
		rqstp->rq_argpages[arghi++] = p;
		pages--;
	}
	rqstp->rq_arghi = arghi;
	return ! pages;
}

/*
 * Release an RPC server buffer
 */
static void
svc_release_buffer(struct svc_rqst *rqstp)
{
	while (rqstp->rq_arghi)
		put_page(rqstp->rq_argpages[--rqstp->rq_arghi]);
	while (rqstp->rq_resused) {
		if (rqstp->rq_respages[--rqstp->rq_resused] == NULL)
			continue;
		put_page(rqstp->rq_respages[rqstp->rq_resused]);
	}
	rqstp->rq_argused = 0;
}

/*
 * Create a thread in the given pool.  Caller must hold BKL.
 */
static int
__svc_create_thread(svc_thread_fn func, struct svc_serv *serv,
		    struct svc_pool *pool)
{
	struct svc_rqst	*rqstp;
	int		error = -ENOMEM;

	rqstp = kzalloc(sizeof(*rqstp), GFP_KERNEL);
	if (!rqstp)
		goto out;

	init_waitqueue_head(&rqstp->rq_wait);

	if (!(rqstp->rq_argp = kmalloc(serv->sv_xdrsize, GFP_KERNEL))
	 || !(rqstp->rq_resp = kmalloc(serv->sv_xdrsize, GFP_KERNEL))
	 || !svc_init_buffer(rqstp, serv->sv_bufsz))
		goto out_thread;

	serv->sv_nrthreads++;
	spin_lock_bh(&pool->sp_lock);
	pool->sp_nrthreads++;
	spin_unlock_bh(&pool->sp_lock);
	rqstp->rq_server = serv;
	rqstp->rq_pool = pool;
	error = kernel_thread((int (*)(void *)) func, rqstp, 0);
	if (error < 0)
		goto out_thread;
	svc_sock_update_bufs(serv);
	error = 0;
out:
	return error;

out_thread:
	svc_exit_thread(rqstp);
	goto out;
}

/*
 * Create a thread in the default pool.  Caller must hold BKL.
 */
int
svc_create_thread(svc_thread_fn func, struct svc_serv *serv)
{
	return __svc_create_thread(func, serv, &serv->sv_pools[0]);
}

/*
 * Called from a server thread as it's exiting.  Caller must hold BKL.
 */
void
svc_exit_thread(struct svc_rqst *rqstp)
{
	struct svc_serv	*serv = rqstp->rq_server;
	struct svc_pool	*pool = rqstp->rq_pool;

	svc_release_buffer(rqstp);
	kfree(rqstp->rq_resp);
	kfree(rqstp->rq_argp);
	kfree(rqstp->rq_auth_data);

	spin_lock_bh(&pool->sp_lock);
	pool->sp_nrthreads--;
	spin_unlock_bh(&pool->sp_lock);

	kfree(rqstp);

	/* Release the server */
	if (serv)
		svc_destroy(serv);
}

/*
 * Register an RPC service with the local portmapper.
 * To unregister a service, call this routine with 
 * proto and port == 0.
 */
int
svc_register(struct svc_serv *serv, int proto, unsigned short port)
{
	struct svc_program	*progp;
	unsigned long		flags;
	int			i, error = 0, dummy;

	progp = serv->sv_program;

	dprintk("RPC: svc_register(%s, %s, %d)\n",
		progp->pg_name, proto == IPPROTO_UDP? "udp" : "tcp", port);

	if (!port)
		clear_thread_flag(TIF_SIGPENDING);

	for (i = 0; i < progp->pg_nvers; i++) {
		if (progp->pg_vers[i] == NULL)
			continue;
		error = rpc_register(progp->pg_prog, i, proto, port, &dummy);
		if (error < 0)
			break;
		if (port && !dummy) {
			error = -EACCES;
			break;
		}
	}

	if (!port) {
		spin_lock_irqsave(&current->sighand->siglock, flags);
		recalc_sigpending();
		spin_unlock_irqrestore(&current->sighand->siglock, flags);
	}

	return error;
}

/*
 * Process the RPC request.
 */
int
svc_process(struct svc_rqst *rqstp)
{
	struct svc_program	*progp;
	struct svc_version	*versp = NULL;	/* compiler food */
	struct svc_procedure	*procp = NULL;
	struct kvec *		argv = &rqstp->rq_arg.head[0];
	struct kvec *		resv = &rqstp->rq_res.head[0];
	struct svc_serv		*serv = rqstp->rq_server;
	kxdrproc_t		xdr;
	__be32			*statp;
	u32			dir, prog, vers, proc;
	__be32			auth_stat, rpc_stat;
	int			auth_res;
	__be32			*accept_statp;

	rpc_stat = rpc_success;

	if (argv->iov_len < 6*4)
		goto err_short_len;

	/* setup response xdr_buf.
	 * Initially it has just one page 
	 */
	svc_take_page(rqstp); /* must succeed */
	resv->iov_base = page_address(rqstp->rq_respages[0]);
	resv->iov_len = 0;
	rqstp->rq_res.pages = rqstp->rq_respages+1;
	rqstp->rq_res.len = 0;
	rqstp->rq_res.page_base = 0;
	rqstp->rq_res.page_len = 0;
	rqstp->rq_res.buflen = PAGE_SIZE;
	rqstp->rq_res.tail[0].iov_base = NULL;
	rqstp->rq_res.tail[0].iov_len = 0;
	/* Will be turned off only in gss privacy case: */
	rqstp->rq_sendfile_ok = 1;
	/* tcp needs a space for the record length... */
	if (rqstp->rq_prot == IPPROTO_TCP)
		svc_putnl(resv, 0);

	rqstp->rq_xid = svc_getu32(argv);
	svc_putu32(resv, rqstp->rq_xid);

	dir  = svc_getnl(argv);
	vers = svc_getnl(argv);

	/* First words of reply: */
	svc_putnl(resv, 1);		/* REPLY */

	if (dir != 0)		/* direction != CALL */
		goto err_bad_dir;
	if (vers != 2)		/* RPC version number */
		goto err_bad_rpc;

	/* Save position in case we later decide to reject: */
	accept_statp = resv->iov_base + resv->iov_len;

	svc_putnl(resv, 0);		/* ACCEPT */

	rqstp->rq_prog = prog = svc_getnl(argv);	/* program number */
	rqstp->rq_vers = vers = svc_getnl(argv);	/* version number */
	rqstp->rq_proc = proc = svc_getnl(argv);	/* procedure number */

	progp = serv->sv_program;

	for (progp = serv->sv_program; progp; progp = progp->pg_next)
		if (prog == progp->pg_prog)
			break;

	/*
	 * Decode auth data, and add verifier to reply buffer.
	 * We do this before anything else in order to get a decent
	 * auth verifier.
	 */
	auth_res = svc_authenticate(rqstp, &auth_stat);
	/* Also give the program a chance to reject this call: */
	if (auth_res == SVC_OK && progp) {
		auth_stat = rpc_autherr_badcred;
		auth_res = progp->pg_authenticate(rqstp);
	}
	switch (auth_res) {
	case SVC_OK:
		break;
	case SVC_GARBAGE:
		rpc_stat = rpc_garbage_args;
		goto err_bad;
	case SVC_SYSERR:
		rpc_stat = rpc_system_err;
		goto err_bad;
	case SVC_DENIED:
		goto err_bad_auth;
	case SVC_DROP:
		goto dropit;
	case SVC_COMPLETE:
		goto sendit;
	}

	if (progp == NULL)
		goto err_bad_prog;

	if (vers >= progp->pg_nvers ||
	  !(versp = progp->pg_vers[vers]))
		goto err_bad_vers;

	procp = versp->vs_proc + proc;
	if (proc >= versp->vs_nproc || !procp->pc_func)
		goto err_bad_proc;
	rqstp->rq_server   = serv;
	rqstp->rq_procinfo = procp;

	/* Syntactic check complete */
	serv->sv_stats->rpccnt++;

	/* Build the reply header. */
	statp = resv->iov_base +resv->iov_len;
	svc_putnl(resv, RPC_SUCCESS);

	/* Bump per-procedure stats counter */
	procp->pc_count++;

	/* Initialize storage for argp and resp */
	memset(rqstp->rq_argp, 0, procp->pc_argsize);
	memset(rqstp->rq_resp, 0, procp->pc_ressize);

	/* un-reserve some of the out-queue now that we have a 
	 * better idea of reply size
	 */
	if (procp->pc_xdrressize)
		svc_reserve(rqstp, procp->pc_xdrressize<<2);

	/* Call the function that processes the request. */
	if (!versp->vs_dispatch) {
		/* Decode arguments */
		xdr = procp->pc_decode;
		if (xdr && !xdr(rqstp, argv->iov_base, rqstp->rq_argp))
			goto err_garbage;

		*statp = procp->pc_func(rqstp, rqstp->rq_argp, rqstp->rq_resp);

		/* Encode reply */
		if (*statp == rpc_success && (xdr = procp->pc_encode)
		 && !xdr(rqstp, resv->iov_base+resv->iov_len, rqstp->rq_resp)) {
			dprintk("svc: failed to encode reply\n");
			/* serv->sv_stats->rpcsystemerr++; */
			*statp = rpc_system_err;
		}
	} else {
		dprintk("svc: calling dispatcher\n");
		if (!versp->vs_dispatch(rqstp, statp)) {
			/* Release reply info */
			if (procp->pc_release)
				procp->pc_release(rqstp, NULL, rqstp->rq_resp);
			goto dropit;
		}
	}

	/* Check RPC status result */
	if (*statp != rpc_success)
		resv->iov_len = ((void*)statp)  - resv->iov_base + 4;

	/* Release reply info */
	if (procp->pc_release)
		procp->pc_release(rqstp, NULL, rqstp->rq_resp);

	if (procp->pc_encode == NULL)
		goto dropit;

 sendit:
	if (svc_authorise(rqstp))
		goto dropit;
	return svc_send(rqstp);

 dropit:
	svc_authorise(rqstp);	/* doesn't hurt to call this twice */
	dprintk("svc: svc_process dropit\n");
	svc_drop(rqstp);
	return 0;

err_short_len:
#ifdef RPC_PARANOIA
	printk("svc: short len %Zd, dropping request\n", argv->iov_len);
#endif
	goto dropit;			/* drop request */

err_bad_dir:
#ifdef RPC_PARANOIA
	printk("svc: bad direction %d, dropping request\n", dir);
#endif
	serv->sv_stats->rpcbadfmt++;
	goto dropit;			/* drop request */

err_bad_rpc:
	serv->sv_stats->rpcbadfmt++;
	svc_putnl(resv, 1);	/* REJECT */
	svc_putnl(resv, 0);	/* RPC_MISMATCH */
	svc_putnl(resv, 2);	/* Only RPCv2 supported */
	svc_putnl(resv, 2);
	goto sendit;

err_bad_auth:
	dprintk("svc: authentication failed (%d)\n", ntohl(auth_stat));
	serv->sv_stats->rpcbadauth++;
	/* Restore write pointer to location of accept status: */
	xdr_ressize_check(rqstp, accept_statp);
	svc_putnl(resv, 1);	/* REJECT */
	svc_putnl(resv, 1);	/* AUTH_ERROR */
	svc_putnl(resv, ntohl(auth_stat));	/* status */
	goto sendit;

err_bad_prog:
	dprintk("svc: unknown program %d\n", prog);
	serv->sv_stats->rpcbadfmt++;
	svc_putnl(resv, RPC_PROG_UNAVAIL);
	goto sendit;

err_bad_vers:
#ifdef RPC_PARANOIA
	printk("svc: unknown version (%d)\n", vers);
#endif
	serv->sv_stats->rpcbadfmt++;
	svc_putnl(resv, RPC_PROG_MISMATCH);
	svc_putnl(resv, progp->pg_lovers);
	svc_putnl(resv, progp->pg_hivers);
	goto sendit;

err_bad_proc:
#ifdef RPC_PARANOIA
	printk("svc: unknown procedure (%d)\n", proc);
#endif
	serv->sv_stats->rpcbadfmt++;
	svc_putnl(resv, RPC_PROC_UNAVAIL);
	goto sendit;

err_garbage:
#ifdef RPC_PARANOIA
	printk("svc: failed to decode args\n");
#endif
	rpc_stat = rpc_garbage_args;
err_bad:
	serv->sv_stats->rpcbadfmt++;
	svc_putnl(resv, ntohl(rpc_stat));
	goto sendit;
}
