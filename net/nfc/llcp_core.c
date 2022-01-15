// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 * Copyright (C) 2014 Marvell International Ltd.
 */

#define pr_fmt(fmt) "llcp: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/nfc.h>

#include "nfc.h"
#include "llcp.h"

static u8 llcp_magic[3] = {0x46, 0x66, 0x6d};

static LIST_HEAD(llcp_devices);

static void nfc_llcp_rx_skb(struct nfc_llcp_local *local, struct sk_buff *skb);

void nfc_llcp_sock_link(struct llcp_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock(&l->lock);
}

void nfc_llcp_sock_unlink(struct llcp_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_del_node_init(sk);
	write_unlock(&l->lock);
}

void nfc_llcp_socket_remote_param_init(struct nfc_llcp_sock *sock)
{
	sock->remote_rw = LLCP_DEFAULT_RW;
	sock->remote_miu = LLCP_MAX_MIU + 1;
}

static void nfc_llcp_socket_purge(struct nfc_llcp_sock *sock)
{
	struct nfc_llcp_local *local = sock->local;
	struct sk_buff *s, *tmp;

	pr_debug("%p\n", &sock->sk);

	skb_queue_purge(&sock->tx_queue);
	skb_queue_purge(&sock->tx_pending_queue);

	if (local == NULL)
		return;

	/* Search for local pending SKBs that are related to this socket */
	skb_queue_walk_safe(&local->tx_queue, s, tmp) {
		if (s->sk != &sock->sk)
			continue;

		skb_unlink(s, &local->tx_queue);
		kfree_skb(s);
	}
}

static void nfc_llcp_socket_release(struct nfc_llcp_local *local, bool device,
				    int err)
{
	struct sock *sk;
	struct hlist_node *tmp;
	struct nfc_llcp_sock *llcp_sock;

	skb_queue_purge(&local->tx_queue);

	write_lock(&local->sockets.lock);

	sk_for_each_safe(sk, tmp, &local->sockets.head) {
		llcp_sock = nfc_llcp_sock(sk);

		bh_lock_sock(sk);

		nfc_llcp_socket_purge(llcp_sock);

		if (sk->sk_state == LLCP_CONNECTED)
			nfc_put_device(llcp_sock->dev);

		if (sk->sk_state == LLCP_LISTEN) {
			struct nfc_llcp_sock *lsk, *n;
			struct sock *accept_sk;

			list_for_each_entry_safe(lsk, n,
						 &llcp_sock->accept_queue,
						 accept_queue) {
				accept_sk = &lsk->sk;
				bh_lock_sock(accept_sk);

				nfc_llcp_accept_unlink(accept_sk);

				if (err)
					accept_sk->sk_err = err;
				accept_sk->sk_state = LLCP_CLOSED;
				accept_sk->sk_state_change(sk);

				bh_unlock_sock(accept_sk);
			}
		}

		if (err)
			sk->sk_err = err;
		sk->sk_state = LLCP_CLOSED;
		sk->sk_state_change(sk);

		bh_unlock_sock(sk);

		sk_del_node_init(sk);
	}

	write_unlock(&local->sockets.lock);

	/* If we still have a device, we keep the RAW sockets alive */
	if (device == true)
		return;

	write_lock(&local->raw_sockets.lock);

	sk_for_each_safe(sk, tmp, &local->raw_sockets.head) {
		llcp_sock = nfc_llcp_sock(sk);

		bh_lock_sock(sk);

		nfc_llcp_socket_purge(llcp_sock);

		if (err)
			sk->sk_err = err;
		sk->sk_state = LLCP_CLOSED;
		sk->sk_state_change(sk);

		bh_unlock_sock(sk);

		sk_del_node_init(sk);
	}

	write_unlock(&local->raw_sockets.lock);
}

struct nfc_llcp_local *nfc_llcp_local_get(struct nfc_llcp_local *local)
{
	kref_get(&local->ref);

	return local;
}

static void local_cleanup(struct nfc_llcp_local *local)
{
	nfc_llcp_socket_release(local, false, ENXIO);
	del_timer_sync(&local->link_timer);
	skb_queue_purge(&local->tx_queue);
	cancel_work_sync(&local->tx_work);
	cancel_work_sync(&local->rx_work);
	cancel_work_sync(&local->timeout_work);
	kfree_skb(local->rx_pending);
	del_timer_sync(&local->sdreq_timer);
	cancel_work_sync(&local->sdreq_timeout_work);
	nfc_llcp_free_sdp_tlv_list(&local->pending_sdreqs);
}

static void local_release(struct kref *ref)
{
	struct nfc_llcp_local *local;

	local = container_of(ref, struct nfc_llcp_local, ref);

	list_del(&local->list);
	local_cleanup(local);
	kfree(local);
}

int nfc_llcp_local_put(struct nfc_llcp_local *local)
{
	if (local == NULL)
		return 0;

	return kref_put(&local->ref, local_release);
}

static struct nfc_llcp_sock *nfc_llcp_sock_get(struct nfc_llcp_local *local,
					       u8 ssap, u8 dsap)
{
	struct sock *sk;
	struct nfc_llcp_sock *llcp_sock, *tmp_sock;

	pr_debug("ssap dsap %d %d\n", ssap, dsap);

	if (ssap == 0 && dsap == 0)
		return NULL;

	read_lock(&local->sockets.lock);

	llcp_sock = NULL;

	sk_for_each(sk, &local->sockets.head) {
		tmp_sock = nfc_llcp_sock(sk);

		if (tmp_sock->ssap == ssap && tmp_sock->dsap == dsap) {
			llcp_sock = tmp_sock;
			break;
		}
	}

	read_unlock(&local->sockets.lock);

	if (llcp_sock == NULL)
		return NULL;

	sock_hold(&llcp_sock->sk);

	return llcp_sock;
}

static void nfc_llcp_sock_put(struct nfc_llcp_sock *sock)
{
	sock_put(&sock->sk);
}

static void nfc_llcp_timeout_work(struct work_struct *work)
{
	struct nfc_llcp_local *local = container_of(work, struct nfc_llcp_local,
						    timeout_work);

	nfc_dep_link_down(local->dev);
}

static void nfc_llcp_symm_timer(struct timer_list *t)
{
	struct nfc_llcp_local *local = from_timer(local, t, link_timer);

	pr_err("SYMM timeout\n");

	schedule_work(&local->timeout_work);
}

static void nfc_llcp_sdreq_timeout_work(struct work_struct *work)
{
	unsigned long time;
	HLIST_HEAD(nl_sdres_list);
	struct hlist_node *n;
	struct nfc_llcp_sdp_tlv *sdp;
	struct nfc_llcp_local *local = container_of(work, struct nfc_llcp_local,
						    sdreq_timeout_work);

	mutex_lock(&local->sdreq_lock);

	time = jiffies - msecs_to_jiffies(3 * local->remote_lto);

	hlist_for_each_entry_safe(sdp, n, &local->pending_sdreqs, node) {
		if (time_after(sdp->time, time))
			continue;

		sdp->sap = LLCP_SDP_UNBOUND;

		hlist_del(&sdp->node);

		hlist_add_head(&sdp->node, &nl_sdres_list);
	}

	if (!hlist_empty(&local->pending_sdreqs))
		mod_timer(&local->sdreq_timer,
			  jiffies + msecs_to_jiffies(3 * local->remote_lto));

	mutex_unlock(&local->sdreq_lock);

	if (!hlist_empty(&nl_sdres_list))
		nfc_genl_llc_send_sdres(local->dev, &nl_sdres_list);
}

static void nfc_llcp_sdreq_timer(struct timer_list *t)
{
	struct nfc_llcp_local *local = from_timer(local, t, sdreq_timer);

	schedule_work(&local->sdreq_timeout_work);
}

struct nfc_llcp_local *nfc_llcp_find_local(struct nfc_dev *dev)
{
	struct nfc_llcp_local *local;

	list_for_each_entry(local, &llcp_devices, list)
		if (local->dev == dev)
			return local;

	pr_debug("No device found\n");

	return NULL;
}

static char *wks[] = {
	NULL,
	NULL, /* SDP */
	"urn:nfc:sn:ip",
	"urn:nfc:sn:obex",
	"urn:nfc:sn:snep",
};

static int nfc_llcp_wks_sap(const char *service_name, size_t service_name_len)
{
	int sap, num_wks;

	pr_debug("%s\n", service_name);

	if (service_name == NULL)
		return -EINVAL;

	num_wks = ARRAY_SIZE(wks);

	for (sap = 0; sap < num_wks; sap++) {
		if (wks[sap] == NULL)
			continue;

		if (strncmp(wks[sap], service_name, service_name_len) == 0)
			return sap;
	}

	return -EINVAL;
}

static
struct nfc_llcp_sock *nfc_llcp_sock_from_sn(struct nfc_llcp_local *local,
					    const u8 *sn, size_t sn_len)
{
	struct sock *sk;
	struct nfc_llcp_sock *llcp_sock, *tmp_sock;

	pr_debug("sn %zd %p\n", sn_len, sn);

	if (sn == NULL || sn_len == 0)
		return NULL;

	read_lock(&local->sockets.lock);

	llcp_sock = NULL;

	sk_for_each(sk, &local->sockets.head) {
		tmp_sock = nfc_llcp_sock(sk);

		pr_debug("llcp sock %p\n", tmp_sock);

		if (tmp_sock->sk.sk_type == SOCK_STREAM &&
		    tmp_sock->sk.sk_state != LLCP_LISTEN)
			continue;

		if (tmp_sock->sk.sk_type == SOCK_DGRAM &&
		    tmp_sock->sk.sk_state != LLCP_BOUND)
			continue;

		if (tmp_sock->service_name == NULL ||
		    tmp_sock->service_name_len == 0)
			continue;

		if (tmp_sock->service_name_len != sn_len)
			continue;

		if (memcmp(sn, tmp_sock->service_name, sn_len) == 0) {
			llcp_sock = tmp_sock;
			break;
		}
	}

	read_unlock(&local->sockets.lock);

	pr_debug("Found llcp sock %p\n", llcp_sock);

	return llcp_sock;
}

u8 nfc_llcp_get_sdp_ssap(struct nfc_llcp_local *local,
			 struct nfc_llcp_sock *sock)
{
	mutex_lock(&local->sdp_lock);

	if (sock->service_name != NULL && sock->service_name_len > 0) {
		int ssap = nfc_llcp_wks_sap(sock->service_name,
					    sock->service_name_len);

		if (ssap > 0) {
			pr_debug("WKS %d\n", ssap);

			/* This is a WKS, let's check if it's free */
			if (local->local_wks & BIT(ssap)) {
				mutex_unlock(&local->sdp_lock);

				return LLCP_SAP_MAX;
			}

			set_bit(ssap, &local->local_wks);
			mutex_unlock(&local->sdp_lock);

			return ssap;
		}

		/*
		 * Check if there already is a non WKS socket bound
		 * to this service name.
		 */
		if (nfc_llcp_sock_from_sn(local, sock->service_name,
					  sock->service_name_len) != NULL) {
			mutex_unlock(&local->sdp_lock);

			return LLCP_SAP_MAX;
		}

		mutex_unlock(&local->sdp_lock);

		return LLCP_SDP_UNBOUND;

	} else if (sock->ssap != 0 && sock->ssap < LLCP_WKS_NUM_SAP) {
		if (!test_bit(sock->ssap, &local->local_wks)) {
			set_bit(sock->ssap, &local->local_wks);
			mutex_unlock(&local->sdp_lock);

			return sock->ssap;
		}
	}

	mutex_unlock(&local->sdp_lock);

	return LLCP_SAP_MAX;
}

u8 nfc_llcp_get_local_ssap(struct nfc_llcp_local *local)
{
	u8 local_ssap;

	mutex_lock(&local->sdp_lock);

	local_ssap = find_first_zero_bit(&local->local_sap, LLCP_LOCAL_NUM_SAP);
	if (local_ssap == LLCP_LOCAL_NUM_SAP) {
		mutex_unlock(&local->sdp_lock);
		return LLCP_SAP_MAX;
	}

	set_bit(local_ssap, &local->local_sap);

	mutex_unlock(&local->sdp_lock);

	return local_ssap + LLCP_LOCAL_SAP_OFFSET;
}

void nfc_llcp_put_ssap(struct nfc_llcp_local *local, u8 ssap)
{
	u8 local_ssap;
	unsigned long *sdp;

	if (ssap < LLCP_WKS_NUM_SAP) {
		local_ssap = ssap;
		sdp = &local->local_wks;
	} else if (ssap < LLCP_LOCAL_NUM_SAP) {
		atomic_t *client_cnt;

		local_ssap = ssap - LLCP_WKS_NUM_SAP;
		sdp = &local->local_sdp;
		client_cnt = &local->local_sdp_cnt[local_ssap];

		pr_debug("%d clients\n", atomic_read(client_cnt));

		mutex_lock(&local->sdp_lock);

		if (atomic_dec_and_test(client_cnt)) {
			struct nfc_llcp_sock *l_sock;

			pr_debug("No more clients for SAP %d\n", ssap);

			clear_bit(local_ssap, sdp);

			/* Find the listening sock and set it back to UNBOUND */
			l_sock = nfc_llcp_sock_get(local, ssap, LLCP_SAP_SDP);
			if (l_sock) {
				l_sock->ssap = LLCP_SDP_UNBOUND;
				nfc_llcp_sock_put(l_sock);
			}
		}

		mutex_unlock(&local->sdp_lock);

		return;
	} else if (ssap < LLCP_MAX_SAP) {
		local_ssap = ssap - LLCP_LOCAL_NUM_SAP;
		sdp = &local->local_sap;
	} else {
		return;
	}

	mutex_lock(&local->sdp_lock);

	clear_bit(local_ssap, sdp);

	mutex_unlock(&local->sdp_lock);
}

static u8 nfc_llcp_reserve_sdp_ssap(struct nfc_llcp_local *local)
{
	u8 ssap;

	mutex_lock(&local->sdp_lock);

	ssap = find_first_zero_bit(&local->local_sdp, LLCP_SDP_NUM_SAP);
	if (ssap == LLCP_SDP_NUM_SAP) {
		mutex_unlock(&local->sdp_lock);

		return LLCP_SAP_MAX;
	}

	pr_debug("SDP ssap %d\n", LLCP_WKS_NUM_SAP + ssap);

	set_bit(ssap, &local->local_sdp);

	mutex_unlock(&local->sdp_lock);

	return LLCP_WKS_NUM_SAP + ssap;
}

static int nfc_llcp_build_gb(struct nfc_llcp_local *local)
{
	u8 *gb_cur, version, version_length;
	u8 lto_length, wks_length, miux_length;
	const u8 *version_tlv = NULL, *lto_tlv = NULL,
	   *wks_tlv = NULL, *miux_tlv = NULL;
	__be16 wks = cpu_to_be16(local->local_wks);
	u8 gb_len = 0;
	int ret = 0;

	version = LLCP_VERSION_11;
	version_tlv = nfc_llcp_build_tlv(LLCP_TLV_VERSION, &version,
					 1, &version_length);
	if (!version_tlv) {
		ret = -ENOMEM;
		goto out;
	}
	gb_len += version_length;

	lto_tlv = nfc_llcp_build_tlv(LLCP_TLV_LTO, &local->lto, 1, &lto_length);
	if (!lto_tlv) {
		ret = -ENOMEM;
		goto out;
	}
	gb_len += lto_length;

	pr_debug("Local wks 0x%lx\n", local->local_wks);
	wks_tlv = nfc_llcp_build_tlv(LLCP_TLV_WKS, (u8 *)&wks, 2, &wks_length);
	if (!wks_tlv) {
		ret = -ENOMEM;
		goto out;
	}
	gb_len += wks_length;

	miux_tlv = nfc_llcp_build_tlv(LLCP_TLV_MIUX, (u8 *)&local->miux, 0,
				      &miux_length);
	if (!miux_tlv) {
		ret = -ENOMEM;
		goto out;
	}
	gb_len += miux_length;

	gb_len += ARRAY_SIZE(llcp_magic);

	if (gb_len > NFC_MAX_GT_LEN) {
		ret = -EINVAL;
		goto out;
	}

	gb_cur = local->gb;

	memcpy(gb_cur, llcp_magic, ARRAY_SIZE(llcp_magic));
	gb_cur += ARRAY_SIZE(llcp_magic);

	memcpy(gb_cur, version_tlv, version_length);
	gb_cur += version_length;

	memcpy(gb_cur, lto_tlv, lto_length);
	gb_cur += lto_length;

	memcpy(gb_cur, wks_tlv, wks_length);
	gb_cur += wks_length;

	memcpy(gb_cur, miux_tlv, miux_length);
	gb_cur += miux_length;

	local->gb_len = gb_len;

out:
	kfree(version_tlv);
	kfree(lto_tlv);
	kfree(wks_tlv);
	kfree(miux_tlv);

	return ret;
}

u8 *nfc_llcp_general_bytes(struct nfc_dev *dev, size_t *general_bytes_len)
{
	struct nfc_llcp_local *local;

	local = nfc_llcp_find_local(dev);
	if (local == NULL) {
		*general_bytes_len = 0;
		return NULL;
	}

	nfc_llcp_build_gb(local);

	*general_bytes_len = local->gb_len;

	return local->gb;
}

int nfc_llcp_set_remote_gb(struct nfc_dev *dev, const u8 *gb, u8 gb_len)
{
	struct nfc_llcp_local *local;

	if (gb_len < 3 || gb_len > NFC_MAX_GT_LEN)
		return -EINVAL;

	local = nfc_llcp_find_local(dev);
	if (local == NULL) {
		pr_err("No LLCP device\n");
		return -ENODEV;
	}

	memset(local->remote_gb, 0, NFC_MAX_GT_LEN);
	memcpy(local->remote_gb, gb, gb_len);
	local->remote_gb_len = gb_len;

	if (memcmp(local->remote_gb, llcp_magic, 3)) {
		pr_err("MAC does not support LLCP\n");
		return -EINVAL;
	}

	return nfc_llcp_parse_gb_tlv(local,
				     &local->remote_gb[3],
				     local->remote_gb_len - 3);
}

static u8 nfc_llcp_dsap(const struct sk_buff *pdu)
{
	return (pdu->data[0] & 0xfc) >> 2;
}

static u8 nfc_llcp_ptype(const struct sk_buff *pdu)
{
	return ((pdu->data[0] & 0x03) << 2) | ((pdu->data[1] & 0xc0) >> 6);
}

static u8 nfc_llcp_ssap(const struct sk_buff *pdu)
{
	return pdu->data[1] & 0x3f;
}

static u8 nfc_llcp_ns(const struct sk_buff *pdu)
{
	return pdu->data[2] >> 4;
}

static u8 nfc_llcp_nr(const struct sk_buff *pdu)
{
	return pdu->data[2] & 0xf;
}

static void nfc_llcp_set_nrns(struct nfc_llcp_sock *sock, struct sk_buff *pdu)
{
	pdu->data[2] = (sock->send_n << 4) | (sock->recv_n);
	sock->send_n = (sock->send_n + 1) % 16;
	sock->recv_ack_n = (sock->recv_n - 1) % 16;
}

void nfc_llcp_send_to_raw_sock(struct nfc_llcp_local *local,
			       struct sk_buff *skb, u8 direction)
{
	struct sk_buff *skb_copy = NULL, *nskb;
	struct sock *sk;
	u8 *data;

	read_lock(&local->raw_sockets.lock);

	sk_for_each(sk, &local->raw_sockets.head) {
		if (sk->sk_state != LLCP_BOUND)
			continue;

		if (skb_copy == NULL) {
			skb_copy = __pskb_copy_fclone(skb, NFC_RAW_HEADER_SIZE,
						      GFP_ATOMIC, true);

			if (skb_copy == NULL)
				continue;

			data = skb_push(skb_copy, NFC_RAW_HEADER_SIZE);

			data[0] = local->dev ? local->dev->idx : 0xFF;
			data[1] = direction & 0x01;
			data[1] |= (RAW_PAYLOAD_LLCP << 1);
		}

		nskb = skb_clone(skb_copy, GFP_ATOMIC);
		if (!nskb)
			continue;

		if (sock_queue_rcv_skb(sk, nskb))
			kfree_skb(nskb);
	}

	read_unlock(&local->raw_sockets.lock);

	kfree_skb(skb_copy);
}

static void nfc_llcp_tx_work(struct work_struct *work)
{
	struct nfc_llcp_local *local = container_of(work, struct nfc_llcp_local,
						    tx_work);
	struct sk_buff *skb;
	struct sock *sk;
	struct nfc_llcp_sock *llcp_sock;

	skb = skb_dequeue(&local->tx_queue);
	if (skb != NULL) {
		sk = skb->sk;
		llcp_sock = nfc_llcp_sock(sk);

		if (llcp_sock == NULL && nfc_llcp_ptype(skb) == LLCP_PDU_I) {
			kfree_skb(skb);
			nfc_llcp_send_symm(local->dev);
		} else if (llcp_sock && !llcp_sock->remote_ready) {
			skb_queue_head(&local->tx_queue, skb);
			nfc_llcp_send_symm(local->dev);
		} else {
			struct sk_buff *copy_skb = NULL;
			u8 ptype = nfc_llcp_ptype(skb);
			int ret;

			pr_debug("Sending pending skb\n");
			print_hex_dump_debug("LLCP Tx: ", DUMP_PREFIX_OFFSET,
					     16, 1, skb->data, skb->len, true);

			if (ptype == LLCP_PDU_DISC && sk != NULL &&
			    sk->sk_state == LLCP_DISCONNECTING) {
				nfc_llcp_sock_unlink(&local->sockets, sk);
				sock_orphan(sk);
				sock_put(sk);
			}

			if (ptype == LLCP_PDU_I)
				copy_skb = skb_copy(skb, GFP_ATOMIC);

			__net_timestamp(skb);

			nfc_llcp_send_to_raw_sock(local, skb,
						  NFC_DIRECTION_TX);

			ret = nfc_data_exchange(local->dev, local->target_idx,
						skb, nfc_llcp_recv, local);

			if (ret) {
				kfree_skb(copy_skb);
				goto out;
			}

			if (ptype == LLCP_PDU_I && copy_skb)
				skb_queue_tail(&llcp_sock->tx_pending_queue,
					       copy_skb);
		}
	} else {
		nfc_llcp_send_symm(local->dev);
	}

out:
	mod_timer(&local->link_timer,
		  jiffies + msecs_to_jiffies(2 * local->remote_lto));
}

static struct nfc_llcp_sock *nfc_llcp_connecting_sock_get(struct nfc_llcp_local *local,
							  u8 ssap)
{
	struct sock *sk;
	struct nfc_llcp_sock *llcp_sock;

	read_lock(&local->connecting_sockets.lock);

	sk_for_each(sk, &local->connecting_sockets.head) {
		llcp_sock = nfc_llcp_sock(sk);

		if (llcp_sock->ssap == ssap) {
			sock_hold(&llcp_sock->sk);
			goto out;
		}
	}

	llcp_sock = NULL;

out:
	read_unlock(&local->connecting_sockets.lock);

	return llcp_sock;
}

static struct nfc_llcp_sock *nfc_llcp_sock_get_sn(struct nfc_llcp_local *local,
						  const u8 *sn, size_t sn_len)
{
	struct nfc_llcp_sock *llcp_sock;

	llcp_sock = nfc_llcp_sock_from_sn(local, sn, sn_len);

	if (llcp_sock == NULL)
		return NULL;

	sock_hold(&llcp_sock->sk);

	return llcp_sock;
}

static const u8 *nfc_llcp_connect_sn(const struct sk_buff *skb, size_t *sn_len)
{
	u8 type, length;
	const u8 *tlv = &skb->data[2];
	size_t tlv_array_len = skb->len - LLCP_HEADER_SIZE, offset = 0;

	while (offset < tlv_array_len) {
		type = tlv[0];
		length = tlv[1];

		pr_debug("type 0x%x length %d\n", type, length);

		if (type == LLCP_TLV_SN) {
			*sn_len = length;
			return &tlv[2];
		}

		offset += length + 2;
		tlv += length + 2;
	}

	return NULL;
}

static void nfc_llcp_recv_ui(struct nfc_llcp_local *local,
			     struct sk_buff *skb)
{
	struct nfc_llcp_sock *llcp_sock;
	struct nfc_llcp_ui_cb *ui_cb;
	u8 dsap, ssap;

	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);

	ui_cb = nfc_llcp_ui_skb_cb(skb);
	ui_cb->dsap = dsap;
	ui_cb->ssap = ssap;

	pr_debug("%d %d\n", dsap, ssap);

	/* We're looking for a bound socket, not a client one */
	llcp_sock = nfc_llcp_sock_get(local, dsap, LLCP_SAP_SDP);
	if (llcp_sock == NULL || llcp_sock->sk.sk_type != SOCK_DGRAM)
		return;

	/* There is no sequence with UI frames */
	skb_pull(skb, LLCP_HEADER_SIZE);
	if (!sock_queue_rcv_skb(&llcp_sock->sk, skb)) {
		/*
		 * UI frames will be freed from the socket layer, so we
		 * need to keep them alive until someone receives them.
		 */
		skb_get(skb);
	} else {
		pr_err("Receive queue is full\n");
	}

	nfc_llcp_sock_put(llcp_sock);
}

static void nfc_llcp_recv_connect(struct nfc_llcp_local *local,
				  const struct sk_buff *skb)
{
	struct sock *new_sk, *parent;
	struct nfc_llcp_sock *sock, *new_sock;
	u8 dsap, ssap, reason;

	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);

	pr_debug("%d %d\n", dsap, ssap);

	if (dsap != LLCP_SAP_SDP) {
		sock = nfc_llcp_sock_get(local, dsap, LLCP_SAP_SDP);
		if (sock == NULL || sock->sk.sk_state != LLCP_LISTEN) {
			reason = LLCP_DM_NOBOUND;
			goto fail;
		}
	} else {
		const u8 *sn;
		size_t sn_len;

		sn = nfc_llcp_connect_sn(skb, &sn_len);
		if (sn == NULL) {
			reason = LLCP_DM_NOBOUND;
			goto fail;
		}

		pr_debug("Service name length %zu\n", sn_len);

		sock = nfc_llcp_sock_get_sn(local, sn, sn_len);
		if (sock == NULL) {
			reason = LLCP_DM_NOBOUND;
			goto fail;
		}
	}

	lock_sock(&sock->sk);

	parent = &sock->sk;

	if (sk_acceptq_is_full(parent)) {
		reason = LLCP_DM_REJ;
		release_sock(&sock->sk);
		sock_put(&sock->sk);
		goto fail;
	}

	if (sock->ssap == LLCP_SDP_UNBOUND) {
		u8 ssap = nfc_llcp_reserve_sdp_ssap(local);

		pr_debug("First client, reserving %d\n", ssap);

		if (ssap == LLCP_SAP_MAX) {
			reason = LLCP_DM_REJ;
			release_sock(&sock->sk);
			sock_put(&sock->sk);
			goto fail;
		}

		sock->ssap = ssap;
	}

	new_sk = nfc_llcp_sock_alloc(NULL, parent->sk_type, GFP_ATOMIC, 0);
	if (new_sk == NULL) {
		reason = LLCP_DM_REJ;
		release_sock(&sock->sk);
		sock_put(&sock->sk);
		goto fail;
	}

	new_sock = nfc_llcp_sock(new_sk);
	new_sock->dev = local->dev;
	new_sock->local = nfc_llcp_local_get(local);
	new_sock->rw = sock->rw;
	new_sock->miux = sock->miux;
	new_sock->nfc_protocol = sock->nfc_protocol;
	new_sock->dsap = ssap;
	new_sock->target_idx = local->target_idx;
	new_sock->parent = parent;
	new_sock->ssap = sock->ssap;
	if (sock->ssap < LLCP_LOCAL_NUM_SAP && sock->ssap >= LLCP_WKS_NUM_SAP) {
		atomic_t *client_count;

		pr_debug("reserved_ssap %d for %p\n", sock->ssap, new_sock);

		client_count =
			&local->local_sdp_cnt[sock->ssap - LLCP_WKS_NUM_SAP];

		atomic_inc(client_count);
		new_sock->reserved_ssap = sock->ssap;
	}

	nfc_llcp_parse_connection_tlv(new_sock, &skb->data[LLCP_HEADER_SIZE],
				      skb->len - LLCP_HEADER_SIZE);

	pr_debug("new sock %p sk %p\n", new_sock, &new_sock->sk);

	nfc_llcp_sock_link(&local->sockets, new_sk);

	nfc_llcp_accept_enqueue(&sock->sk, new_sk);

	nfc_get_device(local->dev->idx);

	new_sk->sk_state = LLCP_CONNECTED;

	/* Wake the listening processes */
	parent->sk_data_ready(parent);

	/* Send CC */
	nfc_llcp_send_cc(new_sock);

	release_sock(&sock->sk);
	sock_put(&sock->sk);

	return;

fail:
	/* Send DM */
	nfc_llcp_send_dm(local, dsap, ssap, reason);
}

int nfc_llcp_queue_i_frames(struct nfc_llcp_sock *sock)
{
	int nr_frames = 0;
	struct nfc_llcp_local *local = sock->local;

	pr_debug("Remote ready %d tx queue len %d remote rw %d",
		 sock->remote_ready, skb_queue_len(&sock->tx_pending_queue),
		 sock->remote_rw);

	/* Try to queue some I frames for transmission */
	while (sock->remote_ready &&
	       skb_queue_len(&sock->tx_pending_queue) < sock->remote_rw) {
		struct sk_buff *pdu;

		pdu = skb_dequeue(&sock->tx_queue);
		if (pdu == NULL)
			break;

		/* Update N(S)/N(R) */
		nfc_llcp_set_nrns(sock, pdu);

		skb_queue_tail(&local->tx_queue, pdu);
		nr_frames++;
	}

	return nr_frames;
}

static void nfc_llcp_recv_hdlc(struct nfc_llcp_local *local,
			       struct sk_buff *skb)
{
	struct nfc_llcp_sock *llcp_sock;
	struct sock *sk;
	u8 dsap, ssap, ptype, ns, nr;

	ptype = nfc_llcp_ptype(skb);
	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);
	ns = nfc_llcp_ns(skb);
	nr = nfc_llcp_nr(skb);

	pr_debug("%d %d R %d S %d\n", dsap, ssap, nr, ns);

	llcp_sock = nfc_llcp_sock_get(local, dsap, ssap);
	if (llcp_sock == NULL) {
		nfc_llcp_send_dm(local, dsap, ssap, LLCP_DM_NOCONN);
		return;
	}

	sk = &llcp_sock->sk;
	lock_sock(sk);
	if (sk->sk_state == LLCP_CLOSED) {
		release_sock(sk);
		nfc_llcp_sock_put(llcp_sock);
	}

	/* Pass the payload upstream */
	if (ptype == LLCP_PDU_I) {
		pr_debug("I frame, queueing on %p\n", &llcp_sock->sk);

		if (ns == llcp_sock->recv_n)
			llcp_sock->recv_n = (llcp_sock->recv_n + 1) % 16;
		else
			pr_err("Received out of sequence I PDU\n");

		skb_pull(skb, LLCP_HEADER_SIZE + LLCP_SEQUENCE_SIZE);
		if (!sock_queue_rcv_skb(&llcp_sock->sk, skb)) {
			/*
			 * I frames will be freed from the socket layer, so we
			 * need to keep them alive until someone receives them.
			 */
			skb_get(skb);
		} else {
			pr_err("Receive queue is full\n");
		}
	}

	/* Remove skbs from the pending queue */
	if (llcp_sock->send_ack_n != nr) {
		struct sk_buff *s, *tmp;
		u8 n;

		llcp_sock->send_ack_n = nr;

		/* Remove and free all skbs until ns == nr */
		skb_queue_walk_safe(&llcp_sock->tx_pending_queue, s, tmp) {
			n = nfc_llcp_ns(s);

			skb_unlink(s, &llcp_sock->tx_pending_queue);
			kfree_skb(s);

			if (n == nr)
				break;
		}

		/* Re-queue the remaining skbs for transmission */
		skb_queue_reverse_walk_safe(&llcp_sock->tx_pending_queue,
					    s, tmp) {
			skb_unlink(s, &llcp_sock->tx_pending_queue);
			skb_queue_head(&local->tx_queue, s);
		}
	}

	if (ptype == LLCP_PDU_RR)
		llcp_sock->remote_ready = true;
	else if (ptype == LLCP_PDU_RNR)
		llcp_sock->remote_ready = false;

	if (nfc_llcp_queue_i_frames(llcp_sock) == 0 && ptype == LLCP_PDU_I)
		nfc_llcp_send_rr(llcp_sock);

	release_sock(sk);
	nfc_llcp_sock_put(llcp_sock);
}

static void nfc_llcp_recv_disc(struct nfc_llcp_local *local,
			       const struct sk_buff *skb)
{
	struct nfc_llcp_sock *llcp_sock;
	struct sock *sk;
	u8 dsap, ssap;

	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);

	if ((dsap == 0) && (ssap == 0)) {
		pr_debug("Connection termination");
		nfc_dep_link_down(local->dev);
		return;
	}

	llcp_sock = nfc_llcp_sock_get(local, dsap, ssap);
	if (llcp_sock == NULL) {
		nfc_llcp_send_dm(local, dsap, ssap, LLCP_DM_NOCONN);
		return;
	}

	sk = &llcp_sock->sk;
	lock_sock(sk);

	nfc_llcp_socket_purge(llcp_sock);

	if (sk->sk_state == LLCP_CLOSED) {
		release_sock(sk);
		nfc_llcp_sock_put(llcp_sock);
	}

	if (sk->sk_state == LLCP_CONNECTED) {
		nfc_put_device(local->dev);
		sk->sk_state = LLCP_CLOSED;
		sk->sk_state_change(sk);
	}

	nfc_llcp_send_dm(local, dsap, ssap, LLCP_DM_DISC);

	release_sock(sk);
	nfc_llcp_sock_put(llcp_sock);
}

static void nfc_llcp_recv_cc(struct nfc_llcp_local *local,
			     const struct sk_buff *skb)
{
	struct nfc_llcp_sock *llcp_sock;
	struct sock *sk;
	u8 dsap, ssap;

	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);

	llcp_sock = nfc_llcp_connecting_sock_get(local, dsap);
	if (llcp_sock == NULL) {
		pr_err("Invalid CC\n");
		nfc_llcp_send_dm(local, dsap, ssap, LLCP_DM_NOCONN);

		return;
	}

	sk = &llcp_sock->sk;

	/* Unlink from connecting and link to the client array */
	nfc_llcp_sock_unlink(&local->connecting_sockets, sk);
	nfc_llcp_sock_link(&local->sockets, sk);
	llcp_sock->dsap = ssap;

	nfc_llcp_parse_connection_tlv(llcp_sock, &skb->data[LLCP_HEADER_SIZE],
				      skb->len - LLCP_HEADER_SIZE);

	sk->sk_state = LLCP_CONNECTED;
	sk->sk_state_change(sk);

	nfc_llcp_sock_put(llcp_sock);
}

static void nfc_llcp_recv_dm(struct nfc_llcp_local *local,
			     const struct sk_buff *skb)
{
	struct nfc_llcp_sock *llcp_sock;
	struct sock *sk;
	u8 dsap, ssap, reason;

	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);
	reason = skb->data[2];

	pr_debug("%d %d reason %d\n", ssap, dsap, reason);

	switch (reason) {
	case LLCP_DM_NOBOUND:
	case LLCP_DM_REJ:
		llcp_sock = nfc_llcp_connecting_sock_get(local, dsap);
		break;

	default:
		llcp_sock = nfc_llcp_sock_get(local, dsap, ssap);
		break;
	}

	if (llcp_sock == NULL) {
		pr_debug("Already closed\n");
		return;
	}

	sk = &llcp_sock->sk;

	sk->sk_err = ENXIO;
	sk->sk_state = LLCP_CLOSED;
	sk->sk_state_change(sk);

	nfc_llcp_sock_put(llcp_sock);
}

static void nfc_llcp_recv_snl(struct nfc_llcp_local *local,
			      const struct sk_buff *skb)
{
	struct nfc_llcp_sock *llcp_sock;
	u8 dsap, ssap, type, length, tid, sap;
	const u8 *tlv;
	u16 tlv_len, offset;
	const char *service_name;
	size_t service_name_len;
	struct nfc_llcp_sdp_tlv *sdp;
	HLIST_HEAD(llc_sdres_list);
	size_t sdres_tlvs_len;
	HLIST_HEAD(nl_sdres_list);

	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);

	pr_debug("%d %d\n", dsap, ssap);

	if (dsap != LLCP_SAP_SDP || ssap != LLCP_SAP_SDP) {
		pr_err("Wrong SNL SAP\n");
		return;
	}

	tlv = &skb->data[LLCP_HEADER_SIZE];
	tlv_len = skb->len - LLCP_HEADER_SIZE;
	offset = 0;
	sdres_tlvs_len = 0;

	while (offset < tlv_len) {
		type = tlv[0];
		length = tlv[1];

		switch (type) {
		case LLCP_TLV_SDREQ:
			tid = tlv[2];
			service_name = (char *) &tlv[3];
			service_name_len = length - 1;

			pr_debug("Looking for %.16s\n", service_name);

			if (service_name_len == strlen("urn:nfc:sn:sdp") &&
			    !strncmp(service_name, "urn:nfc:sn:sdp",
				     service_name_len)) {
				sap = 1;
				goto add_snl;
			}

			llcp_sock = nfc_llcp_sock_from_sn(local, service_name,
							  service_name_len);
			if (!llcp_sock) {
				sap = 0;
				goto add_snl;
			}

			/*
			 * We found a socket but its ssap has not been reserved
			 * yet. We need to assign it for good and send a reply.
			 * The ssap will be freed when the socket is closed.
			 */
			if (llcp_sock->ssap == LLCP_SDP_UNBOUND) {
				atomic_t *client_count;

				sap = nfc_llcp_reserve_sdp_ssap(local);

				pr_debug("Reserving %d\n", sap);

				if (sap == LLCP_SAP_MAX) {
					sap = 0;
					goto add_snl;
				}

				client_count =
					&local->local_sdp_cnt[sap -
							      LLCP_WKS_NUM_SAP];

				atomic_inc(client_count);

				llcp_sock->ssap = sap;
				llcp_sock->reserved_ssap = sap;
			} else {
				sap = llcp_sock->ssap;
			}

			pr_debug("%p %d\n", llcp_sock, sap);

add_snl:
			sdp = nfc_llcp_build_sdres_tlv(tid, sap);
			if (sdp == NULL)
				goto exit;

			sdres_tlvs_len += sdp->tlv_len;
			hlist_add_head(&sdp->node, &llc_sdres_list);
			break;

		case LLCP_TLV_SDRES:
			mutex_lock(&local->sdreq_lock);

			pr_debug("LLCP_TLV_SDRES: searching tid %d\n", tlv[2]);

			hlist_for_each_entry(sdp, &local->pending_sdreqs, node) {
				if (sdp->tid != tlv[2])
					continue;

				sdp->sap = tlv[3];

				pr_debug("Found: uri=%s, sap=%d\n",
					 sdp->uri, sdp->sap);

				hlist_del(&sdp->node);

				hlist_add_head(&sdp->node, &nl_sdres_list);

				break;
			}

			mutex_unlock(&local->sdreq_lock);
			break;

		default:
			pr_err("Invalid SNL tlv value 0x%x\n", type);
			break;
		}

		offset += length + 2;
		tlv += length + 2;
	}

exit:
	if (!hlist_empty(&nl_sdres_list))
		nfc_genl_llc_send_sdres(local->dev, &nl_sdres_list);

	if (!hlist_empty(&llc_sdres_list))
		nfc_llcp_send_snl_sdres(local, &llc_sdres_list, sdres_tlvs_len);
}

static void nfc_llcp_recv_agf(struct nfc_llcp_local *local, struct sk_buff *skb)
{
	u8 ptype;
	u16 pdu_len;
	struct sk_buff *new_skb;

	if (skb->len <= LLCP_HEADER_SIZE) {
		pr_err("Malformed AGF PDU\n");
		return;
	}

	skb_pull(skb, LLCP_HEADER_SIZE);

	while (skb->len > LLCP_AGF_PDU_HEADER_SIZE) {
		pdu_len = skb->data[0] << 8 | skb->data[1];

		skb_pull(skb, LLCP_AGF_PDU_HEADER_SIZE);

		if (pdu_len < LLCP_HEADER_SIZE || pdu_len > skb->len) {
			pr_err("Malformed AGF PDU\n");
			return;
		}

		ptype = nfc_llcp_ptype(skb);

		if (ptype == LLCP_PDU_SYMM || ptype == LLCP_PDU_AGF)
			goto next;

		new_skb = nfc_alloc_recv_skb(pdu_len, GFP_KERNEL);
		if (new_skb == NULL) {
			pr_err("Could not allocate PDU\n");
			return;
		}

		skb_put_data(new_skb, skb->data, pdu_len);

		nfc_llcp_rx_skb(local, new_skb);

		kfree_skb(new_skb);
next:
		skb_pull(skb, pdu_len);
	}
}

static void nfc_llcp_rx_skb(struct nfc_llcp_local *local, struct sk_buff *skb)
{
	u8 dsap, ssap, ptype;

	ptype = nfc_llcp_ptype(skb);
	dsap = nfc_llcp_dsap(skb);
	ssap = nfc_llcp_ssap(skb);

	pr_debug("ptype 0x%x dsap 0x%x ssap 0x%x\n", ptype, dsap, ssap);

	if (ptype != LLCP_PDU_SYMM)
		print_hex_dump_debug("LLCP Rx: ", DUMP_PREFIX_OFFSET, 16, 1,
				     skb->data, skb->len, true);

	switch (ptype) {
	case LLCP_PDU_SYMM:
		pr_debug("SYMM\n");
		break;

	case LLCP_PDU_UI:
		pr_debug("UI\n");
		nfc_llcp_recv_ui(local, skb);
		break;

	case LLCP_PDU_CONNECT:
		pr_debug("CONNECT\n");
		nfc_llcp_recv_connect(local, skb);
		break;

	case LLCP_PDU_DISC:
		pr_debug("DISC\n");
		nfc_llcp_recv_disc(local, skb);
		break;

	case LLCP_PDU_CC:
		pr_debug("CC\n");
		nfc_llcp_recv_cc(local, skb);
		break;

	case LLCP_PDU_DM:
		pr_debug("DM\n");
		nfc_llcp_recv_dm(local, skb);
		break;

	case LLCP_PDU_SNL:
		pr_debug("SNL\n");
		nfc_llcp_recv_snl(local, skb);
		break;

	case LLCP_PDU_I:
	case LLCP_PDU_RR:
	case LLCP_PDU_RNR:
		pr_debug("I frame\n");
		nfc_llcp_recv_hdlc(local, skb);
		break;

	case LLCP_PDU_AGF:
		pr_debug("AGF frame\n");
		nfc_llcp_recv_agf(local, skb);
		break;
	}
}

static void nfc_llcp_rx_work(struct work_struct *work)
{
	struct nfc_llcp_local *local = container_of(work, struct nfc_llcp_local,
						    rx_work);
	struct sk_buff *skb;

	skb = local->rx_pending;
	if (skb == NULL) {
		pr_debug("No pending SKB\n");
		return;
	}

	__net_timestamp(skb);

	nfc_llcp_send_to_raw_sock(local, skb, NFC_DIRECTION_RX);

	nfc_llcp_rx_skb(local, skb);

	schedule_work(&local->tx_work);
	kfree_skb(local->rx_pending);
	local->rx_pending = NULL;
}

static void __nfc_llcp_recv(struct nfc_llcp_local *local, struct sk_buff *skb)
{
	local->rx_pending = skb;
	del_timer(&local->link_timer);
	schedule_work(&local->rx_work);
}

void nfc_llcp_recv(void *data, struct sk_buff *skb, int err)
{
	struct nfc_llcp_local *local = (struct nfc_llcp_local *) data;

	pr_debug("Received an LLCP PDU\n");
	if (err < 0) {
		pr_err("err %d\n", err);
		return;
	}

	__nfc_llcp_recv(local, skb);
}

int nfc_llcp_data_received(struct nfc_dev *dev, struct sk_buff *skb)
{
	struct nfc_llcp_local *local;

	local = nfc_llcp_find_local(dev);
	if (local == NULL) {
		kfree_skb(skb);
		return -ENODEV;
	}

	__nfc_llcp_recv(local, skb);

	return 0;
}

void nfc_llcp_mac_is_down(struct nfc_dev *dev)
{
	struct nfc_llcp_local *local;

	local = nfc_llcp_find_local(dev);
	if (local == NULL)
		return;

	local->remote_miu = LLCP_DEFAULT_MIU;
	local->remote_lto = LLCP_DEFAULT_LTO;

	/* Close and purge all existing sockets */
	nfc_llcp_socket_release(local, true, 0);
}

void nfc_llcp_mac_is_up(struct nfc_dev *dev, u32 target_idx,
			u8 comm_mode, u8 rf_mode)
{
	struct nfc_llcp_local *local;

	pr_debug("rf mode %d\n", rf_mode);

	local = nfc_llcp_find_local(dev);
	if (local == NULL)
		return;

	local->target_idx = target_idx;
	local->comm_mode = comm_mode;
	local->rf_mode = rf_mode;

	if (rf_mode == NFC_RF_INITIATOR) {
		pr_debug("Queueing Tx work\n");

		schedule_work(&local->tx_work);
	} else {
		mod_timer(&local->link_timer,
			  jiffies + msecs_to_jiffies(local->remote_lto));
	}
}

int nfc_llcp_register_device(struct nfc_dev *ndev)
{
	struct nfc_llcp_local *local;

	local = kzalloc(sizeof(struct nfc_llcp_local), GFP_KERNEL);
	if (local == NULL)
		return -ENOMEM;

	local->dev = ndev;
	INIT_LIST_HEAD(&local->list);
	kref_init(&local->ref);
	mutex_init(&local->sdp_lock);
	timer_setup(&local->link_timer, nfc_llcp_symm_timer, 0);

	skb_queue_head_init(&local->tx_queue);
	INIT_WORK(&local->tx_work, nfc_llcp_tx_work);

	local->rx_pending = NULL;
	INIT_WORK(&local->rx_work, nfc_llcp_rx_work);

	INIT_WORK(&local->timeout_work, nfc_llcp_timeout_work);

	rwlock_init(&local->sockets.lock);
	rwlock_init(&local->connecting_sockets.lock);
	rwlock_init(&local->raw_sockets.lock);

	local->lto = 150; /* 1500 ms */
	local->rw = LLCP_MAX_RW;
	local->miux = cpu_to_be16(LLCP_MAX_MIUX);
	local->local_wks = 0x1; /* LLC Link Management */

	nfc_llcp_build_gb(local);

	local->remote_miu = LLCP_DEFAULT_MIU;
	local->remote_lto = LLCP_DEFAULT_LTO;

	mutex_init(&local->sdreq_lock);
	INIT_HLIST_HEAD(&local->pending_sdreqs);
	timer_setup(&local->sdreq_timer, nfc_llcp_sdreq_timer, 0);
	INIT_WORK(&local->sdreq_timeout_work, nfc_llcp_sdreq_timeout_work);

	list_add(&local->list, &llcp_devices);

	return 0;
}

void nfc_llcp_unregister_device(struct nfc_dev *dev)
{
	struct nfc_llcp_local *local = nfc_llcp_find_local(dev);

	if (local == NULL) {
		pr_debug("No such device\n");
		return;
	}

	local_cleanup(local);

	nfc_llcp_local_put(local);
}

int __init nfc_llcp_init(void)
{
	return nfc_llcp_sock_init();
}

void nfc_llcp_exit(void)
{
	nfc_llcp_sock_exit();
}
