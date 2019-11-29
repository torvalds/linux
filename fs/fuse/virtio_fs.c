// SPDX-License-Identifier: GPL-2.0
/*
 * virtio-fs: Virtio Filesystem
 * Copyright (C) 2018 Red Hat, Inc.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_fs.h>
#include <linux/delay.h>
#include <linux/fs_context.h>
#include <linux/highmem.h>
#include "fuse_i.h"

/* List of virtio-fs device instances and a lock for the list. Also provides
 * mutual exclusion in device removal and mounting path
 */
static DEFINE_MUTEX(virtio_fs_mutex);
static LIST_HEAD(virtio_fs_instances);

enum {
	VQ_HIPRIO,
	VQ_REQUEST
};

/* Per-virtqueue state */
struct virtio_fs_vq {
	spinlock_t lock;
	struct virtqueue *vq;     /* protected by ->lock */
	struct work_struct done_work;
	struct list_head queued_reqs;
	struct list_head end_reqs;	/* End these requests */
	struct delayed_work dispatch_work;
	struct fuse_dev *fud;
	bool connected;
	long in_flight;
	char name[24];
} ____cacheline_aligned_in_smp;

/* A virtio-fs device instance */
struct virtio_fs {
	struct kref refcount;
	struct list_head list;    /* on virtio_fs_instances */
	char *tag;
	struct virtio_fs_vq *vqs;
	unsigned int nvqs;               /* number of virtqueues */
	unsigned int num_request_queues; /* number of request queues */
};

struct virtio_fs_forget {
	struct fuse_in_header ih;
	struct fuse_forget_in arg;
	/* This request can be temporarily queued on virt queue */
	struct list_head list;
};

static int virtio_fs_enqueue_req(struct virtio_fs_vq *fsvq,
				 struct fuse_req *req, bool in_flight);

static inline struct virtio_fs_vq *vq_to_fsvq(struct virtqueue *vq)
{
	struct virtio_fs *fs = vq->vdev->priv;

	return &fs->vqs[vq->index];
}

static inline struct fuse_pqueue *vq_to_fpq(struct virtqueue *vq)
{
	return &vq_to_fsvq(vq)->fud->pq;
}

/* Should be called with fsvq->lock held. */
static inline void inc_in_flight_req(struct virtio_fs_vq *fsvq)
{
	fsvq->in_flight++;
}

/* Should be called with fsvq->lock held. */
static inline void dec_in_flight_req(struct virtio_fs_vq *fsvq)
{
	WARN_ON(fsvq->in_flight <= 0);
	fsvq->in_flight--;
}

static void release_virtio_fs_obj(struct kref *ref)
{
	struct virtio_fs *vfs = container_of(ref, struct virtio_fs, refcount);

	kfree(vfs->vqs);
	kfree(vfs);
}

/* Make sure virtiofs_mutex is held */
static void virtio_fs_put(struct virtio_fs *fs)
{
	kref_put(&fs->refcount, release_virtio_fs_obj);
}

static void virtio_fs_fiq_release(struct fuse_iqueue *fiq)
{
	struct virtio_fs *vfs = fiq->priv;

	mutex_lock(&virtio_fs_mutex);
	virtio_fs_put(vfs);
	mutex_unlock(&virtio_fs_mutex);
}

static void virtio_fs_drain_queue(struct virtio_fs_vq *fsvq)
{
	WARN_ON(fsvq->in_flight < 0);

	/* Wait for in flight requests to finish.*/
	while (1) {
		spin_lock(&fsvq->lock);
		if (!fsvq->in_flight) {
			spin_unlock(&fsvq->lock);
			break;
		}
		spin_unlock(&fsvq->lock);
		/* TODO use completion instead of timeout */
		usleep_range(1000, 2000);
	}

	flush_work(&fsvq->done_work);
	flush_delayed_work(&fsvq->dispatch_work);
}

static void virtio_fs_drain_all_queues(struct virtio_fs *fs)
{
	struct virtio_fs_vq *fsvq;
	int i;

	for (i = 0; i < fs->nvqs; i++) {
		fsvq = &fs->vqs[i];
		virtio_fs_drain_queue(fsvq);
	}
}

static void virtio_fs_start_all_queues(struct virtio_fs *fs)
{
	struct virtio_fs_vq *fsvq;
	int i;

	for (i = 0; i < fs->nvqs; i++) {
		fsvq = &fs->vqs[i];
		spin_lock(&fsvq->lock);
		fsvq->connected = true;
		spin_unlock(&fsvq->lock);
	}
}

/* Add a new instance to the list or return -EEXIST if tag name exists*/
static int virtio_fs_add_instance(struct virtio_fs *fs)
{
	struct virtio_fs *fs2;
	bool duplicate = false;

	mutex_lock(&virtio_fs_mutex);

	list_for_each_entry(fs2, &virtio_fs_instances, list) {
		if (strcmp(fs->tag, fs2->tag) == 0)
			duplicate = true;
	}

	if (!duplicate)
		list_add_tail(&fs->list, &virtio_fs_instances);

	mutex_unlock(&virtio_fs_mutex);

	if (duplicate)
		return -EEXIST;
	return 0;
}

/* Return the virtio_fs with a given tag, or NULL */
static struct virtio_fs *virtio_fs_find_instance(const char *tag)
{
	struct virtio_fs *fs;

	mutex_lock(&virtio_fs_mutex);

	list_for_each_entry(fs, &virtio_fs_instances, list) {
		if (strcmp(fs->tag, tag) == 0) {
			kref_get(&fs->refcount);
			goto found;
		}
	}

	fs = NULL; /* not found */

found:
	mutex_unlock(&virtio_fs_mutex);

	return fs;
}

static void virtio_fs_free_devs(struct virtio_fs *fs)
{
	unsigned int i;

	for (i = 0; i < fs->nvqs; i++) {
		struct virtio_fs_vq *fsvq = &fs->vqs[i];

		if (!fsvq->fud)
			continue;

		fuse_dev_free(fsvq->fud);
		fsvq->fud = NULL;
	}
}

/* Read filesystem name from virtio config into fs->tag (must kfree()). */
static int virtio_fs_read_tag(struct virtio_device *vdev, struct virtio_fs *fs)
{
	char tag_buf[sizeof_field(struct virtio_fs_config, tag)];
	char *end;
	size_t len;

	virtio_cread_bytes(vdev, offsetof(struct virtio_fs_config, tag),
			   &tag_buf, sizeof(tag_buf));
	end = memchr(tag_buf, '\0', sizeof(tag_buf));
	if (end == tag_buf)
		return -EINVAL; /* empty tag */
	if (!end)
		end = &tag_buf[sizeof(tag_buf)];

	len = end - tag_buf;
	fs->tag = devm_kmalloc(&vdev->dev, len + 1, GFP_KERNEL);
	if (!fs->tag)
		return -ENOMEM;
	memcpy(fs->tag, tag_buf, len);
	fs->tag[len] = '\0';
	return 0;
}

/* Work function for hiprio completion */
static void virtio_fs_hiprio_done_work(struct work_struct *work)
{
	struct virtio_fs_vq *fsvq = container_of(work, struct virtio_fs_vq,
						 done_work);
	struct virtqueue *vq = fsvq->vq;

	/* Free completed FUSE_FORGET requests */
	spin_lock(&fsvq->lock);
	do {
		unsigned int len;
		void *req;

		virtqueue_disable_cb(vq);

		while ((req = virtqueue_get_buf(vq, &len)) != NULL) {
			kfree(req);
			dec_in_flight_req(fsvq);
		}
	} while (!virtqueue_enable_cb(vq) && likely(!virtqueue_is_broken(vq)));
	spin_unlock(&fsvq->lock);
}

static void virtio_fs_request_dispatch_work(struct work_struct *work)
{
	struct fuse_req *req;
	struct virtio_fs_vq *fsvq = container_of(work, struct virtio_fs_vq,
						 dispatch_work.work);
	struct fuse_conn *fc = fsvq->fud->fc;
	int ret;

	pr_debug("virtio-fs: worker %s called.\n", __func__);
	while (1) {
		spin_lock(&fsvq->lock);
		req = list_first_entry_or_null(&fsvq->end_reqs, struct fuse_req,
					       list);
		if (!req) {
			spin_unlock(&fsvq->lock);
			break;
		}

		list_del_init(&req->list);
		spin_unlock(&fsvq->lock);
		fuse_request_end(fc, req);
	}

	/* Dispatch pending requests */
	while (1) {
		spin_lock(&fsvq->lock);
		req = list_first_entry_or_null(&fsvq->queued_reqs,
					       struct fuse_req, list);
		if (!req) {
			spin_unlock(&fsvq->lock);
			return;
		}
		list_del_init(&req->list);
		spin_unlock(&fsvq->lock);

		ret = virtio_fs_enqueue_req(fsvq, req, true);
		if (ret < 0) {
			if (ret == -ENOMEM || ret == -ENOSPC) {
				spin_lock(&fsvq->lock);
				list_add_tail(&req->list, &fsvq->queued_reqs);
				schedule_delayed_work(&fsvq->dispatch_work,
						      msecs_to_jiffies(1));
				spin_unlock(&fsvq->lock);
				return;
			}
			req->out.h.error = ret;
			spin_lock(&fsvq->lock);
			dec_in_flight_req(fsvq);
			spin_unlock(&fsvq->lock);
			pr_err("virtio-fs: virtio_fs_enqueue_req() failed %d\n",
			       ret);
			fuse_request_end(fc, req);
		}
	}
}

static void virtio_fs_hiprio_dispatch_work(struct work_struct *work)
{
	struct virtio_fs_forget *forget;
	struct virtio_fs_vq *fsvq = container_of(work, struct virtio_fs_vq,
						 dispatch_work.work);
	struct virtqueue *vq = fsvq->vq;
	struct scatterlist sg;
	struct scatterlist *sgs[] = {&sg};
	bool notify;
	int ret;

	pr_debug("virtio-fs: worker %s called.\n", __func__);
	while (1) {
		spin_lock(&fsvq->lock);
		forget = list_first_entry_or_null(&fsvq->queued_reqs,
					struct virtio_fs_forget, list);
		if (!forget) {
			spin_unlock(&fsvq->lock);
			return;
		}

		list_del(&forget->list);
		if (!fsvq->connected) {
			dec_in_flight_req(fsvq);
			spin_unlock(&fsvq->lock);
			kfree(forget);
			continue;
		}

		sg_init_one(&sg, forget, sizeof(*forget));

		/* Enqueue the request */
		dev_dbg(&vq->vdev->dev, "%s\n", __func__);
		ret = virtqueue_add_sgs(vq, sgs, 1, 0, forget, GFP_ATOMIC);
		if (ret < 0) {
			if (ret == -ENOMEM || ret == -ENOSPC) {
				pr_debug("virtio-fs: Could not queue FORGET: err=%d. Will try later\n",
					 ret);
				list_add_tail(&forget->list,
						&fsvq->queued_reqs);
				schedule_delayed_work(&fsvq->dispatch_work,
						msecs_to_jiffies(1));
			} else {
				pr_debug("virtio-fs: Could not queue FORGET: err=%d. Dropping it.\n",
					 ret);
				dec_in_flight_req(fsvq);
				kfree(forget);
			}
			spin_unlock(&fsvq->lock);
			return;
		}

		notify = virtqueue_kick_prepare(vq);
		spin_unlock(&fsvq->lock);

		if (notify)
			virtqueue_notify(vq);
		pr_debug("virtio-fs: worker %s dispatched one forget request.\n",
			 __func__);
	}
}

/* Allocate and copy args into req->argbuf */
static int copy_args_to_argbuf(struct fuse_req *req)
{
	struct fuse_args *args = req->args;
	unsigned int offset = 0;
	unsigned int num_in;
	unsigned int num_out;
	unsigned int len;
	unsigned int i;

	num_in = args->in_numargs - args->in_pages;
	num_out = args->out_numargs - args->out_pages;
	len = fuse_len_args(num_in, (struct fuse_arg *) args->in_args) +
	      fuse_len_args(num_out, args->out_args);

	req->argbuf = kmalloc(len, GFP_ATOMIC);
	if (!req->argbuf)
		return -ENOMEM;

	for (i = 0; i < num_in; i++) {
		memcpy(req->argbuf + offset,
		       args->in_args[i].value,
		       args->in_args[i].size);
		offset += args->in_args[i].size;
	}

	return 0;
}

/* Copy args out of and free req->argbuf */
static void copy_args_from_argbuf(struct fuse_args *args, struct fuse_req *req)
{
	unsigned int remaining;
	unsigned int offset;
	unsigned int num_in;
	unsigned int num_out;
	unsigned int i;

	remaining = req->out.h.len - sizeof(req->out.h);
	num_in = args->in_numargs - args->in_pages;
	num_out = args->out_numargs - args->out_pages;
	offset = fuse_len_args(num_in, (struct fuse_arg *)args->in_args);

	for (i = 0; i < num_out; i++) {
		unsigned int argsize = args->out_args[i].size;

		if (args->out_argvar &&
		    i == args->out_numargs - 1 &&
		    argsize > remaining) {
			argsize = remaining;
		}

		memcpy(args->out_args[i].value, req->argbuf + offset, argsize);
		offset += argsize;

		if (i != args->out_numargs - 1)
			remaining -= argsize;
	}

	/* Store the actual size of the variable-length arg */
	if (args->out_argvar)
		args->out_args[args->out_numargs - 1].size = remaining;

	kfree(req->argbuf);
	req->argbuf = NULL;
}

/* Work function for request completion */
static void virtio_fs_requests_done_work(struct work_struct *work)
{
	struct virtio_fs_vq *fsvq = container_of(work, struct virtio_fs_vq,
						 done_work);
	struct fuse_pqueue *fpq = &fsvq->fud->pq;
	struct fuse_conn *fc = fsvq->fud->fc;
	struct virtqueue *vq = fsvq->vq;
	struct fuse_req *req;
	struct fuse_args_pages *ap;
	struct fuse_req *next;
	struct fuse_args *args;
	unsigned int len, i, thislen;
	struct page *page;
	LIST_HEAD(reqs);

	/* Collect completed requests off the virtqueue */
	spin_lock(&fsvq->lock);
	do {
		virtqueue_disable_cb(vq);

		while ((req = virtqueue_get_buf(vq, &len)) != NULL) {
			spin_lock(&fpq->lock);
			list_move_tail(&req->list, &reqs);
			spin_unlock(&fpq->lock);
		}
	} while (!virtqueue_enable_cb(vq) && likely(!virtqueue_is_broken(vq)));
	spin_unlock(&fsvq->lock);

	/* End requests */
	list_for_each_entry_safe(req, next, &reqs, list) {
		/*
		 * TODO verify that server properly follows FUSE protocol
		 * (oh.uniq, oh.len)
		 */
		args = req->args;
		copy_args_from_argbuf(args, req);

		if (args->out_pages && args->page_zeroing) {
			len = args->out_args[args->out_numargs - 1].size;
			ap = container_of(args, typeof(*ap), args);
			for (i = 0; i < ap->num_pages; i++) {
				thislen = ap->descs[i].length;
				if (len < thislen) {
					WARN_ON(ap->descs[i].offset);
					page = ap->pages[i];
					zero_user_segment(page, len, thislen);
					len = 0;
				} else {
					len -= thislen;
				}
			}
		}

		spin_lock(&fpq->lock);
		clear_bit(FR_SENT, &req->flags);
		list_del_init(&req->list);
		spin_unlock(&fpq->lock);

		fuse_request_end(fc, req);
		spin_lock(&fsvq->lock);
		dec_in_flight_req(fsvq);
		spin_unlock(&fsvq->lock);
	}
}

/* Virtqueue interrupt handler */
static void virtio_fs_vq_done(struct virtqueue *vq)
{
	struct virtio_fs_vq *fsvq = vq_to_fsvq(vq);

	dev_dbg(&vq->vdev->dev, "%s %s\n", __func__, fsvq->name);

	schedule_work(&fsvq->done_work);
}

/* Initialize virtqueues */
static int virtio_fs_setup_vqs(struct virtio_device *vdev,
			       struct virtio_fs *fs)
{
	struct virtqueue **vqs;
	vq_callback_t **callbacks;
	const char **names;
	unsigned int i;
	int ret = 0;

	virtio_cread(vdev, struct virtio_fs_config, num_request_queues,
		     &fs->num_request_queues);
	if (fs->num_request_queues == 0)
		return -EINVAL;

	fs->nvqs = 1 + fs->num_request_queues;
	fs->vqs = kcalloc(fs->nvqs, sizeof(fs->vqs[VQ_HIPRIO]), GFP_KERNEL);
	if (!fs->vqs)
		return -ENOMEM;

	vqs = kmalloc_array(fs->nvqs, sizeof(vqs[VQ_HIPRIO]), GFP_KERNEL);
	callbacks = kmalloc_array(fs->nvqs, sizeof(callbacks[VQ_HIPRIO]),
					GFP_KERNEL);
	names = kmalloc_array(fs->nvqs, sizeof(names[VQ_HIPRIO]), GFP_KERNEL);
	if (!vqs || !callbacks || !names) {
		ret = -ENOMEM;
		goto out;
	}

	callbacks[VQ_HIPRIO] = virtio_fs_vq_done;
	snprintf(fs->vqs[VQ_HIPRIO].name, sizeof(fs->vqs[VQ_HIPRIO].name),
			"hiprio");
	names[VQ_HIPRIO] = fs->vqs[VQ_HIPRIO].name;
	INIT_WORK(&fs->vqs[VQ_HIPRIO].done_work, virtio_fs_hiprio_done_work);
	INIT_LIST_HEAD(&fs->vqs[VQ_HIPRIO].queued_reqs);
	INIT_LIST_HEAD(&fs->vqs[VQ_HIPRIO].end_reqs);
	INIT_DELAYED_WORK(&fs->vqs[VQ_HIPRIO].dispatch_work,
			virtio_fs_hiprio_dispatch_work);
	spin_lock_init(&fs->vqs[VQ_HIPRIO].lock);

	/* Initialize the requests virtqueues */
	for (i = VQ_REQUEST; i < fs->nvqs; i++) {
		spin_lock_init(&fs->vqs[i].lock);
		INIT_WORK(&fs->vqs[i].done_work, virtio_fs_requests_done_work);
		INIT_DELAYED_WORK(&fs->vqs[i].dispatch_work,
				  virtio_fs_request_dispatch_work);
		INIT_LIST_HEAD(&fs->vqs[i].queued_reqs);
		INIT_LIST_HEAD(&fs->vqs[i].end_reqs);
		snprintf(fs->vqs[i].name, sizeof(fs->vqs[i].name),
			 "requests.%u", i - VQ_REQUEST);
		callbacks[i] = virtio_fs_vq_done;
		names[i] = fs->vqs[i].name;
	}

	ret = virtio_find_vqs(vdev, fs->nvqs, vqs, callbacks, names, NULL);
	if (ret < 0)
		goto out;

	for (i = 0; i < fs->nvqs; i++)
		fs->vqs[i].vq = vqs[i];

	virtio_fs_start_all_queues(fs);
out:
	kfree(names);
	kfree(callbacks);
	kfree(vqs);
	if (ret)
		kfree(fs->vqs);
	return ret;
}

/* Free virtqueues (device must already be reset) */
static void virtio_fs_cleanup_vqs(struct virtio_device *vdev,
				  struct virtio_fs *fs)
{
	vdev->config->del_vqs(vdev);
}

static int virtio_fs_probe(struct virtio_device *vdev)
{
	struct virtio_fs *fs;
	int ret;

	fs = kzalloc(sizeof(*fs), GFP_KERNEL);
	if (!fs)
		return -ENOMEM;
	kref_init(&fs->refcount);
	vdev->priv = fs;

	ret = virtio_fs_read_tag(vdev, fs);
	if (ret < 0)
		goto out;

	ret = virtio_fs_setup_vqs(vdev, fs);
	if (ret < 0)
		goto out;

	/* TODO vq affinity */

	/* Bring the device online in case the filesystem is mounted and
	 * requests need to be sent before we return.
	 */
	virtio_device_ready(vdev);

	ret = virtio_fs_add_instance(fs);
	if (ret < 0)
		goto out_vqs;

	return 0;

out_vqs:
	vdev->config->reset(vdev);
	virtio_fs_cleanup_vqs(vdev, fs);

out:
	vdev->priv = NULL;
	kfree(fs);
	return ret;
}

static void virtio_fs_stop_all_queues(struct virtio_fs *fs)
{
	struct virtio_fs_vq *fsvq;
	int i;

	for (i = 0; i < fs->nvqs; i++) {
		fsvq = &fs->vqs[i];
		spin_lock(&fsvq->lock);
		fsvq->connected = false;
		spin_unlock(&fsvq->lock);
	}
}

static void virtio_fs_remove(struct virtio_device *vdev)
{
	struct virtio_fs *fs = vdev->priv;

	mutex_lock(&virtio_fs_mutex);
	/* This device is going away. No one should get new reference */
	list_del_init(&fs->list);
	virtio_fs_stop_all_queues(fs);
	virtio_fs_drain_all_queues(fs);
	vdev->config->reset(vdev);
	virtio_fs_cleanup_vqs(vdev, fs);

	vdev->priv = NULL;
	/* Put device reference on virtio_fs object */
	virtio_fs_put(fs);
	mutex_unlock(&virtio_fs_mutex);
}

#ifdef CONFIG_PM_SLEEP
static int virtio_fs_freeze(struct virtio_device *vdev)
{
	/* TODO need to save state here */
	pr_warn("virtio-fs: suspend/resume not yet supported\n");
	return -EOPNOTSUPP;
}

static int virtio_fs_restore(struct virtio_device *vdev)
{
	 /* TODO need to restore state here */
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

const static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_FS, VIRTIO_DEV_ANY_ID },
	{},
};

const static unsigned int feature_table[] = {};

static struct virtio_driver virtio_fs_driver = {
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= id_table,
	.feature_table		= feature_table,
	.feature_table_size	= ARRAY_SIZE(feature_table),
	.probe			= virtio_fs_probe,
	.remove			= virtio_fs_remove,
#ifdef CONFIG_PM_SLEEP
	.freeze			= virtio_fs_freeze,
	.restore		= virtio_fs_restore,
#endif
};

static void virtio_fs_wake_forget_and_unlock(struct fuse_iqueue *fiq)
__releases(fiq->lock)
{
	struct fuse_forget_link *link;
	struct virtio_fs_forget *forget;
	struct scatterlist sg;
	struct scatterlist *sgs[] = {&sg};
	struct virtio_fs *fs;
	struct virtqueue *vq;
	struct virtio_fs_vq *fsvq;
	bool notify;
	u64 unique;
	int ret;

	link = fuse_dequeue_forget(fiq, 1, NULL);
	unique = fuse_get_unique(fiq);

	fs = fiq->priv;
	fsvq = &fs->vqs[VQ_HIPRIO];
	spin_unlock(&fiq->lock);

	/* Allocate a buffer for the request */
	forget = kmalloc(sizeof(*forget), GFP_NOFS | __GFP_NOFAIL);

	forget->ih = (struct fuse_in_header){
		.opcode = FUSE_FORGET,
		.nodeid = link->forget_one.nodeid,
		.unique = unique,
		.len = sizeof(*forget),
	};
	forget->arg = (struct fuse_forget_in){
		.nlookup = link->forget_one.nlookup,
	};

	sg_init_one(&sg, forget, sizeof(*forget));

	/* Enqueue the request */
	spin_lock(&fsvq->lock);

	if (!fsvq->connected) {
		kfree(forget);
		spin_unlock(&fsvq->lock);
		goto out;
	}

	vq = fsvq->vq;
	dev_dbg(&vq->vdev->dev, "%s\n", __func__);

	ret = virtqueue_add_sgs(vq, sgs, 1, 0, forget, GFP_ATOMIC);
	if (ret < 0) {
		if (ret == -ENOMEM || ret == -ENOSPC) {
			pr_debug("virtio-fs: Could not queue FORGET: err=%d. Will try later.\n",
				 ret);
			list_add_tail(&forget->list, &fsvq->queued_reqs);
			schedule_delayed_work(&fsvq->dispatch_work,
					msecs_to_jiffies(1));
			inc_in_flight_req(fsvq);
		} else {
			pr_debug("virtio-fs: Could not queue FORGET: err=%d. Dropping it.\n",
				 ret);
			kfree(forget);
		}
		spin_unlock(&fsvq->lock);
		goto out;
	}

	inc_in_flight_req(fsvq);
	notify = virtqueue_kick_prepare(vq);

	spin_unlock(&fsvq->lock);

	if (notify)
		virtqueue_notify(vq);
out:
	kfree(link);
}

static void virtio_fs_wake_interrupt_and_unlock(struct fuse_iqueue *fiq)
__releases(fiq->lock)
{
	/*
	 * TODO interrupts.
	 *
	 * Normal fs operations on a local filesystems aren't interruptible.
	 * Exceptions are blocking lock operations; for example fcntl(F_SETLKW)
	 * with shared lock between host and guest.
	 */
	spin_unlock(&fiq->lock);
}

/* Return the number of scatter-gather list elements required */
static unsigned int sg_count_fuse_req(struct fuse_req *req)
{
	struct fuse_args *args = req->args;
	struct fuse_args_pages *ap = container_of(args, typeof(*ap), args);
	unsigned int total_sgs = 1 /* fuse_in_header */;

	if (args->in_numargs - args->in_pages)
		total_sgs += 1;

	if (args->in_pages)
		total_sgs += ap->num_pages;

	if (!test_bit(FR_ISREPLY, &req->flags))
		return total_sgs;

	total_sgs += 1 /* fuse_out_header */;

	if (args->out_numargs - args->out_pages)
		total_sgs += 1;

	if (args->out_pages)
		total_sgs += ap->num_pages;

	return total_sgs;
}

/* Add pages to scatter-gather list and return number of elements used */
static unsigned int sg_init_fuse_pages(struct scatterlist *sg,
				       struct page **pages,
				       struct fuse_page_desc *page_descs,
				       unsigned int num_pages,
				       unsigned int total_len)
{
	unsigned int i;
	unsigned int this_len;

	for (i = 0; i < num_pages && total_len; i++) {
		sg_init_table(&sg[i], 1);
		this_len =  min(page_descs[i].length, total_len);
		sg_set_page(&sg[i], pages[i], this_len, page_descs[i].offset);
		total_len -= this_len;
	}

	return i;
}

/* Add args to scatter-gather list and return number of elements used */
static unsigned int sg_init_fuse_args(struct scatterlist *sg,
				      struct fuse_req *req,
				      struct fuse_arg *args,
				      unsigned int numargs,
				      bool argpages,
				      void *argbuf,
				      unsigned int *len_used)
{
	struct fuse_args_pages *ap = container_of(req->args, typeof(*ap), args);
	unsigned int total_sgs = 0;
	unsigned int len;

	len = fuse_len_args(numargs - argpages, args);
	if (len)
		sg_init_one(&sg[total_sgs++], argbuf, len);

	if (argpages)
		total_sgs += sg_init_fuse_pages(&sg[total_sgs],
						ap->pages, ap->descs,
						ap->num_pages,
						args[numargs - 1].size);

	if (len_used)
		*len_used = len;

	return total_sgs;
}

/* Add a request to a virtqueue and kick the device */
static int virtio_fs_enqueue_req(struct virtio_fs_vq *fsvq,
				 struct fuse_req *req, bool in_flight)
{
	/* requests need at least 4 elements */
	struct scatterlist *stack_sgs[6];
	struct scatterlist stack_sg[ARRAY_SIZE(stack_sgs)];
	struct scatterlist **sgs = stack_sgs;
	struct scatterlist *sg = stack_sg;
	struct virtqueue *vq;
	struct fuse_args *args = req->args;
	unsigned int argbuf_used = 0;
	unsigned int out_sgs = 0;
	unsigned int in_sgs = 0;
	unsigned int total_sgs;
	unsigned int i;
	int ret;
	bool notify;
	struct fuse_pqueue *fpq;

	/* Does the sglist fit on the stack? */
	total_sgs = sg_count_fuse_req(req);
	if (total_sgs > ARRAY_SIZE(stack_sgs)) {
		sgs = kmalloc_array(total_sgs, sizeof(sgs[0]), GFP_ATOMIC);
		sg = kmalloc_array(total_sgs, sizeof(sg[0]), GFP_ATOMIC);
		if (!sgs || !sg) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/* Use a bounce buffer since stack args cannot be mapped */
	ret = copy_args_to_argbuf(req);
	if (ret < 0)
		goto out;

	/* Request elements */
	sg_init_one(&sg[out_sgs++], &req->in.h, sizeof(req->in.h));
	out_sgs += sg_init_fuse_args(&sg[out_sgs], req,
				     (struct fuse_arg *)args->in_args,
				     args->in_numargs, args->in_pages,
				     req->argbuf, &argbuf_used);

	/* Reply elements */
	if (test_bit(FR_ISREPLY, &req->flags)) {
		sg_init_one(&sg[out_sgs + in_sgs++],
			    &req->out.h, sizeof(req->out.h));
		in_sgs += sg_init_fuse_args(&sg[out_sgs + in_sgs], req,
					    args->out_args, args->out_numargs,
					    args->out_pages,
					    req->argbuf + argbuf_used, NULL);
	}

	WARN_ON(out_sgs + in_sgs != total_sgs);

	for (i = 0; i < total_sgs; i++)
		sgs[i] = &sg[i];

	spin_lock(&fsvq->lock);

	if (!fsvq->connected) {
		spin_unlock(&fsvq->lock);
		ret = -ENOTCONN;
		goto out;
	}

	vq = fsvq->vq;
	ret = virtqueue_add_sgs(vq, sgs, out_sgs, in_sgs, req, GFP_ATOMIC);
	if (ret < 0) {
		spin_unlock(&fsvq->lock);
		goto out;
	}

	/* Request successfully sent. */
	fpq = &fsvq->fud->pq;
	spin_lock(&fpq->lock);
	list_add_tail(&req->list, fpq->processing);
	spin_unlock(&fpq->lock);
	set_bit(FR_SENT, &req->flags);
	/* matches barrier in request_wait_answer() */
	smp_mb__after_atomic();

	if (!in_flight)
		inc_in_flight_req(fsvq);
	notify = virtqueue_kick_prepare(vq);

	spin_unlock(&fsvq->lock);

	if (notify)
		virtqueue_notify(vq);

out:
	if (ret < 0 && req->argbuf) {
		kfree(req->argbuf);
		req->argbuf = NULL;
	}
	if (sgs != stack_sgs) {
		kfree(sgs);
		kfree(sg);
	}

	return ret;
}

static void virtio_fs_wake_pending_and_unlock(struct fuse_iqueue *fiq)
__releases(fiq->lock)
{
	unsigned int queue_id = VQ_REQUEST; /* TODO multiqueue */
	struct virtio_fs *fs;
	struct fuse_req *req;
	struct virtio_fs_vq *fsvq;
	int ret;

	WARN_ON(list_empty(&fiq->pending));
	req = list_last_entry(&fiq->pending, struct fuse_req, list);
	clear_bit(FR_PENDING, &req->flags);
	list_del_init(&req->list);
	WARN_ON(!list_empty(&fiq->pending));
	spin_unlock(&fiq->lock);

	fs = fiq->priv;

	pr_debug("%s: opcode %u unique %#llx nodeid %#llx in.len %u out.len %u\n",
		  __func__, req->in.h.opcode, req->in.h.unique,
		 req->in.h.nodeid, req->in.h.len,
		 fuse_len_args(req->args->out_numargs, req->args->out_args));

	fsvq = &fs->vqs[queue_id];
	ret = virtio_fs_enqueue_req(fsvq, req, false);
	if (ret < 0) {
		if (ret == -ENOMEM || ret == -ENOSPC) {
			/*
			 * Virtqueue full. Retry submission from worker
			 * context as we might be holding fc->bg_lock.
			 */
			spin_lock(&fsvq->lock);
			list_add_tail(&req->list, &fsvq->queued_reqs);
			inc_in_flight_req(fsvq);
			schedule_delayed_work(&fsvq->dispatch_work,
						msecs_to_jiffies(1));
			spin_unlock(&fsvq->lock);
			return;
		}
		req->out.h.error = ret;
		pr_err("virtio-fs: virtio_fs_enqueue_req() failed %d\n", ret);

		/* Can't end request in submission context. Use a worker */
		spin_lock(&fsvq->lock);
		list_add_tail(&req->list, &fsvq->end_reqs);
		schedule_delayed_work(&fsvq->dispatch_work, 0);
		spin_unlock(&fsvq->lock);
		return;
	}
}

const static struct fuse_iqueue_ops virtio_fs_fiq_ops = {
	.wake_forget_and_unlock		= virtio_fs_wake_forget_and_unlock,
	.wake_interrupt_and_unlock	= virtio_fs_wake_interrupt_and_unlock,
	.wake_pending_and_unlock	= virtio_fs_wake_pending_and_unlock,
	.release			= virtio_fs_fiq_release,
};

static int virtio_fs_fill_super(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);
	struct virtio_fs *fs = fc->iq.priv;
	unsigned int i;
	int err;
	struct fuse_fs_context ctx = {
		.rootmode = S_IFDIR,
		.default_permissions = 1,
		.allow_other = 1,
		.max_read = UINT_MAX,
		.blksize = 512,
		.destroy = true,
		.no_control = true,
		.no_force_umount = true,
		.no_mount_options = true,
	};

	mutex_lock(&virtio_fs_mutex);

	/* After holding mutex, make sure virtiofs device is still there.
	 * Though we are holding a reference to it, drive ->remove might
	 * still have cleaned up virtual queues. In that case bail out.
	 */
	err = -EINVAL;
	if (list_empty(&fs->list)) {
		pr_info("virtio-fs: tag <%s> not found\n", fs->tag);
		goto err;
	}

	err = -ENOMEM;
	/* Allocate fuse_dev for hiprio and notification queues */
	for (i = 0; i < VQ_REQUEST; i++) {
		struct virtio_fs_vq *fsvq = &fs->vqs[i];

		fsvq->fud = fuse_dev_alloc();
		if (!fsvq->fud)
			goto err_free_fuse_devs;
	}

	ctx.fudptr = (void **)&fs->vqs[VQ_REQUEST].fud;
	err = fuse_fill_super_common(sb, &ctx);
	if (err < 0)
		goto err_free_fuse_devs;

	fc = fs->vqs[VQ_REQUEST].fud->fc;

	for (i = 0; i < fs->nvqs; i++) {
		struct virtio_fs_vq *fsvq = &fs->vqs[i];

		if (i == VQ_REQUEST)
			continue; /* already initialized */
		fuse_dev_install(fsvq->fud, fc);
	}

	/* Previous unmount will stop all queues. Start these again */
	virtio_fs_start_all_queues(fs);
	fuse_send_init(fc);
	mutex_unlock(&virtio_fs_mutex);
	return 0;

err_free_fuse_devs:
	virtio_fs_free_devs(fs);
err:
	mutex_unlock(&virtio_fs_mutex);
	return err;
}

static void virtio_kill_sb(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);
	struct virtio_fs *vfs;
	struct virtio_fs_vq *fsvq;

	/* If mount failed, we can still be called without any fc */
	if (!fc)
		return fuse_kill_sb_anon(sb);

	vfs = fc->iq.priv;
	fsvq = &vfs->vqs[VQ_HIPRIO];

	/* Stop forget queue. Soon destroy will be sent */
	spin_lock(&fsvq->lock);
	fsvq->connected = false;
	spin_unlock(&fsvq->lock);
	virtio_fs_drain_all_queues(vfs);

	fuse_kill_sb_anon(sb);

	/* fuse_kill_sb_anon() must have sent destroy. Stop all queues
	 * and drain one more time and free fuse devices. Freeing fuse
	 * devices will drop their reference on fuse_conn and that in
	 * turn will drop its reference on virtio_fs object.
	 */
	virtio_fs_stop_all_queues(vfs);
	virtio_fs_drain_all_queues(vfs);
	virtio_fs_free_devs(vfs);
}

static int virtio_fs_test_super(struct super_block *sb,
				struct fs_context *fsc)
{
	struct fuse_conn *fc = fsc->s_fs_info;

	return fc->iq.priv == get_fuse_conn_super(sb)->iq.priv;
}

static int virtio_fs_set_super(struct super_block *sb,
			       struct fs_context *fsc)
{
	int err;

	err = get_anon_bdev(&sb->s_dev);
	if (!err)
		fuse_conn_get(fsc->s_fs_info);

	return err;
}

static int virtio_fs_get_tree(struct fs_context *fsc)
{
	struct virtio_fs *fs;
	struct super_block *sb;
	struct fuse_conn *fc;
	int err;

	/* This gets a reference on virtio_fs object. This ptr gets installed
	 * in fc->iq->priv. Once fuse_conn is going away, it calls ->put()
	 * to drop the reference to this object.
	 */
	fs = virtio_fs_find_instance(fsc->source);
	if (!fs) {
		pr_info("virtio-fs: tag <%s> not found\n", fsc->source);
		return -EINVAL;
	}

	fc = kzalloc(sizeof(struct fuse_conn), GFP_KERNEL);
	if (!fc) {
		mutex_lock(&virtio_fs_mutex);
		virtio_fs_put(fs);
		mutex_unlock(&virtio_fs_mutex);
		return -ENOMEM;
	}

	fuse_conn_init(fc, get_user_ns(current_user_ns()), &virtio_fs_fiq_ops,
		       fs);
	fc->release = fuse_free_conn;
	fc->delete_stale = true;

	fsc->s_fs_info = fc;
	sb = sget_fc(fsc, virtio_fs_test_super, virtio_fs_set_super);
	fuse_conn_put(fc);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		err = virtio_fs_fill_super(sb);
		if (err) {
			deactivate_locked_super(sb);
			return err;
		}

		sb->s_flags |= SB_ACTIVE;
	}

	WARN_ON(fsc->root);
	fsc->root = dget(sb->s_root);
	return 0;
}

static const struct fs_context_operations virtio_fs_context_ops = {
	.get_tree	= virtio_fs_get_tree,
};

static int virtio_fs_init_fs_context(struct fs_context *fsc)
{
	fsc->ops = &virtio_fs_context_ops;
	return 0;
}

static struct file_system_type virtio_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "virtiofs",
	.init_fs_context = virtio_fs_init_fs_context,
	.kill_sb	= virtio_kill_sb,
};

static int __init virtio_fs_init(void)
{
	int ret;

	ret = register_virtio_driver(&virtio_fs_driver);
	if (ret < 0)
		return ret;

	ret = register_filesystem(&virtio_fs_type);
	if (ret < 0) {
		unregister_virtio_driver(&virtio_fs_driver);
		return ret;
	}

	return 0;
}
module_init(virtio_fs_init);

static void __exit virtio_fs_exit(void)
{
	unregister_filesystem(&virtio_fs_type);
	unregister_virtio_driver(&virtio_fs_driver);
}
module_exit(virtio_fs_exit);

MODULE_AUTHOR("Stefan Hajnoczi <stefanha@redhat.com>");
MODULE_DESCRIPTION("Virtio Filesystem");
MODULE_LICENSE("GPL");
MODULE_ALIAS_FS(KBUILD_MODNAME);
MODULE_DEVICE_TABLE(virtio, id_table);
