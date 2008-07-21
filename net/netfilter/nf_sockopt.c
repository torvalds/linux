#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/mutex.h>
#include <net/sock.h>

#include "nf_internals.h"

/* Sockopts only registered and called from user context, so
   net locking would be overkill.  Also, [gs]etsockopt calls may
   sleep. */
static DEFINE_MUTEX(nf_sockopt_mutex);
static LIST_HEAD(nf_sockopts);

/* Do exclusive ranges overlap? */
static inline int overlap(int min1, int max1, int min2, int max2)
{
	return max1 > min2 && min1 < max2;
}

/* Functions to register sockopt ranges (exclusive). */
int nf_register_sockopt(struct nf_sockopt_ops *reg)
{
	struct nf_sockopt_ops *ops;
	int ret = 0;

	if (mutex_lock_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	list_for_each_entry(ops, &nf_sockopts, list) {
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
	mutex_unlock(&nf_sockopt_mutex);
	return ret;
}
EXPORT_SYMBOL(nf_register_sockopt);

void nf_unregister_sockopt(struct nf_sockopt_ops *reg)
{
	mutex_lock(&nf_sockopt_mutex);
	list_del(&reg->list);
	mutex_unlock(&nf_sockopt_mutex);
}
EXPORT_SYMBOL(nf_unregister_sockopt);

static struct nf_sockopt_ops *nf_sockopt_find(struct sock *sk, int pf,
		int val, int get)
{
	struct nf_sockopt_ops *ops;

	if (!net_eq(sock_net(sk), &init_net))
		return ERR_PTR(-ENOPROTOOPT);

	if (mutex_lock_interruptible(&nf_sockopt_mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(ops, &nf_sockopts, list) {
		if (ops->pf == pf) {
			if (!try_module_get(ops->owner))
				goto out_nosup;

			if (get) {
				if (val >= ops->get_optmin &&
						val < ops->get_optmax)
					goto out;
			} else {
				if (val >= ops->set_optmin &&
						val < ops->set_optmax)
					goto out;
			}
			module_put(ops->owner);
		}
	}
out_nosup:
	ops = ERR_PTR(-ENOPROTOOPT);
out:
	mutex_unlock(&nf_sockopt_mutex);
	return ops;
}

/* Call get/setsockopt() */
static int nf_sockopt(struct sock *sk, int pf, int val,
		      char __user *opt, int *len, int get)
{
	struct nf_sockopt_ops *ops;
	int ret;

	ops = nf_sockopt_find(sk, pf, val, get);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	if (get)
		ret = ops->get(sk, val, opt, len);
	else
		ret = ops->set(sk, val, opt, *len);

	module_put(ops->owner);
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

#ifdef CONFIG_COMPAT
static int compat_nf_sockopt(struct sock *sk, int pf, int val,
			     char __user *opt, int *len, int get)
{
	struct nf_sockopt_ops *ops;
	int ret;

	ops = nf_sockopt_find(sk, pf, val, get);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	if (get) {
		if (ops->compat_get)
			ret = ops->compat_get(sk, val, opt, len);
		else
			ret = ops->get(sk, val, opt, len);
	} else {
		if (ops->compat_set)
			ret = ops->compat_set(sk, val, opt, *len);
		else
			ret = ops->set(sk, val, opt, *len);
	}

	module_put(ops->owner);
	return ret;
}

int compat_nf_setsockopt(struct sock *sk, int pf,
		int val, char __user *opt, int len)
{
	return compat_nf_sockopt(sk, pf, val, opt, &len, 0);
}
EXPORT_SYMBOL(compat_nf_setsockopt);

int compat_nf_getsockopt(struct sock *sk, int pf,
		int val, char __user *opt, int *len)
{
	return compat_nf_sockopt(sk, pf, val, opt, len, 1);
}
EXPORT_SYMBOL(compat_nf_getsockopt);
#endif
