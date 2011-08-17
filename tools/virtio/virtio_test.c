#define _GNU_SOURCE
#include <getopt.h>
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
#include <linux/vhost.h>
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include "../../drivers/vhost/test.h"

struct vq_info {
	int kick;
	int call;
	int num;
	int idx;
	void *ring;
	/* copy used for control */
	struct vring vring;
	struct virtqueue *vq;
};

struct vdev_info {
	struct virtio_device vdev;
	int control;
	struct pollfd fds[1];
	struct vq_info vqs[1];
	int nvqs;
	void *buf;
	size_t buf_size;
	struct vhost_memory *mem;
};

void vq_notify(struct virtqueue *vq)
{
	struct vq_info *info = vq->priv;
	unsigned long long v = 1;
	int r;
	r = write(info->kick, &v, sizeof v);
	assert(r == sizeof v);
}

void vq_callback(struct virtqueue *vq)
{
}


void vhost_vq_setup(struct vdev_info *dev, struct vq_info *info)
{
	struct vhost_vring_state state = { .index = info->idx };
	struct vhost_vring_file file = { .index = info->idx };
	unsigned long long features = dev->vdev.features[0];
	struct vhost_vring_addr addr = {
		.index = info->idx,
		.desc_user_addr = (uint64_t)(unsigned long)info->vring.desc,
		.avail_user_addr = (uint64_t)(unsigned long)info->vring.avail,
		.used_user_addr = (uint64_t)(unsigned long)info->vring.used,
	};
	int r;
	r = ioctl(dev->control, VHOST_SET_FEATURES, &features);
	assert(r >= 0);
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
	file.fd = info->call;
	r = ioctl(dev->control, VHOST_SET_VRING_CALL, &file);
	assert(r >= 0);
}

static void vq_info_add(struct vdev_info *dev, int num)
{
	struct vq_info *info = &dev->vqs[dev->nvqs];
	int r;
	info->idx = dev->nvqs;
	info->kick = eventfd(0, EFD_NONBLOCK);
	info->call = eventfd(0, EFD_NONBLOCK);
	r = posix_memalign(&info->ring, 4096, vring_size(num, 4096));
	assert(r >= 0);
	memset(info->ring, 0, vring_size(num, 4096));
	vring_init(&info->vring, num, info->ring, 4096);
	info->vq = vring_new_virtqueue(info->vring.num, 4096, &dev->vdev, info->ring,
				       vq_notify, vq_callback, "test");
	assert(info->vq);
	info->vq->priv = info;
	vhost_vq_setup(dev, info);
	dev->fds[info->idx].fd = info->call;
	dev->fds[info->idx].events = POLLIN;
	dev->nvqs++;
}

static void vdev_info_init(struct vdev_info* dev, unsigned long long features)
{
	int r;
	memset(dev, 0, sizeof *dev);
	dev->vdev.features[0] = features;
	dev->vdev.features[1] = features >> 32;
	dev->buf_size = 1024;
	dev->buf = malloc(dev->buf_size);
	assert(dev->buf);
        dev->control = open("/dev/vhost-test", O_RDWR);
	assert(dev->control >= 0);
	r = ioctl(dev->control, VHOST_SET_OWNER, NULL);
	assert(r >= 0);
	dev->mem = malloc(offsetof(struct vhost_memory, regions) +
			  sizeof dev->mem->regions[0]);
	assert(dev->mem);
	memset(dev->mem, 0, offsetof(struct vhost_memory, regions) +
                          sizeof dev->mem->regions[0]);
	dev->mem->nregions = 1;
	dev->mem->regions[0].guest_phys_addr = (long)dev->buf;
	dev->mem->regions[0].userspace_addr = (long)dev->buf;
	dev->mem->regions[0].memory_size = dev->buf_size;
	r = ioctl(dev->control, VHOST_SET_MEM_TABLE, dev->mem);
	assert(r >= 0);
}

/* TODO: this is pretty bad: we get a cache line bounce
 * for the wait queue on poll and another one on read,
 * plus the read which is there just to clear the
 * current state. */
static void wait_for_interrupt(struct vdev_info *dev)
{
	int i;
	unsigned long long val;
	poll(dev->fds, dev->nvqs, -1);
	for (i = 0; i < dev->nvqs; ++i)
		if (dev->fds[i].revents & POLLIN) {
			read(dev->fds[i].fd, &val, sizeof val);
		}
}

static void run_test(struct vdev_info *dev, struct vq_info *vq, int bufs)
{
	struct scatterlist sl;
	long started = 0, completed = 0;
	long completed_before;
	int r, test = 1;
	unsigned len;
	long long spurious = 0;
	r = ioctl(dev->control, VHOST_TEST_RUN, &test);
	assert(r >= 0);
	for (;;) {
		virtqueue_disable_cb(vq->vq);
		completed_before = completed;
		do {
			if (started < bufs) {
				sg_init_one(&sl, dev->buf, dev->buf_size);
				r = virtqueue_add_buf(vq->vq, &sl, 1, 0,
						      dev->buf + started);
				if (likely(r >= 0)) {
					++started;
					virtqueue_kick(vq->vq);
				}
			} else
				r = -1;

			/* Flush out completed bufs if any */
			if (virtqueue_get_buf(vq->vq, &len)) {
				++completed;
				r = 0;
			}

		} while (r >= 0);
		if (completed == completed_before)
			++spurious;
		assert(completed <= bufs);
		assert(started <= bufs);
		if (completed == bufs)
			break;
		if (virtqueue_enable_cb(vq->vq)) {
			wait_for_interrupt(dev);
		}
	}
	test = 0;
	r = ioctl(dev->control, VHOST_TEST_RUN, &test);
	assert(r >= 0);
	fprintf(stderr, "spurious wakeus: 0x%llx\n", spurious);
}

const char optstring[] = "h";
const struct option longopts[] = {
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
	}
};

static void help()
{
	fprintf(stderr, "Usage: virtio_test [--help]"
		" [--no-indirect]"
		" [--no-event-idx]"
		"\n");
}

int main(int argc, char **argv)
{
	struct vdev_info dev;
	unsigned long long features = (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
		(1ULL << VIRTIO_RING_F_EVENT_IDX);
	int o;

	for (;;) {
		o = getopt_long(argc, argv, optstring, longopts, NULL);
		switch (o) {
		case -1:
			goto done;
		case '?':
			help();
			exit(2);
		case 'e':
			features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
			break;
		case 'h':
			help();
			goto done;
		case 'i':
			features &= ~(1ULL << VIRTIO_RING_F_INDIRECT_DESC);
			break;
		default:
			assert(0);
			break;
		}
	}

done:
	vdev_info_init(&dev, features);
	vq_info_add(&dev, 256);
	run_test(&dev, &dev.vqs[0], 0x100000);
	return 0;
}
