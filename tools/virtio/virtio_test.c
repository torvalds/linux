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
#include <linux/virtio_types.h>
#include <linux/vhost.h>
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include "../../drivers/vhost/test.h"

#define RANDOM_BATCH -1

/* Unused */
void *__kmalloc_fake, *__kfree_ignore_start, *__kfree_ignore_end;

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

static const struct vhost_vring_file no_backend = { .fd = -1 },
				     backend = { .fd = 1 };
static const struct vhost_vring_state null_state = {};

bool vq_notify(struct virtqueue *vq)
{
	struct vq_info *info = vq->priv;
	unsigned long long v = 1;
	int r;
	r = write(info->kick, &v, sizeof v);
	assert(r == sizeof v);
	return true;
}

void vq_callback(struct virtqueue *vq)
{
}


void vhost_vq_setup(struct vdev_info *dev, struct vq_info *info)
{
	struct vhost_vring_state state = { .index = info->idx };
	struct vhost_vring_file file = { .index = info->idx };
	unsigned long long features = dev->vdev.features;
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

static void vq_reset(struct vq_info *info, int num, struct virtio_device *vdev)
{
	if (info->vq)
		vring_del_virtqueue(info->vq);

	memset(info->ring, 0, vring_size(num, 4096));
	vring_init(&info->vring, num, info->ring, 4096);
	info->vq = vring_new_virtqueue(info->idx, num, 4096, vdev, true, false,
				       info->ring, vq_notify, vq_callback, "test");
	assert(info->vq);
	info->vq->priv = info;
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
	vq_reset(info, num, &dev->vdev);
	vhost_vq_setup(dev, info);
	dev->fds[info->idx].fd = info->call;
	dev->fds[info->idx].events = POLLIN;
	dev->nvqs++;
}

static void vdev_info_init(struct vdev_info* dev, unsigned long long features)
{
	int r;
	memset(dev, 0, sizeof *dev);
	dev->vdev.features = features;
	INIT_LIST_HEAD(&dev->vdev.vqs);
	spin_lock_init(&dev->vdev.vqs_list_lock);
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

static void run_test(struct vdev_info *dev, struct vq_info *vq,
		     bool delayed, int batch, int reset_n, int bufs)
{
	struct scatterlist sl;
	long started = 0, completed = 0, next_reset = reset_n;
	long completed_before, started_before;
	int r, test = 1;
	unsigned int len;
	long long spurious = 0;
	const bool random_batch = batch == RANDOM_BATCH;

	r = ioctl(dev->control, VHOST_TEST_RUN, &test);
	assert(r >= 0);
	if (!reset_n) {
		next_reset = INT_MAX;
	}

	for (;;) {
		virtqueue_disable_cb(vq->vq);
		completed_before = completed;
		started_before = started;
		do {
			const bool reset = completed > next_reset;
			if (random_batch)
				batch = (random() % vq->vring.num) + 1;

			while (started < bufs &&
			       (started - completed) < batch) {
				sg_init_one(&sl, dev->buf, dev->buf_size);
				r = virtqueue_add_outbuf(vq->vq, &sl, 1,
							 dev->buf + started,
							 GFP_ATOMIC);
				if (unlikely(r != 0)) {
					if (r == -ENOSPC &&
					    started > started_before)
						r = 0;
					else
						r = -1;
					break;
				}

				++started;

				if (unlikely(!virtqueue_kick(vq->vq))) {
					r = -1;
					break;
				}
			}

			if (started >= bufs)
				r = -1;

			if (reset) {
				r = ioctl(dev->control, VHOST_TEST_SET_BACKEND,
					  &no_backend);
				assert(!r);
			}

			/* Flush out completed bufs if any */
			while (virtqueue_get_buf(vq->vq, &len)) {
				++completed;
				r = 0;
			}

			if (reset) {
				struct vhost_vring_state s = { .index = 0 };

				vq_reset(vq, vq->vring.num, &dev->vdev);

				r = ioctl(dev->control, VHOST_GET_VRING_BASE,
					  &s);
				assert(!r);

				s.num = 0;
				r = ioctl(dev->control, VHOST_SET_VRING_BASE,
					  &null_state);
				assert(!r);

				r = ioctl(dev->control, VHOST_TEST_SET_BACKEND,
					  &backend);
				assert(!r);

				started = completed;
				while (completed > next_reset)
					next_reset += completed;
			}
		} while (r == 0);
		if (completed == completed_before && started == started_before)
			++spurious;
		assert(completed <= bufs);
		assert(started <= bufs);
		if (completed == bufs)
			break;
		if (delayed) {
			if (virtqueue_enable_cb_delayed(vq->vq))
				wait_for_interrupt(dev);
		} else {
			if (virtqueue_enable_cb(vq->vq))
				wait_for_interrupt(dev);
		}
	}
	test = 0;
	r = ioctl(dev->control, VHOST_TEST_RUN, &test);
	assert(r >= 0);
	fprintf(stderr,
		"spurious wakeups: 0x%llx started=0x%lx completed=0x%lx\n",
		spurious, started, completed);
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
		.name = "batch",
		.val = 'b',
		.has_arg = required_argument,
	},
	{
		.name = "reset",
		.val = 'r',
		.has_arg = optional_argument,
	},
	{
	}
};

static void help(void)
{
	fprintf(stderr, "Usage: virtio_test [--help]"
		" [--no-indirect]"
		" [--no-event-idx]"
		" [--no-virtio-1]"
		" [--delayed-interrupt]"
		" [--batch=random/N]"
		" [--reset=N]"
		"\n");
}

int main(int argc, char **argv)
{
	struct vdev_info dev;
	unsigned long long features = (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
		(1ULL << VIRTIO_RING_F_EVENT_IDX) | (1ULL << VIRTIO_F_VERSION_1);
	long batch = 1, reset = 0;
	int o;
	bool delayed = false;

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
		case '0':
			features &= ~(1ULL << VIRTIO_F_VERSION_1);
			break;
		case 'D':
			delayed = true;
			break;
		case 'b':
			if (0 == strcmp(optarg, "random")) {
				batch = RANDOM_BATCH;
			} else {
				batch = strtol(optarg, NULL, 10);
				assert(batch > 0);
				assert(batch < (long)INT_MAX + 1);
			}
			break;
		case 'r':
			if (!optarg) {
				reset = 1;
			} else {
				reset = strtol(optarg, NULL, 10);
				assert(reset > 0);
				assert(reset < (long)INT_MAX + 1);
			}
			break;
		default:
			assert(0);
			break;
		}
	}

done:
	vdev_info_init(&dev, features);
	vq_info_add(&dev, 256);
	run_test(&dev, &dev.vqs[0], delayed, batch, reset, 0x100000);
	return 0;
}
