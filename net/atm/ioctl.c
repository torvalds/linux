/* ATM ioctl handling */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
/* 2003 John Levon  <levon@movementarian.org> */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

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
EXPORT_SYMBOL(register_atm_ioctl);

void deregister_atm_ioctl(struct atm_ioctl *ioctl)
{
	mutex_lock(&ioctl_mutex);
	list_del(&ioctl->list);
	mutex_unlock(&ioctl_mutex);
}
EXPORT_SYMBOL(deregister_atm_ioctl);

static int do_vcc_ioctl(struct socket *sock, unsigned int cmd,
			unsigned long arg, int compat)
{
	struct sock *sk = sock->sk;
	struct atm_vcc *vcc;
	int error;
	struct list_head *pos;
	void __user *argp = (void __user *)arg;

	vcc = ATM_SD(sock);
	switch (cmd) {
	case SIOCOUTQ:
		if (sock->state != SS_CONNECTED ||
		    !test_bit(ATM_VF_READY, &vcc->flags)) {
			error =  -EINVAL;
			goto done;
		}
		error = put_user(sk->sk_sndbuf - sk_wmem_alloc_get(sk),
				 (int __user *)argp) ? -EFAULT : 0;
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
		net_warn_ratelimited("ATM_SETSC is obsolete; used by %s:%d\n",
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
			net_warn_ratelimited("32-bit task cannot be atmsigd\n");
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
		error = get_user(backend, (atm_backend_t __user *)argp);
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
		break;
	}
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
		struct atm_ioctl *ic = list_entry(pos, struct atm_ioctl, list);
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
/*
 * FIXME:
 * The compat_ioctl handling is duplicated, using both these conversion
 * routines and the compat argument to the actual handlers. Both
 * versions are somewhat incomplete and should be merged, e.g. by
 * moving the ioctl number translation into the actual handlers and
 * killing the conversion code.
 *
 * -arnd, November 2009
 */
#define ATM_GETLINKRATE32 _IOW('a', ATMIOC_ITF+1, struct compat_atmif_sioc)
#define ATM_GETNAMES32    _IOW('a', ATMIOC_ITF+3, struct compat_atm_iobuf)
#define ATM_GETTYPE32     _IOW('a', ATMIOC_ITF+4, struct compat_atmif_sioc)
#define ATM_GETESI32	  _IOW('a', ATMIOC_ITF+5, struct compat_atmif_sioc)
#define ATM_GETADDR32	  _IOW('a', ATMIOC_ITF+6, struct compat_atmif_sioc)
#define ATM_RSTADDR32	  _IOW('a', ATMIOC_ITF+7, struct compat_atmif_sioc)
#define ATM_ADDADDR32	  _IOW('a', ATMIOC_ITF+8, struct compat_atmif_sioc)
#define ATM_DELADDR32	  _IOW('a', ATMIOC_ITF+9, struct compat_atmif_sioc)
#define ATM_GETCIRANGE32  _IOW('a', ATMIOC_ITF+10, struct compat_atmif_sioc)
#define ATM_SETCIRANGE32  _IOW('a', ATMIOC_ITF+11, struct compat_atmif_sioc)
#define ATM_SETESI32      _IOW('a', ATMIOC_ITF+12, struct compat_atmif_sioc)
#define ATM_SETESIF32     _IOW('a', ATMIOC_ITF+13, struct compat_atmif_sioc)
#define ATM_GETSTAT32     _IOW('a', ATMIOC_SARCOM+0, struct compat_atmif_sioc)
#define ATM_GETSTATZ32    _IOW('a', ATMIOC_SARCOM+1, struct compat_atmif_sioc)
#define ATM_GETLOOP32	  _IOW('a', ATMIOC_SARCOM+2, struct compat_atmif_sioc)
#define ATM_SETLOOP32	  _IOW('a', ATMIOC_SARCOM+3, struct compat_atmif_sioc)
#define ATM_QUERYLOOP32	  _IOW('a', ATMIOC_SARCOM+4, struct compat_atmif_sioc)

static struct {
	unsigned int cmd32;
	unsigned int cmd;
} atm_ioctl_map[] = {
	{ ATM_GETLINKRATE32, ATM_GETLINKRATE },
	{ ATM_GETNAMES32,    ATM_GETNAMES },
	{ ATM_GETTYPE32,     ATM_GETTYPE },
	{ ATM_GETESI32,	     ATM_GETESI },
	{ ATM_GETADDR32,     ATM_GETADDR },
	{ ATM_RSTADDR32,     ATM_RSTADDR },
	{ ATM_ADDADDR32,     ATM_ADDADDR },
	{ ATM_DELADDR32,     ATM_DELADDR },
	{ ATM_GETCIRANGE32,  ATM_GETCIRANGE },
	{ ATM_SETCIRANGE32,  ATM_SETCIRANGE },
	{ ATM_SETESI32,	     ATM_SETESI },
	{ ATM_SETESIF32,     ATM_SETESIF },
	{ ATM_GETSTAT32,     ATM_GETSTAT },
	{ ATM_GETSTATZ32,    ATM_GETSTATZ },
	{ ATM_GETLOOP32,     ATM_GETLOOP },
	{ ATM_SETLOOP32,     ATM_SETLOOP },
	{ ATM_QUERYLOOP32,   ATM_QUERYLOOP },
};

#define NR_ATM_IOCTL ARRAY_SIZE(atm_ioctl_map)

static int do_atm_iobuf(struct socket *sock, unsigned int cmd,
			unsigned long arg)
{
	struct atm_iobuf __user *iobuf;
	struct compat_atm_iobuf __user *iobuf32;
	u32 data;
	void __user *datap;
	int len, err;

	iobuf = compat_alloc_user_space(sizeof(*iobuf));
	iobuf32 = compat_ptr(arg);

	if (get_user(len, &iobuf32->length) ||
	    get_user(data, &iobuf32->buffer))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(len, &iobuf->length) ||
	    put_user(datap, &iobuf->buffer))
		return -EFAULT;

	err = do_vcc_ioctl(sock, cmd, (unsigned long) iobuf, 0);

	if (!err) {
		if (copy_in_user(&iobuf32->length, &iobuf->length,
				 sizeof(int)))
			err = -EFAULT;
	}

	return err;
}

static int do_atmif_sioc(struct socket *sock, unsigned int cmd,
			 unsigned long arg)
{
	struct atmif_sioc __user *sioc;
	struct compat_atmif_sioc __user *sioc32;
	u32 data;
	void __user *datap;
	int err;

	sioc = compat_alloc_user_space(sizeof(*sioc));
	sioc32 = compat_ptr(arg);

	if (copy_in_user(&sioc->number, &sioc32->number, 2 * sizeof(int)) ||
	    get_user(data, &sioc32->arg))
		return -EFAULT;
	datap = compat_ptr(data);
	if (put_user(datap, &sioc->arg))
		return -EFAULT;

	err = do_vcc_ioctl(sock, cmd, (unsigned long) sioc, 0);

	if (!err) {
		if (copy_in_user(&sioc32->length, &sioc->length,
				 sizeof(int)))
			err = -EFAULT;
	}
	return err;
}

static int do_atm_ioctl(struct socket *sock, unsigned int cmd32,
			unsigned long arg)
{
	int i;
	unsigned int cmd = 0;

	switch (cmd32) {
	case SONET_GETSTAT:
	case SONET_GETSTATZ:
	case SONET_GETDIAG:
	case SONET_SETDIAG:
	case SONET_CLRDIAG:
	case SONET_SETFRAMING:
	case SONET_GETFRAMING:
	case SONET_GETFRSENSE:
		return do_atmif_sioc(sock, cmd32, arg);
	}

	for (i = 0; i < NR_ATM_IOCTL; i++) {
		if (cmd32 == atm_ioctl_map[i].cmd32) {
			cmd = atm_ioctl_map[i].cmd;
			break;
		}
	}
	if (i == NR_ATM_IOCTL)
		return -EINVAL;

	switch (cmd) {
	case ATM_GETNAMES:
		return do_atm_iobuf(sock, cmd, arg);

	case ATM_GETLINKRATE:
	case ATM_GETTYPE:
	case ATM_GETESI:
	case ATM_GETADDR:
	case ATM_RSTADDR:
	case ATM_ADDADDR:
	case ATM_DELADDR:
	case ATM_GETCIRANGE:
	case ATM_SETCIRANGE:
	case ATM_SETESI:
	case ATM_SETESIF:
	case ATM_GETSTAT:
	case ATM_GETSTATZ:
	case ATM_GETLOOP:
	case ATM_SETLOOP:
	case ATM_QUERYLOOP:
		return do_atmif_sioc(sock, cmd, arg);
	}

	return -EINVAL;
}

int vcc_compat_ioctl(struct socket *sock, unsigned int cmd,
		     unsigned long arg)
{
	int ret;

	ret = do_vcc_ioctl(sock, cmd, arg, 1);
	if (ret != -ENOIOCTLCMD)
		return ret;

	return do_atm_ioctl(sock, cmd, arg);
}
#endif
