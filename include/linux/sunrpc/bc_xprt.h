/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************

(c) 2008 NetApp.  All Rights Reserved.


******************************************************************************/

/*
 * Functions to create and manage the backchannel
 */

#ifndef _LINUX_SUNRPC_BC_XPRT_H
#define _LINUX_SUNRPC_BC_XPRT_H

#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>

#ifdef CONFIG_SUNRPC_BACKCHANNEL
struct rpc_rqst *xprt_lookup_bc_request(struct rpc_xprt *xprt, __be32 xid);
void xprt_complete_bc_request(struct rpc_rqst *req, uint32_t copied);
void xprt_init_bc_request(struct rpc_rqst *req, struct rpc_task *task);
void xprt_free_bc_request(struct rpc_rqst *req);
int xprt_setup_backchannel(struct rpc_xprt *, unsigned int min_reqs);
void xprt_destroy_backchannel(struct rpc_xprt *, unsigned int max_reqs);

/* Socket backchannel transport methods */
int xprt_setup_bc(struct rpc_xprt *xprt, unsigned int min_reqs);
void xprt_destroy_bc(struct rpc_xprt *xprt, unsigned int max_reqs);
void xprt_free_bc_rqst(struct rpc_rqst *req);
unsigned int xprt_bc_max_slots(struct rpc_xprt *xprt);

/*
 * Determine if a shared backchannel is in use
 */
static inline bool svc_is_backchannel(const struct svc_rqst *rqstp)
{
	return rqstp->rq_server->sv_bc_enabled;
}

static inline void set_bc_enabled(struct svc_serv *serv)
{
	serv->sv_bc_enabled = true;
}
#else /* CONFIG_SUNRPC_BACKCHANNEL */
static inline int xprt_setup_backchannel(struct rpc_xprt *xprt,
					 unsigned int min_reqs)
{
	return 0;
}

static inline void xprt_destroy_backchannel(struct rpc_xprt *xprt,
					    unsigned int max_reqs)
{
}

static inline bool svc_is_backchannel(const struct svc_rqst *rqstp)
{
	return false;
}

static inline void set_bc_enabled(struct svc_serv *serv)
{
}

static inline void xprt_free_bc_request(struct rpc_rqst *req)
{
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */
#endif /* _LINUX_SUNRPC_BC_XPRT_H */
