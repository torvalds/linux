/*-
 * Copyright (c) 2016 Akshay Jaggi <jaggi@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * gntdev.c
 * 
 * Interface to /dev/xen/gntdev.
 * 
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/rman.h>
#include <sys/tree.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/bitset.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>

#include <machine/md_var.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/error.h>
#include <xen/xen_intr.h>
#include <xen/gnttab.h>
#include <xen/gntdev.h>

MALLOC_DEFINE(M_GNTDEV, "gntdev", "Xen grant-table user-space device");

#define MAX_OFFSET_COUNT ((0xffffffffffffffffull >> PAGE_SHIFT) + 1)

static d_open_t gntdev_open;
static d_ioctl_t gntdev_ioctl;
static d_mmap_single_t gntdev_mmap_single;

static struct cdevsw gntdev_devsw = {
	.d_version = D_VERSION,
	.d_open = gntdev_open,
	.d_ioctl = gntdev_ioctl,
	.d_mmap_single = gntdev_mmap_single,
	.d_name = "gntdev",
};

static device_t gntdev_dev = NULL;

struct gntdev_gref;
struct gntdev_gmap;
STAILQ_HEAD(gref_list_head, gntdev_gref);
STAILQ_HEAD(gmap_list_head, gntdev_gmap);
RB_HEAD(gref_tree_head, gntdev_gref);
RB_HEAD(gmap_tree_head, gntdev_gmap);

struct file_offset_struct {
	RB_ENTRY(file_offset_struct)	next;
	uint64_t			file_offset;
	uint64_t			count;
};

static int
offset_cmp(struct file_offset_struct *f1, struct file_offset_struct *f2)
{
	return (f1->file_offset - f2->file_offset);
}

RB_HEAD(file_offset_head, file_offset_struct);
RB_GENERATE_STATIC(file_offset_head, file_offset_struct, next, offset_cmp);

struct per_user_data {
	struct mtx		user_data_lock;
	struct gref_tree_head	gref_tree;
	struct gmap_tree_head	gmap_tree;
	struct file_offset_head	file_offset;
};

/*
 * Get offset into the file which will be used while mmapping the
 * appropriate pages by the userspace program.
 */
static int
get_file_offset(struct per_user_data *priv_user, uint32_t count,
    uint64_t *file_offset)
{
	struct file_offset_struct *offset, *offset_tmp;

	if (count == 0)
		return (EINVAL);
	mtx_lock(&priv_user->user_data_lock);
	RB_FOREACH_SAFE(offset, file_offset_head, &priv_user->file_offset,
	    offset_tmp) {
		if (offset->count >= count) {
			offset->count -= count;
			*file_offset = offset->file_offset + offset->count *
			    PAGE_SIZE;
			if (offset->count == 0) {
				RB_REMOVE(file_offset_head,
				    &priv_user->file_offset, offset);
				free(offset, M_GNTDEV);
			}
			mtx_unlock(&priv_user->user_data_lock);
			return (0);
		}
	}
	mtx_unlock(&priv_user->user_data_lock);

	return (ENOSPC);
}

static void
put_file_offset(struct per_user_data *priv_user, uint32_t count,
    uint64_t file_offset)
{
	struct file_offset_struct *offset, *offset_nxt, *offset_prv;

	offset = malloc(sizeof(*offset), M_GNTDEV, M_WAITOK | M_ZERO);
	offset->file_offset = file_offset;
	offset->count = count;

	mtx_lock(&priv_user->user_data_lock);
	RB_INSERT(file_offset_head, &priv_user->file_offset, offset);
	offset_nxt = RB_NEXT(file_offset_head, &priv_user->file_offset, offset);
	offset_prv = RB_PREV(file_offset_head, &priv_user->file_offset, offset);
	if (offset_nxt != NULL &&
	    offset_nxt->file_offset == offset->file_offset + offset->count *
	    PAGE_SIZE) {
		offset->count += offset_nxt->count;
		RB_REMOVE(file_offset_head, &priv_user->file_offset,
		    offset_nxt);
		free(offset_nxt, M_GNTDEV);
	}
	if (offset_prv != NULL &&
	    offset->file_offset == offset_prv->file_offset + offset_prv->count *
	    PAGE_SIZE) {
		offset_prv->count += offset->count;
		RB_REMOVE(file_offset_head, &priv_user->file_offset, offset);
		free(offset, M_GNTDEV);
	}
	mtx_unlock(&priv_user->user_data_lock);
}

static int	gntdev_gmap_pg_ctor(void *handle, vm_ooffset_t size,
    vm_prot_t prot, vm_ooffset_t foff, struct ucred *cred, u_short *color);
static void	gntdev_gmap_pg_dtor(void *handle);
static int	gntdev_gmap_pg_fault(vm_object_t object, vm_ooffset_t offset,
    int prot, vm_page_t *mres);

static struct cdev_pager_ops gntdev_gmap_pg_ops = {
	.cdev_pg_fault = gntdev_gmap_pg_fault,
	.cdev_pg_ctor =	gntdev_gmap_pg_ctor,
	.cdev_pg_dtor =	gntdev_gmap_pg_dtor,
};

struct cleanup_data_struct {
	struct mtx to_kill_grefs_mtx;
	struct mtx to_kill_gmaps_mtx;
	struct gref_list_head to_kill_grefs;
	struct gmap_list_head to_kill_gmaps;
};

static struct cleanup_data_struct cleanup_data = {
	.to_kill_grefs = STAILQ_HEAD_INITIALIZER(cleanup_data.to_kill_grefs),
	.to_kill_gmaps = STAILQ_HEAD_INITIALIZER(cleanup_data.to_kill_gmaps),
};
MTX_SYSINIT(to_kill_grefs_mtx, &cleanup_data.to_kill_grefs_mtx,
    "gntdev to_kill_grefs mutex", MTX_DEF);
MTX_SYSINIT(to_kill_gmaps_mtx, &cleanup_data.to_kill_gmaps_mtx,
    "gntdev to_kill_gmaps mutex", MTX_DEF);

static void	cleanup_function(void *arg, __unused int pending);
static struct task cleanup_task = TASK_INITIALIZER(0, cleanup_function,
    &cleanup_data);

struct notify_data {
	uint64_t		index;
	uint32_t		action;
	uint32_t		event_channel_port;
	xen_intr_handle_t	notify_evtchn_handle;
};

static void	notify(struct notify_data *notify, vm_page_t page);

/*-------------------- Grant Allocation Methods  -----------------------------*/

struct gntdev_gref {
	union gref_next_union {
		STAILQ_ENTRY(gntdev_gref) 		list;
		RB_ENTRY(gntdev_gref)	 		tree;
	}			gref_next;
	uint64_t		file_index;
	grant_ref_t		gref_id;
	vm_page_t		page;
	struct notify_data	*notify;
};

static int
gref_cmp(struct gntdev_gref *g1, struct gntdev_gref *g2)
{
	return (g1->file_index - g2->file_index);
}

RB_GENERATE_STATIC(gref_tree_head, gntdev_gref, gref_next.tree, gref_cmp);

/*
 * Traverse over the device-list of to-be-deleted grants allocated, and
 * if all accesses, both local mmaps and foreign maps, to them have ended,
 * destroy them.
 */
static void
gref_list_dtor(struct cleanup_data_struct *cleanup_data)
{
	struct gref_list_head tmp_grefs;
	struct gntdev_gref *gref, *gref_tmp, *gref_previous;

	STAILQ_INIT(&tmp_grefs);
	mtx_lock(&cleanup_data->to_kill_grefs_mtx);
	STAILQ_SWAP(&cleanup_data->to_kill_grefs, &tmp_grefs, gntdev_gref);
	mtx_unlock(&cleanup_data->to_kill_grefs_mtx);

	gref_previous = NULL;
	STAILQ_FOREACH_SAFE(gref, &tmp_grefs, gref_next.list, gref_tmp) {
		if (gref->page && gref->page->object == NULL) {
			if (gref->notify) {
				notify(gref->notify, gref->page);
			}
			if (gref->gref_id != GRANT_REF_INVALID) {
				if (gnttab_query_foreign_access(gref->gref_id))
					continue;
				if (gnttab_end_foreign_access_ref(gref->gref_id)
				    == 0)
					continue;
				gnttab_free_grant_reference(gref->gref_id);
			}
			vm_page_unwire(gref->page, PQ_NONE);
			vm_page_free(gref->page);
			gref->page = NULL;
		}
		if (gref->page == NULL) {
			if (gref_previous == NULL)
				STAILQ_REMOVE_HEAD(&tmp_grefs, gref_next.list);
			else
				STAILQ_REMOVE_AFTER(&tmp_grefs, gref_previous,
				    gref_next.list);
			if (gref->notify)
				free(gref->notify, M_GNTDEV);
			free(gref, M_GNTDEV);
		}
		else
			gref_previous = gref;
	}

	if (!STAILQ_EMPTY(&tmp_grefs)) {
		mtx_lock(&cleanup_data->to_kill_grefs_mtx);
		STAILQ_CONCAT(&cleanup_data->to_kill_grefs, &tmp_grefs);
		mtx_unlock(&cleanup_data->to_kill_grefs_mtx);
	}
}

/*
 * Find count number of contiguous allocated grants for a given userspace
 * program by file-offset (index).
 */
static struct gntdev_gref*
gntdev_find_grefs(struct per_user_data *priv_user,
	uint64_t index, uint32_t count)
{
	struct gntdev_gref find_gref, *gref, *gref_start = NULL;

	find_gref.file_index = index;

	mtx_lock(&priv_user->user_data_lock);
	gref_start = RB_FIND(gref_tree_head, &priv_user->gref_tree, &find_gref);
	for (gref = gref_start; gref != NULL && count > 0; gref =
	    RB_NEXT(gref_tree_head, &priv_user->gref_tree, gref)) {
		if (index != gref->file_index)
			break;
		index += PAGE_SIZE;
		count--;
	}
	mtx_unlock(&priv_user->user_data_lock);

	if (count)
		return (NULL);
	return (gref_start);
}

/*
 * IOCTL_GNTDEV_ALLOC_GREF
 * Allocate required number of wired pages for the request, grant foreign
 * access to the physical frames for these pages, and add details about
 * this allocation to the per user private data, so that these pages can
 * be mmapped by the userspace program.
 */
static int
gntdev_alloc_gref(struct ioctl_gntdev_alloc_gref *arg)
{
	uint32_t i;
	int error, readonly;
	uint64_t file_offset;
	struct gntdev_gref *grefs;
	struct per_user_data *priv_user;

	readonly = !(arg->flags & GNTDEV_ALLOC_FLAG_WRITABLE);

	error = devfs_get_cdevpriv((void**) &priv_user);
	if (error != 0)
		return (EINVAL);

	/* Cleanup grefs and free pages. */
	taskqueue_enqueue(taskqueue_thread, &cleanup_task);

	/* Get file offset for this request. */
	error = get_file_offset(priv_user, arg->count, &file_offset);
	if (error != 0)
		return (error);

	/* Allocate grefs. */
	grefs = malloc(sizeof(*grefs) * arg->count, M_GNTDEV, M_WAITOK);

	for (i = 0; i < arg->count; i++) {
		grefs[i].file_index = file_offset + i * PAGE_SIZE;
		grefs[i].gref_id = GRANT_REF_INVALID;
		grefs[i].notify = NULL;
		grefs[i].page = vm_page_alloc(NULL, 0, VM_ALLOC_NORMAL
			| VM_ALLOC_NOOBJ | VM_ALLOC_WIRED | VM_ALLOC_ZERO);
		if (grefs[i].page == NULL) {
			log(LOG_ERR, "Page allocation failed.");
			error = ENOMEM;
			break;
		}
		if ((grefs[i].page->flags & PG_ZERO) == 0) {
			/*
			 * Zero the allocated page, as we don't want to 
			 * leak our memory to other domains.
			 */
			pmap_zero_page(grefs[i].page);
		}
		grefs[i].page->valid = VM_PAGE_BITS_ALL;

		error = gnttab_grant_foreign_access(arg->domid,
			(VM_PAGE_TO_PHYS(grefs[i].page) >> PAGE_SHIFT),
			readonly, &grefs[i].gref_id);
		if (error != 0) {
			log(LOG_ERR, "Grant Table Hypercall failed.");
			break;
		}
	}

	if (error != 0) {
		/*
		 * If target domain maps the gref (by guessing the gref-id),
		 * then we can't clean it up yet and we have to leave the 
		 * page in place so as to not leak our memory to that domain.
		 * Add it to a global list to be cleaned up later.
		 */
		mtx_lock(&cleanup_data.to_kill_grefs_mtx);
		for (i = 0; i < arg->count; i++)
			STAILQ_INSERT_TAIL(&cleanup_data.to_kill_grefs,
			    &grefs[i], gref_next.list);
		mtx_unlock(&cleanup_data.to_kill_grefs_mtx);
		
		taskqueue_enqueue(taskqueue_thread, &cleanup_task);

		return (error);
	}

	/* Copy the output values. */
	arg->index = file_offset;
	for (i = 0; i < arg->count; i++)
		suword32(&arg->gref_ids[i], grefs[i].gref_id);

	/* Modify the per user private data. */
	mtx_lock(&priv_user->user_data_lock);
	for (i = 0; i < arg->count; i++)
		RB_INSERT(gref_tree_head, &priv_user->gref_tree, &grefs[i]);
	mtx_unlock(&priv_user->user_data_lock);

	return (error);
}

/*
 * IOCTL_GNTDEV_DEALLOC_GREF
 * Remove grant allocation information from the per user private data, so
 * that it can't be mmapped anymore by the userspace program, and add it
 * to the to-be-deleted grants global device-list.
 */
static int
gntdev_dealloc_gref(struct ioctl_gntdev_dealloc_gref *arg)
{
	int error;
	uint32_t count;
	struct gntdev_gref *gref, *gref_tmp;
	struct per_user_data *priv_user;

	error = devfs_get_cdevpriv((void**) &priv_user);
	if (error != 0)
		return (EINVAL);

	gref = gntdev_find_grefs(priv_user, arg->index, arg->count);
	if (gref == NULL) {
		log(LOG_ERR, "Can't find requested grant-refs.");
		return (EINVAL);
	}

	/* Remove the grefs from user private data. */
	count = arg->count;
	mtx_lock(&priv_user->user_data_lock);
	mtx_lock(&cleanup_data.to_kill_grefs_mtx);
	for (; gref != NULL && count > 0; gref = gref_tmp) {
		gref_tmp = RB_NEXT(gref_tree_head, &priv_user->gref_tree, gref);
		RB_REMOVE(gref_tree_head, &priv_user->gref_tree, gref);
		STAILQ_INSERT_TAIL(&cleanup_data.to_kill_grefs, gref,
		    gref_next.list);
		count--;
	}
	mtx_unlock(&cleanup_data.to_kill_grefs_mtx);
	mtx_unlock(&priv_user->user_data_lock);
	
	taskqueue_enqueue(taskqueue_thread, &cleanup_task);
	put_file_offset(priv_user, arg->count, arg->index);

	return (0);
}

/*-------------------- Grant Mapping Methods  --------------------------------*/

struct gntdev_gmap_map {
	vm_object_t	mem;
	struct resource	*pseudo_phys_res;
	int 		pseudo_phys_res_id;
	vm_paddr_t	phys_base_addr;
};

struct gntdev_gmap {
	union gmap_next_union {
		STAILQ_ENTRY(gntdev_gmap)		list;
		RB_ENTRY(gntdev_gmap)			tree;
	}				gmap_next;
	uint64_t			file_index;
	uint32_t			count;
	struct gnttab_map_grant_ref	*grant_map_ops;
	struct gntdev_gmap_map		*map;
	struct notify_data		*notify;
};

static int
gmap_cmp(struct gntdev_gmap *g1, struct gntdev_gmap *g2)
{
	return (g1->file_index - g2->file_index);
}

RB_GENERATE_STATIC(gmap_tree_head, gntdev_gmap, gmap_next.tree, gmap_cmp);

/*
 * Traverse over the device-list of to-be-deleted grant mappings, and if
 * the region is no longer mmapped by anyone, free the memory used to
 * store information about the mapping.
 */
static void
gmap_list_dtor(struct cleanup_data_struct *cleanup_data)
{
	struct gmap_list_head tmp_gmaps;
	struct gntdev_gmap *gmap, *gmap_tmp, *gmap_previous;

	STAILQ_INIT(&tmp_gmaps);
	mtx_lock(&cleanup_data->to_kill_gmaps_mtx);
	STAILQ_SWAP(&cleanup_data->to_kill_gmaps, &tmp_gmaps, gntdev_gmap);
	mtx_unlock(&cleanup_data->to_kill_gmaps_mtx);

	gmap_previous = NULL;
	STAILQ_FOREACH_SAFE(gmap, &tmp_gmaps, gmap_next.list, gmap_tmp) {
		if (gmap->map == NULL) {
			if (gmap_previous == NULL)
				STAILQ_REMOVE_HEAD(&tmp_gmaps, gmap_next.list);
			else
				STAILQ_REMOVE_AFTER(&tmp_gmaps, gmap_previous,
				    gmap_next.list);

			if (gmap->notify)
				free(gmap->notify, M_GNTDEV);
			free(gmap->grant_map_ops, M_GNTDEV);
			free(gmap, M_GNTDEV);
		}
		else
			gmap_previous = gmap;
	}

	if (!STAILQ_EMPTY(&tmp_gmaps)) {
		mtx_lock(&cleanup_data->to_kill_gmaps_mtx);
		STAILQ_CONCAT(&cleanup_data->to_kill_gmaps, &tmp_gmaps);
		mtx_unlock(&cleanup_data->to_kill_gmaps_mtx);
	}
}

/*
 * Find mapped grants for a given userspace program, by file-offset (index)
 * and count, as supplied during the map-ioctl.
 */
static struct gntdev_gmap*
gntdev_find_gmap(struct per_user_data *priv_user,
	uint64_t index, uint32_t count)
{
	struct gntdev_gmap find_gmap, *gmap;

	find_gmap.file_index = index;

	mtx_lock(&priv_user->user_data_lock);
	gmap = RB_FIND(gmap_tree_head, &priv_user->gmap_tree, &find_gmap);
	mtx_unlock(&priv_user->user_data_lock);

	if (gmap != NULL && gmap->count == count)
		return (gmap);
	return (NULL);
}

/*
 * Remove the pages from the mgtdevice pager, call the unmap hypercall,
 * free the xenmem resource. This function is called during the
 * destruction of the mgtdevice pager, which happens when all mmaps to
 * it have been removed, and the unmap-ioctl has been performed.
 */
static int
notify_unmap_cleanup(struct gntdev_gmap *gmap)
{
	uint32_t i;
	int error, count;
	vm_page_t m;
	struct gnttab_unmap_grant_ref *unmap_ops;
	
	unmap_ops = malloc(sizeof(struct gnttab_unmap_grant_ref) * gmap->count,
			M_GNTDEV, M_WAITOK);
	
	/* Enumerate freeable maps. */
	count = 0;
	for (i = 0; i < gmap->count; i++) {
		if (gmap->grant_map_ops[i].handle != -1) {
			unmap_ops[count].handle = gmap->grant_map_ops[i].handle;
			unmap_ops[count].host_addr =
				gmap->grant_map_ops[i].host_addr;
			unmap_ops[count].dev_bus_addr = 0;
			count++;
		}
	}
	
	/* Perform notification. */
	if (count > 0 && gmap->notify) {
		vm_page_t page;
		uint64_t page_offset;

		page_offset = gmap->notify->index - gmap->file_index;
		page = PHYS_TO_VM_PAGE(gmap->map->phys_base_addr + page_offset);
		notify(gmap->notify, page);
	}
	
	/* Free the pages. */
	VM_OBJECT_WLOCK(gmap->map->mem);
retry:
	for (i = 0; i < gmap->count; i++) {
		m = vm_page_lookup(gmap->map->mem, i);
		if (m == NULL)
			continue;
		if (vm_page_sleep_if_busy(m, "pcmdum"))
			goto retry;
		cdev_pager_free_page(gmap->map->mem, m);
	}
	VM_OBJECT_WUNLOCK(gmap->map->mem);
	
	/* Perform unmap hypercall. */
	error = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
	    unmap_ops, count);
	
	for (i = 0; i < gmap->count; i++) {
		gmap->grant_map_ops[i].handle = -1;
		gmap->grant_map_ops[i].host_addr = 0;
	}
	
	if (gmap->map) {
		error = xenmem_free(gntdev_dev, gmap->map->pseudo_phys_res_id,
		    gmap->map->pseudo_phys_res);
		KASSERT(error == 0,
		    ("Unable to release memory resource: %d", error));

		free(gmap->map, M_GNTDEV);
		gmap->map = NULL;
	}
	
	free(unmap_ops, M_GNTDEV);
	
	return (error);
}

/*
 * IOCTL_GNTDEV_MAP_GRANT_REF
 * Populate structures for mapping the grant reference in the per user
 * private data. Actual resource allocation and map hypercall is performed
 * during the mmap.
 */
static int
gntdev_map_grant_ref(struct ioctl_gntdev_map_grant_ref *arg)
{
	uint32_t i;
	int error;
	struct gntdev_gmap *gmap;
	struct per_user_data *priv_user;

	error = devfs_get_cdevpriv((void**) &priv_user);
	if (error != 0)
		return (EINVAL);

	gmap = malloc(sizeof(*gmap), M_GNTDEV, M_WAITOK | M_ZERO);
	gmap->count = arg->count;
	gmap->grant_map_ops =
	    malloc(sizeof(struct gnttab_map_grant_ref) * arg->count,
	        M_GNTDEV, M_WAITOK | M_ZERO);

	for (i = 0; i < arg->count; i++) {
		struct ioctl_gntdev_grant_ref ref;

		error = copyin(&arg->refs[i], &ref, sizeof(ref));
		if (error != 0) {
			free(gmap->grant_map_ops, M_GNTDEV);
			free(gmap, M_GNTDEV);
			return (error);
		}
		gmap->grant_map_ops[i].dom = ref.domid;
		gmap->grant_map_ops[i].ref = ref.ref;
		gmap->grant_map_ops[i].handle = -1;
		gmap->grant_map_ops[i].flags = GNTMAP_host_map;
	}

	error = get_file_offset(priv_user, arg->count, &gmap->file_index);
	if (error != 0) {
		free(gmap->grant_map_ops, M_GNTDEV);
		free(gmap, M_GNTDEV);
		return (error);
	}

	mtx_lock(&priv_user->user_data_lock);
	RB_INSERT(gmap_tree_head, &priv_user->gmap_tree, gmap);
	mtx_unlock(&priv_user->user_data_lock);

	arg->index = gmap->file_index;

	return (error);
}

/*
 * IOCTL_GNTDEV_UNMAP_GRANT_REF
 * Remove the map information from the per user private data and add it
 * to the global device-list of mappings to be deleted. A reference to
 * the mgtdevice pager is also decreased, the reason for which is
 * explained in mmap_gmap().
 */
static int
gntdev_unmap_grant_ref(struct ioctl_gntdev_unmap_grant_ref *arg)
{
	int error;
	struct gntdev_gmap *gmap;
	struct per_user_data *priv_user;

	error = devfs_get_cdevpriv((void**) &priv_user);
	if (error != 0)
		return (EINVAL);

	gmap = gntdev_find_gmap(priv_user, arg->index, arg->count);
	if (gmap == NULL) {
		log(LOG_ERR, "Can't find requested grant-map.");
		return (EINVAL);
	}

	mtx_lock(&priv_user->user_data_lock);
	mtx_lock(&cleanup_data.to_kill_gmaps_mtx);
	RB_REMOVE(gmap_tree_head, &priv_user->gmap_tree, gmap);
	STAILQ_INSERT_TAIL(&cleanup_data.to_kill_gmaps, gmap, gmap_next.list);
	mtx_unlock(&cleanup_data.to_kill_gmaps_mtx);
	mtx_unlock(&priv_user->user_data_lock);
	
	if (gmap->map)
		vm_object_deallocate(gmap->map->mem);

	taskqueue_enqueue(taskqueue_thread, &cleanup_task);
	put_file_offset(priv_user, arg->count, arg->index);
	
	return (0);
}

/*
 * IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR
 * Get file-offset and count for a given mapping, from the virtual address
 * where the mapping is mmapped.
 * Please note, this only works for grants mapped by this domain, and not
 * grants allocated. Count doesn't make much sense in reference to grants
 * allocated. Also, because this function is present in the linux gntdev
 * device, but not in the linux gntalloc one, most userspace code only use
 * it for mapped grants.
 */
static int
gntdev_get_offset_for_vaddr(struct ioctl_gntdev_get_offset_for_vaddr *arg,
	struct thread *td)
{
	int error;
	vm_map_t map;
	vm_map_entry_t entry;
	vm_object_t mem;
	vm_pindex_t pindex;
	vm_prot_t prot;
	boolean_t wired;
	struct gntdev_gmap *gmap;
	int rc;

	map = &td->td_proc->p_vmspace->vm_map;
	error = vm_map_lookup(&map, arg->vaddr, VM_PROT_NONE, &entry,
		    &mem, &pindex, &prot, &wired);
	if (error != KERN_SUCCESS)
		return (EINVAL);

	if ((mem->type != OBJT_MGTDEVICE) ||
	    (mem->un_pager.devp.ops != &gntdev_gmap_pg_ops)) {
		rc = EINVAL;
		goto out;
	}

	gmap = mem->handle;
	if (gmap == NULL ||
	    (entry->end - entry->start) != (gmap->count * PAGE_SIZE)) {
		rc = EINVAL;
		goto out;
	}

	arg->count = gmap->count;
	arg->offset = gmap->file_index;
	rc = 0;

out:
	vm_map_lookup_done(map, entry);
	return (rc);
}

/*-------------------- Grant Mapping Pager  ----------------------------------*/

static int
gntdev_gmap_pg_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{

	return (0);
}

static void
gntdev_gmap_pg_dtor(void *handle)
{

	notify_unmap_cleanup((struct gntdev_gmap *)handle);
}

static int
gntdev_gmap_pg_fault(vm_object_t object, vm_ooffset_t offset, int prot,
    vm_page_t *mres)
{
	struct gntdev_gmap *gmap = object->handle;
	vm_pindex_t pidx, ridx;
	vm_page_t page, oldm;
	vm_ooffset_t relative_offset;

	if (gmap->map == NULL)
		return (VM_PAGER_FAIL);

	relative_offset = offset - gmap->file_index;

	pidx = OFF_TO_IDX(offset);
	ridx = OFF_TO_IDX(relative_offset);
	if (ridx >= gmap->count ||
	    gmap->grant_map_ops[ridx].status != GNTST_okay)
		return (VM_PAGER_FAIL);

	page = PHYS_TO_VM_PAGE(gmap->map->phys_base_addr + relative_offset);
	if (page == NULL)
		return (VM_PAGER_FAIL);

	KASSERT((page->flags & PG_FICTITIOUS) != 0,
	    ("not fictitious %p", page));
	KASSERT(page->wire_count == 1, ("wire_count not 1 %p", page));
	KASSERT(vm_page_busied(page) == 0, ("page %p is busy", page));

	if (*mres != NULL) {
		oldm = *mres;
		vm_page_lock(oldm);
		vm_page_free(oldm);
		vm_page_unlock(oldm);
		*mres = NULL;
	}

	vm_page_insert(page, object, pidx);
	page->valid = VM_PAGE_BITS_ALL;
	vm_page_xbusy(page);
	*mres = page;
	return (VM_PAGER_OK);
}

/*------------------ Grant Table Methods  ------------------------------------*/

static void
notify(struct notify_data *notify, vm_page_t page)
{
	if (notify->action & UNMAP_NOTIFY_CLEAR_BYTE) {
		uint8_t *mem;
		uint64_t offset;

		offset = notify->index & PAGE_MASK;
		mem = (uint8_t *)pmap_quick_enter_page(page);
		mem[offset] = 0;
		pmap_quick_remove_page((vm_offset_t)mem);
	}
	if (notify->action & UNMAP_NOTIFY_SEND_EVENT) {
		xen_intr_signal(notify->notify_evtchn_handle);
		xen_intr_unbind(&notify->notify_evtchn_handle);
	}
	notify->action = 0;
}

/*
 * Helper to copy new arguments from the notify ioctl into
 * the existing notify data.
 */
static int
copy_notify_helper(struct notify_data *destination,
    struct ioctl_gntdev_unmap_notify *source)
{
	xen_intr_handle_t handlep = NULL;

	/*
	 * "Get" before "Put"ting previous reference, as we might be
	 * holding the last reference to the event channel port.
	 */
	if (source->action & UNMAP_NOTIFY_SEND_EVENT)
		if (xen_intr_get_evtchn_from_port(source->event_channel_port,
		    &handlep) != 0)
			return (EINVAL);

	if (destination->action & UNMAP_NOTIFY_SEND_EVENT)
		xen_intr_unbind(&destination->notify_evtchn_handle);

	destination->action = source->action;
	destination->event_channel_port = source->event_channel_port;
	destination->index = source->index;
	destination->notify_evtchn_handle = handlep;

	return (0);
}

/*
 * IOCTL_GNTDEV_SET_UNMAP_NOTIFY
 * Set unmap notification inside the appropriate grant. It sends a
 * notification when the grant is completely munmapped by this domain
 * and ready for destruction.
 */
static int
gntdev_set_unmap_notify(struct ioctl_gntdev_unmap_notify *arg)
{
	int error;
	uint64_t index;
	struct per_user_data *priv_user;
	struct gntdev_gref *gref = NULL;
	struct gntdev_gmap *gmap;

	error = devfs_get_cdevpriv((void**) &priv_user);
	if (error != 0)
		return (EINVAL);

	if (arg->action & ~(UNMAP_NOTIFY_CLEAR_BYTE|UNMAP_NOTIFY_SEND_EVENT))
		return (EINVAL);

	index = arg->index & ~PAGE_MASK;
	gref = gntdev_find_grefs(priv_user, index, 1);
	if (gref) {
		if (gref->notify == NULL)
			gref->notify = malloc(sizeof(*arg), M_GNTDEV,
			    M_WAITOK | M_ZERO);
		return (copy_notify_helper(gref->notify, arg));
	}

	error = EINVAL;
	mtx_lock(&priv_user->user_data_lock);
	RB_FOREACH(gmap, gmap_tree_head, &priv_user->gmap_tree) {
		if (arg->index >= gmap->file_index &&
		    arg->index < gmap->file_index + gmap->count * PAGE_SIZE) {
			if (gmap->notify == NULL)
				gmap->notify = malloc(sizeof(*arg), M_GNTDEV,
				    M_WAITOK | M_ZERO);
			error = copy_notify_helper(gmap->notify, arg);
			break;
		}
	}
	mtx_unlock(&priv_user->user_data_lock);

	return (error);
}

/*------------------ Gntdev Char Device Methods  -----------------------------*/

static void
cleanup_function(void *arg, __unused int pending)
{

	gref_list_dtor((struct cleanup_data_struct *) arg);
	gmap_list_dtor((struct cleanup_data_struct *) arg);
}

static void
per_user_data_dtor(void *arg)
{
	struct gntdev_gref *gref, *gref_tmp;
	struct gntdev_gmap *gmap, *gmap_tmp;
	struct file_offset_struct *offset, *offset_tmp;
	struct per_user_data *priv_user;

	priv_user = (struct per_user_data *) arg;

	mtx_lock(&priv_user->user_data_lock);

	mtx_lock(&cleanup_data.to_kill_grefs_mtx);
	RB_FOREACH_SAFE(gref, gref_tree_head, &priv_user->gref_tree, gref_tmp) {
		RB_REMOVE(gref_tree_head, &priv_user->gref_tree, gref);
		STAILQ_INSERT_TAIL(&cleanup_data.to_kill_grefs, gref,
		    gref_next.list);
	}
	mtx_unlock(&cleanup_data.to_kill_grefs_mtx);

	mtx_lock(&cleanup_data.to_kill_gmaps_mtx);
	RB_FOREACH_SAFE(gmap, gmap_tree_head, &priv_user->gmap_tree, gmap_tmp) {
		RB_REMOVE(gmap_tree_head, &priv_user->gmap_tree, gmap);
		STAILQ_INSERT_TAIL(&cleanup_data.to_kill_gmaps, gmap,
		    gmap_next.list);
		if (gmap->map)
			vm_object_deallocate(gmap->map->mem);
	}
	mtx_unlock(&cleanup_data.to_kill_gmaps_mtx);

	RB_FOREACH_SAFE(offset, file_offset_head, &priv_user->file_offset,
	    offset_tmp) {
		RB_REMOVE(file_offset_head, &priv_user->file_offset, offset);
		free(offset, M_GNTDEV);
	}

	mtx_unlock(&priv_user->user_data_lock);

	taskqueue_enqueue(taskqueue_thread, &cleanup_task);

	mtx_destroy(&priv_user->user_data_lock);
	free(priv_user, M_GNTDEV);
}

static int
gntdev_open(struct cdev *dev, int flag, int otyp, struct thread *td)
{
	int error;
	struct per_user_data *priv_user;
	struct file_offset_struct *offset;

	priv_user = malloc(sizeof(*priv_user), M_GNTDEV, M_WAITOK | M_ZERO);
	RB_INIT(&priv_user->gref_tree);
	RB_INIT(&priv_user->gmap_tree);
	RB_INIT(&priv_user->file_offset);
	offset = malloc(sizeof(*offset), M_GNTDEV, M_WAITOK | M_ZERO);
	offset->file_offset = 0;
	offset->count = MAX_OFFSET_COUNT;
	RB_INSERT(file_offset_head, &priv_user->file_offset, offset);
	mtx_init(&priv_user->user_data_lock,
	    "per user data mutex", NULL, MTX_DEF);

	error = devfs_set_cdevpriv(priv_user, per_user_data_dtor);
	if (error != 0)
		per_user_data_dtor(priv_user);

	return (error);
}

static int
gntdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
	int fflag, struct thread *td)
{
	int error;

	switch (cmd) {
	case IOCTL_GNTDEV_SET_UNMAP_NOTIFY:
		error = gntdev_set_unmap_notify(
		    (struct ioctl_gntdev_unmap_notify*) data);
		break;
	case IOCTL_GNTDEV_ALLOC_GREF:
		error = gntdev_alloc_gref(
		    (struct ioctl_gntdev_alloc_gref*) data);
		break;
	case IOCTL_GNTDEV_DEALLOC_GREF:
		error = gntdev_dealloc_gref(
		    (struct ioctl_gntdev_dealloc_gref*) data);
		break;
	case IOCTL_GNTDEV_MAP_GRANT_REF:
		error = gntdev_map_grant_ref(
		    (struct ioctl_gntdev_map_grant_ref*) data);
		break;
	case IOCTL_GNTDEV_UNMAP_GRANT_REF:
		error = gntdev_unmap_grant_ref(
		    (struct ioctl_gntdev_unmap_grant_ref*) data);
		break;
	case IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR:
		error = gntdev_get_offset_for_vaddr(
		    (struct ioctl_gntdev_get_offset_for_vaddr*) data, td);
		break;
	default:
		error = ENOSYS;
		break;
	}

	return (error);
}

/*
 * MMAP an allocated grant into user memory.
 * Please note, that the grants must not already be mmapped, otherwise
 * this function will fail.
 */
static int
mmap_gref(struct per_user_data *priv_user, struct gntdev_gref *gref_start,
    uint32_t count, vm_size_t size, struct vm_object **object)
{
	vm_object_t mem_obj;
	struct gntdev_gref *gref;

	mem_obj = vm_object_allocate(OBJT_PHYS, size);
	if (mem_obj == NULL)
		return (ENOMEM);

	mtx_lock(&priv_user->user_data_lock);
	VM_OBJECT_WLOCK(mem_obj);
	for (gref = gref_start; gref != NULL && count > 0; gref =
	    RB_NEXT(gref_tree_head, &priv_user->gref_tree, gref)) {
		if (gref->page->object)
			break;

		vm_page_insert(gref->page, mem_obj,
		    OFF_TO_IDX(gref->file_index));

		count--;
	}
	VM_OBJECT_WUNLOCK(mem_obj);
	mtx_unlock(&priv_user->user_data_lock);

	if (count) {
		vm_object_deallocate(mem_obj);
		return (EINVAL);
	}

	*object = mem_obj;

	return (0);

}

/*
 * MMAP a mapped grant into user memory.
 */
static int
mmap_gmap(struct per_user_data *priv_user, struct gntdev_gmap *gmap_start,
    vm_ooffset_t *offset, vm_size_t size, struct vm_object **object, int nprot)
{
	uint32_t i;
	int error;

	/*
	 * The grant map hypercall might already be done.
	 * If that is the case, increase a reference to the
	 * vm object and return the already allocated object.
	 */
	if (gmap_start->map) {
		vm_object_reference(gmap_start->map->mem);
		*object = gmap_start->map->mem;
		return (0);
	}

	gmap_start->map = malloc(sizeof(*(gmap_start->map)), M_GNTDEV,
	    M_WAITOK | M_ZERO);

	/* Allocate the xen pseudo physical memory resource. */
	gmap_start->map->pseudo_phys_res_id = 0;
	gmap_start->map->pseudo_phys_res = xenmem_alloc(gntdev_dev,
	    &gmap_start->map->pseudo_phys_res_id, size);
	if (gmap_start->map->pseudo_phys_res == NULL) {
		free(gmap_start->map, M_GNTDEV);
		gmap_start->map = NULL;
		return (ENOMEM);
	}
	gmap_start->map->phys_base_addr =
	    rman_get_start(gmap_start->map->pseudo_phys_res);

	/* Allocate the mgtdevice pager. */
	gmap_start->map->mem = cdev_pager_allocate(gmap_start, OBJT_MGTDEVICE,
	    &gntdev_gmap_pg_ops, size, nprot, *offset, NULL);
	if (gmap_start->map->mem == NULL) {
		xenmem_free(gntdev_dev, gmap_start->map->pseudo_phys_res_id,
		    gmap_start->map->pseudo_phys_res);
		free(gmap_start->map, M_GNTDEV);
		gmap_start->map = NULL;
		return (ENOMEM);
	}

	for (i = 0; i < gmap_start->count; i++) {
		gmap_start->grant_map_ops[i].host_addr =
		    gmap_start->map->phys_base_addr + i * PAGE_SIZE;

		if ((nprot & PROT_WRITE) == 0)
			gmap_start->grant_map_ops[i].flags |= GNTMAP_readonly;
	}
	/* Make the MAP hypercall. */
	error = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
	    gmap_start->grant_map_ops, gmap_start->count);
	if (error != 0) {
		/*
		 * Deallocate pager.
		 * Pager deallocation will automatically take care of
		 * xenmem deallocation, etc.
		 */
		vm_object_deallocate(gmap_start->map->mem);

		return (EINVAL);
	}

	/* Retry EAGAIN maps. */
	for (i = 0; i < gmap_start->count; i++) {
		int delay = 1;
		while (delay < 256 &&
		    gmap_start->grant_map_ops[i].status == GNTST_eagain) {
			HYPERVISOR_grant_table_op( GNTTABOP_map_grant_ref,
			    &gmap_start->grant_map_ops[i], 1);
			pause(("gntmap"), delay * SBT_1MS);
			delay++;
		}
		if (gmap_start->grant_map_ops[i].status == GNTST_eagain)
			gmap_start->grant_map_ops[i].status = GNTST_bad_page;

		if (gmap_start->grant_map_ops[i].status != GNTST_okay) {
			/*
			 * Deallocate pager.
			 * Pager deallocation will automatically take care of
			 * xenmem deallocation, notification, unmap hypercall,
			 * etc.
			 */
			vm_object_deallocate(gmap_start->map->mem);

			return (EINVAL);
		}
	}

	/*
	 * Add a reference to the vm object. We do not want
	 * the vm object to be deleted when all the mmaps are
	 * unmapped, because it may be re-mmapped. Instead,
	 * we want the object to be deleted, when along with
	 * munmaps, we have also processed the unmap-ioctl.
	 */
	vm_object_reference(gmap_start->map->mem);

	*object = gmap_start->map->mem;

	return (0);
}

static int
gntdev_mmap_single(struct cdev *cdev, vm_ooffset_t *offset, vm_size_t size,
    struct vm_object **object, int nprot)
{
	int error;
	uint32_t count;
	struct gntdev_gref *gref_start;
	struct gntdev_gmap *gmap_start;
	struct per_user_data *priv_user;

	error = devfs_get_cdevpriv((void**) &priv_user);
	if (error != 0)
		return (EINVAL);

	count = OFF_TO_IDX(size);

	gref_start = gntdev_find_grefs(priv_user, *offset, count);
	if (gref_start) {
		error = mmap_gref(priv_user, gref_start, count, size, object);
		return (error);
	}

	gmap_start = gntdev_find_gmap(priv_user, *offset, count);
	if (gmap_start) {
		error = mmap_gmap(priv_user, gmap_start, offset, size, object,
		    nprot);
		return (error);
	}

	return (EINVAL);
}

/*------------------ Private Device Attachment Functions  --------------------*/
static void
gntdev_identify(driver_t *driver, device_t parent)
{

	KASSERT((xen_domain()),
	    ("Trying to attach gntdev device on non Xen domain"));

	if (BUS_ADD_CHILD(parent, 0, "gntdev", 0) == NULL)
		panic("unable to attach gntdev user-space device");
}

static int
gntdev_probe(device_t dev)
{

	gntdev_dev = dev;
	device_set_desc(dev, "Xen grant-table user-space device");
	return (BUS_PROBE_NOWILDCARD);
}

static int
gntdev_attach(device_t dev)
{

	make_dev_credf(MAKEDEV_ETERNAL, &gntdev_devsw, 0, NULL, UID_ROOT,
	    GID_WHEEL, 0600, "xen/gntdev");
	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t gntdev_methods[] = {
	DEVMETHOD(device_identify, gntdev_identify),
	DEVMETHOD(device_probe, gntdev_probe),
	DEVMETHOD(device_attach, gntdev_attach),
	DEVMETHOD_END
};

static driver_t gntdev_driver = {
	"gntdev",
	gntdev_methods,
	0,
};

devclass_t gntdev_devclass;

DRIVER_MODULE(gntdev, xenpv, gntdev_driver, gntdev_devclass, 0, 0);
MODULE_DEPEND(gntdev, xenpv, 1, 1, 1);
