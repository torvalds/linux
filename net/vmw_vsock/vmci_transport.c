// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2007-2013 VMware, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/smp.h>
#include <linux/socket.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <net/af_vsock.h>

#include "vmci_transport_notify.h"

static int vmci_transport_recv_dgram_cb(void *data, struct vmci_datagram *dg);
static int vmci_transport_recv_stream_cb(void *data, struct vmci_datagram *dg);
static void vmci_transport_peer_detach_cb(u32 sub_id,
					  const struct vmci_event_data *ed,
					  void *client_data);
static void vmci_transport_recv_pkt_work(struct work_struct *work);
static void vmci_transport_cleanup(struct work_struct *work);
static int vmci_transport_recv_listen(struct sock *sk,
				      struct vmci_transport_packet *pkt);
static int vmci_transport_recv_connecting_server(
					struct sock *sk,
					struct sock *pending,
					struct vmci_transport_packet *pkt);
static int vmci_transport_recv_connecting_client(
					struct sock *sk,
					struct vmci_transport_packet *pkt);
static int vmci_transport_recv_connecting_client_negotiate(
					struct sock *sk,
					struct vmci_transport_packet *pkt);
static int vmci_transport_recv_connecting_client_invalid(
					struct sock *sk,
					struct vmci_transport_packet *pkt);
static int vmci_transport_recv_connected(struct sock *sk,
					 struct vmci_transport_packet *pkt);
static bool vmci_transport_old_proto_override(bool *old_pkt_proto);
static u16 vmci_transport_new_proto_supported_versions(void);
static bool vmci_transport_proto_to_notify_struct(struct sock *sk, u16 *proto,
						  bool old_pkt_proto);
static bool vmci_check_transport(struct vsock_sock *vsk);

struct vmci_transport_recv_pkt_info {
	struct work_struct work;
	struct sock *sk;
	struct vmci_transport_packet pkt;
};

static LIST_HEAD(vmci_transport_cleanup_list);
static DEFINE_SPINLOCK(vmci_transport_cleanup_lock);
static DECLARE_WORK(vmci_transport_cleanup_work, vmci_transport_cleanup);

static struct vmci_handle vmci_transport_stream_handle = { VMCI_INVALID_ID,
							   VMCI_INVALID_ID };
static u32 vmci_transport_qp_resumed_sub_id = VMCI_INVALID_ID;

static int PROTOCOL_OVERRIDE = -1;

static struct vsock_transport vmci_transport; /* forward declaration */

/* Helper function to convert from a VMCI error code to a VSock error code. */

static s32 vmci_transport_error_to_vsock_error(s32 vmci_error)
{
	switch (vmci_error) {
	case VMCI_ERROR_NO_MEM:
		return -ENOMEM;
	case VMCI_ERROR_DUPLICATE_ENTRY:
	case VMCI_ERROR_ALREADY_EXISTS:
		return -EADDRINUSE;
	case VMCI_ERROR_NO_ACCESS:
		return -EPERM;
	case VMCI_ERROR_NO_RESOURCES:
		return -ENOBUFS;
	case VMCI_ERROR_INVALID_RESOURCE:
		return -EHOSTUNREACH;
	case VMCI_ERROR_INVALID_ARGS:
	default:
		break;
	}
	return -EINVAL;
}

static u32 vmci_transport_peer_rid(u32 peer_cid)
{
	if (VMADDR_CID_HYPERVISOR == peer_cid)
		return VMCI_TRANSPORT_HYPERVISOR_PACKET_RID;

	return VMCI_TRANSPORT_PACKET_RID;
}

static inline void
vmci_transport_packet_init(struct vmci_transport_packet *pkt,
			   struct sockaddr_vm *src,
			   struct sockaddr_vm *dst,
			   u8 type,
			   u64 size,
			   u64 mode,
			   struct vmci_transport_waiting_info *wait,
			   u16 proto,
			   struct vmci_handle handle)
{
	/* We register the stream control handler as an any cid handle so we
	 * must always send from a source address of VMADDR_CID_ANY
	 */
	pkt->dg.src = vmci_make_handle(VMADDR_CID_ANY,
				       VMCI_TRANSPORT_PACKET_RID);
	pkt->dg.dst = vmci_make_handle(dst->svm_cid,
				       vmci_transport_peer_rid(dst->svm_cid));
	pkt->dg.payload_size = sizeof(*pkt) - sizeof(pkt->dg);
	pkt->version = VMCI_TRANSPORT_PACKET_VERSION;
	pkt->type = type;
	pkt->src_port = src->svm_port;
	pkt->dst_port = dst->svm_port;
	memset(&pkt->proto, 0, sizeof(pkt->proto));
	memset(&pkt->_reserved2, 0, sizeof(pkt->_reserved2));

	switch (pkt->type) {
	case VMCI_TRANSPORT_PACKET_TYPE_INVALID:
		pkt->u.size = 0;
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_REQUEST:
	case VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE:
		pkt->u.size = size;
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_OFFER:
	case VMCI_TRANSPORT_PACKET_TYPE_ATTACH:
		pkt->u.handle = handle;
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_WROTE:
	case VMCI_TRANSPORT_PACKET_TYPE_READ:
	case VMCI_TRANSPORT_PACKET_TYPE_RST:
		pkt->u.size = 0;
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_SHUTDOWN:
		pkt->u.mode = mode;
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_WAITING_READ:
	case VMCI_TRANSPORT_PACKET_TYPE_WAITING_WRITE:
		memcpy(&pkt->u.wait, wait, sizeof(pkt->u.wait));
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_REQUEST2:
	case VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE2:
		pkt->u.size = size;
		pkt->proto = proto;
		break;
	}
}

static inline void
vmci_transport_packet_get_addresses(struct vmci_transport_packet *pkt,
				    struct sockaddr_vm *local,
				    struct sockaddr_vm *remote)
{
	vsock_addr_init(local, pkt->dg.dst.context, pkt->dst_port);
	vsock_addr_init(remote, pkt->dg.src.context, pkt->src_port);
}

static int
__vmci_transport_send_control_pkt(struct vmci_transport_packet *pkt,
				  struct sockaddr_vm *src,
				  struct sockaddr_vm *dst,
				  enum vmci_transport_packet_type type,
				  u64 size,
				  u64 mode,
				  struct vmci_transport_waiting_info *wait,
				  u16 proto,
				  struct vmci_handle handle,
				  bool convert_error)
{
	int err;

	vmci_transport_packet_init(pkt, src, dst, type, size, mode, wait,
				   proto, handle);
	err = vmci_datagram_send(&pkt->dg);
	if (convert_error && (err < 0))
		return vmci_transport_error_to_vsock_error(err);

	return err;
}

static int
vmci_transport_reply_control_pkt_fast(struct vmci_transport_packet *pkt,
				      enum vmci_transport_packet_type type,
				      u64 size,
				      u64 mode,
				      struct vmci_transport_waiting_info *wait,
				      struct vmci_handle handle)
{
	struct vmci_transport_packet reply;
	struct sockaddr_vm src, dst;

	if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_RST) {
		return 0;
	} else {
		vmci_transport_packet_get_addresses(pkt, &src, &dst);
		return __vmci_transport_send_control_pkt(&reply, &src, &dst,
							 type,
							 size, mode, wait,
							 VSOCK_PROTO_INVALID,
							 handle, true);
	}
}

static int
vmci_transport_send_control_pkt_bh(struct sockaddr_vm *src,
				   struct sockaddr_vm *dst,
				   enum vmci_transport_packet_type type,
				   u64 size,
				   u64 mode,
				   struct vmci_transport_waiting_info *wait,
				   struct vmci_handle handle)
{
	/* Note that it is safe to use a single packet across all CPUs since
	 * two tasklets of the same type are guaranteed to not ever run
	 * simultaneously. If that ever changes, or VMCI stops using tasklets,
	 * we can use per-cpu packets.
	 */
	static struct vmci_transport_packet pkt;

	return __vmci_transport_send_control_pkt(&pkt, src, dst, type,
						 size, mode, wait,
						 VSOCK_PROTO_INVALID, handle,
						 false);
}

static int
vmci_transport_alloc_send_control_pkt(struct sockaddr_vm *src,
				      struct sockaddr_vm *dst,
				      enum vmci_transport_packet_type type,
				      u64 size,
				      u64 mode,
				      struct vmci_transport_waiting_info *wait,
				      u16 proto,
				      struct vmci_handle handle)
{
	struct vmci_transport_packet *pkt;
	int err;

	pkt = kmalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	err = __vmci_transport_send_control_pkt(pkt, src, dst, type, size,
						mode, wait, proto, handle,
						true);
	kfree(pkt);

	return err;
}

static int
vmci_transport_send_control_pkt(struct sock *sk,
				enum vmci_transport_packet_type type,
				u64 size,
				u64 mode,
				struct vmci_transport_waiting_info *wait,
				u16 proto,
				struct vmci_handle handle)
{
	struct vsock_sock *vsk;

	vsk = vsock_sk(sk);

	if (!vsock_addr_bound(&vsk->local_addr))
		return -EINVAL;

	if (!vsock_addr_bound(&vsk->remote_addr))
		return -EINVAL;

	return vmci_transport_alloc_send_control_pkt(&vsk->local_addr,
						     &vsk->remote_addr,
						     type, size, mode,
						     wait, proto, handle);
}

static int vmci_transport_send_reset_bh(struct sockaddr_vm *dst,
					struct sockaddr_vm *src,
					struct vmci_transport_packet *pkt)
{
	if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_RST)
		return 0;
	return vmci_transport_send_control_pkt_bh(
					dst, src,
					VMCI_TRANSPORT_PACKET_TYPE_RST, 0,
					0, NULL, VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_reset(struct sock *sk,
				     struct vmci_transport_packet *pkt)
{
	struct sockaddr_vm *dst_ptr;
	struct sockaddr_vm dst;
	struct vsock_sock *vsk;

	if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_RST)
		return 0;

	vsk = vsock_sk(sk);

	if (!vsock_addr_bound(&vsk->local_addr))
		return -EINVAL;

	if (vsock_addr_bound(&vsk->remote_addr)) {
		dst_ptr = &vsk->remote_addr;
	} else {
		vsock_addr_init(&dst, pkt->dg.src.context,
				pkt->src_port);
		dst_ptr = &dst;
	}
	return vmci_transport_alloc_send_control_pkt(&vsk->local_addr, dst_ptr,
					     VMCI_TRANSPORT_PACKET_TYPE_RST,
					     0, 0, NULL, VSOCK_PROTO_INVALID,
					     VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_negotiate(struct sock *sk, size_t size)
{
	return vmci_transport_send_control_pkt(
					sk,
					VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE,
					size, 0, NULL,
					VSOCK_PROTO_INVALID,
					VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_negotiate2(struct sock *sk, size_t size,
					  u16 version)
{
	return vmci_transport_send_control_pkt(
					sk,
					VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE2,
					size, 0, NULL, version,
					VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_qp_offer(struct sock *sk,
					struct vmci_handle handle)
{
	return vmci_transport_send_control_pkt(
					sk, VMCI_TRANSPORT_PACKET_TYPE_OFFER, 0,
					0, NULL,
					VSOCK_PROTO_INVALID, handle);
}

static int vmci_transport_send_attach(struct sock *sk,
				      struct vmci_handle handle)
{
	return vmci_transport_send_control_pkt(
					sk, VMCI_TRANSPORT_PACKET_TYPE_ATTACH,
					0, 0, NULL, VSOCK_PROTO_INVALID,
					handle);
}

static int vmci_transport_reply_reset(struct vmci_transport_packet *pkt)
{
	return vmci_transport_reply_control_pkt_fast(
						pkt,
						VMCI_TRANSPORT_PACKET_TYPE_RST,
						0, 0, NULL,
						VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_invalid_bh(struct sockaddr_vm *dst,
					  struct sockaddr_vm *src)
{
	return vmci_transport_send_control_pkt_bh(
					dst, src,
					VMCI_TRANSPORT_PACKET_TYPE_INVALID,
					0, 0, NULL, VMCI_INVALID_HANDLE);
}

int vmci_transport_send_wrote_bh(struct sockaddr_vm *dst,
				 struct sockaddr_vm *src)
{
	return vmci_transport_send_control_pkt_bh(
					dst, src,
					VMCI_TRANSPORT_PACKET_TYPE_WROTE, 0,
					0, NULL, VMCI_INVALID_HANDLE);
}

int vmci_transport_send_read_bh(struct sockaddr_vm *dst,
				struct sockaddr_vm *src)
{
	return vmci_transport_send_control_pkt_bh(
					dst, src,
					VMCI_TRANSPORT_PACKET_TYPE_READ, 0,
					0, NULL, VMCI_INVALID_HANDLE);
}

int vmci_transport_send_wrote(struct sock *sk)
{
	return vmci_transport_send_control_pkt(
					sk, VMCI_TRANSPORT_PACKET_TYPE_WROTE, 0,
					0, NULL, VSOCK_PROTO_INVALID,
					VMCI_INVALID_HANDLE);
}

int vmci_transport_send_read(struct sock *sk)
{
	return vmci_transport_send_control_pkt(
					sk, VMCI_TRANSPORT_PACKET_TYPE_READ, 0,
					0, NULL, VSOCK_PROTO_INVALID,
					VMCI_INVALID_HANDLE);
}

int vmci_transport_send_waiting_write(struct sock *sk,
				      struct vmci_transport_waiting_info *wait)
{
	return vmci_transport_send_control_pkt(
				sk, VMCI_TRANSPORT_PACKET_TYPE_WAITING_WRITE,
				0, 0, wait, VSOCK_PROTO_INVALID,
				VMCI_INVALID_HANDLE);
}

int vmci_transport_send_waiting_read(struct sock *sk,
				     struct vmci_transport_waiting_info *wait)
{
	return vmci_transport_send_control_pkt(
				sk, VMCI_TRANSPORT_PACKET_TYPE_WAITING_READ,
				0, 0, wait, VSOCK_PROTO_INVALID,
				VMCI_INVALID_HANDLE);
}

static int vmci_transport_shutdown(struct vsock_sock *vsk, int mode)
{
	return vmci_transport_send_control_pkt(
					&vsk->sk,
					VMCI_TRANSPORT_PACKET_TYPE_SHUTDOWN,
					0, mode, NULL,
					VSOCK_PROTO_INVALID,
					VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_conn_request(struct sock *sk, size_t size)
{
	return vmci_transport_send_control_pkt(sk,
					VMCI_TRANSPORT_PACKET_TYPE_REQUEST,
					size, 0, NULL,
					VSOCK_PROTO_INVALID,
					VMCI_INVALID_HANDLE);
}

static int vmci_transport_send_conn_request2(struct sock *sk, size_t size,
					     u16 version)
{
	return vmci_transport_send_control_pkt(
					sk, VMCI_TRANSPORT_PACKET_TYPE_REQUEST2,
					size, 0, NULL, version,
					VMCI_INVALID_HANDLE);
}

static struct sock *vmci_transport_get_pending(
					struct sock *listener,
					struct vmci_transport_packet *pkt)
{
	struct vsock_sock *vlistener;
	struct vsock_sock *vpending;
	struct sock *pending;
	struct sockaddr_vm src;

	vsock_addr_init(&src, pkt->dg.src.context, pkt->src_port);

	vlistener = vsock_sk(listener);

	list_for_each_entry(vpending, &vlistener->pending_links,
			    pending_links) {
		if (vsock_addr_equals_addr(&src, &vpending->remote_addr) &&
		    pkt->dst_port == vpending->local_addr.svm_port) {
			pending = sk_vsock(vpending);
			sock_hold(pending);
			goto found;
		}
	}

	pending = NULL;
found:
	return pending;

}

static void vmci_transport_release_pending(struct sock *pending)
{
	sock_put(pending);
}

/* We allow two kinds of sockets to communicate with a restricted VM: 1)
 * trusted sockets 2) sockets from applications running as the same user as the
 * VM (this is only true for the host side and only when using hosted products)
 */

static bool vmci_transport_is_trusted(struct vsock_sock *vsock, u32 peer_cid)
{
	return vsock->trusted ||
	       vmci_is_context_owner(peer_cid, vsock->owner->uid);
}

/* We allow sending datagrams to and receiving datagrams from a restricted VM
 * only if it is trusted as described in vmci_transport_is_trusted.
 */

static bool vmci_transport_allow_dgram(struct vsock_sock *vsock, u32 peer_cid)
{
	if (VMADDR_CID_HYPERVISOR == peer_cid)
		return true;

	if (vsock->cached_peer != peer_cid) {
		vsock->cached_peer = peer_cid;
		if (!vmci_transport_is_trusted(vsock, peer_cid) &&
		    (vmci_context_get_priv_flags(peer_cid) &
		     VMCI_PRIVILEGE_FLAG_RESTRICTED)) {
			vsock->cached_peer_allow_dgram = false;
		} else {
			vsock->cached_peer_allow_dgram = true;
		}
	}

	return vsock->cached_peer_allow_dgram;
}

static int
vmci_transport_queue_pair_alloc(struct vmci_qp **qpair,
				struct vmci_handle *handle,
				u64 produce_size,
				u64 consume_size,
				u32 peer, u32 flags, bool trusted)
{
	int err = 0;

	if (trusted) {
		/* Try to allocate our queue pair as trusted. This will only
		 * work if vsock is running in the host.
		 */

		err = vmci_qpair_alloc(qpair, handle, produce_size,
				       consume_size,
				       peer, flags,
				       VMCI_PRIVILEGE_FLAG_TRUSTED);
		if (err != VMCI_ERROR_NO_ACCESS)
			goto out;

	}

	err = vmci_qpair_alloc(qpair, handle, produce_size, consume_size,
			       peer, flags, VMCI_NO_PRIVILEGE_FLAGS);
out:
	if (err < 0) {
		pr_err_once("Could not attach to queue pair with %d\n", err);
		err = vmci_transport_error_to_vsock_error(err);
	}

	return err;
}

static int
vmci_transport_datagram_create_hnd(u32 resource_id,
				   u32 flags,
				   vmci_datagram_recv_cb recv_cb,
				   void *client_data,
				   struct vmci_handle *out_handle)
{
	int err = 0;

	/* Try to allocate our datagram handler as trusted. This will only work
	 * if vsock is running in the host.
	 */

	err = vmci_datagram_create_handle_priv(resource_id, flags,
					       VMCI_PRIVILEGE_FLAG_TRUSTED,
					       recv_cb,
					       client_data, out_handle);

	if (err == VMCI_ERROR_NO_ACCESS)
		err = vmci_datagram_create_handle(resource_id, flags,
						  recv_cb, client_data,
						  out_handle);

	return err;
}

/* This is invoked as part of a tasklet that's scheduled when the VMCI
 * interrupt fires.  This is run in bottom-half context and if it ever needs to
 * sleep it should defer that work to a work queue.
 */

static int vmci_transport_recv_dgram_cb(void *data, struct vmci_datagram *dg)
{
	struct sock *sk;
	size_t size;
	struct sk_buff *skb;
	struct vsock_sock *vsk;

	sk = (struct sock *)data;

	/* This handler is privileged when this module is running on the host.
	 * We will get datagrams from all endpoints (even VMs that are in a
	 * restricted context). If we get one from a restricted context then
	 * the destination socket must be trusted.
	 *
	 * NOTE: We access the socket struct without holding the lock here.
	 * This is ok because the field we are interested is never modified
	 * outside of the create and destruct socket functions.
	 */
	vsk = vsock_sk(sk);
	if (!vmci_transport_allow_dgram(vsk, dg->src.context))
		return VMCI_ERROR_NO_ACCESS;

	size = VMCI_DG_SIZE(dg);

	/* Attach the packet to the socket's receive queue as an sk_buff. */
	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return VMCI_ERROR_NO_MEM;

	/* sk_receive_skb() will do a sock_put(), so hold here. */
	sock_hold(sk);
	skb_put(skb, size);
	memcpy(skb->data, dg, size);
	sk_receive_skb(sk, skb, 0);

	return VMCI_SUCCESS;
}

static bool vmci_transport_stream_allow(u32 cid, u32 port)
{
	static const u32 non_socket_contexts[] = {
		VMADDR_CID_LOCAL,
	};
	int i;

	BUILD_BUG_ON(sizeof(cid) != sizeof(*non_socket_contexts));

	for (i = 0; i < ARRAY_SIZE(non_socket_contexts); i++) {
		if (cid == non_socket_contexts[i])
			return false;
	}

	return true;
}

/* This is invoked as part of a tasklet that's scheduled when the VMCI
 * interrupt fires.  This is run in bottom-half context but it defers most of
 * its work to the packet handling work queue.
 */

static int vmci_transport_recv_stream_cb(void *data, struct vmci_datagram *dg)
{
	struct sock *sk;
	struct sockaddr_vm dst;
	struct sockaddr_vm src;
	struct vmci_transport_packet *pkt;
	struct vsock_sock *vsk;
	bool bh_process_pkt;
	int err;

	sk = NULL;
	err = VMCI_SUCCESS;
	bh_process_pkt = false;

	/* Ignore incoming packets from contexts without sockets, or resources
	 * that aren't vsock implementations.
	 */

	if (!vmci_transport_stream_allow(dg->src.context, -1)
	    || vmci_transport_peer_rid(dg->src.context) != dg->src.resource)
		return VMCI_ERROR_NO_ACCESS;

	if (VMCI_DG_SIZE(dg) < sizeof(*pkt))
		/* Drop datagrams that do not contain full VSock packets. */
		return VMCI_ERROR_INVALID_ARGS;

	pkt = (struct vmci_transport_packet *)dg;

	/* Find the socket that should handle this packet.  First we look for a
	 * connected socket and if there is none we look for a socket bound to
	 * the destintation address.
	 */
	vsock_addr_init(&src, pkt->dg.src.context, pkt->src_port);
	vsock_addr_init(&dst, pkt->dg.dst.context, pkt->dst_port);

	sk = vsock_find_connected_socket(&src, &dst);
	if (!sk) {
		sk = vsock_find_bound_socket(&dst);
		if (!sk) {
			/* We could not find a socket for this specified
			 * address.  If this packet is a RST, we just drop it.
			 * If it is another packet, we send a RST.  Note that
			 * we do not send a RST reply to RSTs so that we do not
			 * continually send RSTs between two endpoints.
			 *
			 * Note that since this is a reply, dst is src and src
			 * is dst.
			 */
			if (vmci_transport_send_reset_bh(&dst, &src, pkt) < 0)
				pr_err("unable to send reset\n");

			err = VMCI_ERROR_NOT_FOUND;
			goto out;
		}
	}

	/* If the received packet type is beyond all types known to this
	 * implementation, reply with an invalid message.  Hopefully this will
	 * help when implementing backwards compatibility in the future.
	 */
	if (pkt->type >= VMCI_TRANSPORT_PACKET_TYPE_MAX) {
		vmci_transport_send_invalid_bh(&dst, &src);
		err = VMCI_ERROR_INVALID_ARGS;
		goto out;
	}

	/* This handler is privileged when this module is running on the host.
	 * We will get datagram connect requests from all endpoints (even VMs
	 * that are in a restricted context). If we get one from a restricted
	 * context then the destination socket must be trusted.
	 *
	 * NOTE: We access the socket struct without holding the lock here.
	 * This is ok because the field we are interested is never modified
	 * outside of the create and destruct socket functions.
	 */
	vsk = vsock_sk(sk);
	if (!vmci_transport_allow_dgram(vsk, pkt->dg.src.context)) {
		err = VMCI_ERROR_NO_ACCESS;
		goto out;
	}

	/* We do most everything in a work queue, but let's fast path the
	 * notification of reads and writes to help data transfer performance.
	 * We can only do this if there is no process context code executing
	 * for this socket since that may change the state.
	 */
	bh_lock_sock(sk);

	if (!sock_owned_by_user(sk)) {
		/* The local context ID may be out of date, update it. */
		vsk->local_addr.svm_cid = dst.svm_cid;

		if (sk->sk_state == TCP_ESTABLISHED)
			vmci_trans(vsk)->notify_ops->handle_notify_pkt(
					sk, pkt, true, &dst, &src,
					&bh_process_pkt);
	}

	bh_unlock_sock(sk);

	if (!bh_process_pkt) {
		struct vmci_transport_recv_pkt_info *recv_pkt_info;

		recv_pkt_info = kmalloc(sizeof(*recv_pkt_info), GFP_ATOMIC);
		if (!recv_pkt_info) {
			if (vmci_transport_send_reset_bh(&dst, &src, pkt) < 0)
				pr_err("unable to send reset\n");

			err = VMCI_ERROR_NO_MEM;
			goto out;
		}

		recv_pkt_info->sk = sk;
		memcpy(&recv_pkt_info->pkt, pkt, sizeof(recv_pkt_info->pkt));
		INIT_WORK(&recv_pkt_info->work, vmci_transport_recv_pkt_work);

		schedule_work(&recv_pkt_info->work);
		/* Clear sk so that the reference count incremented by one of
		 * the Find functions above is not decremented below.  We need
		 * that reference count for the packet handler we've scheduled
		 * to run.
		 */
		sk = NULL;
	}

out:
	if (sk)
		sock_put(sk);

	return err;
}

static void vmci_transport_handle_detach(struct sock *sk)
{
	struct vsock_sock *vsk;

	vsk = vsock_sk(sk);
	if (!vmci_handle_is_invalid(vmci_trans(vsk)->qp_handle)) {
		sock_set_flag(sk, SOCK_DONE);

		/* On a detach the peer will not be sending or receiving
		 * anymore.
		 */
		vsk->peer_shutdown = SHUTDOWN_MASK;

		/* We should not be sending anymore since the peer won't be
		 * there to receive, but we can still receive if there is data
		 * left in our consume queue. If the local endpoint is a host,
		 * we can't call vsock_stream_has_data, since that may block,
		 * but a host endpoint can't read data once the VM has
		 * detached, so there is no available data in that case.
		 */
		if (vsk->local_addr.svm_cid == VMADDR_CID_HOST ||
		    vsock_stream_has_data(vsk) <= 0) {
			if (sk->sk_state == TCP_SYN_SENT) {
				/* The peer may detach from a queue pair while
				 * we are still in the connecting state, i.e.,
				 * if the peer VM is killed after attaching to
				 * a queue pair, but before we complete the
				 * handshake. In that case, we treat the detach
				 * event like a reset.
				 */

				sk->sk_state = TCP_CLOSE;
				sk->sk_err = ECONNRESET;
				sk_error_report(sk);
				return;
			}
			sk->sk_state = TCP_CLOSE;
		}
		sk->sk_state_change(sk);
	}
}

static void vmci_transport_peer_detach_cb(u32 sub_id,
					  const struct vmci_event_data *e_data,
					  void *client_data)
{
	struct vmci_transport *trans = client_data;
	const struct vmci_event_payload_qp *e_payload;

	e_payload = vmci_event_data_const_payload(e_data);

	/* XXX This is lame, we should provide a way to lookup sockets by
	 * qp_handle.
	 */
	if (vmci_handle_is_invalid(e_payload->handle) ||
	    !vmci_handle_is_equal(trans->qp_handle, e_payload->handle))
		return;

	/* We don't ask for delayed CBs when we subscribe to this event (we
	 * pass 0 as flags to vmci_event_subscribe()).  VMCI makes no
	 * guarantees in that case about what context we might be running in,
	 * so it could be BH or process, blockable or non-blockable.  So we
	 * need to account for all possible contexts here.
	 */
	spin_lock_bh(&trans->lock);
	if (!trans->sk)
		goto out;

	/* Apart from here, trans->lock is only grabbed as part of sk destruct,
	 * where trans->sk isn't locked.
	 */
	bh_lock_sock(trans->sk);

	vmci_transport_handle_detach(trans->sk);

	bh_unlock_sock(trans->sk);
 out:
	spin_unlock_bh(&trans->lock);
}

static void vmci_transport_qp_resumed_cb(u32 sub_id,
					 const struct vmci_event_data *e_data,
					 void *client_data)
{
	vsock_for_each_connected_socket(&vmci_transport,
					vmci_transport_handle_detach);
}

static void vmci_transport_recv_pkt_work(struct work_struct *work)
{
	struct vmci_transport_recv_pkt_info *recv_pkt_info;
	struct vmci_transport_packet *pkt;
	struct sock *sk;

	recv_pkt_info =
		container_of(work, struct vmci_transport_recv_pkt_info, work);
	sk = recv_pkt_info->sk;
	pkt = &recv_pkt_info->pkt;

	lock_sock(sk);

	/* The local context ID may be out of date. */
	vsock_sk(sk)->local_addr.svm_cid = pkt->dg.dst.context;

	switch (sk->sk_state) {
	case TCP_LISTEN:
		vmci_transport_recv_listen(sk, pkt);
		break;
	case TCP_SYN_SENT:
		/* Processing of pending connections for servers goes through
		 * the listening socket, so see vmci_transport_recv_listen()
		 * for that path.
		 */
		vmci_transport_recv_connecting_client(sk, pkt);
		break;
	case TCP_ESTABLISHED:
		vmci_transport_recv_connected(sk, pkt);
		break;
	default:
		/* Because this function does not run in the same context as
		 * vmci_transport_recv_stream_cb it is possible that the
		 * socket has closed. We need to let the other side know or it
		 * could be sitting in a connect and hang forever. Send a
		 * reset to prevent that.
		 */
		vmci_transport_send_reset(sk, pkt);
		break;
	}

	release_sock(sk);
	kfree(recv_pkt_info);
	/* Release reference obtained in the stream callback when we fetched
	 * this socket out of the bound or connected list.
	 */
	sock_put(sk);
}

static int vmci_transport_recv_listen(struct sock *sk,
				      struct vmci_transport_packet *pkt)
{
	struct sock *pending;
	struct vsock_sock *vpending;
	int err;
	u64 qp_size;
	bool old_request = false;
	bool old_pkt_proto = false;

	/* Because we are in the listen state, we could be receiving a packet
	 * for ourself or any previous connection requests that we received.
	 * If it's the latter, we try to find a socket in our list of pending
	 * connections and, if we do, call the appropriate handler for the
	 * state that that socket is in.  Otherwise we try to service the
	 * connection request.
	 */
	pending = vmci_transport_get_pending(sk, pkt);
	if (pending) {
		lock_sock(pending);

		/* The local context ID may be out of date. */
		vsock_sk(pending)->local_addr.svm_cid = pkt->dg.dst.context;

		switch (pending->sk_state) {
		case TCP_SYN_SENT:
			err = vmci_transport_recv_connecting_server(sk,
								    pending,
								    pkt);
			break;
		default:
			vmci_transport_send_reset(pending, pkt);
			err = -EINVAL;
		}

		if (err < 0)
			vsock_remove_pending(sk, pending);

		release_sock(pending);
		vmci_transport_release_pending(pending);

		return err;
	}

	/* The listen state only accepts connection requests.  Reply with a
	 * reset unless we received a reset.
	 */

	if (!(pkt->type == VMCI_TRANSPORT_PACKET_TYPE_REQUEST ||
	      pkt->type == VMCI_TRANSPORT_PACKET_TYPE_REQUEST2)) {
		vmci_transport_reply_reset(pkt);
		return -EINVAL;
	}

	if (pkt->u.size == 0) {
		vmci_transport_reply_reset(pkt);
		return -EINVAL;
	}

	/* If this socket can't accommodate this connection request, we send a
	 * reset.  Otherwise we create and initialize a child socket and reply
	 * with a connection negotiation.
	 */
	if (sk->sk_ack_backlog >= sk->sk_max_ack_backlog) {
		vmci_transport_reply_reset(pkt);
		return -ECONNREFUSED;
	}

	pending = vsock_create_connected(sk);
	if (!pending) {
		vmci_transport_send_reset(sk, pkt);
		return -ENOMEM;
	}

	vpending = vsock_sk(pending);

	vsock_addr_init(&vpending->local_addr, pkt->dg.dst.context,
			pkt->dst_port);
	vsock_addr_init(&vpending->remote_addr, pkt->dg.src.context,
			pkt->src_port);

	err = vsock_assign_transport(vpending, vsock_sk(sk));
	/* Transport assigned (looking at remote_addr) must be the same
	 * where we received the request.
	 */
	if (err || !vmci_check_transport(vpending)) {
		vmci_transport_send_reset(sk, pkt);
		sock_put(pending);
		return err;
	}

	/* If the proposed size fits within our min/max, accept it. Otherwise
	 * propose our own size.
	 */
	if (pkt->u.size >= vpending->buffer_min_size &&
	    pkt->u.size <= vpending->buffer_max_size) {
		qp_size = pkt->u.size;
	} else {
		qp_size = vpending->buffer_size;
	}

	/* Figure out if we are using old or new requests based on the
	 * overrides pkt types sent by our peer.
	 */
	if (vmci_transport_old_proto_override(&old_pkt_proto)) {
		old_request = old_pkt_proto;
	} else {
		if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_REQUEST)
			old_request = true;
		else if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_REQUEST2)
			old_request = false;

	}

	if (old_request) {
		/* Handle a REQUEST (or override) */
		u16 version = VSOCK_PROTO_INVALID;
		if (vmci_transport_proto_to_notify_struct(
			pending, &version, true))
			err = vmci_transport_send_negotiate(pending, qp_size);
		else
			err = -EINVAL;

	} else {
		/* Handle a REQUEST2 (or override) */
		int proto_int = pkt->proto;
		int pos;
		u16 active_proto_version = 0;

		/* The list of possible protocols is the intersection of all
		 * protocols the client supports ... plus all the protocols we
		 * support.
		 */
		proto_int &= vmci_transport_new_proto_supported_versions();

		/* We choose the highest possible protocol version and use that
		 * one.
		 */
		pos = fls(proto_int);
		if (pos) {
			active_proto_version = (1 << (pos - 1));
			if (vmci_transport_proto_to_notify_struct(
				pending, &active_proto_version, false))
				err = vmci_transport_send_negotiate2(pending,
							qp_size,
							active_proto_version);
			else
				err = -EINVAL;

		} else {
			err = -EINVAL;
		}
	}

	if (err < 0) {
		vmci_transport_send_reset(sk, pkt);
		sock_put(pending);
		err = vmci_transport_error_to_vsock_error(err);
		goto out;
	}

	vsock_add_pending(sk, pending);
	sk_acceptq_added(sk);

	pending->sk_state = TCP_SYN_SENT;
	vmci_trans(vpending)->produce_size =
		vmci_trans(vpending)->consume_size = qp_size;
	vpending->buffer_size = qp_size;

	vmci_trans(vpending)->notify_ops->process_request(pending);

	/* We might never receive another message for this socket and it's not
	 * connected to any process, so we have to ensure it gets cleaned up
	 * ourself.  Our delayed work function will take care of that.  Note
	 * that we do not ever cancel this function since we have few
	 * guarantees about its state when calling cancel_delayed_work().
	 * Instead we hold a reference on the socket for that function and make
	 * it capable of handling cases where it needs to do nothing but
	 * release that reference.
	 */
	vpending->listener = sk;
	sock_hold(sk);
	sock_hold(pending);
	schedule_delayed_work(&vpending->pending_work, HZ);

out:
	return err;
}

static int
vmci_transport_recv_connecting_server(struct sock *listener,
				      struct sock *pending,
				      struct vmci_transport_packet *pkt)
{
	struct vsock_sock *vpending;
	struct vmci_handle handle;
	struct vmci_qp *qpair;
	bool is_local;
	u32 flags;
	u32 detach_sub_id;
	int err;
	int skerr;

	vpending = vsock_sk(pending);
	detach_sub_id = VMCI_INVALID_ID;

	switch (pkt->type) {
	case VMCI_TRANSPORT_PACKET_TYPE_OFFER:
		if (vmci_handle_is_invalid(pkt->u.handle)) {
			vmci_transport_send_reset(pending, pkt);
			skerr = EPROTO;
			err = -EINVAL;
			goto destroy;
		}
		break;
	default:
		/* Close and cleanup the connection. */
		vmci_transport_send_reset(pending, pkt);
		skerr = EPROTO;
		err = pkt->type == VMCI_TRANSPORT_PACKET_TYPE_RST ? 0 : -EINVAL;
		goto destroy;
	}

	/* In order to complete the connection we need to attach to the offered
	 * queue pair and send an attach notification.  We also subscribe to the
	 * detach event so we know when our peer goes away, and we do that
	 * before attaching so we don't miss an event.  If all this succeeds,
	 * we update our state and wakeup anything waiting in accept() for a
	 * connection.
	 */

	/* We don't care about attach since we ensure the other side has
	 * attached by specifying the ATTACH_ONLY flag below.
	 */
	err = vmci_event_subscribe(VMCI_EVENT_QP_PEER_DETACH,
				   vmci_transport_peer_detach_cb,
				   vmci_trans(vpending), &detach_sub_id);
	if (err < VMCI_SUCCESS) {
		vmci_transport_send_reset(pending, pkt);
		err = vmci_transport_error_to_vsock_error(err);
		skerr = -err;
		goto destroy;
	}

	vmci_trans(vpending)->detach_sub_id = detach_sub_id;

	/* Now attach to the queue pair the client created. */
	handle = pkt->u.handle;

	/* vpending->local_addr always has a context id so we do not need to
	 * worry about VMADDR_CID_ANY in this case.
	 */
	is_local =
	    vpending->remote_addr.svm_cid == vpending->local_addr.svm_cid;
	flags = VMCI_QPFLAG_ATTACH_ONLY;
	flags |= is_local ? VMCI_QPFLAG_LOCAL : 0;

	err = vmci_transport_queue_pair_alloc(
					&qpair,
					&handle,
					vmci_trans(vpending)->produce_size,
					vmci_trans(vpending)->consume_size,
					pkt->dg.src.context,
					flags,
					vmci_transport_is_trusted(
						vpending,
						vpending->remote_addr.svm_cid));
	if (err < 0) {
		vmci_transport_send_reset(pending, pkt);
		skerr = -err;
		goto destroy;
	}

	vmci_trans(vpending)->qp_handle = handle;
	vmci_trans(vpending)->qpair = qpair;

	/* When we send the attach message, we must be ready to handle incoming
	 * control messages on the newly connected socket. So we move the
	 * pending socket to the connected state before sending the attach
	 * message. Otherwise, an incoming packet triggered by the attach being
	 * received by the peer may be processed concurrently with what happens
	 * below after sending the attach message, and that incoming packet
	 * will find the listening socket instead of the (currently) pending
	 * socket. Note that enqueueing the socket increments the reference
	 * count, so even if a reset comes before the connection is accepted,
	 * the socket will be valid until it is removed from the queue.
	 *
	 * If we fail sending the attach below, we remove the socket from the
	 * connected list and move the socket to TCP_CLOSE before
	 * releasing the lock, so a pending slow path processing of an incoming
	 * packet will not see the socket in the connected state in that case.
	 */
	pending->sk_state = TCP_ESTABLISHED;

	vsock_insert_connected(vpending);

	/* Notify our peer of our attach. */
	err = vmci_transport_send_attach(pending, handle);
	if (err < 0) {
		vsock_remove_connected(vpending);
		pr_err("Could not send attach\n");
		vmci_transport_send_reset(pending, pkt);
		err = vmci_transport_error_to_vsock_error(err);
		skerr = -err;
		goto destroy;
	}

	/* We have a connection. Move the now connected socket from the
	 * listener's pending list to the accept queue so callers of accept()
	 * can find it.
	 */
	vsock_remove_pending(listener, pending);
	vsock_enqueue_accept(listener, pending);

	/* Callers of accept() will be waiting on the listening socket, not
	 * the pending socket.
	 */
	listener->sk_data_ready(listener);

	return 0;

destroy:
	pending->sk_err = skerr;
	pending->sk_state = TCP_CLOSE;
	/* As long as we drop our reference, all necessary cleanup will handle
	 * when the cleanup function drops its reference and our destruct
	 * implementation is called.  Note that since the listen handler will
	 * remove pending from the pending list upon our failure, the cleanup
	 * function won't drop the additional reference, which is why we do it
	 * here.
	 */
	sock_put(pending);

	return err;
}

static int
vmci_transport_recv_connecting_client(struct sock *sk,
				      struct vmci_transport_packet *pkt)
{
	struct vsock_sock *vsk;
	int err;
	int skerr;

	vsk = vsock_sk(sk);

	switch (pkt->type) {
	case VMCI_TRANSPORT_PACKET_TYPE_ATTACH:
		if (vmci_handle_is_invalid(pkt->u.handle) ||
		    !vmci_handle_is_equal(pkt->u.handle,
					  vmci_trans(vsk)->qp_handle)) {
			skerr = EPROTO;
			err = -EINVAL;
			goto destroy;
		}

		/* Signify the socket is connected and wakeup the waiter in
		 * connect(). Also place the socket in the connected table for
		 * accounting (it can already be found since it's in the bound
		 * table).
		 */
		sk->sk_state = TCP_ESTABLISHED;
		sk->sk_socket->state = SS_CONNECTED;
		vsock_insert_connected(vsk);
		sk->sk_state_change(sk);

		break;
	case VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE:
	case VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE2:
		if (pkt->u.size == 0
		    || pkt->dg.src.context != vsk->remote_addr.svm_cid
		    || pkt->src_port != vsk->remote_addr.svm_port
		    || !vmci_handle_is_invalid(vmci_trans(vsk)->qp_handle)
		    || vmci_trans(vsk)->qpair
		    || vmci_trans(vsk)->produce_size != 0
		    || vmci_trans(vsk)->consume_size != 0
		    || vmci_trans(vsk)->detach_sub_id != VMCI_INVALID_ID) {
			skerr = EPROTO;
			err = -EINVAL;

			goto destroy;
		}

		err = vmci_transport_recv_connecting_client_negotiate(sk, pkt);
		if (err) {
			skerr = -err;
			goto destroy;
		}

		break;
	case VMCI_TRANSPORT_PACKET_TYPE_INVALID:
		err = vmci_transport_recv_connecting_client_invalid(sk, pkt);
		if (err) {
			skerr = -err;
			goto destroy;
		}

		break;
	case VMCI_TRANSPORT_PACKET_TYPE_RST:
		/* Older versions of the linux code (WS 6.5 / ESX 4.0) used to
		 * continue processing here after they sent an INVALID packet.
		 * This meant that we got a RST after the INVALID. We ignore a
		 * RST after an INVALID. The common code doesn't send the RST
		 * ... so we can hang if an old version of the common code
		 * fails between getting a REQUEST and sending an OFFER back.
		 * Not much we can do about it... except hope that it doesn't
		 * happen.
		 */
		if (vsk->ignore_connecting_rst) {
			vsk->ignore_connecting_rst = false;
		} else {
			skerr = ECONNRESET;
			err = 0;
			goto destroy;
		}

		break;
	default:
		/* Close and cleanup the connection. */
		skerr = EPROTO;
		err = -EINVAL;
		goto destroy;
	}

	return 0;

destroy:
	vmci_transport_send_reset(sk, pkt);

	sk->sk_state = TCP_CLOSE;
	sk->sk_err = skerr;
	sk_error_report(sk);
	return err;
}

static int vmci_transport_recv_connecting_client_negotiate(
					struct sock *sk,
					struct vmci_transport_packet *pkt)
{
	int err;
	struct vsock_sock *vsk;
	struct vmci_handle handle;
	struct vmci_qp *qpair;
	u32 detach_sub_id;
	bool is_local;
	u32 flags;
	bool old_proto = true;
	bool old_pkt_proto;
	u16 version;

	vsk = vsock_sk(sk);
	handle = VMCI_INVALID_HANDLE;
	detach_sub_id = VMCI_INVALID_ID;

	/* If we have gotten here then we should be past the point where old
	 * linux vsock could have sent the bogus rst.
	 */
	vsk->sent_request = false;
	vsk->ignore_connecting_rst = false;

	/* Verify that we're OK with the proposed queue pair size */
	if (pkt->u.size < vsk->buffer_min_size ||
	    pkt->u.size > vsk->buffer_max_size) {
		err = -EINVAL;
		goto destroy;
	}

	/* At this point we know the CID the peer is using to talk to us. */

	if (vsk->local_addr.svm_cid == VMADDR_CID_ANY)
		vsk->local_addr.svm_cid = pkt->dg.dst.context;

	/* Setup the notify ops to be the highest supported version that both
	 * the server and the client support.
	 */

	if (vmci_transport_old_proto_override(&old_pkt_proto)) {
		old_proto = old_pkt_proto;
	} else {
		if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE)
			old_proto = true;
		else if (pkt->type == VMCI_TRANSPORT_PACKET_TYPE_NEGOTIATE2)
			old_proto = false;

	}

	if (old_proto)
		version = VSOCK_PROTO_INVALID;
	else
		version = pkt->proto;

	if (!vmci_transport_proto_to_notify_struct(sk, &version, old_proto)) {
		err = -EINVAL;
		goto destroy;
	}

	/* Subscribe to detach events first.
	 *
	 * XXX We attach once for each queue pair created for now so it is easy
	 * to find the socket (it's provided), but later we should only
	 * subscribe once and add a way to lookup sockets by queue pair handle.
	 */
	err = vmci_event_subscribe(VMCI_EVENT_QP_PEER_DETACH,
				   vmci_transport_peer_detach_cb,
				   vmci_trans(vsk), &detach_sub_id);
	if (err < VMCI_SUCCESS) {
		err = vmci_transport_error_to_vsock_error(err);
		goto destroy;
	}

	/* Make VMCI select the handle for us. */
	handle = VMCI_INVALID_HANDLE;
	is_local = vsk->remote_addr.svm_cid == vsk->local_addr.svm_cid;
	flags = is_local ? VMCI_QPFLAG_LOCAL : 0;

	err = vmci_transport_queue_pair_alloc(&qpair,
					      &handle,
					      pkt->u.size,
					      pkt->u.size,
					      vsk->remote_addr.svm_cid,
					      flags,
					      vmci_transport_is_trusted(
						  vsk,
						  vsk->
						  remote_addr.svm_cid));
	if (err < 0)
		goto destroy;

	err = vmci_transport_send_qp_offer(sk, handle);
	if (err < 0) {
		err = vmci_transport_error_to_vsock_error(err);
		goto destroy;
	}

	vmci_trans(vsk)->qp_handle = handle;
	vmci_trans(vsk)->qpair = qpair;

	vmci_trans(vsk)->produce_size = vmci_trans(vsk)->consume_size =
		pkt->u.size;

	vmci_trans(vsk)->detach_sub_id = detach_sub_id;

	vmci_trans(vsk)->notify_ops->process_negotiate(sk);

	return 0;

destroy:
	if (detach_sub_id != VMCI_INVALID_ID)
		vmci_event_unsubscribe(detach_sub_id);

	if (!vmci_handle_is_invalid(handle))
		vmci_qpair_detach(&qpair);

	return err;
}

static int
vmci_transport_recv_connecting_client_invalid(struct sock *sk,
					      struct vmci_transport_packet *pkt)
{
	int err = 0;
	struct vsock_sock *vsk = vsock_sk(sk);

	if (vsk->sent_request) {
		vsk->sent_request = false;
		vsk->ignore_connecting_rst = true;

		err = vmci_transport_send_conn_request(sk, vsk->buffer_size);
		if (err < 0)
			err = vmci_transport_error_to_vsock_error(err);
		else
			err = 0;

	}

	return err;
}

static int vmci_transport_recv_connected(struct sock *sk,
					 struct vmci_transport_packet *pkt)
{
	struct vsock_sock *vsk;
	bool pkt_processed = false;

	/* In cases where we are closing the connection, it's sufficient to
	 * mark the state change (and maybe error) and wake up any waiting
	 * threads. Since this is a connected socket, it's owned by a user
	 * process and will be cleaned up when the failure is passed back on
	 * the current or next system call.  Our system call implementations
	 * must therefore check for error and state changes on entry and when
	 * being awoken.
	 */
	switch (pkt->type) {
	case VMCI_TRANSPORT_PACKET_TYPE_SHUTDOWN:
		if (pkt->u.mode) {
			vsk = vsock_sk(sk);

			vsk->peer_shutdown |= pkt->u.mode;
			sk->sk_state_change(sk);
		}
		break;

	case VMCI_TRANSPORT_PACKET_TYPE_RST:
		vsk = vsock_sk(sk);
		/* It is possible that we sent our peer a message (e.g a
		 * WAITING_READ) right before we got notified that the peer had
		 * detached. If that happens then we can get a RST pkt back
		 * from our peer even though there is data available for us to
		 * read. In that case, don't shutdown the socket completely but
		 * instead allow the local client to finish reading data off
		 * the queuepair. Always treat a RST pkt in connected mode like
		 * a clean shutdown.
		 */
		sock_set_flag(sk, SOCK_DONE);
		vsk->peer_shutdown = SHUTDOWN_MASK;
		if (vsock_stream_has_data(vsk) <= 0)
			sk->sk_state = TCP_CLOSING;

		sk->sk_state_change(sk);
		break;

	default:
		vsk = vsock_sk(sk);
		vmci_trans(vsk)->notify_ops->handle_notify_pkt(
				sk, pkt, false, NULL, NULL,
				&pkt_processed);
		if (!pkt_processed)
			return -EINVAL;

		break;
	}

	return 0;
}

static int vmci_transport_socket_init(struct vsock_sock *vsk,
				      struct vsock_sock *psk)
{
	vsk->trans = kmalloc(sizeof(struct vmci_transport), GFP_KERNEL);
	if (!vsk->trans)
		return -ENOMEM;

	vmci_trans(vsk)->dg_handle = VMCI_INVALID_HANDLE;
	vmci_trans(vsk)->qp_handle = VMCI_INVALID_HANDLE;
	vmci_trans(vsk)->qpair = NULL;
	vmci_trans(vsk)->produce_size = vmci_trans(vsk)->consume_size = 0;
	vmci_trans(vsk)->detach_sub_id = VMCI_INVALID_ID;
	vmci_trans(vsk)->notify_ops = NULL;
	INIT_LIST_HEAD(&vmci_trans(vsk)->elem);
	vmci_trans(vsk)->sk = &vsk->sk;
	spin_lock_init(&vmci_trans(vsk)->lock);

	return 0;
}

static void vmci_transport_free_resources(struct list_head *transport_list)
{
	while (!list_empty(transport_list)) {
		struct vmci_transport *transport =
		    list_first_entry(transport_list, struct vmci_transport,
				     elem);
		list_del(&transport->elem);

		if (transport->detach_sub_id != VMCI_INVALID_ID) {
			vmci_event_unsubscribe(transport->detach_sub_id);
			transport->detach_sub_id = VMCI_INVALID_ID;
		}

		if (!vmci_handle_is_invalid(transport->qp_handle)) {
			vmci_qpair_detach(&transport->qpair);
			transport->qp_handle = VMCI_INVALID_HANDLE;
			transport->produce_size = 0;
			transport->consume_size = 0;
		}

		kfree(transport);
	}
}

static void vmci_transport_cleanup(struct work_struct *work)
{
	LIST_HEAD(pending);

	spin_lock_bh(&vmci_transport_cleanup_lock);
	list_replace_init(&vmci_transport_cleanup_list, &pending);
	spin_unlock_bh(&vmci_transport_cleanup_lock);
	vmci_transport_free_resources(&pending);
}

static void vmci_transport_destruct(struct vsock_sock *vsk)
{
	/* transport can be NULL if we hit a failure at init() time */
	if (!vmci_trans(vsk))
		return;

	/* Ensure that the detach callback doesn't use the sk/vsk
	 * we are about to destruct.
	 */
	spin_lock_bh(&vmci_trans(vsk)->lock);
	vmci_trans(vsk)->sk = NULL;
	spin_unlock_bh(&vmci_trans(vsk)->lock);

	if (vmci_trans(vsk)->notify_ops)
		vmci_trans(vsk)->notify_ops->socket_destruct(vsk);

	spin_lock_bh(&vmci_transport_cleanup_lock);
	list_add(&vmci_trans(vsk)->elem, &vmci_transport_cleanup_list);
	spin_unlock_bh(&vmci_transport_cleanup_lock);
	schedule_work(&vmci_transport_cleanup_work);

	vsk->trans = NULL;
}

static void vmci_transport_release(struct vsock_sock *vsk)
{
	vsock_remove_sock(vsk);

	if (!vmci_handle_is_invalid(vmci_trans(vsk)->dg_handle)) {
		vmci_datagram_destroy_handle(vmci_trans(vsk)->dg_handle);
		vmci_trans(vsk)->dg_handle = VMCI_INVALID_HANDLE;
	}
}

static int vmci_transport_dgram_bind(struct vsock_sock *vsk,
				     struct sockaddr_vm *addr)
{
	u32 port;
	u32 flags;
	int err;

	/* VMCI will select a resource ID for us if we provide
	 * VMCI_INVALID_ID.
	 */
	port = addr->svm_port == VMADDR_PORT_ANY ?
			VMCI_INVALID_ID : addr->svm_port;

	if (port <= LAST_RESERVED_PORT && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	flags = addr->svm_cid == VMADDR_CID_ANY ?
				VMCI_FLAG_ANYCID_DG_HND : 0;

	err = vmci_transport_datagram_create_hnd(port, flags,
						 vmci_transport_recv_dgram_cb,
						 &vsk->sk,
						 &vmci_trans(vsk)->dg_handle);
	if (err < VMCI_SUCCESS)
		return vmci_transport_error_to_vsock_error(err);
	vsock_addr_init(&vsk->local_addr, addr->svm_cid,
			vmci_trans(vsk)->dg_handle.resource);

	return 0;
}

static int vmci_transport_dgram_enqueue(
	struct vsock_sock *vsk,
	struct sockaddr_vm *remote_addr,
	struct msghdr *msg,
	size_t len)
{
	int err;
	struct vmci_datagram *dg;

	if (len > VMCI_MAX_DG_PAYLOAD_SIZE)
		return -EMSGSIZE;

	if (!vmci_transport_allow_dgram(vsk, remote_addr->svm_cid))
		return -EPERM;

	/* Allocate a buffer for the user's message and our packet header. */
	dg = kmalloc(len + sizeof(*dg), GFP_KERNEL);
	if (!dg)
		return -ENOMEM;

	memcpy_from_msg(VMCI_DG_PAYLOAD(dg), msg, len);

	dg->dst = vmci_make_handle(remote_addr->svm_cid,
				   remote_addr->svm_port);
	dg->src = vmci_make_handle(vsk->local_addr.svm_cid,
				   vsk->local_addr.svm_port);
	dg->payload_size = len;

	err = vmci_datagram_send(dg);
	kfree(dg);
	if (err < 0)
		return vmci_transport_error_to_vsock_error(err);

	return err - sizeof(*dg);
}

static int vmci_transport_dgram_dequeue(struct vsock_sock *vsk,
					struct msghdr *msg, size_t len,
					int flags)
{
	int err;
	struct vmci_datagram *dg;
	size_t payload_len;
	struct sk_buff *skb;

	if (flags & MSG_OOB || flags & MSG_ERRQUEUE)
		return -EOPNOTSUPP;

	/* Retrieve the head sk_buff from the socket's receive queue. */
	err = 0;
	skb = skb_recv_datagram(&vsk->sk, flags, &err);
	if (!skb)
		return err;

	dg = (struct vmci_datagram *)skb->data;
	if (!dg)
		/* err is 0, meaning we read zero bytes. */
		goto out;

	payload_len = dg->payload_size;
	/* Ensure the sk_buff matches the payload size claimed in the packet. */
	if (payload_len != skb->len - sizeof(*dg)) {
		err = -EINVAL;
		goto out;
	}

	if (payload_len > len) {
		payload_len = len;
		msg->msg_flags |= MSG_TRUNC;
	}

	/* Place the datagram payload in the user's iovec. */
	err = skb_copy_datagram_msg(skb, sizeof(*dg), msg, payload_len);
	if (err)
		goto out;

	if (msg->msg_name) {
		/* Provide the address of the sender. */
		DECLARE_SOCKADDR(struct sockaddr_vm *, vm_addr, msg->msg_name);
		vsock_addr_init(vm_addr, dg->src.context, dg->src.resource);
		msg->msg_namelen = sizeof(*vm_addr);
	}
	err = payload_len;

out:
	skb_free_datagram(&vsk->sk, skb);
	return err;
}

static bool vmci_transport_dgram_allow(u32 cid, u32 port)
{
	if (cid == VMADDR_CID_HYPERVISOR) {
		/* Registrations of PBRPC Servers do not modify VMX/Hypervisor
		 * state and are allowed.
		 */
		return port == VMCI_UNITY_PBRPC_REGISTER;
	}

	return true;
}

static int vmci_transport_connect(struct vsock_sock *vsk)
{
	int err;
	bool old_pkt_proto = false;
	struct sock *sk = &vsk->sk;

	if (vmci_transport_old_proto_override(&old_pkt_proto) &&
		old_pkt_proto) {
		err = vmci_transport_send_conn_request(sk, vsk->buffer_size);
		if (err < 0) {
			sk->sk_state = TCP_CLOSE;
			return err;
		}
	} else {
		int supported_proto_versions =
			vmci_transport_new_proto_supported_versions();
		err = vmci_transport_send_conn_request2(sk, vsk->buffer_size,
				supported_proto_versions);
		if (err < 0) {
			sk->sk_state = TCP_CLOSE;
			return err;
		}

		vsk->sent_request = true;
	}

	return err;
}

static ssize_t vmci_transport_stream_dequeue(
	struct vsock_sock *vsk,
	struct msghdr *msg,
	size_t len,
	int flags)
{
	if (flags & MSG_PEEK)
		return vmci_qpair_peekv(vmci_trans(vsk)->qpair, msg, len, 0);
	else
		return vmci_qpair_dequev(vmci_trans(vsk)->qpair, msg, len, 0);
}

static ssize_t vmci_transport_stream_enqueue(
	struct vsock_sock *vsk,
	struct msghdr *msg,
	size_t len)
{
	return vmci_qpair_enquev(vmci_trans(vsk)->qpair, msg, len, 0);
}

static s64 vmci_transport_stream_has_data(struct vsock_sock *vsk)
{
	return vmci_qpair_consume_buf_ready(vmci_trans(vsk)->qpair);
}

static s64 vmci_transport_stream_has_space(struct vsock_sock *vsk)
{
	return vmci_qpair_produce_free_space(vmci_trans(vsk)->qpair);
}

static u64 vmci_transport_stream_rcvhiwat(struct vsock_sock *vsk)
{
	return vmci_trans(vsk)->consume_size;
}

static bool vmci_transport_stream_is_active(struct vsock_sock *vsk)
{
	return !vmci_handle_is_invalid(vmci_trans(vsk)->qp_handle);
}

static int vmci_transport_notify_poll_in(
	struct vsock_sock *vsk,
	size_t target,
	bool *data_ready_now)
{
	return vmci_trans(vsk)->notify_ops->poll_in(
			&vsk->sk, target, data_ready_now);
}

static int vmci_transport_notify_poll_out(
	struct vsock_sock *vsk,
	size_t target,
	bool *space_available_now)
{
	return vmci_trans(vsk)->notify_ops->poll_out(
			&vsk->sk, target, space_available_now);
}

static int vmci_transport_notify_recv_init(
	struct vsock_sock *vsk,
	size_t target,
	struct vsock_transport_recv_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->recv_init(
			&vsk->sk, target,
			(struct vmci_transport_recv_notify_data *)data);
}

static int vmci_transport_notify_recv_pre_block(
	struct vsock_sock *vsk,
	size_t target,
	struct vsock_transport_recv_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->recv_pre_block(
			&vsk->sk, target,
			(struct vmci_transport_recv_notify_data *)data);
}

static int vmci_transport_notify_recv_pre_dequeue(
	struct vsock_sock *vsk,
	size_t target,
	struct vsock_transport_recv_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->recv_pre_dequeue(
			&vsk->sk, target,
			(struct vmci_transport_recv_notify_data *)data);
}

static int vmci_transport_notify_recv_post_dequeue(
	struct vsock_sock *vsk,
	size_t target,
	ssize_t copied,
	bool data_read,
	struct vsock_transport_recv_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->recv_post_dequeue(
			&vsk->sk, target, copied, data_read,
			(struct vmci_transport_recv_notify_data *)data);
}

static int vmci_transport_notify_send_init(
	struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->send_init(
			&vsk->sk,
			(struct vmci_transport_send_notify_data *)data);
}

static int vmci_transport_notify_send_pre_block(
	struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->send_pre_block(
			&vsk->sk,
			(struct vmci_transport_send_notify_data *)data);
}

static int vmci_transport_notify_send_pre_enqueue(
	struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->send_pre_enqueue(
			&vsk->sk,
			(struct vmci_transport_send_notify_data *)data);
}

static int vmci_transport_notify_send_post_enqueue(
	struct vsock_sock *vsk,
	ssize_t written,
	struct vsock_transport_send_notify_data *data)
{
	return vmci_trans(vsk)->notify_ops->send_post_enqueue(
			&vsk->sk, written,
			(struct vmci_transport_send_notify_data *)data);
}

static bool vmci_transport_old_proto_override(bool *old_pkt_proto)
{
	if (PROTOCOL_OVERRIDE != -1) {
		if (PROTOCOL_OVERRIDE == 0)
			*old_pkt_proto = true;
		else
			*old_pkt_proto = false;

		pr_info("Proto override in use\n");
		return true;
	}

	return false;
}

static bool vmci_transport_proto_to_notify_struct(struct sock *sk,
						  u16 *proto,
						  bool old_pkt_proto)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	if (old_pkt_proto) {
		if (*proto != VSOCK_PROTO_INVALID) {
			pr_err("Can't set both an old and new protocol\n");
			return false;
		}
		vmci_trans(vsk)->notify_ops = &vmci_transport_notify_pkt_ops;
		goto exit;
	}

	switch (*proto) {
	case VSOCK_PROTO_PKT_ON_NOTIFY:
		vmci_trans(vsk)->notify_ops =
			&vmci_transport_notify_pkt_q_state_ops;
		break;
	default:
		pr_err("Unknown notify protocol version\n");
		return false;
	}

exit:
	vmci_trans(vsk)->notify_ops->socket_init(sk);
	return true;
}

static u16 vmci_transport_new_proto_supported_versions(void)
{
	if (PROTOCOL_OVERRIDE != -1)
		return PROTOCOL_OVERRIDE;

	return VSOCK_PROTO_ALL_SUPPORTED;
}

static u32 vmci_transport_get_local_cid(void)
{
	return vmci_get_context_id();
}

static struct vsock_transport vmci_transport = {
	.module = THIS_MODULE,
	.init = vmci_transport_socket_init,
	.destruct = vmci_transport_destruct,
	.release = vmci_transport_release,
	.connect = vmci_transport_connect,
	.dgram_bind = vmci_transport_dgram_bind,
	.dgram_dequeue = vmci_transport_dgram_dequeue,
	.dgram_enqueue = vmci_transport_dgram_enqueue,
	.dgram_allow = vmci_transport_dgram_allow,
	.stream_dequeue = vmci_transport_stream_dequeue,
	.stream_enqueue = vmci_transport_stream_enqueue,
	.stream_has_data = vmci_transport_stream_has_data,
	.stream_has_space = vmci_transport_stream_has_space,
	.stream_rcvhiwat = vmci_transport_stream_rcvhiwat,
	.stream_is_active = vmci_transport_stream_is_active,
	.stream_allow = vmci_transport_stream_allow,
	.notify_poll_in = vmci_transport_notify_poll_in,
	.notify_poll_out = vmci_transport_notify_poll_out,
	.notify_recv_init = vmci_transport_notify_recv_init,
	.notify_recv_pre_block = vmci_transport_notify_recv_pre_block,
	.notify_recv_pre_dequeue = vmci_transport_notify_recv_pre_dequeue,
	.notify_recv_post_dequeue = vmci_transport_notify_recv_post_dequeue,
	.notify_send_init = vmci_transport_notify_send_init,
	.notify_send_pre_block = vmci_transport_notify_send_pre_block,
	.notify_send_pre_enqueue = vmci_transport_notify_send_pre_enqueue,
	.notify_send_post_enqueue = vmci_transport_notify_send_post_enqueue,
	.shutdown = vmci_transport_shutdown,
	.get_local_cid = vmci_transport_get_local_cid,
};

static bool vmci_check_transport(struct vsock_sock *vsk)
{
	return vsk->transport == &vmci_transport;
}

static void vmci_vsock_transport_cb(bool is_host)
{
	int features;

	if (is_host)
		features = VSOCK_TRANSPORT_F_H2G;
	else
		features = VSOCK_TRANSPORT_F_G2H;

	vsock_core_register(&vmci_transport, features);
}

static int __init vmci_transport_init(void)
{
	int err;

	/* Create the datagram handle that we will use to send and receive all
	 * VSocket control messages for this context.
	 */
	err = vmci_transport_datagram_create_hnd(VMCI_TRANSPORT_PACKET_RID,
						 VMCI_FLAG_ANYCID_DG_HND,
						 vmci_transport_recv_stream_cb,
						 NULL,
						 &vmci_transport_stream_handle);
	if (err < VMCI_SUCCESS) {
		pr_err("Unable to create datagram handle. (%d)\n", err);
		return vmci_transport_error_to_vsock_error(err);
	}
	err = vmci_event_subscribe(VMCI_EVENT_QP_RESUMED,
				   vmci_transport_qp_resumed_cb,
				   NULL, &vmci_transport_qp_resumed_sub_id);
	if (err < VMCI_SUCCESS) {
		pr_err("Unable to subscribe to resumed event. (%d)\n", err);
		err = vmci_transport_error_to_vsock_error(err);
		vmci_transport_qp_resumed_sub_id = VMCI_INVALID_ID;
		goto err_destroy_stream_handle;
	}

	/* Register only with dgram feature, other features (H2G, G2H) will be
	 * registered when the first host or guest becomes active.
	 */
	err = vsock_core_register(&vmci_transport, VSOCK_TRANSPORT_F_DGRAM);
	if (err < 0)
		goto err_unsubscribe;

	err = vmci_register_vsock_callback(vmci_vsock_transport_cb);
	if (err < 0)
		goto err_unregister;

	return 0;

err_unregister:
	vsock_core_unregister(&vmci_transport);
err_unsubscribe:
	vmci_event_unsubscribe(vmci_transport_qp_resumed_sub_id);
err_destroy_stream_handle:
	vmci_datagram_destroy_handle(vmci_transport_stream_handle);
	return err;
}
module_init(vmci_transport_init);

static void __exit vmci_transport_exit(void)
{
	cancel_work_sync(&vmci_transport_cleanup_work);
	vmci_transport_free_resources(&vmci_transport_cleanup_list);

	if (!vmci_handle_is_invalid(vmci_transport_stream_handle)) {
		if (vmci_datagram_destroy_handle(
			vmci_transport_stream_handle) != VMCI_SUCCESS)
			pr_err("Couldn't destroy datagram handle\n");
		vmci_transport_stream_handle = VMCI_INVALID_HANDLE;
	}

	if (vmci_transport_qp_resumed_sub_id != VMCI_INVALID_ID) {
		vmci_event_unsubscribe(vmci_transport_qp_resumed_sub_id);
		vmci_transport_qp_resumed_sub_id = VMCI_INVALID_ID;
	}

	vmci_register_vsock_callback(NULL);
	vsock_core_unregister(&vmci_transport);
}
module_exit(vmci_transport_exit);

MODULE_AUTHOR("VMware, Inc.");
MODULE_DESCRIPTION("VMCI transport for Virtual Sockets");
MODULE_VERSION("1.0.5.0-k");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("vmware_vsock");
MODULE_ALIAS_NETPROTO(PF_VSOCK);
