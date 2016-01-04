#include <string.h>
#include <lkl_host.h>
#include "virtio.h"
#include "endian.h"

#define BIT(x) (1<<x)

#define VIRTIO_NET_F_MAC	BIT(5)

struct virtio_net_poll {
	struct virtio_net_dev *dev;
	void *sem;
	int event;
};

struct virtio_net_dev {
	struct virtio_dev dev;
	struct {
		uint8_t mac[6];
		uint16_t status;
		uint16_t max_virqueue_pairs;
	} config __attribute__((packed));
	struct lkl_dev_net_ops *ops;
	union lkl_netdev nd;
	struct virtio_net_poll rx_poll, tx_poll;
};

#define VIRTIO_NET_HDR_F_NEEDS_CSUM    1
#define VIRTIO_NET_HDR_GSO_NONE        0
#define VIRTIO_NET_HDR_GSO_TCPV4       1
#define VIRTIO_NET_HDR_GSO_UDP         3
#define VIRTIO_NET_HDR_GSO_TCPV6       4
#define VIRTIO_NET_HDR_GSO_ECN      0x80

struct virtio_net_header {
        uint8_t flags;
        uint8_t gso_type;
        uint16_t hdr_len;
        uint16_t gso_size;
        uint16_t csum_start;
        uint16_t csum_offset;
        uint16_t num_buffers;
	char data[];
} __attribute((packed));

static int net_check_features(struct virtio_dev *dev)
{
	if (dev->driver_features == dev->device_features)
		return 0;

	return -LKL_EINVAL;
}

#if 0
static void net_notify(struct virtio_dev *dev, int qidx)
{
	if (qidx == 0)
		virtio_process_queue(dev, qidx);
}
#endif

static int net_enqueue(struct virtio_dev *dev, struct virtio_req *req)
{
	struct virtio_net_header *h;
	struct virtio_net_dev *net_dev;
	int ret, len;
	void *buf;

	h = req->buf[0].addr;
	net_dev = container_of(dev, struct virtio_net_dev, dev);
	len = req->buf[0].len - sizeof(*h);
	buf = h->data;

	if (!len && req->buf_count > 1) {
		buf = req->buf[1].addr;
		len = req->buf[1].len;
	}

	if (req->q != dev->queue) {
		ret = net_dev->ops->tx(net_dev->nd, buf, len);
		if (ret < 0) {
			lkl_host_ops.sem_up(net_dev->tx_poll.sem);
			return -1;
		}
	} else {
		h->num_buffers = 1;
		ret = net_dev->ops->rx(net_dev->nd, buf, &len);
		if (ret < 0) {
			lkl_host_ops.sem_up(net_dev->rx_poll.sem);
			return -1;
		}
	}

	virtio_req_complete(req, len + sizeof(*h));
	return 0;
}

static struct virtio_dev_ops net_ops = {
	.check_features = net_check_features,
	.enqueue = net_enqueue,
};

void poll_thread(void *arg)
{
	struct virtio_net_poll *np = (struct virtio_net_poll *)arg;
	int ret;

	while ((ret = np->dev->ops->poll(np->dev->nd, np->event)) >= 0) {
		if (ret & LKL_DEV_NET_POLL_RX)
			virtio_process_queue(&np->dev->dev, 0);
		if (ret & LKL_DEV_NET_POLL_TX)
			virtio_process_queue(&np->dev->dev, 1);
		lkl_host_ops.sem_down(np->sem);
	}
}

int lkl_netdev_add(union lkl_netdev nd, void *mac)
{
	struct virtio_net_dev *dev;
	static int count;
	int ret = -LKL_ENOMEM;

	dev = lkl_host_ops.mem_alloc(sizeof(*dev));
	if (!dev)
		return -LKL_ENOMEM;

	dev->dev.device_id = 1;
	dev->dev.vendor_id = 0;
	dev->dev.device_features = 0;
	if (mac)
		dev->dev.device_features |= VIRTIO_NET_F_MAC;
	dev->dev.config_gen = 0;
	dev->dev.config_data = &dev->config;
	dev->dev.config_len = sizeof(dev->config);
	dev->dev.ops = &net_ops;
	dev->ops = &lkl_dev_net_ops;
	dev->nd = nd;

	if (mac)
		memcpy(dev->config.mac, mac, 6);

	dev->rx_poll.event = LKL_DEV_NET_POLL_RX;
	dev->rx_poll.sem = lkl_host_ops.sem_alloc(0);
	dev->rx_poll.dev = dev;

	dev->tx_poll.event = LKL_DEV_NET_POLL_TX;
	dev->tx_poll.sem = lkl_host_ops.sem_alloc(0);
	dev->tx_poll.dev = dev;

	if (!dev->rx_poll.sem || !dev->tx_poll.sem)
		goto out_free;

	ret = virtio_dev_setup(&dev->dev, 2, 32);
	if (ret)
		goto out_free;

	if (lkl_host_ops.thread_create(poll_thread, &dev->rx_poll) < 0)
		goto out_cleanup_dev;

	if (lkl_host_ops.thread_create(poll_thread, &dev->tx_poll) < 0)
		goto out_cleanup_dev;

	/* RX/TX thread polls will exit when the host netdev handle is closed */

	return count++;

out_cleanup_dev:
	virtio_dev_cleanup(&dev->dev);

out_free:
	if (dev->rx_poll.sem)
		lkl_host_ops.sem_free(dev->rx_poll.sem);
	if (dev->tx_poll.sem)
		lkl_host_ops.sem_free(dev->tx_poll.sem);
	lkl_host_ops.mem_free(dev);

	return ret;
}
