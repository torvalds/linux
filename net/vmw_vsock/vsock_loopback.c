// SPDX-License-Identifier: GPL-2.0-only
/* loopback transport for vsock using virtio_transport_common APIs
 *
 * Copyright (C) 2013-2019 Red Hat, Inc.
 * Authors: Asias He <asias@redhat.com>
 *          Stefan Hajnoczi <stefanha@redhat.com>
 *          Stefano Garzarella <sgarzare@redhat.com>
 *
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/virtio_vsock.h>

struct vsock_loopback {
	struct workqueue_struct *workqueue;

	struct sk_buff_head pkt_queue;
	struct work_struct pkt_work;
};

static struct vsock_loopback the_vsock_loopback;

static u32 vsock_loopback_get_local_cid(void)
{
	return VMADDR_CID_LOCAL;
}

static int vsock_loopback_send_pkt(struct sk_buff *skb)
{
	struct vsock_loopback *vsock = &the_vsock_loopback;
	int len = skb->len;

	virtio_vsock_skb_queue_tail(&vsock->pkt_queue, skb);
	queue_work(vsock->workqueue, &vsock->pkt_work);

	return len;
}

static int vsock_loopback_cancel_pkt(struct vsock_sock *vsk)
{
	struct vsock_loopback *vsock = &the_vsock_loopback;

	virtio_transport_purge_skbs(vsk, &vsock->pkt_queue);

	return 0;
}

static bool vsock_loopback_seqpacket_allow(u32 remote_cid);
static bool vsock_loopback_msgzerocopy_allow(void)
{
	return true;
}

static struct virtio_transport loopback_transport = {
	.transport = {
		.module                   = THIS_MODULE,

		.get_local_cid            = vsock_loopback_get_local_cid,

		.init                     = virtio_transport_do_socket_init,
		.destruct                 = virtio_transport_destruct,
		.release                  = virtio_transport_release,
		.connect                  = virtio_transport_connect,
		.shutdown                 = virtio_transport_shutdown,
		.cancel_pkt               = vsock_loopback_cancel_pkt,

		.dgram_bind               = virtio_transport_dgram_bind,
		.dgram_dequeue            = virtio_transport_dgram_dequeue,
		.dgram_enqueue            = virtio_transport_dgram_enqueue,
		.dgram_allow              = virtio_transport_dgram_allow,

		.stream_dequeue           = virtio_transport_stream_dequeue,
		.stream_enqueue           = virtio_transport_stream_enqueue,
		.stream_has_data          = virtio_transport_stream_has_data,
		.stream_has_space         = virtio_transport_stream_has_space,
		.stream_rcvhiwat          = virtio_transport_stream_rcvhiwat,
		.stream_is_active         = virtio_transport_stream_is_active,
		.stream_allow             = virtio_transport_stream_allow,

		.seqpacket_dequeue        = virtio_transport_seqpacket_dequeue,
		.seqpacket_enqueue        = virtio_transport_seqpacket_enqueue,
		.seqpacket_allow          = vsock_loopback_seqpacket_allow,
		.seqpacket_has_data       = virtio_transport_seqpacket_has_data,

		.msgzerocopy_allow        = vsock_loopback_msgzerocopy_allow,

		.notify_poll_in           = virtio_transport_notify_poll_in,
		.notify_poll_out          = virtio_transport_notify_poll_out,
		.notify_recv_init         = virtio_transport_notify_recv_init,
		.notify_recv_pre_block    = virtio_transport_notify_recv_pre_block,
		.notify_recv_pre_dequeue  = virtio_transport_notify_recv_pre_dequeue,
		.notify_recv_post_dequeue = virtio_transport_notify_recv_post_dequeue,
		.notify_send_init         = virtio_transport_notify_send_init,
		.notify_send_pre_block    = virtio_transport_notify_send_pre_block,
		.notify_send_pre_enqueue  = virtio_transport_notify_send_pre_enqueue,
		.notify_send_post_enqueue = virtio_transport_notify_send_post_enqueue,
		.notify_buffer_size       = virtio_transport_notify_buffer_size,
		.notify_set_rcvlowat      = virtio_transport_notify_set_rcvlowat,

		.read_skb = virtio_transport_read_skb,
	},

	.send_pkt = vsock_loopback_send_pkt,
};

static bool vsock_loopback_seqpacket_allow(u32 remote_cid)
{
	return true;
}

static void vsock_loopback_work(struct work_struct *work)
{
	struct vsock_loopback *vsock =
		container_of(work, struct vsock_loopback, pkt_work);
	struct sk_buff_head pkts;
	struct sk_buff *skb;

	skb_queue_head_init(&pkts);

	spin_lock_bh(&vsock->pkt_queue.lock);
	skb_queue_splice_init(&vsock->pkt_queue, &pkts);
	spin_unlock_bh(&vsock->pkt_queue.lock);

	while ((skb = __skb_dequeue(&pkts))) {
		virtio_transport_deliver_tap_pkt(skb);
		virtio_transport_recv_pkt(&loopback_transport, skb);
	}
}

static int __init vsock_loopback_init(void)
{
	struct vsock_loopback *vsock = &the_vsock_loopback;
	int ret;

	vsock->workqueue = alloc_workqueue("vsock-loopback", 0, 0);
	if (!vsock->workqueue)
		return -ENOMEM;

	skb_queue_head_init(&vsock->pkt_queue);
	INIT_WORK(&vsock->pkt_work, vsock_loopback_work);

	ret = vsock_core_register(&loopback_transport.transport,
				  VSOCK_TRANSPORT_F_LOCAL);
	if (ret)
		goto out_wq;

	return 0;

out_wq:
	destroy_workqueue(vsock->workqueue);
	return ret;
}

static void __exit vsock_loopback_exit(void)
{
	struct vsock_loopback *vsock = &the_vsock_loopback;

	vsock_core_unregister(&loopback_transport.transport);

	flush_work(&vsock->pkt_work);

	virtio_vsock_skb_queue_purge(&vsock->pkt_queue);

	destroy_workqueue(vsock->workqueue);
}

module_init(vsock_loopback_init);
module_exit(vsock_loopback_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Stefano Garzarella <sgarzare@redhat.com>");
MODULE_DESCRIPTION("loopback transport for vsock");
MODULE_ALIAS_NETPROTO(PF_VSOCK);
