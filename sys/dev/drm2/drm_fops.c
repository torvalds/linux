/**
 * \file drm_fops.c
 * File operations for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Daryll Strauss <daryll@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Mon Jan  4 08:58:31 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>

static int drm_open_helper(struct cdev *kdev, int flags, int fmt,
			   DRM_STRUCTPROC *p, struct drm_device *dev);

static int drm_setup(struct drm_device * dev)
{
	int i;
	int ret;

	if (dev->driver->firstopen) {
		ret = dev->driver->firstopen(dev);
		if (ret != 0)
			return ret;
	}

	atomic_set(&dev->ioctl_count, 0);
	atomic_set(&dev->vma_count, 0);

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA) &&
	    !drm_core_check_feature(dev, DRIVER_MODESET)) {
		dev->buf_use = 0;
		atomic_set(&dev->buf_alloc, 0);

		i = drm_dma_setup(dev);
		if (i < 0)
			return i;
	}

	/*
	 * FIXME Linux<->FreeBSD: counter incremented in drm_open() and
	 * reset to 0 here.
	 */
#if 0
	for (i = 0; i < ARRAY_SIZE(dev->counts); i++)
		atomic_set(&dev->counts[i], 0);
#endif

	dev->sigdata.lock = NULL;

	dev->context_flag = 0;
	dev->interrupt_flag = 0;
	dev->dma_flag = 0;
	dev->last_context = 0;
	dev->last_switch = 0;
	dev->last_checked = 0;
	DRM_INIT_WAITQUEUE(&dev->context_wait);
	dev->if_version = 0;

#ifdef FREEBSD_NOTYET
	dev->ctx_start = 0;
	dev->lck_start = 0;

	dev->buf_async = NULL;
	DRM_INIT_WAITQUEUE(&dev->buf_readers);
	DRM_INIT_WAITQUEUE(&dev->buf_writers);
#endif /* FREEBSD_NOTYET */

	DRM_DEBUG("\n");

	/*
	 * The kernel's context could be created here, but is now created
	 * in drm_dma_enqueue.  This is more resource-efficient for
	 * hardware that does not do DMA, but may mean that
	 * drm_select_queue fails between the time the interrupt is
	 * initialized and the time the queues are initialized.
	 */

	return 0;
}

/**
 * Open file.
 *
 * \param inode device inode
 * \param filp file pointer.
 * \return zero on success or a negative number on failure.
 *
 * Searches the DRM device with the same minor number, calls open_helper(), and
 * increments the device open count. If the open count was previous at zero,
 * i.e., it's the first that the device is open, then calls setup().
 */
int drm_open(struct cdev *kdev, int flags, int fmt, DRM_STRUCTPROC *p)
{
	struct drm_device *dev = NULL;
	struct drm_minor *minor;
	int retcode = 0;
	int need_setup = 0;

	minor = kdev->si_drv1;
	if (!minor)
		return ENODEV;

	if (!(dev = minor->dev))
		return ENODEV;

	sx_xlock(&drm_global_mutex);

	/*
	 * FIXME Linux<->FreeBSD: On Linux, counter updated outside
	 * global mutex.
	 */
	if (!dev->open_count++)
		need_setup = 1;

	retcode = drm_open_helper(kdev, flags, fmt, p, dev);
	if (retcode) {
		sx_xunlock(&drm_global_mutex);
		return (-retcode);
	}
	atomic_inc(&dev->counts[_DRM_STAT_OPENS]);
	if (need_setup) {
		retcode = drm_setup(dev);
		if (retcode)
			goto err_undo;
	}
	sx_xunlock(&drm_global_mutex);
	return 0;

err_undo:
	mtx_lock(&Giant); /* FIXME: Giant required? */
	device_unbusy(dev->dev);
	mtx_unlock(&Giant);
	dev->open_count--;
	sx_xunlock(&drm_global_mutex);
	return -retcode;
}
EXPORT_SYMBOL(drm_open);

/**
 * Called whenever a process opens /dev/drm.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param dev device.
 * \return zero on success or a negative number on failure.
 *
 * Creates and initializes a drm_file structure for the file private data in \p
 * filp and add it into the double linked list in \p dev.
 */
static int drm_open_helper(struct cdev *kdev, int flags, int fmt,
			   DRM_STRUCTPROC *p, struct drm_device *dev)
{
	struct drm_file *priv;
	int ret;

	if (flags & O_EXCL)
		return -EBUSY;	/* No exclusive opens */
	if (dev->switch_power_state != DRM_SWITCH_POWER_ON)
		return -EINVAL;

	DRM_DEBUG("pid = %d, device = %s\n", DRM_CURRENTPID, devtoname(kdev));

	priv = malloc(sizeof(*priv), DRM_MEM_FILES, M_NOWAIT | M_ZERO);
	if (!priv)
		return -ENOMEM;

	priv->uid = p->td_ucred->cr_svuid;
	priv->pid = p->td_proc->p_pid;
	priv->minor = kdev->si_drv1;
	priv->ioctl_count = 0;
	/* for compatibility root is always authenticated */
	priv->authenticated = DRM_SUSER(p);
	priv->lock_count = 0;

	INIT_LIST_HEAD(&priv->lhead);
	INIT_LIST_HEAD(&priv->fbs);
	INIT_LIST_HEAD(&priv->event_list);
	priv->event_space = 4096; /* set aside 4k for event buffer */

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_open(dev, priv);

#ifdef FREEBSD_NOTYET
	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_prime_init_file_private(&priv->prime);
#endif /* FREEBSD_NOTYET */

	if (dev->driver->open) {
		ret = dev->driver->open(dev, priv);
		if (ret < 0)
			goto out_free;
	}


	/* if there is no current master make this fd it */
	DRM_LOCK(dev);
	if (!priv->minor->master) {
		/* create a new master */
		priv->minor->master = drm_master_create(priv->minor);
		if (!priv->minor->master) {
			DRM_UNLOCK(dev);
			ret = -ENOMEM;
			goto out_free;
		}

		priv->is_master = 1;
		/* take another reference for the copy in the local file priv */
		priv->master = drm_master_get(priv->minor->master);

		priv->authenticated = 1;

		DRM_UNLOCK(dev);
		if (dev->driver->master_create) {
			ret = dev->driver->master_create(dev, priv->master);
			if (ret) {
				DRM_LOCK(dev);
				/* drop both references if this fails */
				drm_master_put(&priv->minor->master);
				drm_master_put(&priv->master);
				DRM_UNLOCK(dev);
				goto out_free;
			}
		}
		DRM_LOCK(dev);
		if (dev->driver->master_set) {
			ret = dev->driver->master_set(dev, priv, true);
			if (ret) {
				/* drop both references if this fails */
				drm_master_put(&priv->minor->master);
				drm_master_put(&priv->master);
				DRM_UNLOCK(dev);
				goto out_free;
			}
		}
		DRM_UNLOCK(dev);
	} else {
		/* get a reference to the master */
		priv->master = drm_master_get(priv->minor->master);
		DRM_UNLOCK(dev);
	}

	DRM_LOCK(dev);
	list_add(&priv->lhead, &dev->filelist);
	DRM_UNLOCK(dev);

	mtx_lock(&Giant); /* FIXME: Giant required? */
	device_busy(dev->dev);
	mtx_unlock(&Giant);

	ret = devfs_set_cdevpriv(priv, drm_release);
	if (ret != 0)
		drm_release(priv);

	return ret;
      out_free:
	free(priv, DRM_MEM_FILES);
	return ret;
}

static void drm_master_release(struct drm_device *dev, struct drm_file *file_priv)
{

	if (drm_i_have_hw_lock(dev, file_priv)) {
		DRM_DEBUG("File %p released, freeing lock for context %d\n",
			  file_priv, _DRM_LOCKING_CONTEXT(file_priv->master->lock.hw_lock->lock));
		drm_lock_free(&file_priv->master->lock,
			      _DRM_LOCKING_CONTEXT(file_priv->master->lock.hw_lock->lock));
	}
}

static void drm_events_release(struct drm_file *file_priv)
{
	struct drm_device *dev = file_priv->minor->dev;
	struct drm_pending_event *e, *et;
	struct drm_pending_vblank_event *v, *vt;
	unsigned long flags;

	DRM_SPINLOCK_IRQSAVE(&dev->event_lock, flags);

	/* Remove pending flips */
	list_for_each_entry_safe(v, vt, &dev->vblank_event_list, base.link)
		if (v->base.file_priv == file_priv) {
			list_del(&v->base.link);
			drm_vblank_put(dev, v->pipe);
			v->base.destroy(&v->base);
		}

	/* Remove unconsumed events */
	list_for_each_entry_safe(e, et, &file_priv->event_list, link)
		e->destroy(e);

	DRM_SPINUNLOCK_IRQRESTORE(&dev->event_lock, flags);
}

/**
 * Release file.
 *
 * \param inode device inode
 * \param file_priv DRM file private.
 * \return zero on success or a negative number on failure.
 *
 * If the hardware lock is held then free it, and take it again for the kernel
 * context since it's necessary to reclaim buffers. Unlink the file private
 * data from its list and free it. Decreases the open count and if it reaches
 * zero calls drm_lastclose().
 */
void drm_release(void *data)
{
	struct drm_file *file_priv = data;
	struct drm_device *dev = file_priv->minor->dev;

	sx_xlock(&drm_global_mutex);

	DRM_DEBUG("open_count = %d\n", dev->open_count);

	if (dev->driver->preclose)
		dev->driver->preclose(dev, file_priv);

	/* ========================================================
	 * Begin inline drm_release
	 */

	DRM_DEBUG("pid = %d, device = 0x%lx, open_count = %d\n",
		  DRM_CURRENTPID,
		  (long)file_priv->minor->device,
		  dev->open_count);

	/* Release any auth tokens that might point to this file_priv,
	   (do that under the drm_global_mutex) */
	if (file_priv->magic)
		(void) drm_remove_magic(file_priv->master, file_priv->magic);

	/* if the master has gone away we can't do anything with the lock */
	if (file_priv->minor->master)
		drm_master_release(dev, file_priv);

	if (drm_core_check_feature(dev, DRIVER_HAVE_DMA))
		drm_core_reclaim_buffers(dev, file_priv);

	drm_events_release(file_priv);

	seldrain(&file_priv->event_poll);

	if (dev->driver->driver_features & DRIVER_MODESET)
		drm_fb_release(file_priv);

	if (dev->driver->driver_features & DRIVER_GEM)
		drm_gem_release(dev, file_priv);

#ifdef FREEBSD_NOTYET
	mutex_lock(&dev->ctxlist_mutex);
	if (!list_empty(&dev->ctxlist)) {
		struct drm_ctx_list *pos, *n;

		list_for_each_entry_safe(pos, n, &dev->ctxlist, head) {
			if (pos->tag == file_priv &&
			    pos->handle != DRM_KERNEL_CONTEXT) {
				if (dev->driver->context_dtor)
					dev->driver->context_dtor(dev,
								  pos->handle);

				drm_ctxbitmap_free(dev, pos->handle);

				list_del(&pos->head);
				kfree(pos);
				--dev->ctx_count;
			}
		}
	}
	mutex_unlock(&dev->ctxlist_mutex);
#endif /* FREEBSD_NOTYET */

	DRM_LOCK(dev);

	if (file_priv->is_master) {
		struct drm_master *master = file_priv->master;
		struct drm_file *temp;
		list_for_each_entry(temp, &dev->filelist, lhead) {
			if ((temp->master == file_priv->master) &&
			    (temp != file_priv))
				temp->authenticated = 0;
		}

		/**
		 * Since the master is disappearing, so is the
		 * possibility to lock.
		 */

		if (master->lock.hw_lock) {
			if (dev->sigdata.lock == master->lock.hw_lock)
				dev->sigdata.lock = NULL;
			master->lock.hw_lock = NULL;
			master->lock.file_priv = NULL;
			DRM_WAKEUP_INT(&master->lock.lock_queue);
		}

		if (file_priv->minor->master == file_priv->master) {
			/* drop the reference held my the minor */
			if (dev->driver->master_drop)
				dev->driver->master_drop(dev, file_priv, true);
			drm_master_put(&file_priv->minor->master);
		}
	}

	/* drop the reference held my the file priv */
	drm_master_put(&file_priv->master);
	file_priv->is_master = 0;
	list_del(&file_priv->lhead);
	DRM_UNLOCK(dev);

	if (dev->driver->postclose)
		dev->driver->postclose(dev, file_priv);

#ifdef FREEBSD_NOTYET
	if (drm_core_check_feature(dev, DRIVER_PRIME))
		drm_prime_destroy_file_private(&file_priv->prime);
#endif /* FREEBSD_NOTYET */

	free(file_priv, DRM_MEM_FILES);

	/* ========================================================
	 * End inline drm_release
	 */

	atomic_inc(&dev->counts[_DRM_STAT_CLOSES]);
	mtx_lock(&Giant);
	device_unbusy(dev->dev);
	mtx_unlock(&Giant);
	if (!--dev->open_count) {
		if (atomic_read(&dev->ioctl_count)) {
			DRM_ERROR("Device busy: %d\n",
				  atomic_read(&dev->ioctl_count));
		} else
			drm_lastclose(dev);
	}
	sx_xunlock(&drm_global_mutex);
}
EXPORT_SYMBOL(drm_release);

static bool
drm_dequeue_event(struct drm_file *file_priv, struct uio *uio,
    struct drm_pending_event **out)
{
	struct drm_pending_event *e;
	bool ret = false;

	/* Already locked in drm_read(). */
	/* DRM_SPINLOCK_IRQSAVE(&dev->event_lock, flags); */

	*out = NULL;
	if (list_empty(&file_priv->event_list))
		goto out;
	e = list_first_entry(&file_priv->event_list,
			     struct drm_pending_event, link);
	if (e->event->length > uio->uio_resid)
		goto out;

	file_priv->event_space += e->event->length;
	list_del(&e->link);
	*out = e;
	ret = true;

out:
	/* DRM_SPINUNLOCK_IRQRESTORE(&dev->event_lock, flags); */
	return ret;
}

int
drm_read(struct cdev *kdev, struct uio *uio, int ioflag)
{
	struct drm_file *file_priv;
	struct drm_device *dev;
	struct drm_pending_event *e;
	ssize_t error;

	error = devfs_get_cdevpriv((void **)&file_priv);
	if (error != 0) {
		DRM_ERROR("can't find authenticator\n");
		return (EINVAL);
	}

	dev = drm_get_device_from_kdev(kdev);
	mtx_lock(&dev->event_lock);
	while (list_empty(&file_priv->event_list)) {
		if ((ioflag & O_NONBLOCK) != 0) {
			error = EAGAIN;
			goto out;
		}
		error = msleep(&file_priv->event_space, &dev->event_lock,
	           PCATCH, "drmrea", 0);
	       if (error != 0)
		       goto out;
	}

	while (drm_dequeue_event(file_priv, uio, &e)) {
		mtx_unlock(&dev->event_lock);
		error = uiomove(e->event, e->event->length, uio);
		CTR3(KTR_DRM, "drm_event_dequeued %d %d %d", curproc->p_pid,
		    e->event->type, e->event->length);

		e->destroy(e);
		if (error != 0)
			return (error);
		mtx_lock(&dev->event_lock);
	}

out:
	mtx_unlock(&dev->event_lock);
	return (error);
}
EXPORT_SYMBOL(drm_read);

void
drm_event_wakeup(struct drm_pending_event *e)
{
	struct drm_file *file_priv;
	struct drm_device *dev;

	file_priv = e->file_priv;
	dev = file_priv->minor->dev;
	mtx_assert(&dev->event_lock, MA_OWNED);

	wakeup(&file_priv->event_space);
	selwakeup(&file_priv->event_poll);
}

int
drm_poll(struct cdev *kdev, int events, struct thread *td)
{
	struct drm_file *file_priv;
	struct drm_device *dev;
	int error, revents;

	error = devfs_get_cdevpriv((void **)&file_priv);
	if (error != 0) {
		DRM_ERROR("can't find authenticator\n");
		return (EINVAL);
	}

	dev = drm_get_device_from_kdev(kdev);

	revents = 0;
	mtx_lock(&dev->event_lock);
	if ((events & (POLLIN | POLLRDNORM)) != 0) {
		if (list_empty(&file_priv->event_list)) {
			CTR0(KTR_DRM, "drm_poll empty list");
			selrecord(td, &file_priv->event_poll);
		} else {
			revents |= events & (POLLIN | POLLRDNORM);
			CTR1(KTR_DRM, "drm_poll revents %x", revents);
		}
	}
	mtx_unlock(&dev->event_lock);
	return (revents);
}
EXPORT_SYMBOL(drm_poll);

int
drm_mmap_single(struct cdev *kdev, vm_ooffset_t *offset, vm_size_t size,
    struct vm_object **obj_res, int nprot)
{
	struct drm_device *dev;

	dev = drm_get_device_from_kdev(kdev);
	if (dev->drm_ttm_bdev != NULL) {
		return (-ttm_bo_mmap_single(dev->drm_ttm_bdev, offset, size,
		    obj_res, nprot));
	} else if ((dev->driver->driver_features & DRIVER_GEM) != 0) {
		return (-drm_gem_mmap_single(dev, offset, size, obj_res, nprot));
	} else {
		return (ENODEV);
	}
}
