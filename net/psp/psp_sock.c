// SPDX-License-Identifier: GPL-2.0-only

#include <linux/file.h>
#include <linux/net.h>
#include <linux/rcupdate.h>
#include <linux/tcp.h>

#include <net/ip.h>
#include <net/psp.h>
#include "psp.h"

struct psp_dev *psp_dev_get_for_sock(struct sock *sk)
{
	struct psp_dev *psd = NULL;
	struct dst_entry *dst;

	rcu_read_lock();
	dst = __sk_dst_get(sk);
	if (dst) {
		psd = rcu_dereference(dst_dev_rcu(dst)->psp_dev);
		if (psd && !psp_dev_tryget(psd))
			psd = NULL;
	}
	rcu_read_unlock();

	return psd;
}

static struct sk_buff *
psp_validate_xmit(struct sock *sk, struct net_device *dev, struct sk_buff *skb)
{
	struct psp_assoc *pas;
	bool good;

	rcu_read_lock();
	pas = psp_skb_get_assoc_rcu(skb);
	good = !pas || rcu_access_pointer(dev->psp_dev) == pas->psd;
	rcu_read_unlock();
	if (!good) {
		sk_skb_reason_drop(sk, skb, SKB_DROP_REASON_PSP_OUTPUT);
		return NULL;
	}

	return skb;
}

struct psp_assoc *psp_assoc_create(struct psp_dev *psd)
{
	struct psp_assoc *pas;

	lockdep_assert_held(&psd->lock);

	pas = kzalloc(struct_size(pas, drv_data, psd->caps->assoc_drv_spc),
		      GFP_KERNEL_ACCOUNT);
	if (!pas)
		return NULL;

	pas->psd = psd;
	pas->dev_id = psd->id;
	pas->generation = psd->generation;
	psp_dev_get(psd);
	refcount_set(&pas->refcnt, 1);

	list_add_tail(&pas->assocs_list, &psd->active_assocs);

	return pas;
}

static struct psp_assoc *psp_assoc_dummy(struct psp_assoc *pas)
{
	struct psp_dev *psd = pas->psd;
	size_t sz;

	lockdep_assert_held(&psd->lock);

	sz = struct_size(pas, drv_data, psd->caps->assoc_drv_spc);
	return kmemdup(pas, sz, GFP_KERNEL);
}

static int psp_dev_tx_key_add(struct psp_dev *psd, struct psp_assoc *pas,
			      struct netlink_ext_ack *extack)
{
	return psd->ops->tx_key_add(psd, pas, extack);
}

void psp_dev_tx_key_del(struct psp_dev *psd, struct psp_assoc *pas)
{
	if (pas->tx.spi)
		psd->ops->tx_key_del(psd, pas);
	list_del(&pas->assocs_list);
}

static void psp_assoc_free(struct work_struct *work)
{
	struct psp_assoc *pas = container_of(work, struct psp_assoc, work);
	struct psp_dev *psd = pas->psd;

	mutex_lock(&psd->lock);
	if (psd->ops)
		psp_dev_tx_key_del(psd, pas);
	mutex_unlock(&psd->lock);
	psp_dev_put(psd);
	kfree(pas);
}

static void psp_assoc_free_queue(struct rcu_head *head)
{
	struct psp_assoc *pas = container_of(head, struct psp_assoc, rcu);

	INIT_WORK(&pas->work, psp_assoc_free);
	schedule_work(&pas->work);
}

/**
 * psp_assoc_put() - release a reference on a PSP association
 * @pas: association to release
 */
void psp_assoc_put(struct psp_assoc *pas)
{
	if (pas && refcount_dec_and_test(&pas->refcnt))
		call_rcu(&pas->rcu, psp_assoc_free_queue);
}

void psp_sk_assoc_free(struct sock *sk)
{
	struct psp_assoc *pas = rcu_dereference_protected(sk->psp_assoc, 1);

	rcu_assign_pointer(sk->psp_assoc, NULL);
	psp_assoc_put(pas);
}

int psp_sock_assoc_set_rx(struct sock *sk, struct psp_assoc *pas,
			  struct psp_key_parsed *key,
			  struct netlink_ext_ack *extack)
{
	int err;

	memcpy(&pas->rx, key, sizeof(*key));

	lock_sock(sk);

	if (psp_sk_assoc(sk)) {
		NL_SET_ERR_MSG(extack, "Socket already has PSP state");
		err = -EBUSY;
		goto exit_unlock;
	}

	refcount_inc(&pas->refcnt);
	rcu_assign_pointer(sk->psp_assoc, pas);
	err = 0;

exit_unlock:
	release_sock(sk);

	return err;
}

static int psp_sock_recv_queue_check(struct sock *sk, struct psp_assoc *pas)
{
	struct psp_skb_ext *pse;
	struct sk_buff *skb;

	skb_rbtree_walk(skb, &tcp_sk(sk)->out_of_order_queue) {
		pse = skb_ext_find(skb, SKB_EXT_PSP);
		if (!psp_pse_matches_pas(pse, pas))
			return -EBUSY;
	}

	skb_queue_walk(&sk->sk_receive_queue, skb) {
		pse = skb_ext_find(skb, SKB_EXT_PSP);
		if (!psp_pse_matches_pas(pse, pas))
			return -EBUSY;
	}
	return 0;
}

int psp_sock_assoc_set_tx(struct sock *sk, struct psp_dev *psd,
			  u32 version, struct psp_key_parsed *key,
			  struct netlink_ext_ack *extack)
{
	struct inet_connection_sock *icsk;
	struct psp_assoc *pas, *dummy;
	int err;

	lock_sock(sk);

	pas = psp_sk_assoc(sk);
	if (!pas) {
		NL_SET_ERR_MSG(extack, "Socket has no Rx key");
		err = -EINVAL;
		goto exit_unlock;
	}
	if (pas->psd != psd) {
		NL_SET_ERR_MSG(extack, "Rx key from different device");
		err = -EINVAL;
		goto exit_unlock;
	}
	if (pas->version != version) {
		NL_SET_ERR_MSG(extack,
			       "PSP version mismatch with existing state");
		err = -EINVAL;
		goto exit_unlock;
	}
	if (pas->tx.spi) {
		NL_SET_ERR_MSG(extack, "Tx key already set");
		err = -EBUSY;
		goto exit_unlock;
	}

	err = psp_sock_recv_queue_check(sk, pas);
	if (err) {
		NL_SET_ERR_MSG(extack, "Socket has incompatible segments already in the recv queue");
		goto exit_unlock;
	}

	/* Pass a fake association to drivers to make sure they don't
	 * try to store pointers to it. For re-keying we'll need to
	 * re-allocate the assoc structures.
	 */
	dummy = psp_assoc_dummy(pas);
	if (!dummy) {
		err = -ENOMEM;
		goto exit_unlock;
	}

	memcpy(&dummy->tx, key, sizeof(*key));
	err = psp_dev_tx_key_add(psd, dummy, extack);
	if (err)
		goto exit_free_dummy;

	memcpy(pas->drv_data, dummy->drv_data, psd->caps->assoc_drv_spc);
	memcpy(&pas->tx, key, sizeof(*key));

	WRITE_ONCE(sk->sk_validate_xmit_skb, psp_validate_xmit);
	tcp_write_collapse_fence(sk);
	pas->upgrade_seq = tcp_sk(sk)->rcv_nxt;

	icsk = inet_csk(sk);
	icsk->icsk_ext_hdr_len += psp_sk_overhead(sk);
	icsk->icsk_sync_mss(sk, icsk->icsk_pmtu_cookie);

exit_free_dummy:
	kfree(dummy);
exit_unlock:
	release_sock(sk);
	return err;
}

void psp_assocs_key_rotated(struct psp_dev *psd)
{
	struct psp_assoc *pas, *next;

	/* Mark the stale associations as invalid, they will no longer
	 * be able to Rx any traffic.
	 */
	list_for_each_entry_safe(pas, next, &psd->prev_assocs, assocs_list)
		pas->generation |= ~PSP_GEN_VALID_MASK;
	list_splice_init(&psd->prev_assocs, &psd->stale_assocs);
	list_splice_init(&psd->active_assocs, &psd->prev_assocs);

	/* TODO: we should inform the sockets that got shut down */
}

void psp_twsk_init(struct inet_timewait_sock *tw, const struct sock *sk)
{
	struct psp_assoc *pas = psp_sk_assoc(sk);

	if (pas)
		refcount_inc(&pas->refcnt);
	rcu_assign_pointer(tw->psp_assoc, pas);
	tw->tw_validate_xmit_skb = psp_validate_xmit;
}

void psp_twsk_assoc_free(struct inet_timewait_sock *tw)
{
	struct psp_assoc *pas = rcu_dereference_protected(tw->psp_assoc, 1);

	rcu_assign_pointer(tw->psp_assoc, NULL);
	psp_assoc_put(pas);
}

void psp_reply_set_decrypted(const struct sock *sk, struct sk_buff *skb)
{
	struct psp_assoc *pas;

	rcu_read_lock();
	pas = psp_sk_get_assoc_rcu(sk);
	if (pas && pas->tx.spi)
		skb->decrypted = 1;
	rcu_read_unlock();
}
EXPORT_IPV6_MOD_GPL(psp_reply_set_decrypted);
