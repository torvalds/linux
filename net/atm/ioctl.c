/* ATM ioctl handling */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
/* 2003 John Levon  <levon@movementarian.org> */


#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/net.h>		/* struct socket, struct proto_ops */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmdev.h>
#include <linux/atmclip.h>	/* CLIP_*ENCAP */
#include <linux/atmarp.h>	/* manifest constants */
#include <linux/capability.h>
#include <linux/sonet.h>	/* for ioctls */
#include <linux/atmsvc.h>
#include <linux/atmmpc.h>
#include <net/atmclip.h>
#include <linux/atmlec.h>
#include <linux/mutex.h>
#include <asm/ioctls.h>
#include <net/compat.h>

#include "resources.h"
#include "signaling.h"		/* for WAITING and sigd_attach */
#include "common.h"


static DEFINE_MUTEX(ioctl_mutex);
static LIST_HEAD(ioctl_list);


void register_atm_ioctl(struct atm_ioctl *ioctl)
{
	mutex_lock(&ioctl_mutex);
	list_add_tail(&ioctl->list, &ioctl_list);
	mutex_unlock(&ioctl_mutex);
}

void deregister_atm_ioctl(struct atm_ioctl *ioctl)
{
	mutex_lock(&ioctl_mutex);
	list_del(&ioctl->list);
	mutex_unlock(&ioctl_mutex);
}

EXPORT_SYMBOL(register_atm_ioctl);
EXPORT_SYMBOL(deregister_atm_ioctl);

static int do_vcc_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg, int compat)
{
	struct sock *sk = sock->sk;
	struct atm_vcc *vcc;
	int error;
	struct list_head * pos;
	void __user *argp = (void __user *)arg;

	vcc = ATM_SD(sock);
	switch (cmd) {
		case SIOCOUTQ:
			if (sock->state != SS_CONNECTED ||
			    !test_bit(ATM_VF_READY, &vcc->flags)) {
				error =  -EINVAL;
				goto done;
			}
			error = put_user(sk->sk_sndbuf -
					 atomic_read(&sk->sk_wmem_alloc),
					 (int __user *) argp) ? -EFAULT : 0;
			goto done;
		case SIOCINQ:
			{
				struct sk_buff *skb;

				if (sock->state != SS_CONNECTED) {
					error = -EINVAL;
					goto done;
				}
				skb = skb_peek(&sk->sk_receive_queue);
				error = put_user(skb ? skb->len : 0,
						 (int __user *)argp) ? -EFAULT : 0;
				goto done;
			}
		case SIOCGSTAMP: /* borrowed from IP */
#ifdef CONFIG_COMPAT
			if (compat)
				error = compat_sock_get_timestamp(sk, argp);
			else
#endif
				error = sock_get_timestamp(sk, argp);
			goto done;
		case SIOCGSTAMPNS: /* borrowed from IP */
#ifdef CONFIG_COMPAT
			if (compat)
				error = compat_sock_get_timestampns(sk, argp);
			else
#endif
				error = sock_get_timestampns(sk, argp);
			goto done;
		case ATM_SETSC:
			if (net_ratelimit())
				printk(KERN_WARNING "ATM_SETSC is obsolete; used by %s:%d\n",
				       current->comm, task_pid_nr(current));
			error = 0;
			goto done;
		case ATMSIGD_CTRL:
			if (!capable(CAP_NET_ADMIN)) {
				error = -EPERM;
				goto done;
			}
			/*
			 * The user/kernel protocol for exchanging signalling
			 * info uses kernel pointers as opaque references,
			 * so the holder of the file descriptor can scribble
			 * on the kernel... so we should make sure that we
			 * have the same privileges that /proc/kcore needs
			 */
			if (!capable(CAP_SYS_RAWIO)) {
				error = -EPERM;
				goto done;
			}
#ifdef CONFIG_COMPAT
			/* WTF? I don't even want to _think_ about making this
			   work for 32-bit userspace. TBH I don't really want
			   to think about it at all. dwmw2. */
			if (compat) {
				if (net_ratelimit())
					printk(KERN_WARNING "32-bit task cannot be atmsigd\n");
				error = -EINVAL;
				goto done;
			}
#endif
			error = sigd_attach(vcc);
			if (!error)
				sock->state = SS_CONNECTED;
			goto done;
		case ATM_SETBACKEND:
		case ATM_NEWBACKENDIF:
			{
				atm_backend_t backend;
				error = get_user(backend, (atm_backend_t __user *) argp);
				if (error)
					goto done;
				switch (backend) {
					case ATM_BACKEND_PPP:
						request_module("pppoatm");
						break;
					case ATM_BACKEND_BR2684:
						request_module("br2684");
						break;
				}
			}
			break;
		case ATMMPC_CTRL:
		case ATMMPC_DATA:
			request_module("mpoa");
			break;
		case ATMARPD_CTRL:
			request_module("clip");
			break;
		case ATMLEC_CTRL:
			request_module("lec");
			break;
	}

	error = -ENOIOCTLCMD;

	mutex_lock(&ioctl_mutex);
	list_for_each(pos, &ioctl_list) {
		struct atm_ioctl * ic = list_entry(pos, struct atm_ioctl, list);
		if (try_module_get(ic->owner)) {
			error = ic->ioctl(sock, cmd, arg);
			module_put(ic->owner);
			if (error != -ENOIOCTLCMD)
				break;
		}
	}
	mutex_unlock(&ioctl_mutex);

	if (error != -ENOIOCTLCMD)
		goto done;

	error = atm_dev_ioctl(cmd, argp, compat);

done:
	return error;
}


int vcc_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return do_vcc_ioctl(sock, cmd, arg, 0);
}

#ifdef CONFIG_COMPAT
int vcc_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return do_vcc_ioctl(sock, cmd, arg, 1);
}
#endif
