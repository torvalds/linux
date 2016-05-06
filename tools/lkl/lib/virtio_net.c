#include <string.h>
#include <lkl_host.h>
#include "virtio.h"
#include "endian.h"

#include <lkl/linux/virtio_net.h>

#define netdev_of(x) (container_of(x, struct virtio_net_dev, dev))
#define BIT(x) (1ULL << x)

/* We always have 2 queues on a netdev: one for tx, one for rx. */
#define RX_QUEUE_IDX 0
#define TX_QUEUE_IDX 1
#define NUM_QUEUES (TX_QUEUE_IDX + 1)
#define QUEUE_DEPTH 32

/* In fact, we'll hit the limit on the devs string below long before
 * we hit this, but it's good enough for now. */
#define MAX_NET_DEVS 16

#ifdef DEBUG
#define bad_request(s) do {			\
		lkl_printf("%s\n", s);		\
		panic();			\
	} while (0)
#else
#define bad_request(s) lkl_printf("virtio_net: %s\n", s);
#endif /* DEBUG */

struct virtio_net_poll {
	struct virtio_net_dev *dev;
	int event;
};

struct virtio_net_dev {
	struct virtio_dev dev;
	struct lkl_virtio_net_config config;
	struct lkl_dev_net_ops *ops;
	struct lkl_netdev *nd;
	struct virtio_net_poll rx_poll, tx_poll;
	struct lkl_mutex_t **queue_locks;
};

static int net_check_features(struct virtio_dev *dev)
{
	if (dev->driver_features == dev->device_features)
		return 0;

	return -LKL_EINVAL;
}

static void net_acquire_queue(struct virtio_dev *dev, int queue_idx)
{
	lkl_host_ops.mutex_lock(netdev_of(dev)->queue_locks[queue_idx]);
}

static void net_release_queue(struct virtio_dev *dev, int queue_idx)
{
	lkl_host_ops.mutex_unlock(netdev_of(dev)->queue_locks[queue_idx]);
}

static inline int is_rx_queue(struct virtio_dev *dev, struct virtio_queue *queue)
{
       return &dev->queue[RX_QUEUE_IDX] == queue;
}

static inline int is_tx_queue(struct virtio_dev *dev, struct virtio_queue *queue)
{
       return &dev->queue[TX_QUEUE_IDX] == queue;
}

static int net_enqueue(struct virtio_dev *dev, struct virtio_req *req)
{
	struct lkl_virtio_net_hdr_v1 *header;
	struct virtio_net_dev *net_dev;
	int ret, len;
	void *buf;

	header = req->buf[0].addr;
	net_dev = netdev_of(dev);
	len = req->buf[0].len - sizeof(*header);

	buf = &header[1];

	if (!len && req->buf_count > 1) {
		buf = req->buf[1].addr;
		len = req->buf[1].len;
	}

	/* Pick which virtqueue to send the buffer(s) to */
	if (is_tx_queue(dev, req->q)) {
		ret = net_dev->ops->tx(net_dev->nd, buf, len);
		if (ret < 0)
			return -1;
	} else if (is_rx_queue(dev, req->q)) {
		header->num_buffers = 1;
		ret = net_dev->ops->rx(net_dev->nd, buf, &len);
		if (ret < 0)
			return -1;
	} else {
		bad_request("tried to push on non-existent queue");
		return -1;
	}

	virtio_req_complete(req, len + sizeof(*header));
	return 0;
}

static struct virtio_dev_ops net_ops = {
	.check_features = net_check_features,
	.enqueue = net_enqueue,
	.acquire_queue = net_acquire_queue,
	.release_queue = net_release_queue,
};

void poll_thread(void *arg)
{
	struct virtio_net_poll *np = (struct virtio_net_poll *)arg;
	int ret;

	/* Synchronization is handled in virtio_process_queue */
	while ((ret = np->dev->ops->poll(np->dev->nd, np->event)) >= 0) {
		if (ret & LKL_DEV_NET_POLL_RX)
			virtio_process_queue(&np->dev->dev, 0);
		if (ret & LKL_DEV_NET_POLL_TX)
			virtio_process_queue(&np->dev->dev, 1);
	}
}

struct virtio_net_dev *registered_devs[MAX_NET_DEVS];
static int registered_dev_idx = 0;

static int dev_register(struct virtio_net_dev *dev)
{
	if (registered_dev_idx == MAX_NET_DEVS) {
		lkl_printf("Too many virtio_net devices!\n");
		/* This error code is a little bit of a lie */
		return -LKL_ENOMEM;
	} else {
		/* registered_dev_idx is incremented by the caller */
		registered_devs[registered_dev_idx] = dev;
		return 0;
	}
}

static void free_queue_locks(struct lkl_mutex_t **queues, int num_queues)
{
	int i = 0;
	if (!queues)
		return;

	for (i = 0; i < num_queues; i++)
		lkl_host_ops.mutex_free(queues[i]);

	lkl_host_ops.mem_free(queues);
}

static struct lkl_mutex_t **init_queue_locks(int num_queues)
{
	int i;
	struct lkl_mutex_t **ret = lkl_host_ops.mem_alloc(
		sizeof(struct lkl_mutex_t*) * num_queues);
	if (!ret)
		return NULL;

	for (i = 0; i < num_queues; i++) {
		ret[i] = lkl_host_ops.mutex_alloc();
		if (!ret[i]) {
			free_queue_locks(ret, i);
			return NULL;
		}
	}

	return ret;
}

int lkl_netdev_add(struct lkl_netdev *nd, void *mac)
{
	struct virtio_net_dev *dev;
	int ret = -LKL_ENOMEM;

	dev = lkl_host_ops.mem_alloc(sizeof(*dev));
	if (!dev)
		return -LKL_ENOMEM;

	memset(dev, 0, sizeof(*dev));

	dev->dev.device_id = LKL_VIRTIO_ID_NET;
	if (mac)
		dev->dev.device_features |= BIT(LKL_VIRTIO_NET_F_MAC);
	dev->dev.config_data = &dev->config;
	dev->dev.config_len = sizeof(dev->config);
	dev->dev.ops = &net_ops;
	dev->ops = nd->ops;
	dev->nd = nd;
	dev->queue_locks = init_queue_locks(NUM_QUEUES);

	if (!dev->queue_locks)
		goto out_free;

	if (mac)
		memcpy(dev->config.mac, mac, LKL_ETH_ALEN);

	dev->rx_poll.event = LKL_DEV_NET_POLL_RX;
	dev->rx_poll.dev = dev;

	dev->tx_poll.event = LKL_DEV_NET_POLL_TX;
	dev->tx_poll.dev = dev;

	/* MUST match the number of queue locks we initialized. We
	 * could init the queues in virtio_dev_setup to help enforce
	 * this, but netdevs are the only flavor that need these
	 * locks, so it's better to do it here. */
	ret = virtio_dev_setup(&dev->dev, NUM_QUEUES, QUEUE_DEPTH);

	if (ret)
		goto out_free;

	nd->rx_tid = lkl_host_ops.thread_create(poll_thread, &dev->rx_poll);
	if (nd->rx_tid == 0)
		goto out_cleanup_dev;

	nd->tx_tid = lkl_host_ops.thread_create(poll_thread, &dev->tx_poll);
	if (nd->tx_tid == 0)
		goto out_cleanup_dev;

	ret = dev_register(dev);
	if (ret < 0)
		goto out_cleanup_dev;

	return registered_dev_idx++;

out_cleanup_dev:
	virtio_dev_cleanup(&dev->dev);

out_free:
	if (dev->queue_locks)
		free_queue_locks(dev->queue_locks, NUM_QUEUES);
	lkl_host_ops.mem_free(dev);

	return ret;
}

/* Return 0 for success, -1 for failure. */
static int lkl_netdev_remove(struct virtio_net_dev *dev)
{
	if (!dev->nd->ops->close)
		/* Can't kill the poll threads, so we can't do
		 * anything safely. */
		return -1;

	if (dev->nd->ops->close(dev->nd) < 0)
		/* Something went wrong */
		return -1;

	virtio_dev_cleanup(&dev->dev);

	lkl_host_ops.mem_free(dev->nd);
	free_queue_locks(dev->queue_locks, NUM_QUEUES);
	lkl_host_ops.mem_free(dev);

	return 0;
}

int lkl_netdevs_remove(void)
{
	int i = 0, failure_count = 0;

	for (; i < registered_dev_idx; i++)
		failure_count -= lkl_netdev_remove(registered_devs[i]);

	if (failure_count) {
		lkl_printf("WARN: failed to free %d of %d netdevs.\n",
			failure_count, registered_dev_idx);
		return -1;
	}

	return 0;
}
