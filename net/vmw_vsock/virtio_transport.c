/*
 * virtio transport for vsock
 *
 * Copyright (C) 2013-2015 Red Hat, Inc.
 * Author: Asias He <asias@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 *
 * Some of the code is take from Gerd Hoffmann <kraxel@redhat.com>'s
 * early virtio-vsock proof-of-concept bits.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_vsock.h>
#include <net/sock.h>
#include <linux/mutex.h>
#include <net/af_vsock.h>

static struct workqueue_struct *virtio_vsock_workqueue;
static struct virtio_vsock *the_virtio_vsock;
static DEFINE_MUTEX(the_virtio_vsock_mutex); /* protects the_virtio_vsock */
static void virtio_vsock_rx_fill(struct virtio_vsock *vsock);

struct virtio_vsock {
	/* Virtio device */
	struct virtio_device *vdev;
	/* Virtio virtqueue */
	struct virtqueue *vqs[VSOCK_VQ_MAX];
	/* Wait queue for send pkt */
	wait_queue_head_t queue_wait;
	/* Work item to send pkt */
	struct work_struct tx_work;
	/* Work item to recv pkt */
	struct work_struct rx_work;
	/* Mutex to protect send pkt*/
	struct mutex tx_lock;
	/* Mutex to protect recv pkt*/
	struct mutex rx_lock;
	/* Number of recv buffers */
	int rx_buf_nr;
	/* Number of max recv buffers */
	int rx_buf_max_nr;
	/* Used for global tx buf limitation */
	u32 total_tx_buf;
	/* Guest context id, just like guest ip address */
	u32 guest_cid;
};

static struct virtio_vsock *virtio_vsock_get(void)
{
	return the_virtio_vsock;
}

static u32 virtio_transport_get_local_cid(void)
{
	struct virtio_vsock *vsock = virtio_vsock_get();

	return vsock->guest_cid;
}

static int
virtio_transport_send_pkt(struct vsock_sock *vsk,
			  struct virtio_vsock_pkt_info *info)
{
	u32 src_cid, src_port, dst_cid, dst_port;
	int ret, in_sg = 0, out_sg = 0;
	struct virtio_transport *trans;
	struct virtio_vsock_pkt *pkt;
	struct virtio_vsock *vsock;
	struct scatterlist hdr, buf, *sgs[2];
	struct virtqueue *vq;
	u32 pkt_len = info->pkt_len;
	DEFINE_WAIT(wait);

	vsock = virtio_vsock_get();
	if (!vsock)
		return -ENODEV;

	src_cid	= virtio_transport_get_local_cid();
	src_port = vsk->local_addr.svm_port;
	if (!info->remote_cid) {
		dst_cid	= vsk->remote_addr.svm_cid;
		dst_port = vsk->remote_addr.svm_port;
	} else {
		dst_cid = info->remote_cid;
		dst_port = info->remote_port;
	}

	trans = vsk->trans;
	vq = vsock->vqs[VSOCK_VQ_TX];

	if (pkt_len > VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE)
		pkt_len = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE;
	pkt_len = virtio_transport_get_credit(trans, pkt_len);
	/* Do not send zero length OP_RW pkt*/
	if (pkt_len == 0 && info->op == VIRTIO_VSOCK_OP_RW)
		return pkt_len;

	/* Respect global tx buf limitation */
	mutex_lock(&vsock->tx_lock);
	while (pkt_len + vsock->total_tx_buf > VIRTIO_VSOCK_MAX_TX_BUF_SIZE) {
		prepare_to_wait_exclusive(&vsock->queue_wait, &wait,
					  TASK_UNINTERRUPTIBLE);
		mutex_unlock(&vsock->tx_lock);
		schedule();
		mutex_lock(&vsock->tx_lock);
		finish_wait(&vsock->queue_wait, &wait);
	}
	vsock->total_tx_buf += pkt_len;
	mutex_unlock(&vsock->tx_lock);

	pkt = virtio_transport_alloc_pkt(vsk, info, pkt_len,
					 src_cid, src_port,
					 dst_cid, dst_port);
	if (!pkt) {
		mutex_lock(&vsock->tx_lock);
		vsock->total_tx_buf -= pkt_len;
		mutex_unlock(&vsock->tx_lock);
		virtio_transport_put_credit(trans, pkt_len);
		return -ENOMEM;
	}

	pr_debug("%s:info->pkt_len= %d\n", __func__, info->pkt_len);

	/* Will be released in virtio_transport_send_pkt_work */
	sock_hold(&trans->vsk->sk);
	virtio_transport_inc_tx_pkt(pkt);

	/* Put pkt in the virtqueue */
	sg_init_one(&hdr, &pkt->hdr, sizeof(pkt->hdr));
	sgs[out_sg++] = &hdr;
	if (info->msg && info->pkt_len > 0) {
		sg_init_one(&buf, pkt->buf, pkt->len);
	        sgs[out_sg++] = &buf;
	}

	mutex_lock(&vsock->tx_lock);
	while ((ret = virtqueue_add_sgs(vq, sgs, out_sg, in_sg, pkt,
					GFP_KERNEL)) < 0) {
		prepare_to_wait_exclusive(&vsock->queue_wait, &wait,
					  TASK_UNINTERRUPTIBLE);
		mutex_unlock(&vsock->tx_lock);
		schedule();
		mutex_lock(&vsock->tx_lock);
		finish_wait(&vsock->queue_wait, &wait);
	}
	virtqueue_kick(vq);
	mutex_unlock(&vsock->tx_lock);

	return pkt_len;
}

static struct virtio_transport_pkt_ops virtio_ops = {
	.send_pkt = virtio_transport_send_pkt,
};

static void virtio_vsock_rx_fill(struct virtio_vsock *vsock)
{
	int buf_len = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE;
	struct virtio_vsock_pkt *pkt;
	struct scatterlist hdr, buf, *sgs[2];
	struct virtqueue *vq;
	int ret;

	vq = vsock->vqs[VSOCK_VQ_RX];

	do {
		pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
		if (!pkt) {
			pr_debug("%s: fail to allocate pkt\n", __func__);
			goto out;
		}

		/* TODO: use mergeable rx buffer */
		pkt->buf = kmalloc(buf_len, GFP_KERNEL);
		if (!pkt->buf) {
			pr_debug("%s: fail to allocate pkt->buf\n", __func__);
			goto err;
		}

		sg_init_one(&hdr, &pkt->hdr, sizeof(pkt->hdr));
		sgs[0] = &hdr;

		sg_init_one(&buf, pkt->buf, buf_len);
	        sgs[1] = &buf;
		ret = virtqueue_add_sgs(vq, sgs, 0, 2, pkt, GFP_KERNEL);
		if (ret)
			goto err;
		vsock->rx_buf_nr++;
	} while (vq->num_free);
	if (vsock->rx_buf_nr > vsock->rx_buf_max_nr)
		vsock->rx_buf_max_nr = vsock->rx_buf_nr;
out:
	virtqueue_kick(vq);
	return;
err:
	virtqueue_kick(vq);
	virtio_transport_free_pkt(pkt);
	return;
}

static void virtio_transport_send_pkt_work(struct work_struct *work)
{
	struct virtio_vsock *vsock =
		container_of(work, struct virtio_vsock, tx_work);
	struct virtio_vsock_pkt *pkt;
	bool added = false;
	struct virtqueue *vq;
	unsigned int len;
	struct sock *sk;

	vq = vsock->vqs[VSOCK_VQ_TX];
	mutex_lock(&vsock->tx_lock);
	do {
		virtqueue_disable_cb(vq);
		while ((pkt = virtqueue_get_buf(vq, &len)) != NULL) {
			sk = &pkt->trans->vsk->sk;
			virtio_transport_dec_tx_pkt(pkt);
			/* Release refcnt taken in virtio_transport_send_pkt */
			sock_put(sk);
			vsock->total_tx_buf -= pkt->len;
			virtio_transport_free_pkt(pkt);
			added = true;
		}
	} while (!virtqueue_enable_cb(vq));
	mutex_unlock(&vsock->tx_lock);

	if (added)
		wake_up(&vsock->queue_wait);
}

static void virtio_transport_recv_pkt_work(struct work_struct *work)
{
	struct virtio_vsock *vsock =
		container_of(work, struct virtio_vsock, rx_work);
	struct virtio_vsock_pkt *pkt;
	struct virtqueue *vq;
	unsigned int len;

	vq = vsock->vqs[VSOCK_VQ_RX];
	mutex_lock(&vsock->rx_lock);
	do {
		virtqueue_disable_cb(vq);
		while ((pkt = virtqueue_get_buf(vq, &len)) != NULL) {
			pkt->len = len;
			virtio_transport_recv_pkt(pkt);
			vsock->rx_buf_nr--;
		}
	} while (!virtqueue_enable_cb(vq));

	if (vsock->rx_buf_nr < vsock->rx_buf_max_nr / 2)
		virtio_vsock_rx_fill(vsock);
	mutex_unlock(&vsock->rx_lock);
}

static void virtio_vsock_ctrl_done(struct virtqueue *vq)
{
}

static void virtio_vsock_tx_done(struct virtqueue *vq)
{
	struct virtio_vsock *vsock = vq->vdev->priv;

	if (!vsock)
		return;
	queue_work(virtio_vsock_workqueue, &vsock->tx_work);
}

static void virtio_vsock_rx_done(struct virtqueue *vq)
{
	struct virtio_vsock *vsock = vq->vdev->priv;

	if (!vsock)
		return;
	queue_work(virtio_vsock_workqueue, &vsock->rx_work);
}

static int
virtio_transport_socket_init(struct vsock_sock *vsk, struct vsock_sock *psk)
{
	struct virtio_transport *trans;
	int ret;

	ret = virtio_transport_do_socket_init(vsk, psk);
	if (ret)
		return ret;

	trans = vsk->trans;
	trans->ops = &virtio_ops;
	return ret;
}

static struct vsock_transport virtio_transport = {
	.get_local_cid            = virtio_transport_get_local_cid,

	.init                     = virtio_transport_socket_init,
	.destruct                 = virtio_transport_destruct,
	.release                  = virtio_transport_release,
	.connect                  = virtio_transport_connect,
	.shutdown                 = virtio_transport_shutdown,

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

	.set_buffer_size          = virtio_transport_set_buffer_size,
	.set_min_buffer_size      = virtio_transport_set_min_buffer_size,
	.set_max_buffer_size      = virtio_transport_set_max_buffer_size,
	.get_buffer_size          = virtio_transport_get_buffer_size,
	.get_min_buffer_size      = virtio_transport_get_min_buffer_size,
	.get_max_buffer_size      = virtio_transport_get_max_buffer_size,
};

static int virtio_vsock_probe(struct virtio_device *vdev)
{
	vq_callback_t *callbacks[] = {
		virtio_vsock_ctrl_done,
		virtio_vsock_rx_done,
		virtio_vsock_tx_done,
	};
	const char *names[] = {
		"ctrl",
		"rx",
		"tx",
	};
	struct virtio_vsock *vsock = NULL;
	u32 guest_cid;
	int ret;

	ret = mutex_lock_interruptible(&the_virtio_vsock_mutex);
	if (ret)
		return ret;

	/* Only one virtio-vsock device per guest is supported */
	if (the_virtio_vsock) {
		ret = -EBUSY;
		goto out;
	}

	vsock = kzalloc(sizeof(*vsock), GFP_KERNEL);
	if (!vsock) {
		ret = -ENOMEM;
		goto out;
	}

	vsock->vdev = vdev;

	ret = vsock->vdev->config->find_vqs(vsock->vdev, VSOCK_VQ_MAX,
					    vsock->vqs, callbacks, names);
	if (ret < 0)
		goto out;

	vdev->config->get(vdev, offsetof(struct virtio_vsock_config, guest_cid),
			  &guest_cid, sizeof(guest_cid));
	vsock->guest_cid = le32_to_cpu(guest_cid);
	pr_debug("%s:guest_cid=%d\n", __func__, vsock->guest_cid);

	ret = vsock_core_init(&virtio_transport);
	if (ret < 0)
		goto out_vqs;

	vsock->rx_buf_nr = 0;
	vsock->rx_buf_max_nr = 0;

	vdev->priv = the_virtio_vsock = vsock;
	init_waitqueue_head(&vsock->queue_wait);
	mutex_init(&vsock->tx_lock);
	mutex_init(&vsock->rx_lock);
	INIT_WORK(&vsock->rx_work, virtio_transport_recv_pkt_work);
	INIT_WORK(&vsock->tx_work, virtio_transport_send_pkt_work);

	mutex_lock(&vsock->rx_lock);
	virtio_vsock_rx_fill(vsock);
	mutex_unlock(&vsock->rx_lock);

	mutex_unlock(&the_virtio_vsock_mutex);
	return 0;

out_vqs:
	vsock->vdev->config->del_vqs(vsock->vdev);
out:
	kfree(vsock);
	mutex_unlock(&the_virtio_vsock_mutex);
	return ret;
}

static void virtio_vsock_remove(struct virtio_device *vdev)
{
	struct virtio_vsock *vsock = vdev->priv;

	mutex_lock(&the_virtio_vsock_mutex);
	the_virtio_vsock = NULL;
	vsock_core_exit();
	mutex_unlock(&the_virtio_vsock_mutex);

	kfree(vsock);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_VSOCK, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
};

static struct virtio_driver virtio_vsock_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtio_vsock_probe,
	.remove = virtio_vsock_remove,
};

static int __init virtio_vsock_init(void)
{
	int ret;

	virtio_vsock_workqueue = alloc_workqueue("virtio_vsock", 0, 0);
	if (!virtio_vsock_workqueue)
		return -ENOMEM;
	ret = register_virtio_driver(&virtio_vsock_driver);
	if (ret)
		destroy_workqueue(virtio_vsock_workqueue);
	return ret;
}

static void __exit virtio_vsock_exit(void)
{
	unregister_virtio_driver(&virtio_vsock_driver);
	destroy_workqueue(virtio_vsock_workqueue);
}

module_init(virtio_vsock_init);
module_exit(virtio_vsock_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Asias He");
MODULE_DESCRIPTION("virtio transport for vsock");
MODULE_DEVICE_TABLE(virtio, id_table);
