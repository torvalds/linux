/*
 * linux/net/sunrpc/svc_xprt.c
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/freezer.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tcp_states.h>
#include <linux/uaccess.h>
#include <asm/ioctls.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svc_xprt.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

/* List of registered transport classes */
static DEFINE_SPINLOCK(svc_xprt_class_lock);
static LIST_HEAD(svc_xprt_class_list);

int svc_reg_xprt_class(struct svc_xprt_class *xcl)
{
	struct svc_xprt_class *cl;
	int res = -EEXIST;

	dprintk("svc: Adding svc transport class '%s'\n", xcl->xcl_name);

	INIT_LIST_HEAD(&xcl->xcl_list);
	spin_lock(&svc_xprt_class_lock);
	/* Make sure there isn't already a class with the same name */
	list_for_each_entry(cl, &svc_xprt_class_list, xcl_list) {
		if (strcmp(xcl->xcl_name, cl->xcl_name) == 0)
			goto out;
	}
	list_add_tail(&xcl->xcl_list, &svc_xprt_class_list);
	res = 0;
out:
	spin_unlock(&svc_xprt_class_lock);
	return res;
}
EXPORT_SYMBOL_GPL(svc_reg_xprt_class);

void svc_unreg_xprt_class(struct svc_xprt_class *xcl)
{
	dprintk("svc: Removing svc transport class '%s'\n", xcl->xcl_name);
	spin_lock(&svc_xprt_class_lock);
	list_del_init(&xcl->xcl_list);
	spin_unlock(&svc_xprt_class_lock);
}
EXPORT_SYMBOL_GPL(svc_unreg_xprt_class);

/*
 * Called by transport drivers to initialize the transport independent
 * portion of the transport instance.
 */
void svc_xprt_init(struct svc_xprt_class *xcl, struct svc_xprt *xprt)
{
	memset(xprt, 0, sizeof(*xprt));
	xprt->xpt_class = xcl;
	xprt->xpt_ops = xcl->xcl_ops;
}
EXPORT_SYMBOL_GPL(svc_xprt_init);

int svc_create_xprt(struct svc_serv *serv, char *xprt_name, unsigned short port,
		    int flags)
{
	struct svc_xprt_class *xcl;
	int ret = -ENOENT;
	struct sockaddr_in sin = {
		.sin_family		= AF_INET,
		.sin_addr.s_addr	= INADDR_ANY,
		.sin_port		= htons(port),
	};
	dprintk("svc: creating transport %s[%d]\n", xprt_name, port);
	spin_lock(&svc_xprt_class_lock);
	list_for_each_entry(xcl, &svc_xprt_class_list, xcl_list) {
		if (strcmp(xprt_name, xcl->xcl_name) == 0) {
			spin_unlock(&svc_xprt_class_lock);
			if (try_module_get(xcl->xcl_owner)) {
				struct svc_xprt *newxprt;
				ret = 0;
				newxprt = xcl->xcl_ops->xpo_create
					(serv,
					 (struct sockaddr *)&sin, sizeof(sin),
					 flags);
				if (IS_ERR(newxprt)) {
					module_put(xcl->xcl_owner);
					ret = PTR_ERR(newxprt);
				}
			}
			goto out;
		}
	}
	spin_unlock(&svc_xprt_class_lock);
	dprintk("svc: transport %s not found\n", xprt_name);
 out:
	return ret;
}
EXPORT_SYMBOL_GPL(svc_create_xprt);
