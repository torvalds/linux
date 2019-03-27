/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008, Sam Leffler <sam@errno.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/errno.h>
#include <sys/linker.h>
#include <sys/firmware.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/module.h>
#include <sys/eventhandler.h>

#include <sys/filedesc.h>
#include <sys/vnode.h>

/*
 * Loadable firmware support. See sys/sys/firmware.h and firmware(9)
 * form more details on the subsystem.
 *
 * 'struct firmware' is the user-visible part of the firmware table.
 * Additional internal information is stored in a 'struct priv_fw'
 * (currently a static array). A slot is in use if FW_INUSE is true:
 */

#define FW_INUSE(p)	((p)->file != NULL || (p)->fw.name != NULL)

/*
 * fw.name != NULL when an image is registered; file != NULL for
 * autoloaded images whose handling has not been completed.
 *
 * The state of a slot evolves as follows:
 *	firmware_register	-->  fw.name = image_name
 *	(autoloaded image)	-->  file = module reference
 *	firmware_unregister	-->  fw.name = NULL
 *	(unloadentry complete)	-->  file = NULL
 *
 * In order for the above to work, the 'file' field must remain
 * unchanged in firmware_unregister().
 *
 * Images residing in the same module are linked to each other
 * through the 'parent' argument of firmware_register().
 * One image (typically, one with the same name as the module to let
 * the autoloading mechanism work) is considered the parent image for
 * all other images in the same module. Children affect the refcount
 * on the parent image preventing improper unloading of the image itself.
 */

struct priv_fw {
	int		refcnt;		/* reference count */

	/*
	 * parent entry, see above. Set on firmware_register(),
	 * cleared on firmware_unregister().
	 */
	struct priv_fw	*parent;

	int 		flags;	/* record FIRMWARE_UNLOAD requests */
#define FW_UNLOAD	0x100

	/*
	 * 'file' is private info managed by the autoload/unload code.
	 * Set at the end of firmware_get(), cleared only in the
	 * firmware_unload_task, so the latter can depend on its value even
	 * while the lock is not held.
	 */
	linker_file_t   file;	/* module file, if autoloaded */

	/*
	 * 'fw' is the externally visible image information.
	 * We do not make it the first field in priv_fw, to avoid the
	 * temptation of casting pointers to each other.
	 * Use PRIV_FW(fw) to get a pointer to the cointainer of fw.
	 * Beware, PRIV_FW does not work for a NULL pointer.
	 */
	struct firmware	fw;	/* externally visible information */
};

/*
 * PRIV_FW returns the pointer to the container of struct firmware *x.
 * Cast to intptr_t to override the 'const' attribute of x
 */
#define PRIV_FW(x)	((struct priv_fw *)		\
	((intptr_t)(x) - offsetof(struct priv_fw, fw)) )

/*
 * At the moment we use a static array as backing store for the registry.
 * Should we move to a dynamic structure, keep in mind that we cannot
 * reallocate the array because pointers are held externally.
 * A list may work, though.
 */
#define	FIRMWARE_MAX	50
static struct priv_fw firmware_table[FIRMWARE_MAX];

/*
 * Firmware module operations are handled in a separate task as they
 * might sleep and they require directory context to do i/o.
 */
static struct taskqueue *firmware_tq;
static struct task firmware_unload_task;

/*
 * This mutex protects accesses to the firmware table.
 */
static struct mtx firmware_mtx;
MTX_SYSINIT(firmware, &firmware_mtx, "firmware table", MTX_DEF);

/*
 * Helper function to lookup a name.
 * As a side effect, it sets the pointer to a free slot, if any.
 * This way we can concentrate most of the registry scanning in
 * this function, which makes it easier to replace the registry
 * with some other data structure.
 */
static struct priv_fw *
lookup(const char *name, struct priv_fw **empty_slot)
{
	struct priv_fw *fp = NULL;
	struct priv_fw *dummy;
	int i;

	if (empty_slot == NULL)
		empty_slot = &dummy;
	*empty_slot = NULL;
	for (i = 0; i < FIRMWARE_MAX; i++) {
		fp = &firmware_table[i];
		if (fp->fw.name != NULL && strcasecmp(name, fp->fw.name) == 0)
			break;
		else if (!FW_INUSE(fp))
			*empty_slot = fp;
	}
	return (i < FIRMWARE_MAX ) ? fp : NULL;
}

/*
 * Register a firmware image with the specified name.  The
 * image name must not already be registered.  If this is a
 * subimage then parent refers to a previously registered
 * image that this should be associated with.
 */
const struct firmware *
firmware_register(const char *imagename, const void *data, size_t datasize,
    unsigned int version, const struct firmware *parent)
{
	struct priv_fw *match, *frp;
	char *str;

	str = strdup(imagename, M_TEMP);

	mtx_lock(&firmware_mtx);
	/*
	 * Do a lookup to make sure the name is unique or find a free slot.
	 */
	match = lookup(imagename, &frp);
	if (match != NULL) {
		mtx_unlock(&firmware_mtx);
		printf("%s: image %s already registered!\n",
			__func__, imagename);
		free(str, M_TEMP);
		return NULL;
	}
	if (frp == NULL) {
		mtx_unlock(&firmware_mtx);
		printf("%s: cannot register image %s, firmware table full!\n",
		    __func__, imagename);
		free(str, M_TEMP);
		return NULL;
	}
	bzero(frp, sizeof(*frp));	/* start from a clean record */
	frp->fw.name = str;
	frp->fw.data = data;
	frp->fw.datasize = datasize;
	frp->fw.version = version;
	if (parent != NULL)
		frp->parent = PRIV_FW(parent);
	mtx_unlock(&firmware_mtx);
	if (bootverbose)
		printf("firmware: '%s' version %u: %zu bytes loaded at %p\n",
		    imagename, version, datasize, data);
	return &frp->fw;
}

/*
 * Unregister/remove a firmware image.  If there are outstanding
 * references an error is returned and the image is not removed
 * from the registry.
 */
int
firmware_unregister(const char *imagename)
{
	struct priv_fw *fp;
	int err;

	mtx_lock(&firmware_mtx);
	fp = lookup(imagename, NULL);
	if (fp == NULL) {
		/*
		 * It is ok for the lookup to fail; this can happen
		 * when a module is unloaded on last reference and the
		 * module unload handler unregister's each of its
		 * firmware images.
		 */
		err = 0;
	} else if (fp->refcnt != 0) {	/* cannot unregister */
		err = EBUSY;
	} else {
		linker_file_t x = fp->file;	/* save value */

		/*
		 * Clear the whole entry with bzero to make sure we
		 * do not forget anything. Then restore 'file' which is
		 * non-null for autoloaded images.
		 */
		free((void *) (uintptr_t) fp->fw.name, M_TEMP);
		bzero(fp, sizeof(struct priv_fw));
		fp->file = x;
		err = 0;
	}
	mtx_unlock(&firmware_mtx);
	return err;
}

static void
loadimage(void *arg, int npending)
{
	struct thread *td = curthread;
	char *imagename = arg;
	struct priv_fw *fp;
	linker_file_t result;
	int error;

	/* synchronize with the thread that dispatched us */
	mtx_lock(&firmware_mtx);
	mtx_unlock(&firmware_mtx);

	if (td->td_proc->p_fd->fd_rdir == NULL) {
		printf("%s: root not mounted yet, no way to load image\n",
		    imagename);
		goto done;
	}
	error = linker_reference_module(imagename, NULL, &result);
	if (error != 0) {
		printf("%s: could not load firmware image, error %d\n",
		    imagename, error);
		goto done;
	}

	mtx_lock(&firmware_mtx);
	fp = lookup(imagename, NULL);
	if (fp == NULL || fp->file != NULL) {
		mtx_unlock(&firmware_mtx);
		if (fp == NULL)
			printf("%s: firmware image loaded, "
			    "but did not register\n", imagename);
		(void) linker_release_module(imagename, NULL, NULL);
		goto done;
	}
	fp->file = result;	/* record the module identity */
	mtx_unlock(&firmware_mtx);
done:
	wakeup_one(imagename);		/* we're done */
}

/*
 * Lookup and potentially load the specified firmware image.
 * If the firmware is not found in the registry, try to load a kernel
 * module named as the image name.
 * If the firmware is located, a reference is returned. The caller must
 * release this reference for the image to be eligible for removal/unload.
 */
const struct firmware *
firmware_get(const char *imagename)
{
	struct task fwload_task;
	struct thread *td;
	struct priv_fw *fp;

	mtx_lock(&firmware_mtx);
	fp = lookup(imagename, NULL);
	if (fp != NULL)
		goto found;
	/*
	 * Image not present, try to load the module holding it.
	 */
	td = curthread;
	if (priv_check(td, PRIV_FIRMWARE_LOAD) != 0 ||
	    securelevel_gt(td->td_ucred, 0) != 0) {
		mtx_unlock(&firmware_mtx);
		printf("%s: insufficient privileges to "
		    "load firmware image %s\n", __func__, imagename);
		return NULL;
	}
	/* 
	 * Defer load to a thread with known context.  linker_reference_module
	 * may do filesystem i/o which requires root & current dirs, etc.
	 * Also we must not hold any mtx's over this call which is problematic.
	 */
	if (!cold) {
		TASK_INIT(&fwload_task, 0, loadimage, __DECONST(void *,
		    imagename));
		taskqueue_enqueue(firmware_tq, &fwload_task);
		msleep(__DECONST(void *, imagename), &firmware_mtx, 0,
		    "fwload", 0);
	}
	/*
	 * After attempting to load the module, see if the image is registered.
	 */
	fp = lookup(imagename, NULL);
	if (fp == NULL) {
		mtx_unlock(&firmware_mtx);
		return NULL;
	}
found:				/* common exit point on success */
	if (fp->refcnt == 0 && fp->parent != NULL)
		fp->parent->refcnt++;
	fp->refcnt++;
	mtx_unlock(&firmware_mtx);
	return &fp->fw;
}

/*
 * Release a reference to a firmware image returned by firmware_get.
 * The caller may specify, with the FIRMWARE_UNLOAD flag, its desire
 * to release the resource, but the flag is only advisory.
 *
 * If this is the last reference to the firmware image, and this is an
 * autoloaded module, wake up the firmware_unload_task to figure out
 * what to do with the associated module.
 */
void
firmware_put(const struct firmware *p, int flags)
{
	struct priv_fw *fp = PRIV_FW(p);

	mtx_lock(&firmware_mtx);
	fp->refcnt--;
	if (fp->refcnt == 0) {
		if (fp->parent != NULL)
			fp->parent->refcnt--;
		if (flags & FIRMWARE_UNLOAD)
			fp->flags |= FW_UNLOAD;
		if (fp->file)
			taskqueue_enqueue(firmware_tq, &firmware_unload_task);
	}
	mtx_unlock(&firmware_mtx);
}

/*
 * Setup directory state for the firmware_tq thread so we can do i/o.
 */
static void
set_rootvnode(void *arg, int npending)
{

	pwd_ensure_dirs();

	free(arg, M_TEMP);
}

/*
 * Event handler called on mounting of /; bounce a task
 * into the task queue thread to setup it's directories.
 */
static void
firmware_mountroot(void *arg)
{
	struct task *setroot_task;

	setroot_task = malloc(sizeof(struct task), M_TEMP, M_NOWAIT);
	if (setroot_task != NULL) {
		TASK_INIT(setroot_task, 0, set_rootvnode, setroot_task);
		taskqueue_enqueue(firmware_tq, setroot_task);
	} else
		printf("%s: no memory for task!\n", __func__);
}
EVENTHANDLER_DEFINE(mountroot, firmware_mountroot, NULL, 0);

/*
 * The body of the task in charge of unloading autoloaded modules
 * that are not needed anymore.
 * Images can be cross-linked so we may need to make multiple passes,
 * but the time we spend in the loop is bounded because we clear entries
 * as we touch them.
 */
static void
unloadentry(void *unused1, int unused2)
{
	int limit = FIRMWARE_MAX;
	int i;	/* current cycle */

	mtx_lock(&firmware_mtx);
	/*
	 * Scan the table. limit is set to make sure we make another
	 * full sweep after matching an entry that requires unloading.
	 */
	for (i = 0; i < limit; i++) {
		struct priv_fw *fp;
		int err;

		fp = &firmware_table[i % FIRMWARE_MAX];
		if (fp->fw.name == NULL || fp->file == NULL ||
		    fp->refcnt != 0 || (fp->flags & FW_UNLOAD) == 0)
			continue;

		/*
		 * Found an entry. Now:
		 * 1. bump up limit to make sure we make another full round;
		 * 2. clear FW_UNLOAD so we don't try this entry again.
		 * 3. release the lock while trying to unload the module.
		 * 'file' remains set so that the entry cannot be reused
		 * in the meantime (it also means that fp->file will
		 * not change while we release the lock).
		 */
		limit = i + FIRMWARE_MAX;	/* make another full round */
		fp->flags &= ~FW_UNLOAD;	/* do not try again */

		mtx_unlock(&firmware_mtx);
		err = linker_release_module(NULL, NULL, fp->file);
		mtx_lock(&firmware_mtx);

		/*
		 * We rely on the module to call firmware_unregister()
		 * on unload to actually release the entry.
		 * If err = 0 we can drop our reference as the system
		 * accepted it. Otherwise unloading failed (e.g. the
		 * module itself gave an error) so our reference is
		 * still valid.
		 */
		if (err == 0)
			fp->file = NULL; 
	}
	mtx_unlock(&firmware_mtx);
}

/*
 * Module glue.
 */
static int
firmware_modevent(module_t mod, int type, void *unused)
{
	struct priv_fw *fp;
	int i, err;

	switch (type) {
	case MOD_LOAD:
		TASK_INIT(&firmware_unload_task, 0, unloadentry, NULL);
		firmware_tq = taskqueue_create("taskqueue_firmware", M_WAITOK,
		    taskqueue_thread_enqueue, &firmware_tq);
		/* NB: use our own loop routine that sets up context */
		(void) taskqueue_start_threads(&firmware_tq, 1, PWAIT,
		    "firmware taskq");
		if (rootvnode != NULL) {
			/* 
			 * Root is already mounted so we won't get an event;
			 * simulate one here.
			 */
			firmware_mountroot(NULL);
		}
		return 0;

	case MOD_UNLOAD:
		/* request all autoloaded modules to be released */
		mtx_lock(&firmware_mtx);
		for (i = 0; i < FIRMWARE_MAX; i++) {
			fp = &firmware_table[i];
			fp->flags |= FW_UNLOAD;
		}
		mtx_unlock(&firmware_mtx);
		taskqueue_enqueue(firmware_tq, &firmware_unload_task);
		taskqueue_drain(firmware_tq, &firmware_unload_task);
		err = 0;
		for (i = 0; i < FIRMWARE_MAX; i++) {
			fp = &firmware_table[i];
			if (fp->fw.name != NULL) {
				printf("%s: image %p ref %d still active slot %d\n",
					__func__, fp->fw.name,
					fp->refcnt,  i);
				err = EINVAL;
			}
		}
		if (err == 0)
			taskqueue_free(firmware_tq);
		return err;
	}
	return EINVAL;
}

static moduledata_t firmware_mod = {
	"firmware",
	firmware_modevent,
	NULL
};
DECLARE_MODULE(firmware, firmware_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(firmware, 1);
