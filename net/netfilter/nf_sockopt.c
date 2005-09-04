#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/sock.h>

#include "nf_internals.h"

/* Sockopts only registered and called from user context, so
   net locking would be overkill.  Also, [gs]etsockopt calls may
   sleep. */
static DECLARE_MUTEX(nf_sockopt_mutex);
static LIST_HEAD(nf_sockopts);

/* Do exclusive ranges overlap? */
static inline int overlap(int min1, int max1, int min2, int max2)
{
	return max1 > min2 && min1 < max2;
}

/* Functions to register sockopt ranges (exclusive). */
int nf_register_sockopt(struct nf_sockopt_ops *reg)
{
	struct list_head *i;
	int ret = 0;

	if (down_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	list_for_each(i, &nf_sockopts) {
		struct nf_sockopt_ops *ops = (struct nf_sockopt_ops *)i;
		if (ops->pf == reg->pf
		    && (overlap(ops->set_optmin, ops->set_optmax, 
				reg->set_optmin, reg->set_optmax)
			|| overlap(ops->get_optmin, ops->get_optmax, 
				   reg->get_optmin, reg->get_optmax))) {
			NFDEBUG("nf_sock overlap: %u-%u/%u-%u v %u-%u/%u-%u\n",
				ops->set_optmin, ops->set_optmax, 
				ops->get_optmin, ops->get_optmax, 
				reg->set_optmin, reg->set_optmax,
				reg->get_optmin, reg->get_optmax);
			ret = -EBUSY;
			goto out;
		}
	}

	list_add(&reg->list, &nf_sockopts);
out:
	up(&nf_sockopt_mutex);
	return ret;
}
EXPORT_SYMBOL(nf_register_sockopt);

void nf_unregister_sockopt(struct nf_sockopt_ops *reg)
{
	/* No point being interruptible: we're probably in cleanup_module() */
 restart:
	down(&nf_sockopt_mutex);
	if (reg->use != 0) {
		/* To be woken by nf_sockopt call... */
		/* FIXME: Stuart Young's name appears gratuitously. */
		set_current_state(TASK_UNINTERRUPTIBLE);
		reg->cleanup_task = current;
		up(&nf_sockopt_mutex);
		schedule();
		goto restart;
	}
	list_del(&reg->list);
	up(&nf_sockopt_mutex);
}
EXPORT_SYMBOL(nf_unregister_sockopt);

/* Call get/setsockopt() */
static int nf_sockopt(struct sock *sk, int pf, int val, 
		      char __user *opt, int *len, int get)
{
	struct list_head *i;
	struct nf_sockopt_ops *ops;
	int ret;

	if (down_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	list_for_each(i, &nf_sockopts) {
		ops = (struct nf_sockopt_ops *)i;
		if (ops->pf == pf) {
			if (get) {
				if (val >= ops->get_optmin
				    && val < ops->get_optmax) {
					ops->use++;
					up(&nf_sockopt_mutex);
					ret = ops->get(sk, val, opt, len);
					goto out;
				}
			} else {
				if (val >= ops->set_optmin
				    && val < ops->set_optmax) {
					ops->use++;
					up(&nf_sockopt_mutex);
					ret = ops->set(sk, val, opt, *len);
					goto out;
				}
			}
		}
	}
	up(&nf_sockopt_mutex);
	return -ENOPROTOOPT;
	
 out:
	down(&nf_sockopt_mutex);
	ops->use--;
	if (ops->cleanup_task)
		wake_up_process(ops->cleanup_task);
	up(&nf_sockopt_mutex);
	return ret;
}

int nf_setsockopt(struct sock *sk, int pf, int val, char __user *opt,
		  int len)
{
	return nf_sockopt(sk, pf, val, opt, &len, 0);
}
EXPORT_SYMBOL(nf_setsockopt);

int nf_getsockopt(struct sock *sk, int pf, int val, char __user *opt, int *len)
{
	return nf_sockopt(sk, pf, val, opt, len, 1);
}
EXPORT_SYMBOL(nf_getsockopt);

