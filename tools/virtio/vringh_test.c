// SPDX-License-Identifier: GPL-2.0
/* Simple test of virtio code, entirely in userpsace. */
#define _GNU_SOURCE
#include <sched.h>
#include <err.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/virtio.h>
#include <linux/vringh.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_config.h>
#include <linux/uaccess.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>

#define USER_MEM (1024*1024)
void *__user_addr_min, *__user_addr_max;
void *__kmalloc_fake, *__kfree_ignore_start, *__kfree_ignore_end;
static u64 user_addr_offset;

#define RINGSIZE 256
#define ALIGN 4096

static bool never_notify_host(struct virtqueue *vq)
{
	abort();
}

static void never_callback_guest(struct virtqueue *vq)
{
	abort();
}

static bool getrange_iov(struct vringh *vrh, u64 addr, struct vringh_range *r)
{
	if (addr < (u64)(unsigned long)__user_addr_min - user_addr_offset)
		return false;
	if (addr >= (u64)(unsigned long)__user_addr_max - user_addr_offset)
		return false;

	r->start = (u64)(unsigned long)__user_addr_min - user_addr_offset;
	r->end_incl = (u64)(unsigned long)__user_addr_max - 1 - user_addr_offset;
	r->offset = user_addr_offset;
	return true;
}

/* We return single byte ranges. */
static bool getrange_slow(struct vringh *vrh, u64 addr, struct vringh_range *r)
{
	if (addr < (u64)(unsigned long)__user_addr_min - user_addr_offset)
		return false;
	if (addr >= (u64)(unsigned long)__user_addr_max - user_addr_offset)
		return false;

	r->start = addr;
	r->end_incl = r->start;
	r->offset = user_addr_offset;
	return true;
}

struct guest_virtio_device {
	struct virtio_device vdev;
	int to_host_fd;
	unsigned long notifies;
};

static bool parallel_notify_host(struct virtqueue *vq)
{
	int rc;
	struct guest_virtio_device *gvdev;

	gvdev = container_of(vq->vdev, struct guest_virtio_device, vdev);
	rc = write(gvdev->to_host_fd, "", 1);
	if (rc < 0)
		return false;
	gvdev->notifies++;
	return true;
}

static bool no_notify_host(struct virtqueue *vq)
{
	return true;
}

#define NUM_XFERS (10000000)

/* We aim for two "distant" cpus. */
static void find_cpus(unsigned int *first, unsigned int *last)
{
	unsigned int i;

	*first = -1U;
	*last = 0;
	for (i = 0; i < 4096; i++) {
		cpu_set_t set;
		CPU_ZERO(&set);
		CPU_SET(i, &set);
		if (sched_setaffinity(getpid(), sizeof(set), &set) == 0) {
			if (i < *first)
				*first = i;
			if (i > *last)
				*last = i;
		}
	}
}

/* Opencoded version for fast mode */
static inline int vringh_get_head(struct vringh *vrh, u16 *head)
{
	u16 avail_idx, i;
	int err;

	err = get_user(avail_idx, &vrh->vring.avail->idx);
	if (err)
		return err;

	if (vrh->last_avail_idx == avail_idx)
		return 0;

	/* Only get avail ring entries after they have been exposed by guest. */
	virtio_rmb(vrh->weak_barriers);

	i = vrh->last_avail_idx & (vrh->vring.num - 1);

	err = get_user(*head, &vrh->vring.avail->ring[i]);
	if (err)
		return err;

	vrh->last_avail_idx++;
	return 1;
}

static int parallel_test(u64 features,
			 bool (*getrange)(struct vringh *vrh,
					  u64 addr, struct vringh_range *r),
			 bool fast_vringh)
{
	void *host_map, *guest_map;
	int fd, mapsize, to_guest[2], to_host[2];
	unsigned long xfers = 0, notifies = 0, receives = 0;
	unsigned int first_cpu, last_cpu;
	cpu_set_t cpu_set;
	char buf[128];

	/* Create real file to mmap. */
	fd = open("/tmp/vringh_test-file", O_RDWR|O_CREAT|O_TRUNC, 0600);
	if (fd < 0)
		err(1, "Opening /tmp/vringh_test-file");

	/* Extra room at the end for some data, and indirects */
	mapsize = vring_size(RINGSIZE, ALIGN)
		+ RINGSIZE * 2 * sizeof(int)
		+ RINGSIZE * 6 * sizeof(struct vring_desc);
	mapsize = (mapsize + getpagesize() - 1) & ~(getpagesize() - 1);
	ftruncate(fd, mapsize);

	/* Parent and child use separate addresses, to check our mapping logic! */
	host_map = mmap(NULL, mapsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	guest_map = mmap(NULL, mapsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

	pipe(to_guest);
	pipe(to_host);

	CPU_ZERO(&cpu_set);
	find_cpus(&first_cpu, &last_cpu);
	printf("Using CPUS %u and %u\n", first_cpu, last_cpu);
	fflush(stdout);

	if (fork() != 0) {
		struct vringh vrh;
		int status, err, rlen = 0;
		char rbuf[5];

		/* We are the host: never access guest addresses! */
		munmap(guest_map, mapsize);

		__user_addr_min = host_map;
		__user_addr_max = __user_addr_min + mapsize;
		user_addr_offset = host_map - guest_map;
		assert(user_addr_offset);

		close(to_guest[0]);
		close(to_host[1]);

		vring_init(&vrh.vring, RINGSIZE, host_map, ALIGN);
		vringh_init_user(&vrh, features, RINGSIZE, true,
				 vrh.vring.desc, vrh.vring.avail, vrh.vring.used);
		CPU_SET(first_cpu, &cpu_set);
		if (sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set))
			errx(1, "Could not set affinity to cpu %u", first_cpu);

		while (xfers < NUM_XFERS) {
			struct iovec host_riov[2], host_wiov[2];
			struct vringh_iov riov, wiov;
			u16 head, written;

			if (fast_vringh) {
				for (;;) {
					err = vringh_get_head(&vrh, &head);
					if (err != 0)
						break;
					err = vringh_need_notify_user(&vrh);
					if (err < 0)
						errx(1, "vringh_need_notify_user: %i",
						     err);
					if (err) {
						write(to_guest[1], "", 1);
						notifies++;
					}
				}
				if (err != 1)
					errx(1, "vringh_get_head");
				written = 0;
				goto complete;
			} else {
				vringh_iov_init(&riov,
						host_riov,
						ARRAY_SIZE(host_riov));
				vringh_iov_init(&wiov,
						host_wiov,
						ARRAY_SIZE(host_wiov));

				err = vringh_getdesc_user(&vrh, &riov, &wiov,
							  getrange, &head);
			}
			if (err == 0) {
				err = vringh_need_notify_user(&vrh);
				if (err < 0)
					errx(1, "vringh_need_notify_user: %i",
					     err);
				if (err) {
					write(to_guest[1], "", 1);
					notifies++;
				}

				if (!vringh_notify_enable_user(&vrh))
					continue;

				/* Swallow all notifies at once. */
				if (read(to_host[0], buf, sizeof(buf)) < 1)
					break;

				vringh_notify_disable_user(&vrh);
				receives++;
				continue;
			}
			if (err != 1)
				errx(1, "vringh_getdesc_user: %i", err);

			/* We simply copy bytes. */
			if (riov.used) {
				rlen = vringh_iov_pull_user(&riov, rbuf,
							    sizeof(rbuf));
				if (rlen != 4)
					errx(1, "vringh_iov_pull_user: %i",
					     rlen);
				assert(riov.i == riov.used);
				written = 0;
			} else {
				err = vringh_iov_push_user(&wiov, rbuf, rlen);
				if (err != rlen)
					errx(1, "vringh_iov_push_user: %i",
					     err);
				assert(wiov.i == wiov.used);
				written = err;
			}
		complete:
			xfers++;

			err = vringh_complete_user(&vrh, head, written);
			if (err != 0)
				errx(1, "vringh_complete_user: %i", err);
		}

		err = vringh_need_notify_user(&vrh);
		if (err < 0)
			errx(1, "vringh_need_notify_user: %i", err);
		if (err) {
			write(to_guest[1], "", 1);
			notifies++;
		}
		wait(&status);
		if (!WIFEXITED(status))
			errx(1, "Child died with signal %i?", WTERMSIG(status));
		if (WEXITSTATUS(status) != 0)
			errx(1, "Child exited %i?", WEXITSTATUS(status));
		printf("Host: notified %lu, pinged %lu\n", notifies, receives);
		return 0;
	} else {
		struct guest_virtio_device gvdev;
		struct virtqueue *vq;
		unsigned int *data;
		struct vring_desc *indirects;
		unsigned int finished = 0;

		/* We pass sg[]s pointing into here, but we need RINGSIZE+1 */
		data = guest_map + vring_size(RINGSIZE, ALIGN);
		indirects = (void *)data + (RINGSIZE + 1) * 2 * sizeof(int);

		/* We are the guest. */
		munmap(host_map, mapsize);

		close(to_guest[1]);
		close(to_host[0]);

		gvdev.vdev.features = features;
		INIT_LIST_HEAD(&gvdev.vdev.vqs);
		gvdev.to_host_fd = to_host[1];
		gvdev.notifies = 0;

		CPU_SET(first_cpu, &cpu_set);
		if (sched_setaffinity(getpid(), sizeof(cpu_set), &cpu_set))
			err(1, "Could not set affinity to cpu %u", first_cpu);

		vq = vring_new_virtqueue(0, RINGSIZE, ALIGN, &gvdev.vdev, true,
					 false, guest_map,
					 fast_vringh ? no_notify_host
					 : parallel_notify_host,
					 never_callback_guest, "guest vq");

		/* Don't kfree indirects. */
		__kfree_ignore_start = indirects;
		__kfree_ignore_end = indirects + RINGSIZE * 6;

		while (xfers < NUM_XFERS) {
			struct scatterlist sg[4];
			unsigned int num_sg, len;
			int *dbuf, err;
			bool output = !(xfers % 2);

			/* Consume bufs. */
			while ((dbuf = virtqueue_get_buf(vq, &len)) != NULL) {
				if (len == 4)
					assert(*dbuf == finished - 1);
				else if (!fast_vringh)
					assert(*dbuf == finished);
				finished++;
			}

			/* Produce a buffer. */
			dbuf = data + (xfers % (RINGSIZE + 1));

			if (output)
				*dbuf = xfers;
			else
				*dbuf = -1;

			switch ((xfers / sizeof(*dbuf)) % 4) {
			case 0:
				/* Nasty three-element sg list. */
				sg_init_table(sg, num_sg = 3);
				sg_set_buf(&sg[0], (void *)dbuf, 1);
				sg_set_buf(&sg[1], (void *)dbuf + 1, 2);
				sg_set_buf(&sg[2], (void *)dbuf + 3, 1);
				break;
			case 1:
				sg_init_table(sg, num_sg = 2);
				sg_set_buf(&sg[0], (void *)dbuf, 1);
				sg_set_buf(&sg[1], (void *)dbuf + 1, 3);
				break;
			case 2:
				sg_init_table(sg, num_sg = 1);
				sg_set_buf(&sg[0], (void *)dbuf, 4);
				break;
			case 3:
				sg_init_table(sg, num_sg = 4);
				sg_set_buf(&sg[0], (void *)dbuf, 1);
				sg_set_buf(&sg[1], (void *)dbuf + 1, 1);
				sg_set_buf(&sg[2], (void *)dbuf + 2, 1);
				sg_set_buf(&sg[3], (void *)dbuf + 3, 1);
				break;
			}

			/* May allocate an indirect, so force it to allocate
			 * user addr */
			__kmalloc_fake = indirects + (xfers % RINGSIZE) * 4;
			if (output)
				err = virtqueue_add_outbuf(vq, sg, num_sg, dbuf,
							   GFP_KERNEL);
			else
				err = virtqueue_add_inbuf(vq, sg, num_sg,
							  dbuf, GFP_KERNEL);

			if (err == -ENOSPC) {
				if (!virtqueue_enable_cb_delayed(vq))
					continue;
				/* Swallow all notifies at once. */
				if (read(to_guest[0], buf, sizeof(buf)) < 1)
					break;
				
				receives++;
				virtqueue_disable_cb(vq);
				continue;
			}

			if (err)
				errx(1, "virtqueue_add_in/outbuf: %i", err);

			xfers++;
			virtqueue_kick(vq);
		}

		/* Any extra? */
		while (finished != xfers) {
			int *dbuf;
			unsigned int len;

			/* Consume bufs. */
			dbuf = virtqueue_get_buf(vq, &len);
			if (dbuf) {
				if (len == 4)
					assert(*dbuf == finished - 1);
				else
					assert(len == 0);
				finished++;
				continue;
			}

			if (!virtqueue_enable_cb_delayed(vq))
				continue;
			if (read(to_guest[0], buf, sizeof(buf)) < 1)
				break;
				
			receives++;
			virtqueue_disable_cb(vq);
		}

		printf("Guest: notified %lu, pinged %lu\n",
		       gvdev.notifies, receives);
		vring_del_virtqueue(vq);
		return 0;
	}
}

int main(int argc, char *argv[])
{
	struct virtio_device vdev;
	struct virtqueue *vq;
	struct vringh vrh;
	struct scatterlist guest_sg[RINGSIZE], *sgs[2];
	struct iovec host_riov[2], host_wiov[2];
	struct vringh_iov riov, wiov;
	struct vring_used_elem used[RINGSIZE];
	char buf[28];
	u16 head;
	int err;
	unsigned i;
	void *ret;
	bool (*getrange)(struct vringh *vrh, u64 addr, struct vringh_range *r);
	bool fast_vringh = false, parallel = false;

	getrange = getrange_iov;
	vdev.features = 0;
	INIT_LIST_HEAD(&vdev.vqs);

	while (argv[1]) {
		if (strcmp(argv[1], "--indirect") == 0)
			__virtio_set_bit(&vdev, VIRTIO_RING_F_INDIRECT_DESC);
		else if (strcmp(argv[1], "--eventidx") == 0)
			__virtio_set_bit(&vdev, VIRTIO_RING_F_EVENT_IDX);
		else if (strcmp(argv[1], "--virtio-1") == 0)
			__virtio_set_bit(&vdev, VIRTIO_F_VERSION_1);
		else if (strcmp(argv[1], "--slow-range") == 0)
			getrange = getrange_slow;
		else if (strcmp(argv[1], "--fast-vringh") == 0)
			fast_vringh = true;
		else if (strcmp(argv[1], "--parallel") == 0)
			parallel = true;
		else
			errx(1, "Unknown arg %s", argv[1]);
		argv++;
	}

	if (parallel)
		return parallel_test(vdev.features, getrange, fast_vringh);

	if (posix_memalign(&__user_addr_min, PAGE_SIZE, USER_MEM) != 0)
		abort();
	__user_addr_max = __user_addr_min + USER_MEM;
	memset(__user_addr_min, 0, vring_size(RINGSIZE, ALIGN));

	/* Set up guest side. */
	vq = vring_new_virtqueue(0, RINGSIZE, ALIGN, &vdev, true, false,
				 __user_addr_min,
				 never_notify_host, never_callback_guest,
				 "guest vq");

	/* Set up host side. */
	vring_init(&vrh.vring, RINGSIZE, __user_addr_min, ALIGN);
	vringh_init_user(&vrh, vdev.features, RINGSIZE, true,
			 vrh.vring.desc, vrh.vring.avail, vrh.vring.used);

	/* No descriptor to get yet... */
	err = vringh_getdesc_user(&vrh, &riov, &wiov, getrange, &head);
	if (err != 0)
		errx(1, "vringh_getdesc_user: %i", err);

	/* Guest puts in a descriptor. */
	memcpy(__user_addr_max - 1, "a", 1);
	sg_init_table(guest_sg, 1);
	sg_set_buf(&guest_sg[0], __user_addr_max - 1, 1);
	sg_init_table(guest_sg+1, 1);
	sg_set_buf(&guest_sg[1], __user_addr_max - 3, 2);
	sgs[0] = &guest_sg[0];
	sgs[1] = &guest_sg[1];

	/* May allocate an indirect, so force it to allocate user addr */
	__kmalloc_fake = __user_addr_min + vring_size(RINGSIZE, ALIGN);
	err = virtqueue_add_sgs(vq, sgs, 1, 1, &err, GFP_KERNEL);
	if (err)
		errx(1, "virtqueue_add_sgs: %i", err);
	__kmalloc_fake = NULL;

	/* Host retreives it. */
	vringh_iov_init(&riov, host_riov, ARRAY_SIZE(host_riov));
	vringh_iov_init(&wiov, host_wiov, ARRAY_SIZE(host_wiov));

	err = vringh_getdesc_user(&vrh, &riov, &wiov, getrange, &head);
	if (err != 1)
		errx(1, "vringh_getdesc_user: %i", err);

	assert(riov.used == 1);
	assert(riov.iov[0].iov_base == __user_addr_max - 1);
	assert(riov.iov[0].iov_len == 1);
	if (getrange != getrange_slow) {
		assert(wiov.used == 1);
		assert(wiov.iov[0].iov_base == __user_addr_max - 3);
		assert(wiov.iov[0].iov_len == 2);
	} else {
		assert(wiov.used == 2);
		assert(wiov.iov[0].iov_base == __user_addr_max - 3);
		assert(wiov.iov[0].iov_len == 1);
		assert(wiov.iov[1].iov_base == __user_addr_max - 2);
		assert(wiov.iov[1].iov_len == 1);
	}

	err = vringh_iov_pull_user(&riov, buf, 5);
	if (err != 1)
		errx(1, "vringh_iov_pull_user: %i", err);
	assert(buf[0] == 'a');
	assert(riov.i == 1);
	assert(vringh_iov_pull_user(&riov, buf, 5) == 0);

	memcpy(buf, "bcdef", 5);
	err = vringh_iov_push_user(&wiov, buf, 5);
	if (err != 2)
		errx(1, "vringh_iov_push_user: %i", err);
	assert(memcmp(__user_addr_max - 3, "bc", 2) == 0);
	assert(wiov.i == wiov.used);
	assert(vringh_iov_push_user(&wiov, buf, 5) == 0);

	/* Host is done. */
	err = vringh_complete_user(&vrh, head, err);
	if (err != 0)
		errx(1, "vringh_complete_user: %i", err);

	/* Guest should see used token now. */
	__kfree_ignore_start = __user_addr_min + vring_size(RINGSIZE, ALIGN);
	__kfree_ignore_end = __kfree_ignore_start + 1;
	ret = virtqueue_get_buf(vq, &i);
	if (ret != &err)
		errx(1, "virtqueue_get_buf: %p", ret);
	assert(i == 2);

	/* Guest puts in a huge descriptor. */
	sg_init_table(guest_sg, RINGSIZE);
	for (i = 0; i < RINGSIZE; i++) {
		sg_set_buf(&guest_sg[i],
			   __user_addr_max - USER_MEM/4, USER_MEM/4);
	}

	/* Fill contents with recognisable garbage. */
	for (i = 0; i < USER_MEM/4; i++)
		((char *)__user_addr_max - USER_MEM/4)[i] = i;

	/* This will allocate an indirect, so force it to allocate user addr */
	__kmalloc_fake = __user_addr_min + vring_size(RINGSIZE, ALIGN);
	err = virtqueue_add_outbuf(vq, guest_sg, RINGSIZE, &err, GFP_KERNEL);
	if (err)
		errx(1, "virtqueue_add_outbuf (large): %i", err);
	__kmalloc_fake = NULL;

	/* Host picks it up (allocates new iov). */
	vringh_iov_init(&riov, host_riov, ARRAY_SIZE(host_riov));
	vringh_iov_init(&wiov, host_wiov, ARRAY_SIZE(host_wiov));

	err = vringh_getdesc_user(&vrh, &riov, &wiov, getrange, &head);
	if (err != 1)
		errx(1, "vringh_getdesc_user: %i", err);

	assert(riov.max_num & VRINGH_IOV_ALLOCATED);
	assert(riov.iov != host_riov);
	if (getrange != getrange_slow)
		assert(riov.used == RINGSIZE);
	else
		assert(riov.used == RINGSIZE * USER_MEM/4);

	assert(!(wiov.max_num & VRINGH_IOV_ALLOCATED));
	assert(wiov.used == 0);

	/* Pull data back out (in odd chunks), should be as expected. */
	for (i = 0; i < RINGSIZE * USER_MEM/4; i += 3) {
		err = vringh_iov_pull_user(&riov, buf, 3);
		if (err != 3 && i + err != RINGSIZE * USER_MEM/4)
			errx(1, "vringh_iov_pull_user large: %i", err);
		assert(buf[0] == (char)i);
		assert(err < 2 || buf[1] == (char)(i + 1));
		assert(err < 3 || buf[2] == (char)(i + 2));
	}
	assert(riov.i == riov.used);
	vringh_iov_cleanup(&riov);
	vringh_iov_cleanup(&wiov);

	/* Complete using multi interface, just because we can. */
	used[0].id = head;
	used[0].len = 0;
	err = vringh_complete_multi_user(&vrh, used, 1);
	if (err)
		errx(1, "vringh_complete_multi_user(1): %i", err);

	/* Free up those descriptors. */
	ret = virtqueue_get_buf(vq, &i);
	if (ret != &err)
		errx(1, "virtqueue_get_buf: %p", ret);

	/* Add lots of descriptors. */
	sg_init_table(guest_sg, 1);
	sg_set_buf(&guest_sg[0], __user_addr_max - 1, 1);
	for (i = 0; i < RINGSIZE; i++) {
		err = virtqueue_add_outbuf(vq, guest_sg, 1, &err, GFP_KERNEL);
		if (err)
			errx(1, "virtqueue_add_outbuf (multiple): %i", err);
	}

	/* Now get many, and consume them all at once. */
	vringh_iov_init(&riov, host_riov, ARRAY_SIZE(host_riov));
	vringh_iov_init(&wiov, host_wiov, ARRAY_SIZE(host_wiov));

	for (i = 0; i < RINGSIZE; i++) {
		err = vringh_getdesc_user(&vrh, &riov, &wiov, getrange, &head);
		if (err != 1)
			errx(1, "vringh_getdesc_user: %i", err);
		used[i].id = head;
		used[i].len = 0;
	}
	/* Make sure it wraps around ring, to test! */
	assert(vrh.vring.used->idx % RINGSIZE != 0);
	err = vringh_complete_multi_user(&vrh, used, RINGSIZE);
	if (err)
		errx(1, "vringh_complete_multi_user: %i", err);

	/* Free those buffers. */
	for (i = 0; i < RINGSIZE; i++) {
		unsigned len;
		assert(virtqueue_get_buf(vq, &len) != NULL);
	}

	/* Test weird (but legal!) indirect. */
	if (__virtio_test_bit(&vdev, VIRTIO_RING_F_INDIRECT_DESC)) {
		char *data = __user_addr_max - USER_MEM/4;
		struct vring_desc *d = __user_addr_max - USER_MEM/2;
		struct vring vring;

		/* Force creation of direct, which we modify. */
		__virtio_clear_bit(&vdev, VIRTIO_RING_F_INDIRECT_DESC);
		vq = vring_new_virtqueue(0, RINGSIZE, ALIGN, &vdev, true,
					 false, __user_addr_min,
					 never_notify_host,
					 never_callback_guest,
					 "guest vq");

		sg_init_table(guest_sg, 4);
		sg_set_buf(&guest_sg[0], d, sizeof(*d)*2);
		sg_set_buf(&guest_sg[1], d + 2, sizeof(*d)*1);
		sg_set_buf(&guest_sg[2], data + 6, 4);
		sg_set_buf(&guest_sg[3], d + 3, sizeof(*d)*3);

		err = virtqueue_add_outbuf(vq, guest_sg, 4, &err, GFP_KERNEL);
		if (err)
			errx(1, "virtqueue_add_outbuf (indirect): %i", err);

		vring_init(&vring, RINGSIZE, __user_addr_min, ALIGN);

		/* They're used in order, but double-check... */
		assert(vring.desc[0].addr == (unsigned long)d);
		assert(vring.desc[1].addr == (unsigned long)(d+2));
		assert(vring.desc[2].addr == (unsigned long)data + 6);
		assert(vring.desc[3].addr == (unsigned long)(d+3));
		vring.desc[0].flags |= VRING_DESC_F_INDIRECT;
		vring.desc[1].flags |= VRING_DESC_F_INDIRECT;
		vring.desc[3].flags |= VRING_DESC_F_INDIRECT;

		/* First indirect */
		d[0].addr = (unsigned long)data;
		d[0].len = 1;
		d[0].flags = VRING_DESC_F_NEXT;
		d[0].next = 1;
		d[1].addr = (unsigned long)data + 1;
		d[1].len = 2;
		d[1].flags = 0;

		/* Second indirect */
		d[2].addr = (unsigned long)data + 3;
		d[2].len = 3;
		d[2].flags = 0;

		/* Third indirect */
		d[3].addr = (unsigned long)data + 10;
		d[3].len = 5;
		d[3].flags = VRING_DESC_F_NEXT;
		d[3].next = 1;
		d[4].addr = (unsigned long)data + 15;
		d[4].len = 6;
		d[4].flags = VRING_DESC_F_NEXT;
		d[4].next = 2;
		d[5].addr = (unsigned long)data + 21;
		d[5].len = 7;
		d[5].flags = 0;

		/* Host picks it up (allocates new iov). */
		vringh_iov_init(&riov, host_riov, ARRAY_SIZE(host_riov));
		vringh_iov_init(&wiov, host_wiov, ARRAY_SIZE(host_wiov));

		err = vringh_getdesc_user(&vrh, &riov, &wiov, getrange, &head);
		if (err != 1)
			errx(1, "vringh_getdesc_user: %i", err);

		if (head != 0)
			errx(1, "vringh_getdesc_user: head %i not 0", head);

		assert(riov.max_num & VRINGH_IOV_ALLOCATED);
		if (getrange != getrange_slow)
			assert(riov.used == 7);
		else
			assert(riov.used == 28);
		err = vringh_iov_pull_user(&riov, buf, 29);
		assert(err == 28);

		/* Data should be linear. */
		for (i = 0; i < err; i++)
			assert(buf[i] == i);
		vringh_iov_cleanup(&riov);
	}

	/* Don't leak memory... */
	vring_del_virtqueue(vq);
	free(__user_addr_min);

	return 0;
}
