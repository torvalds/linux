/* net/atm/common.c - ATM sockets (common part for PVC and SVC) */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/net.h>		/* struct socket, struct proto_ops */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmdev.h>
#include <linux/socket.h>	/* SOL_SOCKET */
#include <linux/errno.h>	/* error codes */
#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/time.h>		/* struct timeval */
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <net/sock.h>		/* struct sock */

#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <asm/poll.h>


#include "resources.h"		/* atm_find_dev */
#include "common.h"		/* prototypes */
#include "protocols.h"		/* atm_init_<transport> */
#include "addr.h"		/* address registry */
#include "signaling.h"		/* for WAITING and sigd_attach */

struct hlist_head vcc_hash[VCC_HTABLE_SIZE];
DEFINE_RWLOCK(vcc_sklist_lock);

static void __vcc_insert_socket(struct sock *sk)
{
	struct atm_vcc *vcc = atm_sk(sk);
	struct hlist_head *head = &vcc_hash[vcc->vci &
					(VCC_HTABLE_SIZE - 1)];
	sk->sk_hash = vcc->vci & (VCC_HTABLE_SIZE - 1);
	sk_add_node(sk, head);
}

void vcc_insert_socket(struct sock *sk)
{
	write_lock_irq(&vcc_sklist_lock);
	__vcc_insert_socket(sk);
	write_unlock_irq(&vcc_sklist_lock);
}

static void vcc_remove_socket(struct sock *sk)
{
	write_lock_irq(&vcc_sklist_lock);
	sk_del_node_init(sk);
	write_unlock_irq(&vcc_sklist_lock);
}


static struct sk_buff *alloc_tx(struct atm_vcc *vcc,unsigned int size)
{
	struct sk_buff *skb;
	struct sock *sk = sk_atm(vcc);

	if (atomic_read(&sk->sk_wmem_alloc) && !atm_may_send(vcc, size)) {
		pr_debug("Sorry: wmem_alloc = %d, size = %d, sndbuf = %d\n",
			atomic_read(&sk->sk_wmem_alloc), size,
			sk->sk_sndbuf);
		return NULL;
	}
	while (!(skb = alloc_skb(size,GFP_KERNEL))) schedule();
	pr_debug("AlTx %d += %d\n", atomic_read(&sk->sk_wmem_alloc),
		skb->truesize);
	atomic_add(skb->truesize, &sk->sk_wmem_alloc);
	return skb;
}


EXPORT_SYMBOL(vcc_hash);
EXPORT_SYMBOL(vcc_sklist_lock);
EXPORT_SYMBOL(vcc_insert_socket);

static void vcc_sock_destruct(struct sock *sk)
{
	if (atomic_read(&sk->sk_rmem_alloc))
		printk(KERN_DEBUG "vcc_sock_destruct: rmem leakage (%d bytes) detected.\n", atomic_read(&sk->sk_rmem_alloc));

	if (atomic_read(&sk->sk_wmem_alloc))
		printk(KERN_DEBUG "vcc_sock_destruct: wmem leakage (%d bytes) detected.\n", atomic_read(&sk->sk_wmem_alloc));
}

static void vcc_def_wakeup(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);
	if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
		wake_up(sk->sk_sleep);
	read_unlock(&sk->sk_callback_lock);
}

static inline int vcc_writable(struct sock *sk)
{
	struct atm_vcc *vcc = atm_sk(sk);

	return (vcc->qos.txtp.max_sdu +
		atomic_read(&sk->sk_wmem_alloc)) <= sk->sk_sndbuf;
}

static void vcc_write_space(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);

	if (vcc_writable(sk)) {
		if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
			wake_up_interruptible(sk->sk_sleep);

		sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	}

	read_unlock(&sk->sk_callback_lock);
}

static struct proto vcc_proto = {
	.name	  = "VCC",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct atm_vcc),
};

int vcc_create(struct net *net, struct socket *sock, int protocol, int family)
{
	struct sock *sk;
	struct atm_vcc *vcc;

	sock->sk = NULL;
	if (sock->type == SOCK_STREAM)
		return -EINVAL;
	sk = sk_alloc(net, family, GFP_KERNEL, &vcc_proto);
	if (!sk)
		return -ENOMEM;
	sock_init_data(sock, sk);
	sk->sk_state_change = vcc_def_wakeup;
	sk->sk_write_space = vcc_write_space;

	vcc = atm_sk(sk);
	vcc->dev = NULL;
	memset(&vcc->local,0,sizeof(struct sockaddr_atmsvc));
	memset(&vcc->remote,0,sizeof(struct sockaddr_atmsvc));
	vcc->qos.txtp.max_sdu = 1 << 16; /* for meta VCs */
	atomic_set(&sk->sk_wmem_alloc, 0);
	atomic_set(&sk->sk_rmem_alloc, 0);
	vcc->push = NULL;
	vcc->pop = NULL;
	vcc->push_oam = NULL;
	vcc->vpi = vcc->vci = 0; /* no VCI/VPI yet */
	vcc->atm_options = vcc->aal_options = 0;
	sk->sk_destruct = vcc_sock_destruct;
	return 0;
}


static void vcc_destroy_socket(struct sock *sk)
{
	struct atm_vcc *vcc = atm_sk(sk);
	struct sk_buff *skb;

	set_bit(ATM_VF_CLOSE, &vcc->flags);
	clear_bit(ATM_VF_READY, &vcc->flags);
	if (vcc->dev) {
		if (vcc->dev->ops->close)
			vcc->dev->ops->close(vcc);
		if (vcc->push)
			vcc->push(vcc, NULL); /* atmarpd has no push */

		while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
			atm_return(vcc,skb->truesize);
			kfree_skb(skb);
		}

		module_put(vcc->dev->ops->owner);
		atm_dev_put(vcc->dev);
	}

	vcc_remove_socket(sk);
}


int vcc_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		lock_sock(sk);
		vcc_destroy_socket(sock->sk);
		release_sock(sk);
		sock_put(sk);
	}

	return 0;
}


void vcc_release_async(struct atm_vcc *vcc, int reply)
{
	struct sock *sk = sk_atm(vcc);

	set_bit(ATM_VF_CLOSE, &vcc->flags);
	sk->sk_shutdown |= RCV_SHUTDOWN;
	sk->sk_err = -reply;
	clear_bit(ATM_VF_WAITING, &vcc->flags);
	sk->sk_state_change(sk);
}


EXPORT_SYMBOL(vcc_release_async);


void atm_dev_release_vccs(struct atm_dev *dev)
{
	int i;

	write_lock_irq(&vcc_sklist_lock);
	for (i = 0; i < VCC_HTABLE_SIZE; i++) {
		struct hlist_head *head = &vcc_hash[i];
		struct hlist_node *node, *tmp;
		struct sock *s;
		struct atm_vcc *vcc;

		sk_for_each_safe(s, node, tmp, head) {
			vcc = atm_sk(s);
			if (vcc->dev == dev) {
				vcc_release_async(vcc, -EPIPE);
				sk_del_node_init(s);
			}
		}
	}
	write_unlock_irq(&vcc_sklist_lock);
}


static int adjust_tp(struct atm_trafprm *tp,unsigned char aal)
{
	int max_sdu;

	if (!tp->traffic_class) return 0;
	switch (aal) {
		case ATM_AAL0:
			max_sdu = ATM_CELL_SIZE-1;
			break;
		case ATM_AAL34:
			max_sdu = ATM_MAX_AAL34_PDU;
			break;
		default:
			printk(KERN_WARNING "ATM: AAL problems ... "
			    "(%d)\n",aal);
			/* fall through */
		case ATM_AAL5:
			max_sdu = ATM_MAX_AAL5_PDU;
	}
	if (!tp->max_sdu) tp->max_sdu = max_sdu;
	else if (tp->max_sdu > max_sdu) return -EINVAL;
	if (!tp->max_cdv) tp->max_cdv = ATM_MAX_CDV;
	return 0;
}


static int check_ci(struct atm_vcc *vcc, short vpi, int vci)
{
	struct hlist_head *head = &vcc_hash[vci &
					(VCC_HTABLE_SIZE - 1)];
	struct hlist_node *node;
	struct sock *s;
	struct atm_vcc *walk;

	sk_for_each(s, node, head) {
		walk = atm_sk(s);
		if (walk->dev != vcc->dev)
			continue;
		if (test_bit(ATM_VF_ADDR, &walk->flags) && walk->vpi == vpi &&
		    walk->vci == vci && ((walk->qos.txtp.traffic_class !=
		    ATM_NONE && vcc->qos.txtp.traffic_class != ATM_NONE) ||
		    (walk->qos.rxtp.traffic_class != ATM_NONE &&
		    vcc->qos.rxtp.traffic_class != ATM_NONE)))
			return -EADDRINUSE;
	}

	/* allow VCCs with same VPI/VCI iff they don't collide on
	   TX/RX (but we may refuse such sharing for other reasons,
	   e.g. if protocol requires to have both channels) */

	return 0;
}


static int find_ci(struct atm_vcc *vcc, short *vpi, int *vci)
{
	static short p;        /* poor man's per-device cache */
	static int c;
	short old_p;
	int old_c;
	int err;

	if (*vpi != ATM_VPI_ANY && *vci != ATM_VCI_ANY) {
		err = check_ci(vcc, *vpi, *vci);
		return err;
	}
	/* last scan may have left values out of bounds for current device */
	if (*vpi != ATM_VPI_ANY)
		p = *vpi;
	else if (p >= 1 << vcc->dev->ci_range.vpi_bits)
		p = 0;
	if (*vci != ATM_VCI_ANY)
		c = *vci;
	else if (c < ATM_NOT_RSV_VCI || c >= 1 << vcc->dev->ci_range.vci_bits)
			c = ATM_NOT_RSV_VCI;
	old_p = p;
	old_c = c;
	do {
		if (!check_ci(vcc, p, c)) {
			*vpi = p;
			*vci = c;
			return 0;
		}
		if (*vci == ATM_VCI_ANY) {
			c++;
			if (c >= 1 << vcc->dev->ci_range.vci_bits)
				c = ATM_NOT_RSV_VCI;
		}
		if ((c == ATM_NOT_RSV_VCI || *vci != ATM_VCI_ANY) &&
		    *vpi == ATM_VPI_ANY) {
			p++;
			if (p >= 1 << vcc->dev->ci_range.vpi_bits) p = 0;
		}
	}
	while (old_p != p || old_c != c);
	return -EADDRINUSE;
}


static int __vcc_connect(struct atm_vcc *vcc, struct atm_dev *dev, short vpi,
			 int vci)
{
	struct sock *sk = sk_atm(vcc);
	int error;

	if ((vpi != ATM_VPI_UNSPEC && vpi != ATM_VPI_ANY &&
	    vpi >> dev->ci_range.vpi_bits) || (vci != ATM_VCI_UNSPEC &&
	    vci != ATM_VCI_ANY && vci >> dev->ci_range.vci_bits))
		return -EINVAL;
	if (vci > 0 && vci < ATM_NOT_RSV_VCI && !capable(CAP_NET_BIND_SERVICE))
		return -EPERM;
	error = -ENODEV;
	if (!try_module_get(dev->ops->owner))
		return error;
	vcc->dev = dev;
	write_lock_irq(&vcc_sklist_lock);
	if (test_bit(ATM_DF_REMOVED, &dev->flags) ||
	    (error = find_ci(vcc, &vpi, &vci))) {
		write_unlock_irq(&vcc_sklist_lock);
		goto fail_module_put;
	}
	vcc->vpi = vpi;
	vcc->vci = vci;
	__vcc_insert_socket(sk);
	write_unlock_irq(&vcc_sklist_lock);
	switch (vcc->qos.aal) {
		case ATM_AAL0:
			error = atm_init_aal0(vcc);
			vcc->stats = &dev->stats.aal0;
			break;
		case ATM_AAL34:
			error = atm_init_aal34(vcc);
			vcc->stats = &dev->stats.aal34;
			break;
		case ATM_NO_AAL:
			/* ATM_AAL5 is also used in the "0 for default" case */
			vcc->qos.aal = ATM_AAL5;
			/* fall through */
		case ATM_AAL5:
			error = atm_init_aal5(vcc);
			vcc->stats = &dev->stats.aal5;
			break;
		default:
			error = -EPROTOTYPE;
	}
	if (!error) error = adjust_tp(&vcc->qos.txtp,vcc->qos.aal);
	if (!error) error = adjust_tp(&vcc->qos.rxtp,vcc->qos.aal);
	if (error)
		goto fail;
	pr_debug("VCC %d.%d, AAL %d\n",vpi,vci,vcc->qos.aal);
	pr_debug("  TX: %d, PCR %d..%d, SDU %d\n",vcc->qos.txtp.traffic_class,
	    vcc->qos.txtp.min_pcr,vcc->qos.txtp.max_pcr,vcc->qos.txtp.max_sdu);
	pr_debug("  RX: %d, PCR %d..%d, SDU %d\n",vcc->qos.rxtp.traffic_class,
	    vcc->qos.rxtp.min_pcr,vcc->qos.rxtp.max_pcr,vcc->qos.rxtp.max_sdu);

	if (dev->ops->open) {
		if ((error = dev->ops->open(vcc)))
			goto fail;
	}
	return 0;

fail:
	vcc_remove_socket(sk);
fail_module_put:
	module_put(dev->ops->owner);
	/* ensure we get dev module ref count correct */
	vcc->dev = NULL;
	return error;
}


int vcc_connect(struct socket *sock, int itf, short vpi, int vci)
{
	struct atm_dev *dev;
	struct atm_vcc *vcc = ATM_SD(sock);
	int error;

	pr_debug("vcc_connect (vpi %d, vci %d)\n",vpi,vci);
	if (sock->state == SS_CONNECTED)
		return -EISCONN;
	if (sock->state != SS_UNCONNECTED)
		return -EINVAL;
	if (!(vpi || vci))
		return -EINVAL;

	if (vpi != ATM_VPI_UNSPEC && vci != ATM_VCI_UNSPEC)
		clear_bit(ATM_VF_PARTIAL,&vcc->flags);
	else
		if (test_bit(ATM_VF_PARTIAL,&vcc->flags))
			return -EINVAL;
	pr_debug("vcc_connect (TX: cl %d,bw %d-%d,sdu %d; "
	    "RX: cl %d,bw %d-%d,sdu %d,AAL %s%d)\n",
	    vcc->qos.txtp.traffic_class,vcc->qos.txtp.min_pcr,
	    vcc->qos.txtp.max_pcr,vcc->qos.txtp.max_sdu,
	    vcc->qos.rxtp.traffic_class,vcc->qos.rxtp.min_pcr,
	    vcc->qos.rxtp.max_pcr,vcc->qos.rxtp.max_sdu,
	    vcc->qos.aal == ATM_AAL5 ? "" : vcc->qos.aal == ATM_AAL0 ? "" :
	    " ??? code ",vcc->qos.aal == ATM_AAL0 ? 0 : vcc->qos.aal);
	if (!test_bit(ATM_VF_HASQOS, &vcc->flags))
		return -EBADFD;
	if (vcc->qos.txtp.traffic_class == ATM_ANYCLASS ||
	    vcc->qos.rxtp.traffic_class == ATM_ANYCLASS)
		return -EINVAL;
	if (likely(itf != ATM_ITF_ANY)) {
		dev = try_then_request_module(atm_dev_lookup(itf), "atm-device-%d", itf);
	} else {
		dev = NULL;
		mutex_lock(&atm_dev_mutex);
		if (!list_empty(&atm_devs)) {
			dev = list_entry(atm_devs.next, struct atm_dev, dev_list);
			atm_dev_hold(dev);
		}
		mutex_unlock(&atm_dev_mutex);
	}
	if (!dev)
		return -ENODEV;
	error = __vcc_connect(vcc, dev, vpi, vci);
	if (error) {
		atm_dev_put(dev);
		return error;
	}
	if (vpi == ATM_VPI_UNSPEC || vci == ATM_VCI_UNSPEC)
		set_bit(ATM_VF_PARTIAL,&vcc->flags);
	if (test_bit(ATM_VF_READY,&ATM_SD(sock)->flags))
		sock->state = SS_CONNECTED;
	return 0;
}


int vcc_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg,
		size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	int copied, error = -EINVAL;

	if (sock->state != SS_CONNECTED)
		return -ENOTCONN;
	if (flags & ~MSG_DONTWAIT)		/* only handle MSG_DONTWAIT */
		return -EOPNOTSUPP;
	vcc = ATM_SD(sock);
	if (test_bit(ATM_VF_RELEASED,&vcc->flags) ||
	    test_bit(ATM_VF_CLOSE,&vcc->flags) ||
	    !test_bit(ATM_VF_READY, &vcc->flags))
		return 0;

	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &error);
	if (!skb)
		return error;

	copied = skb->len;
	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	error = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (error)
		return error;
	sock_recv_timestamp(msg, sk, skb);
	pr_debug("RcvM %d -= %d\n", atomic_read(&sk->sk_rmem_alloc), skb->truesize);
	atm_return(vcc, skb->truesize);
	skb_free_datagram(sk, skb);
	return copied;
}


int vcc_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *m,
		size_t total_len)
{
	struct sock *sk = sock->sk;
	DEFINE_WAIT(wait);
	struct atm_vcc *vcc;
	struct sk_buff *skb;
	int eff,error;
	const void __user *buff;
	int size;

	lock_sock(sk);
	if (sock->state != SS_CONNECTED) {
		error = -ENOTCONN;
		goto out;
	}
	if (m->msg_name) {
		error = -EISCONN;
		goto out;
	}
	if (m->msg_iovlen != 1) {
		error = -ENOSYS; /* fix this later @@@ */
		goto out;
	}
	buff = m->msg_iov->iov_base;
	size = m->msg_iov->iov_len;
	vcc = ATM_SD(sock);
	if (test_bit(ATM_VF_RELEASED, &vcc->flags) ||
	    test_bit(ATM_VF_CLOSE, &vcc->flags) ||
	    !test_bit(ATM_VF_READY, &vcc->flags)) {
		error = -EPIPE;
		send_sig(SIGPIPE, current, 0);
		goto out;
	}
	if (!size) {
		error = 0;
		goto out;
	}
	if (size < 0 || size > vcc->qos.txtp.max_sdu) {
		error = -EMSGSIZE;
		goto out;
	}

	eff = (size+3) & ~3; /* align to word boundary */
	prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
	error = 0;
	while (!(skb = alloc_tx(vcc,eff))) {
		if (m->msg_flags & MSG_DONTWAIT) {
			error = -EAGAIN;
			break;
		}
		schedule();
		if (signal_pending(current)) {
			error = -ERESTARTSYS;
			break;
		}
		if (test_bit(ATM_VF_RELEASED,&vcc->flags) ||
		    test_bit(ATM_VF_CLOSE,&vcc->flags) ||
		    !test_bit(ATM_VF_READY,&vcc->flags)) {
			error = -EPIPE;
			send_sig(SIGPIPE, current, 0);
			break;
		}
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
	}
	finish_wait(sk->sk_sleep, &wait);
	if (error)
		goto out;
	skb->dev = NULL; /* for paths shared with net_device interfaces */
	ATM_SKB(skb)->atm_options = vcc->atm_options;
	if (copy_from_user(skb_put(skb,size),buff,size)) {
		kfree_skb(skb);
		error = -EFAULT;
		goto out;
	}
	if (eff != size) memset(skb->data+size,0,eff-size);
	error = vcc->dev->ops->send(vcc,skb);
	error = error ? error : size;
out:
	release_sock(sk);
	return error;
}


unsigned int vcc_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct atm_vcc *vcc;
	unsigned int mask;

	poll_wait(file, sk->sk_sleep, wait);
	mask = 0;

	vcc = ATM_SD(sock);

	/* exceptional events */
	if (sk->sk_err)
		mask = POLLERR;

	if (test_bit(ATM_VF_RELEASED, &vcc->flags) ||
	    test_bit(ATM_VF_CLOSE, &vcc->flags))
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	/* writable? */
	if (sock->state == SS_CONNECTING &&
	    test_bit(ATM_VF_WAITING, &vcc->flags))
		return mask;

	if (vcc->qos.txtp.traffic_class != ATM_NONE &&
	    vcc_writable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}


static int atm_change_qos(struct atm_vcc *vcc,struct atm_qos *qos)
{
	int error;

	/*
	 * Don't let the QoS change the already connected AAL type nor the
	 * traffic class.
	 */
	if (qos->aal != vcc->qos.aal ||
	    qos->rxtp.traffic_class != vcc->qos.rxtp.traffic_class ||
	    qos->txtp.traffic_class != vcc->qos.txtp.traffic_class)
		return -EINVAL;
	error = adjust_tp(&qos->txtp,qos->aal);
	if (!error) error = adjust_tp(&qos->rxtp,qos->aal);
	if (error) return error;
	if (!vcc->dev->ops->change_qos) return -EOPNOTSUPP;
	if (sk_atm(vcc)->sk_family == AF_ATMPVC)
		return vcc->dev->ops->change_qos(vcc,qos,ATM_MF_SET);
	return svc_change_qos(vcc,qos);
}


static int check_tp(struct atm_trafprm *tp)
{
	/* @@@ Should be merged with adjust_tp */
	if (!tp->traffic_class || tp->traffic_class == ATM_ANYCLASS) return 0;
	if (tp->traffic_class != ATM_UBR && !tp->min_pcr && !tp->pcr &&
	    !tp->max_pcr) return -EINVAL;
	if (tp->min_pcr == ATM_MAX_PCR) return -EINVAL;
	if (tp->min_pcr && tp->max_pcr && tp->max_pcr != ATM_MAX_PCR &&
	    tp->min_pcr > tp->max_pcr) return -EINVAL;
	/*
	 * We allow pcr to be outside [min_pcr,max_pcr], because later
	 * adjustment may still push it in the valid range.
	 */
	return 0;
}


static int check_qos(struct atm_qos *qos)
{
	int error;

	if (!qos->txtp.traffic_class && !qos->rxtp.traffic_class)
		return -EINVAL;
	if (qos->txtp.traffic_class != qos->rxtp.traffic_class &&
	    qos->txtp.traffic_class && qos->rxtp.traffic_class &&
	    qos->txtp.traffic_class != ATM_ANYCLASS &&
	    qos->rxtp.traffic_class != ATM_ANYCLASS) return -EINVAL;
	error = check_tp(&qos->txtp);
	if (error) return error;
	return check_tp(&qos->rxtp);
}

int vcc_setsockopt(struct socket *sock, int level, int optname,
		   char __user *optval, int optlen)
{
	struct atm_vcc *vcc;
	unsigned long value;
	int error;

	if (__SO_LEVEL_MATCH(optname, level) && optlen != __SO_SIZE(optname))
		return -EINVAL;

	vcc = ATM_SD(sock);
	switch (optname) {
		case SO_ATMQOS:
			{
				struct atm_qos qos;

				if (copy_from_user(&qos,optval,sizeof(qos)))
					return -EFAULT;
				error = check_qos(&qos);
				if (error) return error;
				if (sock->state == SS_CONNECTED)
					return atm_change_qos(vcc,&qos);
				if (sock->state != SS_UNCONNECTED)
					return -EBADFD;
				vcc->qos = qos;
				set_bit(ATM_VF_HASQOS,&vcc->flags);
				return 0;
			}
		case SO_SETCLP:
			if (get_user(value,(unsigned long __user *)optval))
				return -EFAULT;
			if (value) vcc->atm_options |= ATM_ATMOPT_CLP;
			else vcc->atm_options &= ~ATM_ATMOPT_CLP;
			return 0;
		default:
			if (level == SOL_SOCKET) return -EINVAL;
			break;
	}
	if (!vcc->dev || !vcc->dev->ops->setsockopt) return -EINVAL;
	return vcc->dev->ops->setsockopt(vcc,level,optname,optval,optlen);
}


int vcc_getsockopt(struct socket *sock, int level, int optname,
		   char __user *optval, int __user *optlen)
{
	struct atm_vcc *vcc;
	int len;

	if (get_user(len, optlen))
		return -EFAULT;
	if (__SO_LEVEL_MATCH(optname, level) && len != __SO_SIZE(optname))
		return -EINVAL;

	vcc = ATM_SD(sock);
	switch (optname) {
		case SO_ATMQOS:
			if (!test_bit(ATM_VF_HASQOS,&vcc->flags))
				return -EINVAL;
			return copy_to_user(optval,&vcc->qos,sizeof(vcc->qos)) ?
			    -EFAULT : 0;
		case SO_SETCLP:
			return put_user(vcc->atm_options & ATM_ATMOPT_CLP ? 1 :
			  0,(unsigned long __user *)optval) ? -EFAULT : 0;
		case SO_ATMPVC:
			{
				struct sockaddr_atmpvc pvc;

				if (!vcc->dev ||
				    !test_bit(ATM_VF_ADDR,&vcc->flags))
					return -ENOTCONN;
				pvc.sap_family = AF_ATMPVC;
				pvc.sap_addr.itf = vcc->dev->number;
				pvc.sap_addr.vpi = vcc->vpi;
				pvc.sap_addr.vci = vcc->vci;
				return copy_to_user(optval,&pvc,sizeof(pvc)) ?
				    -EFAULT : 0;
			}
		default:
			if (level == SOL_SOCKET) return -EINVAL;
			break;
	}
	if (!vcc->dev || !vcc->dev->ops->getsockopt) return -EINVAL;
	return vcc->dev->ops->getsockopt(vcc, level, optname, optval, len);
}

static int __init atm_init(void)
{
	int error;

	if ((error = proto_register(&vcc_proto, 0)) < 0)
		goto out;

	if ((error = atmpvc_init()) < 0) {
		printk(KERN_ERR "atmpvc_init() failed with %d\n", error);
		goto out_unregister_vcc_proto;
	}
	if ((error = atmsvc_init()) < 0) {
		printk(KERN_ERR "atmsvc_init() failed with %d\n", error);
		goto out_atmpvc_exit;
	}
	if ((error = atm_proc_init()) < 0) {
		printk(KERN_ERR "atm_proc_init() failed with %d\n",error);
		goto out_atmsvc_exit;
	}
	if ((error = atm_sysfs_init()) < 0) {
		printk(KERN_ERR "atm_sysfs_init() failed with %d\n",error);
		goto out_atmproc_exit;
	}
out:
	return error;
out_atmproc_exit:
	atm_proc_exit();
out_atmsvc_exit:
	atmsvc_exit();
out_atmpvc_exit:
	atmsvc_exit();
out_unregister_vcc_proto:
	proto_unregister(&vcc_proto);
	goto out;
}

static void __exit atm_exit(void)
{
	atm_proc_exit();
	atm_sysfs_exit();
	atmsvc_exit();
	atmpvc_exit();
	proto_unregister(&vcc_proto);
}

subsys_initcall(atm_init);

module_exit(atm_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_ATMPVC);
MODULE_ALIAS_NETPROTO(PF_ATMSVC);
