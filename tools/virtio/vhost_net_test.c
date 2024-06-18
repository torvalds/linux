// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdbool.h>
#include <linux/vhost.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/in.h>
#include <linux/if_packet.h>
#include <linux/virtio_net.h>
#include <netinet/ether.h>

#define HDR_LEN		sizeof(struct virtio_net_hdr_mrg_rxbuf)
#define TEST_BUF_LEN	256
#define TEST_PTYPE	ETH_P_LOOPBACK
#define DESC_NUM	256

/* Used by implementation of kmalloc() in tools/virtio/linux/kernel.h */
void *__kmalloc_fake, *__kfree_ignore_start, *__kfree_ignore_end;

struct vq_info {
	int kick;
	int call;
	int idx;
	long started;
	long completed;
	struct pollfd fds;
	void *ring;
	/* copy used for control */
	struct vring vring;
	struct virtqueue *vq;
};

struct vdev_info {
	struct virtio_device vdev;
	int control;
	struct vq_info vqs[2];
	int nvqs;
	void *buf;
	size_t buf_size;
	char *test_buf;
	char *res_buf;
	struct vhost_memory *mem;
	int sock;
	int ifindex;
	unsigned char mac[ETHER_ADDR_LEN];
};

static int tun_alloc(struct vdev_info *dev, char *tun_name)
{
	struct ifreq ifr;
	int len = HDR_LEN;
	int fd, e;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		perror("Cannot open /dev/net/tun");
		return fd;
	}

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_VNET_HDR;
	strncpy(ifr.ifr_name, tun_name, IFNAMSIZ);

	e = ioctl(fd, TUNSETIFF, &ifr);
	if (e < 0) {
		perror("ioctl[TUNSETIFF]");
		close(fd);
		return e;
	}

	e = ioctl(fd, TUNSETVNETHDRSZ, &len);
	if (e < 0) {
		perror("ioctl[TUNSETVNETHDRSZ]");
		close(fd);
		return e;
	}

	e = ioctl(fd, SIOCGIFHWADDR, &ifr);
	if (e < 0) {
		perror("ioctl[SIOCGIFHWADDR]");
		close(fd);
		return e;
	}

	memcpy(dev->mac, &ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
	return fd;
}

static void vdev_create_socket(struct vdev_info *dev, char *tun_name)
{
	struct ifreq ifr;

	dev->sock = socket(AF_PACKET, SOCK_RAW, htons(TEST_PTYPE));
	assert(dev->sock != -1);

	strncpy(ifr.ifr_name, tun_name, IFNAMSIZ);
	assert(ioctl(dev->sock, SIOCGIFINDEX, &ifr) >= 0);

	dev->ifindex = ifr.ifr_ifindex;

	/* Set the flags that bring the device up */
	assert(ioctl(dev->sock, SIOCGIFFLAGS, &ifr) >= 0);
	ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
	assert(ioctl(dev->sock, SIOCSIFFLAGS, &ifr) >= 0);
}

static void vdev_send_packet(struct vdev_info *dev)
{
	char *sendbuf = dev->test_buf + HDR_LEN;
	struct sockaddr_ll saddrll = {0};
	int sockfd = dev->sock;
	int ret;

	saddrll.sll_family = PF_PACKET;
	saddrll.sll_ifindex = dev->ifindex;
	saddrll.sll_halen = ETH_ALEN;
	saddrll.sll_protocol = htons(TEST_PTYPE);

	ret = sendto(sockfd, sendbuf, TEST_BUF_LEN, 0,
		     (struct sockaddr *)&saddrll,
		     sizeof(struct sockaddr_ll));
	assert(ret >= 0);
}

static bool vq_notify(struct virtqueue *vq)
{
	struct vq_info *info = vq->priv;
	unsigned long long v = 1;
	int r;

	r = write(info->kick, &v, sizeof(v));
	assert(r == sizeof(v));

	return true;
}

static void vhost_vq_setup(struct vdev_info *dev, struct vq_info *info)
{
	struct vhost_vring_addr addr = {
		.index = info->idx,
		.desc_user_addr = (uint64_t)(unsigned long)info->vring.desc,
		.avail_user_addr = (uint64_t)(unsigned long)info->vring.avail,
		.used_user_addr = (uint64_t)(unsigned long)info->vring.used,
	};
	struct vhost_vring_state state = { .index = info->idx };
	struct vhost_vring_file file = { .index = info->idx };
	int r;

	state.num = info->vring.num;
	r = ioctl(dev->control, VHOST_SET_VRING_NUM, &state);
	assert(r >= 0);

	state.num = 0;
	r = ioctl(dev->control, VHOST_SET_VRING_BASE, &state);
	assert(r >= 0);

	r = ioctl(dev->control, VHOST_SET_VRING_ADDR, &addr);
	assert(r >= 0);

	file.fd = info->kick;
	r = ioctl(dev->control, VHOST_SET_VRING_KICK, &file);
	assert(r >= 0);
}

static void vq_reset(struct vq_info *info, int num, struct virtio_device *vdev)
{
	if (info->vq)
		vring_del_virtqueue(info->vq);

	memset(info->ring, 0, vring_size(num, 4096));
	vring_init(&info->vring, num, info->ring, 4096);
	info->vq = vring_new_virtqueue(info->idx, num, 4096, vdev, true, false,
				       info->ring, vq_notify, NULL, "test");
	assert(info->vq);
	info->vq->priv = info;
}

static void vq_info_add(struct vdev_info *dev, int idx, int num, int fd)
{
	struct vhost_vring_file backend = { .index = idx, .fd = fd };
	struct vq_info *info = &dev->vqs[idx];
	int r;

	info->idx = idx;
	info->kick = eventfd(0, EFD_NONBLOCK);
	r = posix_memalign(&info->ring, 4096, vring_size(num, 4096));
	assert(r >= 0);
	vq_reset(info, num, &dev->vdev);
	vhost_vq_setup(dev, info);

	r = ioctl(dev->control, VHOST_NET_SET_BACKEND, &backend);
	assert(!r);
}

static void vdev_info_init(struct vdev_info *dev, unsigned long long features)
{
	struct ether_header *eh;
	int i, r;

	dev->vdev.features = features;
	INIT_LIST_HEAD(&dev->vdev.vqs);
	spin_lock_init(&dev->vdev.vqs_list_lock);

	dev->buf_size = (HDR_LEN + TEST_BUF_LEN) * 2;
	dev->buf = malloc(dev->buf_size);
	assert(dev->buf);
	dev->test_buf = dev->buf;
	dev->res_buf = dev->test_buf + HDR_LEN + TEST_BUF_LEN;

	memset(dev->test_buf, 0, HDR_LEN + TEST_BUF_LEN);
	eh = (struct ether_header *)(dev->test_buf + HDR_LEN);
	eh->ether_type = htons(TEST_PTYPE);
	memcpy(eh->ether_dhost, dev->mac, ETHER_ADDR_LEN);
	memcpy(eh->ether_shost, dev->mac, ETHER_ADDR_LEN);

	for (i = sizeof(*eh); i < TEST_BUF_LEN; i++)
		dev->test_buf[i + HDR_LEN] = (char)i;

	dev->control = open("/dev/vhost-net", O_RDWR);
	assert(dev->control >= 0);

	r = ioctl(dev->control, VHOST_SET_OWNER, NULL);
	assert(r >= 0);

	dev->mem = malloc(offsetof(struct vhost_memory, regions) +
			  sizeof(dev->mem->regions[0]));
	assert(dev->mem);
	memset(dev->mem, 0, offsetof(struct vhost_memory, regions) +
	       sizeof(dev->mem->regions[0]));
	dev->mem->nregions = 1;
	dev->mem->regions[0].guest_phys_addr = (long)dev->buf;
	dev->mem->regions[0].userspace_addr = (long)dev->buf;
	dev->mem->regions[0].memory_size = dev->buf_size;

	r = ioctl(dev->control, VHOST_SET_MEM_TABLE, dev->mem);
	assert(r >= 0);

	r = ioctl(dev->control, VHOST_SET_FEATURES, &features);
	assert(r >= 0);

	dev->nvqs = 2;
}

static void wait_for_interrupt(struct vq_info *vq)
{
	unsigned long long val;

	poll(&vq->fds, 1, 100);

	if (vq->fds.revents & POLLIN)
		read(vq->fds.fd, &val, sizeof(val));
}

static void verify_res_buf(char *res_buf)
{
	int i;

	for (i = ETHER_HDR_LEN; i < TEST_BUF_LEN; i++)
		assert(res_buf[i] == (char)i);
}

static void run_tx_test(struct vdev_info *dev, struct vq_info *vq,
			bool delayed, int bufs)
{
	long long spurious = 0;
	struct scatterlist sl;
	unsigned int len;
	int r;

	for (;;) {
		long started_before = vq->started;
		long completed_before = vq->completed;

		virtqueue_disable_cb(vq->vq);
		do {
			while (vq->started < bufs &&
			       (vq->started - vq->completed) < 1) {
				sg_init_one(&sl, dev->test_buf, HDR_LEN + TEST_BUF_LEN);
				r = virtqueue_add_outbuf(vq->vq, &sl, 1,
							 dev->test_buf + vq->started,
							 GFP_ATOMIC);
				if (unlikely(r != 0))
					break;

				++vq->started;

				if (unlikely(!virtqueue_kick(vq->vq))) {
					r = -1;
					break;
				}
			}

			if (vq->started >= bufs)
				r = -1;

			/* Flush out completed bufs if any */
			while (virtqueue_get_buf(vq->vq, &len)) {
				int n;

				n = recvfrom(dev->sock, dev->res_buf, TEST_BUF_LEN, 0, NULL, NULL);
				assert(n == TEST_BUF_LEN);
				verify_res_buf(dev->res_buf);

				++vq->completed;
				r = 0;
			}
		} while (r == 0);

		if (vq->completed == completed_before && vq->started == started_before)
			++spurious;

		assert(vq->completed <= bufs);
		assert(vq->started <= bufs);
		if (vq->completed == bufs)
			break;

		if (delayed) {
			if (virtqueue_enable_cb_delayed(vq->vq))
				wait_for_interrupt(vq);
		} else {
			if (virtqueue_enable_cb(vq->vq))
				wait_for_interrupt(vq);
		}
	}
	printf("TX spurious wakeups: 0x%llx started=0x%lx completed=0x%lx\n",
	       spurious, vq->started, vq->completed);
}

static void run_rx_test(struct vdev_info *dev, struct vq_info *vq,
			bool delayed, int bufs)
{
	long long spurious = 0;
	struct scatterlist sl;
	unsigned int len;
	int r;

	for (;;) {
		long started_before = vq->started;
		long completed_before = vq->completed;

		do {
			while (vq->started < bufs &&
			       (vq->started - vq->completed) < 1) {
				sg_init_one(&sl, dev->res_buf, HDR_LEN + TEST_BUF_LEN);

				r = virtqueue_add_inbuf(vq->vq, &sl, 1,
							dev->res_buf + vq->started,
							GFP_ATOMIC);
				if (unlikely(r != 0))
					break;

				++vq->started;

				vdev_send_packet(dev);

				if (unlikely(!virtqueue_kick(vq->vq))) {
					r = -1;
					break;
				}
			}

			if (vq->started >= bufs)
				r = -1;

			/* Flush out completed bufs if any */
			while (virtqueue_get_buf(vq->vq, &len)) {
				struct ether_header *eh;

				eh = (struct ether_header *)(dev->res_buf + HDR_LEN);

				/* tun netdev is up and running, only handle the
				 * TEST_PTYPE packet.
				 */
				if (eh->ether_type == htons(TEST_PTYPE)) {
					assert(len == TEST_BUF_LEN + HDR_LEN);
					verify_res_buf(dev->res_buf + HDR_LEN);
				}

				++vq->completed;
				r = 0;
			}
		} while (r == 0);

		if (vq->completed == completed_before && vq->started == started_before)
			++spurious;

		assert(vq->completed <= bufs);
		assert(vq->started <= bufs);
		if (vq->completed == bufs)
			break;
	}

	printf("RX spurious wakeups: 0x%llx started=0x%lx completed=0x%lx\n",
	       spurious, vq->started, vq->completed);
}

static const char optstring[] = "h";
static const struct option longopts[] = {
	{
		.name = "help",
		.val = 'h',
	},
	{
		.name = "event-idx",
		.val = 'E',
	},
	{
		.name = "no-event-idx",
		.val = 'e',
	},
	{
		.name = "indirect",
		.val = 'I',
	},
	{
		.name = "no-indirect",
		.val = 'i',
	},
	{
		.name = "virtio-1",
		.val = '1',
	},
	{
		.name = "no-virtio-1",
		.val = '0',
	},
	{
		.name = "delayed-interrupt",
		.val = 'D',
	},
	{
		.name = "no-delayed-interrupt",
		.val = 'd',
	},
	{
		.name = "buf-num",
		.val = 'n',
		.has_arg = required_argument,
	},
	{
		.name = "batch",
		.val = 'b',
		.has_arg = required_argument,
	},
	{
	}
};

static void help(int status)
{
	fprintf(stderr, "Usage: vhost_net_test [--help]"
		" [--no-indirect]"
		" [--no-event-idx]"
		" [--no-virtio-1]"
		" [--delayed-interrupt]"
		" [--buf-num]"
		"\n");

	exit(status);
}

int main(int argc, char **argv)
{
	unsigned long long features = (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
		(1ULL << VIRTIO_RING_F_EVENT_IDX) | (1ULL << VIRTIO_F_VERSION_1);
	char tun_name[IFNAMSIZ];
	long nbufs = 0x100000;
	struct vdev_info dev;
	bool delayed = false;
	int o, fd;

	for (;;) {
		o = getopt_long(argc, argv, optstring, longopts, NULL);
		switch (o) {
		case -1:
			goto done;
		case '?':
			help(2);
		case 'e':
			features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
			break;
		case 'h':
			help(0);
		case 'i':
			features &= ~(1ULL << VIRTIO_RING_F_INDIRECT_DESC);
			break;
		case '0':
			features &= ~(1ULL << VIRTIO_F_VERSION_1);
			break;
		case 'D':
			delayed = true;
			break;
		case 'n':
			nbufs = strtol(optarg, NULL, 10);
			assert(nbufs > 0);
			break;
		default:
			assert(0);
			break;
		}
	}

done:
	memset(&dev, 0, sizeof(dev));
	snprintf(tun_name, IFNAMSIZ, "tun_%d", getpid());

	fd = tun_alloc(&dev, tun_name);
	assert(fd >= 0);

	vdev_info_init(&dev, features);
	vq_info_add(&dev, 0, DESC_NUM, fd);
	vq_info_add(&dev, 1, DESC_NUM, fd);
	vdev_create_socket(&dev, tun_name);

	run_rx_test(&dev, &dev.vqs[0], delayed, nbufs);
	run_tx_test(&dev, &dev.vqs[1], delayed, nbufs);

	return 0;
}
