/*
 * linux/include/linux/sunrpc/svc_xprt.h
 *
 * RPC server transport I/O
 */

#ifndef SUNRPC_SVC_XPRT_H
#define SUNRPC_SVC_XPRT_H

#include <linux/sunrpc/svc.h>

struct svc_xprt_ops {
};

struct svc_xprt_class {
	const char		*xcl_name;
	struct module		*xcl_owner;
	struct svc_xprt_ops	*xcl_ops;
	struct list_head	xcl_list;
	u32			xcl_max_payload;
};

struct svc_xprt {
	struct svc_xprt_class	*xpt_class;
	struct svc_xprt_ops	*xpt_ops;
};

int	svc_reg_xprt_class(struct svc_xprt_class *);
void	svc_unreg_xprt_class(struct svc_xprt_class *);
void	svc_xprt_init(struct svc_xprt_class *, struct svc_xprt *);

#endif /* SUNRPC_SVC_XPRT_H */
