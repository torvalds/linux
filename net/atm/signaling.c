/* net/atm/signaling.c - ATM signaling */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/errno.h>	/* error codes */
#include <linux/kernel.h>	/* printk */
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/sched.h>	/* jiffies and HZ */
#include <linux/atm.h>		/* ATM stuff */
#include <linux/atmsap.h>
#include <linux/atmsvc.h>
#include <linux/atmdev.h>
#include <linux/bitops.h>
#include <linux/slab.h>

#include "resources.h"
#include "signaling.h"

struct atm_vcc *sigd = NULL;

static void sigd_put_skb(struct sk_buff *skb)
{
	if (!sigd) {
		pr_debug("atmsvc: no signaling daemon\n");
		kfree_skb(skb);
		return;
	}
	atm_force_charge(sigd, skb->truesize);
	skb_queue_tail(&sk_atm(sigd)->sk_receive_queue, skb);
	sk_atm(sigd)->sk_data_ready(sk_atm(sigd));
}

static void modify_qos(struct atm_vcc *vcc, struct atmsvc_msg *msg)
{
	struct sk_buff *skb;

	if (test_bit(ATM_VF_RELEASED, &vcc->flags) ||
	    !test_bit(ATM_VF_READY, &vcc->flags))
		return;
	msg->type = as_error;
	if (!vcc->dev->ops->change_qos)
		msg->reply = -EOPNOTSUPP;
	else {
		/* should lock VCC */
		msg->reply = vcc->dev->ops->change_qos(vcc, &msg->qos,
						       msg->reply);
		if (!msg->reply)
			msg->type = as_okay;
	}
	/*
	 * Should probably just turn around the old skb. But the, the buffer
	 * space accounting needs to follow the change too. Maybe later.
	 */
	while (!(skb = alloc_skb(sizeof(struct atmsvc_msg), GFP_KERNEL)))
		schedule();
	*(struct atmsvc_msg *)skb_put(skb, sizeof(struct atmsvc_msg)) = *msg;
	sigd_put_skb(skb);
}

static int sigd_send(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct atmsvc_msg *msg;
	struct atm_vcc *session_vcc;
	struct sock *sk;

	msg = (struct atmsvc_msg *) skb->data;
	atomic_sub(skb->truesize, &sk_atm(vcc)->sk_wmem_alloc);
	vcc = *(struct atm_vcc **) &msg->vcc;
	pr_debug("%d (0x%lx)\n", (int)msg->type, (unsigned long)vcc);
	sk = sk_atm(vcc);

	switch (msg->type) {
	case as_okay:
		sk->sk_err = -msg->reply;
		clear_bit(ATM_VF_WAITING, &vcc->flags);
		if (!*vcc->local.sas_addr.prv && !*vcc->local.sas_addr.pub) {
			vcc->local.sas_family = AF_ATMSVC;
			memcpy(vcc->local.sas_addr.prv,
			       msg->local.sas_addr.prv, ATM_ESA_LEN);
			memcpy(vcc->local.sas_addr.pub,
			       msg->local.sas_addr.pub, ATM_E164_LEN + 1);
		}
		session_vcc = vcc->session ? vcc->session : vcc;
		if (session_vcc->vpi || session_vcc->vci)
			break;
		session_vcc->itf = msg->pvc.sap_addr.itf;
		session_vcc->vpi = msg->pvc.sap_addr.vpi;
		session_vcc->vci = msg->pvc.sap_addr.vci;
		if (session_vcc->vpi || session_vcc->vci)
			session_vcc->qos = msg->qos;
		break;
	case as_error:
		clear_bit(ATM_VF_REGIS, &vcc->flags);
		clear_bit(ATM_VF_READY, &vcc->flags);
		sk->sk_err = -msg->reply;
		clear_bit(ATM_VF_WAITING, &vcc->flags);
		break;
	case as_indicate:
		vcc = *(struct atm_vcc **)&msg->listen_vcc;
		sk = sk_atm(vcc);
		pr_debug("as_indicate!!!\n");
		lock_sock(sk);
		if (sk_acceptq_is_full(sk)) {
			sigd_enq(NULL, as_reject, vcc, NULL, NULL);
			dev_kfree_skb(skb);
			goto as_indicate_complete;
		}
		sk->sk_ack_backlog++;
		skb_queue_tail(&sk->sk_receive_queue, skb);
		pr_debug("waking sk_sleep(sk) 0x%p\n", sk_sleep(sk));
		sk->sk_state_change(sk);
as_indicate_complete:
		release_sock(sk);
		return 0;
	case as_close:
		set_bit(ATM_VF_RELEASED, &vcc->flags);
		vcc_release_async(vcc, msg->reply);
		goto out;
	case as_modify:
		modify_qos(vcc, msg);
		break;
	case as_addparty:
	case as_dropparty:
		sk->sk_err_soft = -msg->reply;
					/* < 0 failure, otherwise ep_ref */
		clear_bit(ATM_VF_WAITING, &vcc->flags);
		break;
	default:
		pr_alert("bad message type %d\n", (int)msg->type);
		return -EINVAL;
	}
	sk->sk_state_change(sk);
out:
	dev_kfree_skb(skb);
	return 0;
}

void sigd_enq2(struct atm_vcc *vcc, enum atmsvc_msg_type type,
	       struct atm_vcc *listen_vcc, const struct sockaddr_atmpvc *pvc,
	       const struct sockaddr_atmsvc *svc, const struct atm_qos *qos,
	       int reply)
{
	struct sk_buff *skb;
	struct atmsvc_msg *msg;
	static unsigned int session = 0;

	pr_debug("%d (0x%p)\n", (int)type, vcc);
	while (!(skb = alloc_skb(sizeof(struct atmsvc_msg), GFP_KERNEL)))
		schedule();
	msg = (struct atmsvc_msg *)skb_put(skb, sizeof(struct atmsvc_msg));
	memset(msg, 0, sizeof(*msg));
	msg->type = type;
	*(struct atm_vcc **) &msg->vcc = vcc;
	*(struct atm_vcc **) &msg->listen_vcc = listen_vcc;
	msg->reply = reply;
	if (qos)
		msg->qos = *qos;
	if (vcc)
		msg->sap = vcc->sap;
	if (svc)
		msg->svc = *svc;
	if (vcc)
		msg->local = vcc->local;
	if (pvc)
		msg->pvc = *pvc;
	if (vcc) {
		if (type == as_connect && test_bit(ATM_VF_SESSION, &vcc->flags))
			msg->session = ++session;
			/* every new pmp connect gets the next session number */
	}
	sigd_put_skb(skb);
	if (vcc)
		set_bit(ATM_VF_REGIS, &vcc->flags);
}

void sigd_enq(struct atm_vcc *vcc, enum atmsvc_msg_type type,
	      struct atm_vcc *listen_vcc, const struct sockaddr_atmpvc *pvc,
	      const struct sockaddr_atmsvc *svc)
{
	sigd_enq2(vcc, type, listen_vcc, pvc, svc, vcc ? &vcc->qos : NULL, 0);
	/* other ISP applications may use "reply" */
}

static void purge_vcc(struct atm_vcc *vcc)
{
	if (sk_atm(vcc)->sk_family == PF_ATMSVC &&
	    !test_bit(ATM_VF_META, &vcc->flags)) {
		set_bit(ATM_VF_RELEASED, &vcc->flags);
		clear_bit(ATM_VF_REGIS, &vcc->flags);
		vcc_release_async(vcc, -EUNATCH);
	}
}

static void sigd_close(struct atm_vcc *vcc)
{
	struct sock *s;
	int i;

	pr_debug("\n");
	sigd = NULL;
	if (skb_peek(&sk_atm(vcc)->sk_receive_queue))
		pr_err("closing with requests pending\n");
	skb_queue_purge(&sk_atm(vcc)->sk_receive_queue);

	read_lock(&vcc_sklist_lock);
	for (i = 0; i < VCC_HTABLE_SIZE; ++i) {
		struct hlist_head *head = &vcc_hash[i];

		sk_for_each(s, head) {
			vcc = atm_sk(s);

			purge_vcc(vcc);
		}
	}
	read_unlock(&vcc_sklist_lock);
}

static struct atmdev_ops sigd_dev_ops = {
	.close = sigd_close,
	.send =	sigd_send
};

static struct atm_dev sigd_dev = {
	.ops =		&sigd_dev_ops,
	.type =		"sig",
	.number =	999,
	.lock =		__SPIN_LOCK_UNLOCKED(sigd_dev.lock)
};

int sigd_attach(struct atm_vcc *vcc)
{
	if (sigd)
		return -EADDRINUSE;
	pr_debug("\n");
	sigd = vcc;
	vcc->dev = &sigd_dev;
	vcc_insert_socket(sk_atm(vcc));
	set_bit(ATM_VF_META, &vcc->flags);
	set_bit(ATM_VF_READY, &vcc->flags);
	return 0;
}
