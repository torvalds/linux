/*	$OpenBSD: drm_linux.c,v 1.126 2025/06/13 07:01:37 jsg Exp $	*/
/*
 * Copyright (c) 2013 Jonathan Gray <jsg@openbsd.org>
 * Copyright (c) 2015, 2016 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/event.h>
#include <sys/filedesc.h>
#include <sys/kthread.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/proc.h>
#include <sys/pool.h>
#include <sys/fcntl.h>

#include <dev/pci/ppbreg.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <acpi/video.h>

#include <linux/dma-buf.h>
#include <linux/mod_devicetable.h>
#include <linux/acpi.h>
#include <linux/pagevec.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/scatterlist.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/notifier.h>
#include <linux/backlight.h>
#include <linux/shrinker.h>
#include <linux/fb.h>
#include <linux/xarray.h>
#include <linux/interval_tree.h>
#include <linux/kthread.h>
#include <linux/processor.h>
#include <linux/sync_file.h>
#include <linux/suspend.h>
#include <linux/slab.h>

#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_print.h>
#include <drm/drm_drv.h>

#if defined(__amd64__) || defined(__i386__)
#include "bios.h"
#endif

/* allowed to sleep */
void
tasklet_unlock_wait(struct tasklet_struct *ts)
{
	while (test_bit(TASKLET_STATE_RUN, &ts->state))
		cpu_relax();
}

/* must not sleep */
void
tasklet_unlock_spin_wait(struct tasklet_struct *ts)
{
	while (test_bit(TASKLET_STATE_RUN, &ts->state))
		cpu_relax();
}

void
tasklet_run(void *arg)
{
	struct tasklet_struct *ts = arg;

	clear_bit(TASKLET_STATE_SCHED, &ts->state);
	if (tasklet_trylock(ts)) {
		if (!atomic_read(&ts->count)) {
			if (ts->use_callback)
				ts->callback(ts);
			else
				ts->func(ts->data);
		}
		tasklet_unlock(ts);
	}
}

/* 32 bit powerpc lacks 64 bit atomics */
#if defined(__powerpc__) && !defined(__powerpc64__)
struct mutex atomic64_mtx = MUTEX_INITIALIZER(IPL_HIGH);
#endif

void
set_current_state(int state)
{
	int prio = state;

	KASSERT(state != TASK_RUNNING);
	/* check if already on the sleep list */
	if (curproc->p_wchan != NULL)
		return;
	sleep_setup(curproc, prio, "schto");
}

void
__set_current_state(int state)
{
	struct proc *p = curproc;

	KASSERT(state == TASK_RUNNING);
	SCHED_LOCK();
	unsleep(p);
	p->p_stat = SONPROC;
	atomic_clearbits_int(&p->p_flag, P_INSCHED);
	SCHED_UNLOCK();
}

void
schedule(void)
{
	schedule_timeout(MAX_SCHEDULE_TIMEOUT);
}

long
schedule_timeout(long timeout)
{
	unsigned long deadline;
	uint64_t nsecs = INFSLP;

	KASSERT(!cold);

	if (timeout != MAX_SCHEDULE_TIMEOUT) {
		deadline = jiffies + timeout;
		nsecs = jiffies_to_nsecs(timeout);
	}
	sleep_finish(nsecs, timeout > 0);
	if (timeout != MAX_SCHEDULE_TIMEOUT)
		timeout = deadline - jiffies;

	return timeout > 0 ? timeout : 0;
}

long
schedule_timeout_uninterruptible(long timeout)
{
	tsleep(curproc, PWAIT, "schtou", timeout);
	return 0;
}

int
wake_up_process(struct proc *p)
{
	int rv;

	SCHED_LOCK();
	rv = wakeup_proc(p);
	SCHED_UNLOCK();
	return rv;
}

int
autoremove_wake_function(struct wait_queue_entry *wqe, unsigned int mode,
    int sync, void *key)
{
	if (wqe->private)
		wake_up_process(wqe->private);
	list_del_init(&wqe->entry);
	return 0;
}

void
prepare_to_wait(wait_queue_head_t *wqh, wait_queue_entry_t *wqe, int state)
{
	mtx_enter(&wqh->lock);
	if (list_empty(&wqe->entry))
		__add_wait_queue(wqh, wqe);
	mtx_leave(&wqh->lock);

	set_current_state(state);
}

void
finish_wait(wait_queue_head_t *wqh, wait_queue_entry_t *wqe)
{
	__set_current_state(TASK_RUNNING);

	mtx_enter(&wqh->lock);
	if (!list_empty(&wqe->entry))
		list_del_init(&wqe->entry);
	mtx_leave(&wqh->lock);
}

void
flush_workqueue(struct workqueue_struct *wq)
{
	if (cold)
		return;

	if (wq)
		taskq_barrier((struct taskq *)wq);
}

bool
flush_work(struct work_struct *work)
{
	if (cold)
		return false;

	if (work->tq)
		taskq_barrier(work->tq);
	return false;
}

bool
flush_delayed_work(struct delayed_work *dwork)
{
	bool ret = false;

	if (cold)
		return false;

	while (timeout_pending(&dwork->to)) {
		tsleep(dwork, PWAIT, "fldwto", 1);
		ret = true;
	}

	if (dwork->tq)
		taskq_barrier(dwork->tq);
	return ret;
}

struct kthread {
	int (*func)(void *);
	void *data;
	struct proc *proc;
	volatile u_int flags;
#define KTHREAD_SHOULDSTOP	0x0000001
#define KTHREAD_STOPPED		0x0000002
#define KTHREAD_SHOULDPARK	0x0000004
#define KTHREAD_PARKED		0x0000008
	LIST_ENTRY(kthread) next;
};

LIST_HEAD(, kthread) kthread_list = LIST_HEAD_INITIALIZER(kthread_list);

void
kthread_func(void *arg)
{
	struct kthread *thread = arg;
	int ret;

	ret = thread->func(thread->data);
	thread->flags |= KTHREAD_STOPPED;
	wakeup(thread);
	kthread_exit(ret);
}

struct proc *
kthread_run(int (*func)(void *), void *data, const char *name)
{
	struct kthread *thread;

	thread = malloc(sizeof(*thread), M_DRM, M_WAITOK);
	thread->func = func;
	thread->data = data;
	thread->flags = 0;
	
	if (kthread_create(kthread_func, thread, &thread->proc, name)) {
		free(thread, M_DRM, sizeof(*thread));
		return ERR_PTR(-ENOMEM);
	}

	LIST_INSERT_HEAD(&kthread_list, thread, next);
	return thread->proc;
}

struct kthread_worker *
kthread_create_worker(unsigned int flags, const char *fmt, ...)
{
	char name[MAXCOMLEN+1];
	va_list ap;

	struct kthread_worker *w = malloc(sizeof(*w), M_DRM, M_WAITOK);
	va_start(ap, fmt);
	vsnprintf(name, sizeof(name), fmt, ap);
	va_end(ap);
	w->tq = taskq_create(name, 1, IPL_HIGH, 0);
	
	return w;
}

void
kthread_destroy_worker(struct kthread_worker *worker)
{
	taskq_destroy(worker->tq);
	free(worker, M_DRM, sizeof(*worker));
	
}

void
kthread_init_work(struct kthread_work *work, void (*func)(struct kthread_work *))
{
	work->tq = NULL;
	task_set(&work->task, (void (*)(void *))func, work);
}

bool
kthread_queue_work(struct kthread_worker *worker, struct kthread_work *work)
{
	work->tq = worker->tq;
	return task_add(work->tq, &work->task);
}

bool
kthread_cancel_work_sync(struct kthread_work *work)
{
	return task_del(work->tq, &work->task);
}

void
kthread_flush_work(struct kthread_work *work)
{
	if (cold)
		return;

	if (work->tq)
		taskq_barrier(work->tq);
}

void
kthread_flush_worker(struct kthread_worker *worker)
{
	if (cold)
		return;

	if (worker->tq)
		taskq_barrier(worker->tq);
}

struct kthread *
kthread_lookup(struct proc *p)
{
	struct kthread *thread;

	LIST_FOREACH(thread, &kthread_list, next) {
		if (thread->proc == p)
			break;
	}
	KASSERT(thread);

	return thread;
}

int
kthread_should_park(void)
{
	struct kthread *thread = kthread_lookup(curproc);
	return (thread->flags & KTHREAD_SHOULDPARK);
}

void
kthread_parkme(void)
{
	struct kthread *thread = kthread_lookup(curproc);

	while (thread->flags & KTHREAD_SHOULDPARK) {
		thread->flags |= KTHREAD_PARKED;
		wakeup(thread);
		tsleep_nsec(thread, PPAUSE, "parkme", INFSLP);
		thread->flags &= ~KTHREAD_PARKED;
	}
}

void
kthread_park(struct proc *p)
{
	struct kthread *thread = kthread_lookup(p);

	while ((thread->flags & KTHREAD_PARKED) == 0) {
		thread->flags |= KTHREAD_SHOULDPARK;
		wake_up_process(thread->proc);
		tsleep_nsec(thread, PPAUSE, "park", INFSLP);
	}
}

void
kthread_unpark(struct proc *p)
{
	struct kthread *thread = kthread_lookup(p);

	thread->flags &= ~KTHREAD_SHOULDPARK;
	wakeup(thread);
}

int
kthread_should_stop(void)
{
	struct kthread *thread = kthread_lookup(curproc);
	return (thread->flags & KTHREAD_SHOULDSTOP);
}

void
kthread_stop(struct proc *p)
{
	struct kthread *thread = kthread_lookup(p);

	while ((thread->flags & KTHREAD_STOPPED) == 0) {
		thread->flags |= KTHREAD_SHOULDSTOP;
		kthread_unpark(p);
		wake_up_process(thread->proc);
		tsleep_nsec(thread, PPAUSE, "stop", INFSLP);
	}
	LIST_REMOVE(thread, next);
	free(thread, M_DRM, sizeof(*thread));
}

#if NBIOS > 0
extern char smbios_board_vendor[];
extern char smbios_board_prod[];
extern char smbios_board_serial[];
#endif

bool
dmi_match(int slot, const char *str)
{
	switch (slot) {
	case DMI_SYS_VENDOR:
		if (hw_vendor != NULL &&
		    !strcmp(hw_vendor, str))
			return true;
		break;
	case DMI_PRODUCT_NAME:
		if (hw_prod != NULL &&
		    !strcmp(hw_prod, str))
			return true;
		break;
	case DMI_PRODUCT_VERSION:
		if (hw_ver != NULL &&
		    !strcmp(hw_ver, str))
			return true;
		break;
#if NBIOS > 0
	case DMI_BOARD_VENDOR:
		if (strcmp(smbios_board_vendor, str) == 0)
			return true;
		break;
	case DMI_BOARD_NAME:
		if (strcmp(smbios_board_prod, str) == 0)
			return true;
		break;
	case DMI_BOARD_SERIAL:
		if (strcmp(smbios_board_serial, str) == 0)
			return true;
		break;
#else
	case DMI_BOARD_VENDOR:
		if (hw_vendor != NULL &&
		    !strcmp(hw_vendor, str))
			return true;
		break;
	case DMI_BOARD_NAME:
		if (hw_prod != NULL &&
		    !strcmp(hw_prod, str))
			return true;
		break;
#endif
	case DMI_NONE:
	default:
		return false;
	}

	return false;
}

static bool
dmi_found(const struct dmi_system_id *dsi)
{
	int i, slot;

	for (i = 0; i < nitems(dsi->matches); i++) {
		slot = dsi->matches[i].slot;
		if (slot == DMI_NONE)
			break;
		if (!dmi_match(slot, dsi->matches[i].substr))
			return false;
	}

	return true;
}

const struct dmi_system_id *
dmi_first_match(const struct dmi_system_id *sysid)
{
	const struct dmi_system_id *dsi;

	for (dsi = sysid; dsi->matches[0].slot != 0 ; dsi++) {
		if (dmi_found(dsi))
			return dsi;
	}

	return NULL;
}

#if NBIOS > 0
extern char smbios_bios_date[];
extern char smbios_bios_version[];
#endif

const char *
dmi_get_system_info(int slot)
{
#if NBIOS > 0
	switch (slot) {
	case DMI_BIOS_DATE:
		return smbios_bios_date;
	case DMI_BIOS_VERSION:
		return smbios_bios_version;
	default:
		printf("%s slot %d not handled\n", __func__, slot);
	}
#endif
	return NULL;
}

int
dmi_check_system(const struct dmi_system_id *sysid)
{
	const struct dmi_system_id *dsi;
	int num = 0;

	for (dsi = sysid; dsi->matches[0].slot != 0 ; dsi++) {
		if (dmi_found(dsi)) {
			num++;
			if (dsi->callback && dsi->callback(dsi))
				break;
		}
	}
	return (num);
}

struct vm_page *
alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	int flags = (gfp_mask & M_NOWAIT) ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK;
	struct uvm_constraint_range *constraint = &no_constraint;
	struct pglist mlist;

	if (gfp_mask & M_CANFAIL)
		flags |= UVM_PLA_FAILOK;
	if (gfp_mask & M_ZERO)
		flags |= UVM_PLA_ZERO;
	if (gfp_mask & __GFP_DMA32)
		constraint = &dma_constraint;

	TAILQ_INIT(&mlist);
	if (uvm_pglistalloc(PAGE_SIZE << order, constraint->ucr_low,
	    constraint->ucr_high, PAGE_SIZE, 0, &mlist, 1, flags))
		return NULL;
	return TAILQ_FIRST(&mlist);
}

void
__free_pages(struct vm_page *page, unsigned int order)
{
	struct pglist mlist;
	int i;
	
	TAILQ_INIT(&mlist);
	for (i = 0; i < (1 << order); i++)
		TAILQ_INSERT_TAIL(&mlist, &page[i], pageq);
	uvm_pglistfree(&mlist);
}

void
__pagevec_release(struct pagevec *pvec)
{
	struct pglist mlist;
	int i;

	TAILQ_INIT(&mlist);
	for (i = 0; i < pvec->nr; i++)
		TAILQ_INSERT_TAIL(&mlist, pvec->pages[i], pageq);
	uvm_pglistfree(&mlist);
	pagevec_reinit(pvec);
}

static struct kmem_va_mode kv_physwait = {
	.kv_map = &phys_map,
	.kv_wait = 1,
};

void *
kmap(struct vm_page *pg)
{
	vaddr_t va;

#if defined (__HAVE_PMAP_DIRECT)
	va = pmap_map_direct(pg);
#else
	va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_physwait, &kp_none, &kd_waitok);
	pmap_kenter_pa(va, VM_PAGE_TO_PHYS(pg), PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());
#endif
	return (void *)va;
}

void
kunmap_va(void *addr)
{
	vaddr_t va = (vaddr_t)addr;

#if defined (__HAVE_PMAP_DIRECT)
	pmap_unmap_direct(va);
#else
	pmap_kremove(va, PAGE_SIZE);
	pmap_update(pmap_kernel());
	km_free((void *)va, PAGE_SIZE, &kv_physwait, &kp_none);
#endif
}

vaddr_t kmap_atomic_va;
int kmap_atomic_inuse;

void *
kmap_atomic_prot(struct vm_page *pg, pgprot_t prot)
{
	KASSERT(!kmap_atomic_inuse);

	kmap_atomic_inuse = 1;
	pmap_kenter_pa(kmap_atomic_va, VM_PAGE_TO_PHYS(pg) | prot,
	    PROT_READ | PROT_WRITE);
	return (void *)kmap_atomic_va;
}

void
kunmap_atomic(void *addr)
{
	KASSERT(kmap_atomic_inuse);
	
	pmap_kremove(kmap_atomic_va, PAGE_SIZE);
	kmap_atomic_inuse = 0;
}

void *
vmap(struct vm_page **pages, unsigned int npages, unsigned long flags,
     pgprot_t prot)
{
	vaddr_t va;
	paddr_t pa;
	int i;

	va = (vaddr_t)km_alloc(PAGE_SIZE * npages, &kv_any, &kp_none,
	    &kd_nowait);
	if (va == 0)
		return NULL;
	for (i = 0; i < npages; i++) {
		pa = VM_PAGE_TO_PHYS(pages[i]) | prot;
		pmap_enter(pmap_kernel(), va + (i * PAGE_SIZE), pa,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED);
		pmap_update(pmap_kernel());
	}

	return (void *)va;
}

void *
vmap_pfn(unsigned long *pfns, unsigned int npfn, pgprot_t prot)
{
	vaddr_t va;
	paddr_t pa;
	int i;

	va = (vaddr_t)km_alloc(PAGE_SIZE * npfn, &kv_any, &kp_none,
	    &kd_nowait);
	if (va == 0)
		return NULL;
	for (i = 0; i < npfn; i++) {
		pa = round_page(pfns[i]) | prot;
		pmap_enter(pmap_kernel(), va + (i * PAGE_SIZE), pa,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED);
		pmap_update(pmap_kernel());
	}

	return (void *)va;
}

void
vunmap(void *addr, size_t size)
{
	vaddr_t va = (vaddr_t)addr;

	pmap_remove(pmap_kernel(), va, va + size);
	pmap_update(pmap_kernel());
	km_free((void *)va, size, &kv_any, &kp_none);
}

bool
is_vmalloc_addr(const void *p)
{
	vaddr_t min, max, addr;

	min = vm_map_min(kernel_map);
	max = vm_map_max(kernel_map);
	addr = (vaddr_t)p;

	if (addr >= min && addr <= max)
		return true;
	else
		return false;
}

void
print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
    int rowsize, int groupsize, const void *buf, size_t len, bool ascii)
{
	const uint8_t *cbuf = buf;
	int i;

	for (i = 0; i < len; i++) {
		if ((i % rowsize) == 0)
			printf("%s", prefix_str);
		printf("%02x", cbuf[i]);
		if ((i % rowsize) == (rowsize - 1))
			printf("\n");
		else
			printf(" ");
	}
}

void *
memchr_inv(const void *s, int c, size_t n)
{
	if (n != 0) {
		const unsigned char *p = s;

		do {
			if (*p++ != (unsigned char)c)
				return ((void *)(p - 1));
		} while (--n != 0);
	}
	return (NULL);
}

int
panic_cmp(struct rb_node *a, struct rb_node *b)
{
	panic(__func__);
}

#undef RB_ROOT
#define RB_ROOT(head)	(head)->rbh_root

RB_GENERATE(linux_root, rb_node, __entry, panic_cmp);

/*
 * This is a fairly minimal implementation of the Linux "idr" API.  It
 * probably isn't very efficient, and definitely isn't RCU safe.  The
 * pre-load buffer is global instead of per-cpu; we rely on the kernel
 * lock to make this work.  We do randomize our IDs in order to make
 * them harder to guess.
 */

int idr_cmp(struct idr_entry *, struct idr_entry *);
SPLAY_PROTOTYPE(idr_tree, idr_entry, entry, idr_cmp);

struct pool idr_pool;
struct idr_entry *idr_entry_cache;

void
idr_init(struct idr *idr)
{
	SPLAY_INIT(&idr->tree);
}

void
idr_destroy(struct idr *idr)
{
	struct idr_entry *id;

	while ((id = SPLAY_MIN(idr_tree, &idr->tree))) {
		SPLAY_REMOVE(idr_tree, &idr->tree, id);
		pool_put(&idr_pool, id);
	}
}

void
idr_preload(unsigned int gfp_mask)
{
	int flags = (gfp_mask & GFP_NOWAIT) ? PR_NOWAIT : PR_WAITOK;

	KERNEL_ASSERT_LOCKED();

	if (idr_entry_cache == NULL)
		idr_entry_cache = pool_get(&idr_pool, flags);
}

/* [start, end) */
int
idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp_mask)
{
	int flags = (gfp_mask & GFP_NOWAIT) ? PR_NOWAIT : PR_WAITOK;
	struct idr_entry *id;
	int begin;

	KERNEL_ASSERT_LOCKED();

	if (idr_entry_cache) {
		id = idr_entry_cache;
		idr_entry_cache = NULL;
	} else {
		id = pool_get(&idr_pool, flags);
		if (id == NULL)
			return -ENOMEM;
	}

	if (end <= 0)
		end = INT_MAX;

#ifdef notyet
	id->id = begin = start + arc4random_uniform(end - start);
#else
	id->id = begin = start;
#endif
	while (SPLAY_INSERT(idr_tree, &idr->tree, id)) {
		if (id->id == end)
			id->id = start;
		else
			id->id++;
		if (id->id == begin) {
			pool_put(&idr_pool, id);
			return -ENOSPC;
		}
	}
	id->ptr = ptr;
	return id->id;
}

void *
idr_replace(struct idr *idr, void *ptr, unsigned long id)
{
	struct idr_entry find, *res;
	void *old;

	find.id = id;
	res = SPLAY_FIND(idr_tree, &idr->tree, &find);
	if (res == NULL)
		return ERR_PTR(-ENOENT);
	old = res->ptr;
	res->ptr = ptr;
	return old;
}

void *
idr_remove(struct idr *idr, unsigned long id)
{
	struct idr_entry find, *res;
	void *ptr = NULL;

	find.id = id;
	res = SPLAY_FIND(idr_tree, &idr->tree, &find);
	if (res) {
		SPLAY_REMOVE(idr_tree, &idr->tree, res);
		ptr = res->ptr;
		pool_put(&idr_pool, res);
	}
	return ptr;
}

void *
idr_find(struct idr *idr, unsigned long id)
{
	struct idr_entry find, *res;

	find.id = id;
	res = SPLAY_FIND(idr_tree, &idr->tree, &find);
	if (res == NULL)
		return NULL;
	return res->ptr;
}

void *
idr_get_next(struct idr *idr, int *id)
{
	struct idr_entry *res;

	SPLAY_FOREACH(res, idr_tree, &idr->tree) {
		if (res->id >= *id) {
			*id = res->id;
			return res->ptr;
		}
	}

	return NULL;
}

int
idr_for_each(struct idr *idr, int (*func)(int, void *, void *), void *data)
{
	struct idr_entry *id;
	int ret;

	SPLAY_FOREACH(id, idr_tree, &idr->tree) {
		ret = func(id->id, id->ptr, data);
		if (ret)
			return ret;
	}

	return 0;
}

int
idr_cmp(struct idr_entry *a, struct idr_entry *b)
{
	return (a->id < b->id ? -1 : a->id > b->id);
}

SPLAY_GENERATE(idr_tree, idr_entry, entry, idr_cmp);

void
ida_init(struct ida *ida)
{
	idr_init(&ida->idr);
}

void
ida_destroy(struct ida *ida)
{
	idr_destroy(&ida->idr);
}

int
ida_simple_get(struct ida *ida, unsigned int start, unsigned int end,
    gfp_t gfp_mask)
{
	return idr_alloc(&ida->idr, NULL, start, end, gfp_mask);
}

void
ida_simple_remove(struct ida *ida, unsigned int id)
{
	idr_remove(&ida->idr, id);
}

/* [start, end] */
int
ida_alloc_range(struct ida *ida, unsigned int start, unsigned int end, gfp_t gfp)
{
	return idr_alloc(&ida->idr, NULL, start, end + 1, gfp);
}

int
ida_alloc_min(struct ida *ida, unsigned int min, gfp_t gfp)
{
	return idr_alloc(&ida->idr, NULL, min, INT_MAX, gfp);
}

int
ida_alloc_max(struct ida *ida, unsigned int max, gfp_t gfp)
{
	return idr_alloc(&ida->idr, NULL, 0, max - 1, gfp);
}

void
ida_free(struct ida *ida, unsigned int id)
{
	idr_remove(&ida->idr, id);
}

int
xarray_cmp(struct xarray_entry *a, struct xarray_entry *b)
{
	return (a->id < b->id ? -1 : a->id > b->id);
}

SPLAY_PROTOTYPE(xarray_tree, xarray_entry, entry, xarray_cmp);
struct pool xa_pool;
SPLAY_GENERATE(xarray_tree, xarray_entry, entry, xarray_cmp);

void
xa_init_flags(struct xarray *xa, gfp_t flags)
{
	SPLAY_INIT(&xa->xa_tree);
	if (flags & XA_FLAGS_LOCK_IRQ)
		mtx_init(&xa->xa_lock, IPL_TTY);
	else
		mtx_init(&xa->xa_lock, IPL_NONE);
	xa->xa_flags = flags;
}

void
xa_destroy(struct xarray *xa)
{
	struct xarray_entry *id;

	while ((id = SPLAY_MIN(xarray_tree, &xa->xa_tree))) {
		SPLAY_REMOVE(xarray_tree, &xa->xa_tree, id);
		pool_put(&xa_pool, id);
	}
}

/* Don't wrap ids. */
int
__xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xarray_range xr,
    gfp_t gfp)
{
	struct xarray_entry *xid;
	uint32_t start = xr.start;
	uint32_t end = xr.end;

	if (start == 0 && (xa->xa_flags & XA_FLAGS_ALLOC1))
		start = 1;

	if (gfp & GFP_NOWAIT) {
		xid = pool_get(&xa_pool, PR_NOWAIT);
	} else {
		mtx_leave(&xa->xa_lock);
		xid = pool_get(&xa_pool, PR_WAITOK);
		mtx_enter(&xa->xa_lock);
	}

	if (xid == NULL)
		return -ENOMEM;

	xid->id = start;

	while (SPLAY_INSERT(xarray_tree, &xa->xa_tree, xid)) {
		if (xid->id == end)
			xid->id = start;
		else
			xid->id++;
		if (xid->id == start) {
			pool_put(&xa_pool, xid);
			return -EBUSY;
		}
	}
	xid->ptr = entry;
	*id = xid->id;
	return 0;
}

/*
 * Wrap ids and store next id.
 * We walk the entire tree so don't special case wrapping.
 * The only caller of this (i915_drm_client.c) doesn't use next id.
 */
int
__xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry,
    struct xarray_range xr, u32 *next, gfp_t gfp)
{
	int r = __xa_alloc(xa, id, entry, xr, gfp);
	*next = *id + 1;
	return r;
}

void *
__xa_erase(struct xarray *xa, unsigned long index)
{
	struct xarray_entry find, *res;
	void *ptr = NULL;

	find.id = index;
	res = SPLAY_FIND(xarray_tree, &xa->xa_tree, &find);
	if (res) {
		SPLAY_REMOVE(xarray_tree, &xa->xa_tree, res);
		ptr = res->ptr;
		pool_put(&xa_pool, res);
	}
	return ptr;
}

void *
__xa_load(struct xarray *xa, unsigned long index)
{
	struct xarray_entry find, *res;

	find.id = index;
	res = SPLAY_FIND(xarray_tree, &xa->xa_tree, &find);
	if (res == NULL)
		return NULL;
	return res->ptr;
}

void *
__xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	struct xarray_entry find, *res;
	void *prev;

	if (entry == NULL)
		return __xa_erase(xa, index);

	find.id = index;
	res = SPLAY_FIND(xarray_tree, &xa->xa_tree, &find);
	if (res != NULL) {
		/* index exists */
		/* XXX Multislot entries updates not implemented yet */
		prev = res->ptr;
		res->ptr = entry;
		return prev;
	}

	/* index not found, add new */
	if (gfp & GFP_NOWAIT) {
		res = pool_get(&xa_pool, PR_NOWAIT);
	} else {
		mtx_leave(&xa->xa_lock);
		res = pool_get(&xa_pool, PR_WAITOK);
		mtx_enter(&xa->xa_lock);
	}
	if (res == NULL)
		return XA_ERROR(-ENOMEM);
	res->id = index;
	res->ptr = entry;
	if (SPLAY_INSERT(xarray_tree, &xa->xa_tree, res) != NULL)
		return XA_ERROR(-EINVAL);
	return NULL; /* no prev entry at index */
}

void *
xa_get_next(struct xarray *xa, unsigned long *index)
{
	struct xarray_entry *res;

	SPLAY_FOREACH(res, xarray_tree, &xa->xa_tree) {
		if (res->id >= *index) {
			*index = res->id;
			return res->ptr;
		}
	}

	return NULL;
}

int
sg_alloc_table(struct sg_table *table, unsigned int nents, gfp_t gfp_mask)
{
	table->sgl = mallocarray(nents, sizeof(struct scatterlist),
	    M_DRM, gfp_mask | M_ZERO);
	if (table->sgl == NULL)
		return -ENOMEM;
	table->nents = table->orig_nents = nents;
	sg_mark_end(&table->sgl[nents - 1]);
	return 0;
}

void
sg_free_table(struct sg_table *table)
{
	free(table->sgl, M_DRM,
	    table->orig_nents * sizeof(struct scatterlist));
	table->orig_nents = 0;
	table->sgl = NULL;
}

int
i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	void *cmd = NULL;
	int cmdlen = 0;
	int err, ret = 0;
	int op;

	iic_acquire_bus(&adap->ic, 0);

	while (num > 2) {
		op = (msgs->flags & I2C_M_RD) ? I2C_OP_READ : I2C_OP_WRITE;
		err = iic_exec(&adap->ic, op, msgs->addr, NULL, 0,
		    msgs->buf, msgs->len, 0);
		if (err) {
			ret = -err;
			goto fail;
		}
		msgs++;
		num--;
		ret++;
	}

	if (num > 1) {
		cmd = msgs->buf;
		cmdlen = msgs->len;
		msgs++;
		num--;
		ret++;
	}

	op = (msgs->flags & I2C_M_RD) ?
	    I2C_OP_READ_WITH_STOP : I2C_OP_WRITE_WITH_STOP;
	err = iic_exec(&adap->ic, op, msgs->addr, cmd, cmdlen,
	    msgs->buf, msgs->len, 0);
	if (err) {
		ret = -err;
		goto fail;
	}
	msgs++;
	ret++;

fail:
	iic_release_bus(&adap->ic, 0);

	return ret;
}

int
__i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int ret, retries;

	retries = adap->retries;
retry:
	if (adap->algo)
		ret = adap->algo->master_xfer(adap, msgs, num);
	else
		ret = i2c_master_xfer(adap, msgs, num);
	if (ret == -EAGAIN && retries > 0) {
		retries--;
		goto retry;
	}

	return ret;
}

int
i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	int ret; 

	if (adap->lock_ops)
		adap->lock_ops->lock_bus(adap, 0);

	ret = __i2c_transfer(adap, msgs, num);

	if (adap->lock_ops)
		adap->lock_ops->unlock_bus(adap, 0);

	return ret;
}

int
i2c_bb_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct i2c_algo_bit_data *algo = adap->algo_data;
	struct i2c_adapter bb;

	memset(&bb, 0, sizeof(bb));
	bb.ic = algo->ic;
	bb.retries = adap->retries;
	return i2c_master_xfer(&bb, msgs, num);
}

uint32_t
i2c_bb_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

struct i2c_algorithm i2c_bit_algo = {
	.master_xfer = i2c_bb_master_xfer,
	.functionality = i2c_bb_functionality
};

int
i2c_bit_add_bus(struct i2c_adapter *adap)
{
	adap->algo = &i2c_bit_algo;
	adap->retries = 3;

	return 0;
}

#if defined(__amd64__) || defined(__i386__)

/*
 * This is a minimal implementation of the Linux vga_get/vga_put
 * interface.  In all likelihood, it will only work for inteldrm(4) as
 * it assumes that if there is another active VGA device in the
 * system, it is sitting behind a PCI bridge.
 */

extern int pci_enumerate_bus(struct pci_softc *,
    int (*)(struct pci_attach_args *), struct pci_attach_args *);

pcitag_t vga_bridge_tag;
int vga_bridge_disabled;

int
vga_disable_bridge(struct pci_attach_args *pa)
{
	pcireg_t bhlc, bc;

	if (pa->pa_domain != 0)
		return 0;

	bhlc = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	if (PCI_HDRTYPE_TYPE(bhlc) != 1)
		return 0;

	bc = pci_conf_read(pa->pa_pc, pa->pa_tag, PPB_REG_BRIDGECONTROL);
	if ((bc & PPB_BC_VGA_ENABLE) == 0)
		return 0;
	bc &= ~PPB_BC_VGA_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PPB_REG_BRIDGECONTROL, bc);

	vga_bridge_tag = pa->pa_tag;
	vga_bridge_disabled = 1;

	return 1;
}

void
vga_get_uninterruptible(struct pci_dev *pdev, int rsrc)
{
	if (pdev->pci->sc_bridgetag != NULL)
		return;
	pci_enumerate_bus(pdev->pci, vga_disable_bridge, NULL);
}

void
vga_put(struct pci_dev *pdev, int rsrc)
{
	pcireg_t bc;

	if (!vga_bridge_disabled)
		return;

	bc = pci_conf_read(pdev->pc, vga_bridge_tag, PPB_REG_BRIDGECONTROL);
	bc |= PPB_BC_VGA_ENABLE;
	pci_conf_write(pdev->pc, vga_bridge_tag, PPB_REG_BRIDGECONTROL, bc);

	vga_bridge_disabled = 0;
}

#endif

suspend_state_t pm_suspend_target_state;

/*
 * ACPI types and interfaces.
 */

#ifdef __HAVE_ACPI
#include "acpi.h"
#endif

#if NACPI > 0

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

struct acpi_fadt acpi_gbl_FADT;

acpi_status
acpi_get_table(const char *sig, int instance,
    struct acpi_table_header **hdr)
{
	struct acpi_softc *sc = acpi_softc;
	struct acpi_q *entry;

	KASSERT(instance == 1);

	if (sc == NULL)
		return AE_NOT_FOUND;

	SIMPLEQ_FOREACH(entry, &sc->sc_tables, q_next) {
		if (memcmp(entry->q_table, sig, strlen(sig)) == 0) {
			*hdr = entry->q_table;
			return 0;
		}
	}

	return AE_NOT_FOUND;
}

void
acpi_put_table(struct acpi_table_header *hdr)
{
}

acpi_status
acpi_get_handle(acpi_handle node, const char *name, acpi_handle *rnode)
{
	node = aml_searchname(node, name);
	if (node == NULL)
		return AE_NOT_FOUND;

	*rnode = node;
	return 0;
}

acpi_status
acpi_get_name(acpi_handle node, int type,  struct acpi_buffer *buffer)
{
	KASSERT(buffer->length != ACPI_ALLOCATE_BUFFER);
	KASSERT(type == ACPI_FULL_PATHNAME);
	strlcpy(buffer->pointer, aml_nodename(node), buffer->length);
	return 0;
}

acpi_status
acpi_evaluate_object(acpi_handle node, const char *name,
    struct acpi_object_list *params, struct acpi_buffer *result)
{
	struct aml_value args[4], res;
	union acpi_object *obj;
	uint8_t *data;
	int i;

	KASSERT(params->count <= nitems(args));

	for (i = 0; i < params->count; i++) {
		args[i].type = params->pointer[i].type;
		switch (args[i].type) {
		case AML_OBJTYPE_INTEGER:
			args[i].v_integer = params->pointer[i].integer.value;
			break;
		case AML_OBJTYPE_BUFFER:
			args[i].length = params->pointer[i].buffer.length;
			args[i].v_buffer = params->pointer[i].buffer.pointer;
			break;
		default:
			printf("%s: arg type 0x%02x", __func__, args[i].type);
			return AE_BAD_PARAMETER;
		}
	}

	if (name) {
		node = aml_searchname(node, name);
		if (node == NULL)
			return AE_NOT_FOUND;
	}
	if (aml_evalnode(acpi_softc, node, params->count, args, &res)) {
		aml_freevalue(&res);
		return AE_ERROR;
	}

	KASSERT(result->length == ACPI_ALLOCATE_BUFFER);

	result->length = sizeof(union acpi_object);
	switch (res.type) {
	case AML_OBJTYPE_BUFFER:
		result->length += res.length;
		result->pointer = malloc(result->length, M_DRM, M_WAITOK);
		obj = (union acpi_object *)result->pointer;
		data = (uint8_t *)(obj + 1);
		obj->type = res.type;
		obj->buffer.length = res.length;
		obj->buffer.pointer = data;
		memcpy(data, res.v_buffer, res.length);
		break;
	default:
		printf("%s: return type 0x%02x", __func__, res.type);
		aml_freevalue(&res);
		return AE_ERROR;
	}

	aml_freevalue(&res);
	return 0;
}

SLIST_HEAD(, notifier_block) drm_linux_acpi_notify_list =
	SLIST_HEAD_INITIALIZER(drm_linux_acpi_notify_list);

int
drm_linux_acpi_notify(struct aml_node *node, int notify, void *arg)
{
	struct acpi_bus_event event;
	struct notifier_block *nb;

	event.device_class = ACPI_VIDEO_CLASS;
	event.type = notify;

	SLIST_FOREACH(nb, &drm_linux_acpi_notify_list, link)
		nb->notifier_call(nb, 0, &event);
	return 0;
}

int
register_acpi_notifier(struct notifier_block *nb)
{
	SLIST_INSERT_HEAD(&drm_linux_acpi_notify_list, nb, link);
	return 0;
}

int
unregister_acpi_notifier(struct notifier_block *nb)
{
	struct notifier_block *tmp;

	SLIST_FOREACH(tmp, &drm_linux_acpi_notify_list, link) {
		if (tmp == nb) {
			SLIST_REMOVE(&drm_linux_acpi_notify_list, nb,
			    notifier_block, link);
			return 0;
		}
	}

	return -ENOENT;
}

const char *
acpi_format_exception(acpi_status status)
{
	switch (status) {
	case AE_NOT_FOUND:
		return "not found";
	case AE_BAD_PARAMETER:
		return "bad parameter";
	default:
		return "unknown";
	}
}

int
acpi_target_system_state(void)
{
	return acpi_softc->sc_state;
}

enum acpi_backlight_type
acpi_video_get_backlight_type(void)
{
	struct wsdisplay_param dp;

	dp.param = WSDISPLAYIO_PARAM_BRIGHTNESS;
	if (ws_get_param && ws_get_param(&dp) == 0)
		return acpi_backlight_video;
	return acpi_backlight_native;
}

#endif

SLIST_HEAD(,backlight_device) backlight_device_list =
    SLIST_HEAD_INITIALIZER(backlight_device_list);

void
backlight_do_update_status(void *arg)
{
	backlight_update_status(arg);
}

struct backlight_device *
backlight_device_register(const char *name, void *kdev, void *data,
    const struct backlight_ops *ops, const struct backlight_properties *props)
{
	struct backlight_device *bd;

	bd = malloc(sizeof(*bd), M_DRM, M_WAITOK);
	bd->ops = ops;
	bd->props = *props;
	bd->data = data;

	task_set(&bd->task, backlight_do_update_status, bd);

	SLIST_INSERT_HEAD(&backlight_device_list, bd, next);
	bd->name = name;
	
	return bd;
}

void
backlight_device_unregister(struct backlight_device *bd)
{
	SLIST_REMOVE(&backlight_device_list, bd, backlight_device, next);
	free(bd, M_DRM, sizeof(*bd));
}

void
backlight_schedule_update_status(struct backlight_device *bd)
{
	task_add(systq, &bd->task);
}

int
backlight_enable(struct backlight_device *bd)
{
	if (bd == NULL)
		return 0;

	bd->props.power = BACKLIGHT_POWER_ON;

	return bd->ops->update_status(bd);
}

int
backlight_disable(struct backlight_device *bd)
{
	if (bd == NULL)
		return 0;

	bd->props.power = BACKLIGHT_POWER_OFF;

	return bd->ops->update_status(bd);
}

struct backlight_device *
backlight_device_get_by_name(const char *name)
{
	struct backlight_device *bd;

	SLIST_FOREACH(bd, &backlight_device_list, next) {
		if (strcmp(name, bd->name) == 0)
			return bd;
	}

	return NULL;
}

struct drvdata {
	struct device *dev;
	void *data;
	SLIST_ENTRY(drvdata) next;
};

SLIST_HEAD(,drvdata) drvdata_list = SLIST_HEAD_INITIALIZER(drvdata_list);

void
dev_set_drvdata(struct device *dev, void *data)
{
	struct drvdata *drvdata;

	SLIST_FOREACH(drvdata, &drvdata_list, next) {
		if (drvdata->dev == dev) {
			drvdata->data = data;
			return;
		}
	}

	if (data == NULL)
		return;

	drvdata = malloc(sizeof(*drvdata), M_DRM, M_WAITOK);
	drvdata->dev = dev;
	drvdata->data = data;

	SLIST_INSERT_HEAD(&drvdata_list, drvdata, next);
}

void *
dev_get_drvdata(struct device *dev)
{
	struct drvdata *drvdata;

	SLIST_FOREACH(drvdata, &drvdata_list, next) {
		if (drvdata->dev == dev)
			return drvdata->data;
	}

	return NULL;
}

void
drm_sysfs_hotplug_event(struct drm_device *dev)
{
	knote_locked(&dev->note, NOTE_CHANGE);
}

void
drm_sysfs_connector_hotplug_event(struct drm_connector *connector)
{
	knote_locked(&connector->dev->note, NOTE_CHANGE);
}

void
drm_sysfs_connector_status_event(struct drm_connector *connector,
    struct drm_property *property)
{
	STUB();
}

void
drm_sysfs_connector_property_event(struct drm_connector *connector,
    struct drm_property *property)
{
	STUB();
}

struct dma_fence *
dma_fence_get(struct dma_fence *fence)
{
	if (fence)
		kref_get(&fence->refcount);
	return fence;
}

struct dma_fence *
dma_fence_get_rcu(struct dma_fence *fence)
{
	if (fence)
		kref_get(&fence->refcount);
	return fence;
}

struct dma_fence *
dma_fence_get_rcu_safe(struct dma_fence **dfp)
{
	struct dma_fence *fence;
	if (dfp == NULL)
		return NULL;
	fence = *dfp;
	if (fence)
		kref_get(&fence->refcount);
	return fence;
}

void
dma_fence_release(struct kref *ref)
{
	struct dma_fence *fence = container_of(ref, struct dma_fence, refcount);
	if (fence->ops && fence->ops->release)
		fence->ops->release(fence);
	else
		free(fence, M_DRM, 0);
}

void
dma_fence_put(struct dma_fence *fence)
{
	if (fence)
		kref_put(&fence->refcount, dma_fence_release);
}

int
dma_fence_signal_timestamp_locked(struct dma_fence *fence, ktime_t timestamp)
{
	struct dma_fence_cb *cur, *tmp;
	struct list_head cb_list;

	if (fence == NULL)
		return -EINVAL;

	if (test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return -EINVAL;

	list_replace(&fence->cb_list, &cb_list);

	fence->timestamp = timestamp;
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);

	list_for_each_entry_safe(cur, tmp, &cb_list, node) {
		INIT_LIST_HEAD(&cur->node);
		cur->func(fence, cur);
	}

	return 0;
}

int
dma_fence_signal(struct dma_fence *fence)
{
	int r;

	if (fence == NULL)
		return -EINVAL;

	mtx_enter(fence->lock);
	r = dma_fence_signal_timestamp_locked(fence, ktime_get());
	mtx_leave(fence->lock);

	return r;
}

int
dma_fence_signal_locked(struct dma_fence *fence)
{
	if (fence == NULL)
		return -EINVAL;

	return dma_fence_signal_timestamp_locked(fence, ktime_get());
}

int
dma_fence_signal_timestamp(struct dma_fence *fence, ktime_t timestamp)
{
	int r;

	if (fence == NULL)
		return -EINVAL;

	mtx_enter(fence->lock);
	r = dma_fence_signal_timestamp_locked(fence, timestamp);
	mtx_leave(fence->lock);

	return r;
}

bool
dma_fence_is_signaled(struct dma_fence *fence)
{
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return true;

	if (fence->ops->signaled && fence->ops->signaled(fence)) {
		dma_fence_signal(fence);
		return true;
	}

	return false;
}

bool
dma_fence_is_signaled_locked(struct dma_fence *fence)
{
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return true;

	if (fence->ops->signaled && fence->ops->signaled(fence)) {
		dma_fence_signal_locked(fence);
		return true;
	}

	return false;
}

ktime_t
dma_fence_timestamp(struct dma_fence *fence)
{
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		while (!test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags))
			CPU_BUSY_CYCLE();
		return fence->timestamp;
	} else {
		return ktime_get();
	}
}

long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, long timeout)
{
	if (timeout < 0)
		return -EINVAL;

	if (fence->ops->wait)
		return fence->ops->wait(fence, intr, timeout);
	else
		return dma_fence_default_wait(fence, intr, timeout);
}

long
dma_fence_wait(struct dma_fence *fence, bool intr)
{
	long ret;

	ret = dma_fence_wait_timeout(fence, intr, MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		return ret;
	
	return 0;
}

void
dma_fence_enable_sw_signaling(struct dma_fence *fence)
{
	if (!test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags) &&
	    !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags) &&
	    fence->ops->enable_signaling) {
		mtx_enter(fence->lock);
		if (!fence->ops->enable_signaling(fence))
			dma_fence_signal_locked(fence);
		mtx_leave(fence->lock);
	}
}

void
dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
    struct mutex *lock, uint64_t context, uint64_t seqno)
{
	fence->ops = ops;
	fence->lock = lock;
	fence->context = context;
	fence->seqno = seqno;
	fence->flags = 0;
	fence->error = 0;
	kref_init(&fence->refcount);
	INIT_LIST_HEAD(&fence->cb_list);
}

int
dma_fence_add_callback(struct dma_fence *fence, struct dma_fence_cb *cb,
    dma_fence_func_t func)
{
	int ret = 0;
	bool was_set;

	if (WARN_ON(!fence || !func))
		return -EINVAL;

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		INIT_LIST_HEAD(&cb->node);
		return -ENOENT;
	}

	mtx_enter(fence->lock);

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		ret = -ENOENT;
	else if (!was_set && fence->ops->enable_signaling) {
		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			ret = -ENOENT;
		}
	}

	if (!ret) {
		cb->func = func;
		list_add_tail(&cb->node, &fence->cb_list);
	} else
		INIT_LIST_HEAD(&cb->node);
	mtx_leave(fence->lock);

	return ret;
}

bool
dma_fence_remove_callback(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	bool ret;

	mtx_enter(fence->lock);

	ret = !list_empty(&cb->node);
	if (ret)
		list_del_init(&cb->node);

	mtx_leave(fence->lock);

	return ret;
}

static atomic64_t drm_fence_context_count = ATOMIC64_INIT(1);

uint64_t
dma_fence_context_alloc(unsigned int num)
{
  return atomic64_add_return(num, &drm_fence_context_count) - num;
}

struct default_wait_cb {
	struct dma_fence_cb base;
	struct proc *proc;
};

static void
dma_fence_default_wait_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct default_wait_cb *wait =
	    container_of(cb, struct default_wait_cb, base);
	wake_up_process(wait->proc);
}

long
dma_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
	long ret = timeout ? timeout : 1;
	unsigned long end;
	int err;
	struct default_wait_cb cb;
	bool was_set;

	KASSERT(timeout <= INT_MAX);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return ret;

	mtx_enter(fence->lock);

	was_set = test_and_set_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
	    &fence->flags);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		goto out;

	if (!was_set && fence->ops->enable_signaling) {
		if (!fence->ops->enable_signaling(fence)) {
			dma_fence_signal_locked(fence);
			goto out;
		}
	}

	if (timeout == 0) {
		ret = 0;
		goto out;
	}

	cb.base.func = dma_fence_default_wait_cb;
	cb.proc = curproc;
	list_add(&cb.base.node, &fence->cb_list);

	end = jiffies + timeout;
	for (ret = timeout; ret > 0; ret = MAX(0, end - jiffies)) {
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
			break;
		err = msleep(curproc, fence->lock, intr ? PCATCH : 0,
		    "dmafence", ret);
		if (err == EINTR || err == ERESTART) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	if (!list_empty(&cb.base.node))
		list_del(&cb.base.node);
out:
	mtx_leave(fence->lock);
	
	return ret;
}

static bool
dma_fence_test_signaled_any(struct dma_fence **fences, uint32_t count,
    uint32_t *idx)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct dma_fence *fence = fences[i];
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

long
dma_fence_wait_any_timeout(struct dma_fence **fences, uint32_t count,
    bool intr, long timeout, uint32_t *idx)
{
	struct default_wait_cb *cb;
	long ret = timeout;
	unsigned long end;
	int i, err;

	KASSERT(timeout <= INT_MAX);

	if (timeout == 0) {
		for (i = 0; i < count; i++) {
			if (dma_fence_is_signaled(fences[i])) {
				if (idx)
					*idx = i;
				return 1;
			}
		}
		return 0;
	}

	cb = mallocarray(count, sizeof(*cb), M_DRM, M_WAITOK|M_CANFAIL|M_ZERO);
	if (cb == NULL)
		return -ENOMEM;
	
	for (i = 0; i < count; i++) {
		struct dma_fence *fence = fences[i];
		cb[i].proc = curproc;
		if (dma_fence_add_callback(fence, &cb[i].base,
		    dma_fence_default_wait_cb)) {
			if (idx)
				*idx = i;
			goto cb_cleanup;
		}
	}

	end = jiffies + timeout;
	for (ret = timeout; ret > 0; ret = MAX(0, end - jiffies)) {
		if (dma_fence_test_signaled_any(fences, count, idx))
			break;
		err = tsleep(curproc, intr ? PCATCH : 0, "dfwat", ret);
		if (err == EINTR || err == ERESTART) {
			ret = -ERESTARTSYS;
			break;
		}
	}

cb_cleanup:
	while (i-- > 0)
		dma_fence_remove_callback(fences[i], &cb[i].base);
	free(cb, M_DRM, count * sizeof(*cb));
	return ret;
}

void
dma_fence_set_deadline(struct dma_fence *f, ktime_t t)
{
	if (f->ops->set_deadline == NULL)
		return;
	if (dma_fence_is_signaled(f) == false)
		f->ops->set_deadline(f, t);
}

static struct dma_fence dma_fence_stub;
static struct mutex dma_fence_stub_mtx = MUTEX_INITIALIZER(IPL_TTY);

static const char *
dma_fence_stub_get_name(struct dma_fence *fence)
{
	return "stub";
}

static const struct dma_fence_ops dma_fence_stub_ops = {
	.get_driver_name = dma_fence_stub_get_name,
	.get_timeline_name = dma_fence_stub_get_name,
};

struct dma_fence *
dma_fence_get_stub(void)
{
	mtx_enter(&dma_fence_stub_mtx);
	if (dma_fence_stub.ops == NULL) {
		dma_fence_init(&dma_fence_stub, &dma_fence_stub_ops,
		    &dma_fence_stub_mtx, 0, 0);
		dma_fence_signal_locked(&dma_fence_stub);
	}
	mtx_leave(&dma_fence_stub_mtx);

	return dma_fence_get(&dma_fence_stub);
}

struct dma_fence *
dma_fence_allocate_private_stub(ktime_t ts)
{
	struct dma_fence *f = malloc(sizeof(*f), M_DRM,
	    M_ZERO | M_WAITOK | M_CANFAIL);
	if (f == NULL)
		return NULL;
	dma_fence_init(f, &dma_fence_stub_ops, &dma_fence_stub_mtx, 0, 0);
	dma_fence_signal_timestamp(f, ts);
	return f;
}

static const char *
dma_fence_array_get_driver_name(struct dma_fence *fence)
{
	return "dma_fence_array";
}

static const char *
dma_fence_array_get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static void
irq_dma_fence_array_work(void *arg)
{
	struct dma_fence_array *dfa = (struct dma_fence_array *)arg;
	dma_fence_signal(&dfa->base);
	dma_fence_put(&dfa->base);
}

static void
dma_fence_array_cb_func(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct dma_fence_array_cb *array_cb =
	    container_of(cb, struct dma_fence_array_cb, cb);
	struct dma_fence_array *dfa = array_cb->array;
	
	if (atomic_dec_and_test(&dfa->num_pending))
		timeout_add(&dfa->to, 1);
	else
		dma_fence_put(&dfa->base);
}

static bool
dma_fence_array_enable_signaling(struct dma_fence *fence)
{
	struct dma_fence_array *dfa = to_dma_fence_array(fence);
	struct dma_fence_array_cb *cb = (void *)(&dfa[1]);
	int i;

	for (i = 0; i < dfa->num_fences; ++i) {
		cb[i].array = dfa;
		dma_fence_get(&dfa->base);
		if (dma_fence_add_callback(dfa->fences[i], &cb[i].cb,
		    dma_fence_array_cb_func)) {
			dma_fence_put(&dfa->base);
			if (atomic_dec_and_test(&dfa->num_pending))
				return false;
		}
	}
	
	return true;
}

static bool
dma_fence_array_signaled(struct dma_fence *fence)
{
	struct dma_fence_array *dfa = to_dma_fence_array(fence);

	return atomic_read(&dfa->num_pending) <= 0;
}

static void
dma_fence_array_release(struct dma_fence *fence)
{
	struct dma_fence_array *dfa = to_dma_fence_array(fence);
	int i;

	for (i = 0; i < dfa->num_fences; ++i)
		dma_fence_put(dfa->fences[i]);

	free(dfa->fences, M_DRM, 0);
	dma_fence_free(fence);
}

struct dma_fence_array *
dma_fence_array_create(int num_fences, struct dma_fence **fences, u64 context,
    unsigned seqno, bool signal_on_any)
{
	struct dma_fence_array *dfa = malloc(sizeof(*dfa) +
	    (num_fences * sizeof(struct dma_fence_array_cb)),
	    M_DRM, M_WAITOK|M_CANFAIL|M_ZERO);
	if (dfa == NULL)
		return NULL;

	mtx_init(&dfa->lock, IPL_TTY);
	dma_fence_init(&dfa->base, &dma_fence_array_ops, &dfa->lock,
	    context, seqno);
	timeout_set(&dfa->to, irq_dma_fence_array_work, dfa);

	dfa->num_fences = num_fences;
	atomic_set(&dfa->num_pending, signal_on_any ? 1 : num_fences);
	dfa->fences = fences;

	return dfa;
}

struct dma_fence *
dma_fence_array_first(struct dma_fence *f)
{
	struct dma_fence_array *dfa;

	if (f == NULL)
		return NULL;

	if ((dfa = to_dma_fence_array(f)) == NULL)
		return f;

	if (dfa->num_fences > 0)
		return dfa->fences[0];

	return NULL;
}

struct dma_fence *
dma_fence_array_next(struct dma_fence *f, unsigned int i)
{
	struct dma_fence_array *dfa;

	if (f == NULL)
		return NULL;

	if ((dfa = to_dma_fence_array(f)) == NULL)
		return NULL;

	if (i < dfa->num_fences)
		return dfa->fences[i];

	return NULL;
}

const struct dma_fence_ops dma_fence_array_ops = {
	.get_driver_name = dma_fence_array_get_driver_name,
	.get_timeline_name = dma_fence_array_get_timeline_name,
	.enable_signaling = dma_fence_array_enable_signaling,
	.signaled = dma_fence_array_signaled,
	.release = dma_fence_array_release,
};

int
dma_fence_chain_find_seqno(struct dma_fence **df, uint64_t seqno)
{
	struct dma_fence_chain *chain;
	struct dma_fence *fence;

	if (seqno == 0)
		return 0;

	if ((chain = to_dma_fence_chain(*df)) == NULL)
		return -EINVAL;

	fence = &chain->base;
	if (fence->seqno < seqno)
		return -EINVAL;

	dma_fence_chain_for_each(*df, fence) {
		if ((*df)->context != fence->context)
			break;

		chain = to_dma_fence_chain(*df);
		if (chain->prev_seqno < seqno)
			break;
	}
	dma_fence_put(fence);

	return 0;
}

void
dma_fence_chain_init(struct dma_fence_chain *chain, struct dma_fence *prev,
    struct dma_fence *fence, uint64_t seqno)
{
	uint64_t context;

	chain->fence = fence;
	chain->prev = prev;
	mtx_init(&chain->lock, IPL_TTY);

	/* if prev is a chain */
	if (to_dma_fence_chain(prev) != NULL) {
		if (__dma_fence_is_later(seqno, prev->seqno, prev->ops)) {
			chain->prev_seqno = prev->seqno;
			context = prev->context;
		} else {
			chain->prev_seqno = 0;
			context = dma_fence_context_alloc(1);
			seqno = prev->seqno;
		}
	} else {
		chain->prev_seqno = 0;
		context = dma_fence_context_alloc(1);
	}

	dma_fence_init(&chain->base, &dma_fence_chain_ops, &chain->lock,
	    context, seqno);
}

static const char *
dma_fence_chain_get_driver_name(struct dma_fence *fence)
{
	return "dma_fence_chain";
}

static const char *
dma_fence_chain_get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static bool dma_fence_chain_enable_signaling(struct dma_fence *);

static void
dma_fence_chain_timo(void *arg)
{
	struct dma_fence_chain *chain = (struct dma_fence_chain *)arg;

	if (dma_fence_chain_enable_signaling(&chain->base) == false)
		dma_fence_signal(&chain->base);
	dma_fence_put(&chain->base);
}

static void
dma_fence_chain_cb(struct dma_fence *f, struct dma_fence_cb *cb)
{
	struct dma_fence_chain *chain =
	    container_of(cb, struct dma_fence_chain, cb);
	timeout_set(&chain->to, dma_fence_chain_timo, chain);
	timeout_add(&chain->to, 1);
	dma_fence_put(f);
}

static bool
dma_fence_chain_enable_signaling(struct dma_fence *fence)
{
	struct dma_fence_chain *chain, *h;
	struct dma_fence *f;

	h = to_dma_fence_chain(fence);
	dma_fence_get(&h->base);
	dma_fence_chain_for_each(fence, &h->base) {
		chain = to_dma_fence_chain(fence);
		if (chain == NULL)
			f = fence;
		else
			f = chain->fence;

		dma_fence_get(f);
		if (!dma_fence_add_callback(f, &h->cb, dma_fence_chain_cb)) {
			dma_fence_put(fence);
			return true;
		}
		dma_fence_put(f);
	}
	dma_fence_put(&h->base);
	return false;
}

static bool
dma_fence_chain_signaled(struct dma_fence *fence)
{
	struct dma_fence_chain *chain;
	struct dma_fence *f;

	dma_fence_chain_for_each(fence, fence) {
		chain = to_dma_fence_chain(fence);
		if (chain == NULL)
			f = fence;
		else
			f = chain->fence;

		if (dma_fence_is_signaled(f) == false) {
			dma_fence_put(fence);
			return false;
		}
	}
	return true;
}

static void
dma_fence_chain_release(struct dma_fence *fence)
{
	struct dma_fence_chain *chain = to_dma_fence_chain(fence);
	struct dma_fence_chain *prev_chain;
	struct dma_fence *prev;

	for (prev = chain->prev; prev != NULL; prev = chain->prev) {
		if (kref_read(&prev->refcount) > 1)
			break;
		if ((prev_chain = to_dma_fence_chain(prev)) == NULL)
			break;
		chain->prev = prev_chain->prev;
		prev_chain->prev = NULL;
		dma_fence_put(prev);
	}
	dma_fence_put(prev);
	dma_fence_put(chain->fence);
	dma_fence_free(fence);
}

struct dma_fence *
dma_fence_chain_walk(struct dma_fence *fence)
{
	struct dma_fence_chain *chain = to_dma_fence_chain(fence), *prev_chain;
	struct dma_fence *prev, *new_prev, *tmp;

	if (chain == NULL) {
		dma_fence_put(fence);
		return NULL;
	}

	while ((prev = dma_fence_get(chain->prev)) != NULL) {
		prev_chain = to_dma_fence_chain(prev);
		if (prev_chain != NULL) {
			if (!dma_fence_is_signaled(prev_chain->fence))
				break;
			new_prev = dma_fence_get(prev_chain->prev);
		} else {
			if (!dma_fence_is_signaled(prev))
				break;
			new_prev = NULL;
		}
		tmp = atomic_cas_ptr(&chain->prev, prev, new_prev);
		dma_fence_put(tmp == prev ? prev : new_prev);
		dma_fence_put(prev);
	}

	dma_fence_put(fence);
	return prev;
}

const struct dma_fence_ops dma_fence_chain_ops = {
	.get_driver_name = dma_fence_chain_get_driver_name,
	.get_timeline_name = dma_fence_chain_get_timeline_name,
	.enable_signaling = dma_fence_chain_enable_signaling,
	.signaled = dma_fence_chain_signaled,
	.release = dma_fence_chain_release,
	.use_64bit_seqno = true,
};

bool
dma_fence_is_container(struct dma_fence *fence)
{
	return (fence->ops == &dma_fence_chain_ops) ||
	    (fence->ops == &dma_fence_array_ops);
}

int
dmabuf_read(struct file *fp, struct uio *uio, int fflags)
{
	return (ENXIO);
}

int
dmabuf_write(struct file *fp, struct uio *uio, int fflags)
{
	return (ENXIO);
}

int
dmabuf_ioctl(struct file *fp, u_long com, caddr_t data, struct proc *p)
{
	return (ENOTTY);
}

int
dmabuf_kqfilter(struct file *fp, struct knote *kn)
{
	return (EINVAL);
}

int
dmabuf_stat(struct file *fp, struct stat *st, struct proc *p)
{
	struct dma_buf *dmabuf = fp->f_data;

	memset(st, 0, sizeof(*st));
	st->st_size = dmabuf->size;
	st->st_mode = S_IFIFO;	/* XXX */
	return (0);
}

int
dmabuf_close(struct file *fp, struct proc *p)
{
	struct dma_buf *dmabuf = fp->f_data;

	fp->f_data = NULL;
	KERNEL_LOCK();
	dmabuf->ops->release(dmabuf);
	KERNEL_UNLOCK();
	free(dmabuf, M_DRM, sizeof(struct dma_buf));
	return (0);
}

int
dmabuf_seek(struct file *fp, off_t *offset, int whence, struct proc *p)
{
	struct dma_buf *dmabuf = fp->f_data;
	off_t newoff;

	if (*offset != 0)
		return (EINVAL);

	switch (whence) {
	case SEEK_SET:
		newoff = 0;
		break;
	case SEEK_END:
		newoff = dmabuf->size;
		break;
	default:
		return (EINVAL);
	}
	mtx_enter(&fp->f_mtx);
	fp->f_offset = newoff;
	mtx_leave(&fp->f_mtx);
	*offset = newoff;
	return (0);
}

const struct fileops dmabufops = {
	.fo_read	= dmabuf_read,
	.fo_write	= dmabuf_write,
	.fo_ioctl	= dmabuf_ioctl,
	.fo_kqfilter	= dmabuf_kqfilter,
	.fo_stat	= dmabuf_stat,
	.fo_close	= dmabuf_close,
	.fo_seek	= dmabuf_seek,
};

struct dma_buf *
dma_buf_export(const struct dma_buf_export_info *info)
{
	struct proc *p = curproc;
	struct dma_buf *dmabuf;
	struct file *fp;

	fp = fnew(p);
	if (fp == NULL)
		return ERR_PTR(-ENFILE);
	fp->f_type = DTYPE_DMABUF;
	fp->f_ops = &dmabufops;
	dmabuf = malloc(sizeof(struct dma_buf), M_DRM, M_WAITOK | M_ZERO);
	dmabuf->priv = info->priv;
	dmabuf->ops = info->ops;
	dmabuf->size = info->size;
	dmabuf->file = fp;
	fp->f_data = dmabuf;
	INIT_LIST_HEAD(&dmabuf->attachments);
	return dmabuf;
}

struct dma_buf *
dma_buf_get(int fd)
{
	struct proc *p = curproc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;

	if ((fp = fd_getfile(fdp, fd)) == NULL)
		return ERR_PTR(-EBADF);

	if (fp->f_type != DTYPE_DMABUF) {
		FRELE(fp, p);
		return ERR_PTR(-EINVAL);
	}

	return fp->f_data;
}

void
dma_buf_put(struct dma_buf *dmabuf)
{
	KASSERT(dmabuf);
	KASSERT(dmabuf->file);

	FRELE(dmabuf->file, curproc);
}

int
dma_buf_fd(struct dma_buf *dmabuf, int flags)
{
	struct proc *p = curproc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp = dmabuf->file;
	int fd, cloexec, error;

	cloexec = (flags & O_CLOEXEC) ? UF_EXCLOSE : 0;

	fdplock(fdp);
restart:
	if ((error = fdalloc(p, 0, &fd)) != 0) {
		if (error == ENOSPC) {
			fdexpand(p);
			goto restart;
		}
		fdpunlock(fdp);
		return -error;
	}

	fdinsert(fdp, fd, cloexec, fp);
	fdpunlock(fdp);

	return fd;
}

void
get_dma_buf(struct dma_buf *dmabuf)
{
	FREF(dmabuf->file);
}

enum pci_bus_speed
pcie_get_speed_cap(struct pci_dev *pdev)
{
	pci_chipset_tag_t	pc;
	pcitag_t		tag;
	int			pos ;
	pcireg_t		xcap, lnkcap = 0, lnkcap2 = 0;
	pcireg_t		id;
	enum pci_bus_speed	cap = PCI_SPEED_UNKNOWN;
	int			bus, device, function;

	if (pdev == NULL)
		return PCI_SPEED_UNKNOWN;

	pc = pdev->pc;
	tag = pdev->tag;

	if (!pci_get_capability(pc, tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL)) 
		return PCI_SPEED_UNKNOWN;

	id = pci_conf_read(pc, tag, PCI_ID_REG);
	pci_decompose_tag(pc, tag, &bus, &device, &function);

	/* we've been informed via and serverworks don't make the cut */
	if (PCI_VENDOR(id) == PCI_VENDOR_VIATECH ||
	    PCI_VENDOR(id) == PCI_VENDOR_RCC)
		return PCI_SPEED_UNKNOWN;

	lnkcap = pci_conf_read(pc, tag, pos + PCI_PCIE_LCAP);
	xcap = pci_conf_read(pc, tag, pos + PCI_PCIE_XCAP);
	if (PCI_PCIE_XCAP_VER(xcap) >= 2)
		lnkcap2 = pci_conf_read(pc, tag, pos + PCI_PCIE_LCAP2);

	lnkcap &= 0x0f;
	lnkcap2 &= 0xfe;

	if (lnkcap2) { /* PCIE GEN 3.0 */
		if (lnkcap2 & 0x02)
			cap = PCIE_SPEED_2_5GT;
		if (lnkcap2 & 0x04)
			cap = PCIE_SPEED_5_0GT;
		if (lnkcap2 & 0x08)
			cap = PCIE_SPEED_8_0GT;
		if (lnkcap2 & 0x10)
			cap = PCIE_SPEED_16_0GT;
		if (lnkcap2 & 0x20)
			cap = PCIE_SPEED_32_0GT;
		if (lnkcap2 & 0x40)
			cap = PCIE_SPEED_64_0GT;
	} else {
		if (lnkcap & 0x01)
			cap = PCIE_SPEED_2_5GT;
		if (lnkcap & 0x02)
			cap = PCIE_SPEED_5_0GT;
	}

	DRM_INFO("probing pcie caps for device %d:%d:%d 0x%04x:0x%04x = %x/%x\n",
	    bus, device, function, PCI_VENDOR(id), PCI_PRODUCT(id), lnkcap,
	    lnkcap2);
	return cap;
}

enum pcie_link_width
pcie_get_width_cap(struct pci_dev *pdev)
{
	pci_chipset_tag_t	pc = pdev->pc;
	pcitag_t		tag = pdev->tag;
	int			pos ;
	pcireg_t		lnkcap = 0;
	pcireg_t		id;
	int			bus, device, function;

	if (!pci_get_capability(pc, tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL)) 
		return PCIE_LNK_WIDTH_UNKNOWN;

	id = pci_conf_read(pc, tag, PCI_ID_REG);
	pci_decompose_tag(pc, tag, &bus, &device, &function);

	lnkcap = pci_conf_read(pc, tag, pos + PCI_PCIE_LCAP);

	DRM_INFO("probing pcie width for device %d:%d:%d 0x%04x:0x%04x = %x\n",
	    bus, device, function, PCI_VENDOR(id), PCI_PRODUCT(id), lnkcap);

	if (lnkcap)
		return (lnkcap & 0x3f0) >> 4;
	return PCIE_LNK_WIDTH_UNKNOWN;
}

bool
pcie_aspm_enabled(struct pci_dev *pdev)
{
	pci_chipset_tag_t	pc = pdev->pc;
	pcitag_t		tag = pdev->tag;
	int			pos ;
	pcireg_t		lcsr;

	if (!pci_get_capability(pc, tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL)) 
		return false;

	lcsr = pci_conf_read(pc, tag, pos + PCI_PCIE_LCSR);
	if ((lcsr & (PCI_PCIE_LCSR_ASPM_L0S | PCI_PCIE_LCSR_ASPM_L1)) != 0)
		return true;
	
	return false;
}

static wait_queue_head_t bit_waitq;
wait_queue_head_t var_waitq;
struct mutex wait_bit_mtx = MUTEX_INITIALIZER(IPL_TTY);

int
wait_on_bit(unsigned long *word, int bit, unsigned mode)
{
	int err;

	if (!test_bit(bit, word))
		return 0;

	mtx_enter(&wait_bit_mtx);
	while (test_bit(bit, word)) {
		err = msleep_nsec(word, &wait_bit_mtx, PWAIT | mode, "wtb",
		    INFSLP);
		if (err) {
			mtx_leave(&wait_bit_mtx);
			return 1;
		}
	}
	mtx_leave(&wait_bit_mtx);
	return 0;
}

int
wait_on_bit_timeout(unsigned long *word, int bit, unsigned mode, int timo)
{
	int err;

	if (!test_bit(bit, word))
		return 0;

	mtx_enter(&wait_bit_mtx);
	while (test_bit(bit, word)) {
		err = msleep(word, &wait_bit_mtx, PWAIT | mode, "wtb", timo);
		if (err) {
			mtx_leave(&wait_bit_mtx);
			return 1;
		}
	}
	mtx_leave(&wait_bit_mtx);
	return 0;
}

void
wake_up_bit(void *word, int bit)
{
	mtx_enter(&wait_bit_mtx);
	wakeup(word);
	mtx_leave(&wait_bit_mtx);
}

void
clear_and_wake_up_bit(int bit, void *word)
{
	clear_bit(bit, word);
	wake_up_bit(word, bit);
}

wait_queue_head_t *
bit_waitqueue(void *word, int bit)
{
	/* XXX hash table of wait queues? */
	return &bit_waitq;
}

wait_queue_head_t *
__var_waitqueue(void *p)
{
	/* XXX hash table of wait queues? */
	return &bit_waitq;
}

struct workqueue_struct *system_wq;
struct workqueue_struct *system_highpri_wq;
struct workqueue_struct *system_unbound_wq;
struct workqueue_struct *system_long_wq;
struct taskq *taskletq;

void
drm_linux_init(void)
{
	system_wq = (struct workqueue_struct *)
	    taskq_create("drmwq", 4, IPL_HIGH, 0);
	system_highpri_wq = (struct workqueue_struct *)
	    taskq_create("drmhpwq", 4, IPL_HIGH, 0);
	system_unbound_wq = (struct workqueue_struct *)
	    taskq_create("drmubwq", 4, IPL_HIGH, 0);
	system_long_wq = (struct workqueue_struct *)
	    taskq_create("drmlwq", 4, IPL_HIGH, 0);

	taskletq = taskq_create("drmtskl", 1, IPL_HIGH, 0);

	init_waitqueue_head(&bit_waitq);
	init_waitqueue_head(&var_waitq);

	pool_init(&idr_pool, sizeof(struct idr_entry), 0, IPL_TTY, 0,
	    "idrpl", NULL);
	pool_init(&xa_pool, sizeof(struct xarray_entry), 0, IPL_NONE, 0,
	    "xapl", NULL);

	kmap_atomic_va =
	    (vaddr_t)km_alloc(PAGE_SIZE, &kv_any, &kp_none, &kd_waitok);

#if NACPI > 0
	if (acpi_softc) {
		memcpy(&acpi_gbl_FADT, acpi_softc->sc_fadt,
		    sizeof(acpi_gbl_FADT));
	}
#endif
}

void
drm_linux_exit(void)
{
	pool_destroy(&xa_pool);
	pool_destroy(&idr_pool);

	taskq_destroy(taskletq);

	taskq_destroy((struct taskq *)system_long_wq);
	taskq_destroy((struct taskq *)system_unbound_wq);
	taskq_destroy((struct taskq *)system_highpri_wq);
	taskq_destroy((struct taskq *)system_wq);
}

#define PCIE_ECAP_RESIZE_BAR	0x15
#define RBCAP0			0x04
#define RBCTRL0			0x08
#define RBCTRL_BARINDEX_MASK	0x07
#define RBCTRL_BARSIZE_MASK	0x1f00
#define RBCTRL_BARSIZE_SHIFT	8

/* size in MB is 1 << nsize */
int
pci_resize_resource(struct pci_dev *pdev, int bar, int nsize)
{
	pcireg_t	reg;
	uint32_t	offset, capid;

	KASSERT(bar == 0);

	offset = PCI_PCIE_ECAP;

	/* search PCI Express Extended Capabilities */
	do {
		reg = pci_conf_read(pdev->pc, pdev->tag, offset);
		capid = PCI_PCIE_ECAP_ID(reg);
		if (capid == PCIE_ECAP_RESIZE_BAR)
			break;
		offset = PCI_PCIE_ECAP_NEXT(reg);
	} while (capid != 0);

	if (capid == 0) {
		printf("%s: could not find resize bar cap!\n", __func__);
		return -ENOTSUP;
	}

	reg = pci_conf_read(pdev->pc, pdev->tag, offset + RBCAP0);

	if ((reg & (1 << (nsize + 4))) == 0) {
		printf("%s size not supported\n", __func__);
		return -ENOTSUP;
	}

	reg = pci_conf_read(pdev->pc, pdev->tag, offset + RBCTRL0);
	if ((reg & RBCTRL_BARINDEX_MASK) != 0) {
		printf("%s BAR index not 0\n", __func__);
		return -EINVAL;
	}

	reg &= ~RBCTRL_BARSIZE_MASK;
	reg |= (nsize << RBCTRL_BARSIZE_SHIFT) & RBCTRL_BARSIZE_MASK;

	pci_conf_write(pdev->pc, pdev->tag, offset + RBCTRL0, reg);

	return 0;
}

TAILQ_HEAD(, shrinker) shrinkers = TAILQ_HEAD_INITIALIZER(shrinkers);

struct shrinker *
shrinker_alloc(u_int flags, const char *format, ...)
{
	struct shrinker *s;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	s->seeks = DEFAULT_SEEKS;
	return s;
}

void
shrinker_register(struct shrinker *shrinker)
{
	TAILQ_INSERT_TAIL(&shrinkers, shrinker, next);
}

void
shrinker_free(struct shrinker *shrinker)
{
	TAILQ_REMOVE(&shrinkers, shrinker, next);
	kfree(shrinker);
}

unsigned long
drmbackoff(long npages)
{
	struct shrink_control sc;
	struct shrinker *shrinker;
	u_long ret, freed = 0;

	shrinker = TAILQ_FIRST(&shrinkers);
	while (shrinker && npages > 0) {
		sc.nr_to_scan = npages;
		ret = shrinker->scan_objects(shrinker, &sc);
		if (ret == SHRINK_STOP)
			break;
		npages -= ret;
		freed += ret;
		shrinker = TAILQ_NEXT(shrinker, next);
	}

	return freed;
}

void *
bitmap_zalloc(u_int n, gfp_t flags)
{
	return kcalloc(BITS_TO_LONGS(n), sizeof(long), flags);
}

void
bitmap_free(void *p)
{
	kfree(p);
}

int
atomic_dec_and_mutex_lock(volatile int *v, struct rwlock *lock)
{
	if (atomic_add_unless(v, -1, 1))
		return 0;

	rw_enter_write(lock);
	if (atomic_dec_return(v) == 0)
		return 1;
	rw_exit_write(lock);
	return 0;
}

int
printk(const char *fmt, ...)
{
	int ret, level;
	va_list ap;

	if (fmt != NULL && *fmt == '\001') {
		level = fmt[1];
#ifndef DRMDEBUG
		if (level >= KERN_INFO[1] && level <= '9')
			return 0;
#endif
		fmt += 2;
	}

	va_start(ap, fmt);
	ret = vprintf(fmt, ap);
	va_end(ap);

	return ret;
}

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

struct interval_tree_node *
interval_tree_iter_first(struct rb_root_cached *root, unsigned long start,
    unsigned long last)
{
	struct interval_tree_node *node;
	struct rb_node *rb;

	for (rb = rb_first_cached(root); rb; rb = rb_next(rb)) {
		node = rb_entry(rb, typeof(*node), rb);
		if (LAST(node) >= start && START(node) <= last)
			return node;
	}
	return NULL;
}

void
interval_tree_remove(struct interval_tree_node *node,
    struct rb_root_cached *root) 
{
	rb_erase_cached(&node->rb, root);
}

void
interval_tree_insert(struct interval_tree_node *node,
    struct rb_root_cached *root)
{
	struct rb_node **iter = &root->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct interval_tree_node *iter_node;

	while (*iter) {
		parent = *iter;
		iter_node = rb_entry(*iter, struct interval_tree_node, rb);

		if (node->start < iter_node->start)
			iter = &(*iter)->rb_left;
		else
			iter = &(*iter)->rb_right;
	}

	rb_link_node(&node->rb, parent, iter);
	rb_insert_color_cached(&node->rb, root, false);
}

int
syncfile_read(struct file *fp, struct uio *uio, int fflags)
{
	return ENXIO;
}

int
syncfile_write(struct file *fp, struct uio *uio, int fflags)
{
	return ENXIO;
}

int
syncfile_ioctl(struct file *fp, u_long com, caddr_t data, struct proc *p)
{
	return ENOTTY;
}

int
syncfile_kqfilter(struct file *fp, struct knote *kn)
{
	return EINVAL;
}

int
syncfile_stat(struct file *fp, struct stat *st, struct proc *p)
{
	memset(st, 0, sizeof(*st));
	st->st_mode = S_IFIFO;	/* XXX */
	return 0;
}

int
syncfile_close(struct file *fp, struct proc *p)
{
	struct sync_file *sf = fp->f_data;

	dma_fence_put(sf->fence);
	fp->f_data = NULL;
	free(sf, M_DRM, sizeof(struct sync_file));
	return 0;
}

int
syncfile_seek(struct file *fp, off_t *offset, int whence, struct proc *p)
{
	off_t newoff;

	if (*offset != 0)
		return EINVAL;

	switch (whence) {
	case SEEK_SET:
		newoff = 0;
		break;
	case SEEK_END:
		newoff = 0;
		break;
	default:
		return EINVAL;
	}
	mtx_enter(&fp->f_mtx);
	fp->f_offset = newoff;
	mtx_leave(&fp->f_mtx);
	*offset = newoff;
	return 0;
}

const struct fileops syncfileops = {
	.fo_read	= syncfile_read,
	.fo_write	= syncfile_write,
	.fo_ioctl	= syncfile_ioctl,
	.fo_kqfilter	= syncfile_kqfilter,
	.fo_stat	= syncfile_stat,
	.fo_close	= syncfile_close,
	.fo_seek	= syncfile_seek,
};

void
fd_install(int fd, struct file *fp)
{
	struct proc *p = curproc;
	struct filedesc *fdp = p->p_fd;

	fdplock(fdp);
	/* all callers use get_unused_fd_flags(O_CLOEXEC) */
	fdinsert(fdp, fd, UF_EXCLOSE, fp);
	fdpunlock(fdp);
}

void
fput(struct file *fp)
{
	FRELE(fp, curproc);
}

int
get_unused_fd_flags(unsigned int flags)
{
	struct proc *p = curproc;
	struct filedesc *fdp = p->p_fd;
	int error, fd;

	KASSERT((flags & O_CLOEXEC) != 0);

	fdplock(fdp);
retryalloc:
	if ((error = fdalloc(p, 0, &fd)) != 0) {
		if (error == ENOSPC) {
			fdexpand(p);
			goto retryalloc;
		}
		fdpunlock(fdp);
		return -1;
	}
	fdpunlock(fdp);

	return fd;
}

void
put_unused_fd(int fd)
{
	struct filedesc *fdp = curproc->p_fd;

	fdplock(fdp);
	fdremove(fdp, fd);
	fdpunlock(fdp);
}

struct dma_fence *
sync_file_get_fence(int fd)
{
	struct proc *p = curproc;
	struct filedesc *fdp = p->p_fd;
	struct file *fp;
	struct sync_file *sf;
	struct dma_fence *f;

	if ((fp = fd_getfile(fdp, fd)) == NULL)
		return NULL;

	if (fp->f_type != DTYPE_SYNC) {
		FRELE(fp, p);
		return NULL;
	}
	sf = fp->f_data;
	f = dma_fence_get(sf->fence);
	FRELE(sf->file, p);
	return f;
}

struct sync_file *
sync_file_create(struct dma_fence *fence)
{
	struct proc *p = curproc;
	struct sync_file *sf;
	struct file *fp;

	fp = fnew(p);
	if (fp == NULL)
		return NULL;
	fp->f_type = DTYPE_SYNC;
	fp->f_ops = &syncfileops;
	sf = malloc(sizeof(struct sync_file), M_DRM, M_WAITOK | M_ZERO);
	sf->file = fp;
	sf->fence = dma_fence_get(fence);
	fp->f_data = sf;
	return sf;
}

void *
memremap(phys_addr_t phys_addr, size_t size, int flags)
{
	STUB();
	return NULL;
}

void
memunmap(void *addr)
{
	STUB();
}

void
kfree_const(const void *addr)
{
        kfree(addr);
}

#include <linux/platform_device.h>

bus_dma_tag_t
dma_tag_lookup(struct device *dev)
{
	extern struct cfdriver drm_cd;
	struct drm_device *drm;
	int i;

	for (i = 0; i < drm_cd.cd_ndevs; i++) {
		drm = drm_cd.cd_devs[i];
		if (drm && drm->dev == dev)
			return drm->dmat;
	}

	return ((struct platform_device *)dev)->dmat;
}

LIST_HEAD(, drm_dmamem) dmamem_list = LIST_HEAD_INITIALIZER(dmamem_list);

void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
    int gfp)
{
	bus_dma_tag_t dmat = dma_tag_lookup(dev);
	struct drm_dmamem *mem;

	mem = drm_dmamem_alloc(dmat, size, PAGE_SIZE, 1, size,
	    BUS_DMA_COHERENT, 0);
	if (mem == NULL)
		return NULL;
	*dma_handle = mem->map->dm_segs[0].ds_addr;
	LIST_INSERT_HEAD(&dmamem_list, mem, next);
	return mem->kva;
}

void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
    dma_addr_t dma_handle)
{
	bus_dma_tag_t dmat = dma_tag_lookup(dev);
	struct drm_dmamem *mem;

	LIST_FOREACH(mem, &dmamem_list, next) {
		if (mem->kva == cpu_addr)
			break;
	}
	KASSERT(mem);
	KASSERT(mem->size == size);
	KASSERT(mem->map->dm_segs[0].ds_addr == dma_handle);

	LIST_REMOVE(mem, next);
	drm_dmamem_free(dmat, mem);
}

int
dma_get_sgtable(struct device *dev, struct sg_table *sgt, void *cpu_addr,
    dma_addr_t dma_addr, size_t size)
{
	paddr_t pa;
	int ret;

	if (!pmap_extract(pmap_kernel(), (vaddr_t)cpu_addr, &pa))
		return -EINVAL;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret)
		return ret;

	sg_set_page(sgt->sgl, PHYS_TO_VM_PAGE(pa), size, 0);
	return 0;
}

dma_addr_t
dma_map_resource(struct device *dev, phys_addr_t phys_addr, size_t size,
    enum dma_data_direction dir, u_long attr)
{
	bus_dma_tag_t dmat= dma_tag_lookup(dev);
	bus_dmamap_t map;
	bus_dma_segment_t seg;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &map))
		return DMA_MAPPING_ERROR;
	seg.ds_addr = phys_addr;
	seg.ds_len = size;
	if (bus_dmamap_load_raw(dmat, map, &seg, 1, size, BUS_DMA_WAITOK)) {
		bus_dmamap_destroy(dmat, map);
		return DMA_MAPPING_ERROR;
	}

	return map->dm_segs[0].ds_addr;
}

#ifdef BUS_DMA_FIXED

#include <linux/iommu.h>

size_t
iommu_map_sgtable(struct iommu_domain *domain, u_long iova,
    struct sg_table *sgt, int prot)
{
	bus_dma_segment_t seg;
	int error;

	error = bus_dmamap_create(domain->dmat, sgt->sgl->length, 1,
	    sgt->sgl->length, 0, BUS_DMA_WAITOK, &sgt->dmamap);
	if (error)
		return -ENOMEM;

	sgt->dmamap->dm_segs[0].ds_addr = iova;
	sgt->dmamap->dm_segs[0].ds_len = sgt->sgl->length;
	sgt->dmamap->dm_nsegs = 1;
	seg.ds_addr = VM_PAGE_TO_PHYS(sgt->sgl->__page);
	seg.ds_len = sgt->sgl->length;
	error = bus_dmamap_load_raw(domain->dmat, sgt->dmamap, &seg, 1,
	    sgt->sgl->length, BUS_DMA_WAITOK | BUS_DMA_FIXED);
	if (error)
		return -ENOMEM;

	return sg_dma_len(sgt->sgl);
}

size_t
iommu_unmap(struct iommu_domain *domain, u_long iova, size_t size)
{
	STUB();
	return 0;
}

struct iommu_domain *
iommu_get_domain_for_dev(struct device *dev)
{
	STUB();
	return NULL;
}

phys_addr_t
iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	STUB();
	return 0;
}

struct iommu_domain *
iommu_domain_alloc(struct bus_type *type)
{
	return malloc(sizeof(struct iommu_domain), M_DEVBUF, M_WAITOK | M_ZERO);
}

int
iommu_attach_device(struct iommu_domain *domain, struct device *dev)
{
	struct platform_device *pdev = (struct platform_device *)dev;

	domain->dmat = pdev->dmat;
	return 0;
}

#endif

#include <linux/component.h>

struct component {
	struct device *dev;
	struct device *adev;
	const struct component_ops *ops;
	SLIST_ENTRY(component) next;
};

SLIST_HEAD(,component) component_list = SLIST_HEAD_INITIALIZER(component_list);

int
component_add(struct device *dev, const struct component_ops *ops)
{
	struct component *component;

	component = malloc(sizeof(*component), M_DEVBUF, M_WAITOK | M_ZERO);
	component->dev = dev;
	component->ops = ops;
	SLIST_INSERT_HEAD(&component_list, component, next);
	return 0;
}

int
component_add_typed(struct device *dev, const struct component_ops *ops,
	int type)
{
	return component_add(dev, ops);
}

int
component_bind_all(struct device *dev, void *data)
{
	struct component *component;
	int ret = 0;

	SLIST_FOREACH(component, &component_list, next) {
		if (component->adev == dev) {
			ret = component->ops->bind(component->dev, NULL, data);
			if (ret)
				break;
		}
	}

	return ret;
}

struct component_match_entry {
	int (*compare)(struct device *, void *);
	void *data;
};

struct component_match {
	struct component_match_entry match[4];
	int nmatches;
};

int
component_master_add_with_match(struct device *dev,
    const struct component_master_ops *ops, struct component_match *match)
{
	struct component *component;
	int found = 0;
	int i, ret;

	SLIST_FOREACH(component, &component_list, next) {
		for (i = 0; i < match->nmatches; i++) {
			struct component_match_entry *m = &match->match[i];
			if (m->compare(component->dev, m->data)) {
				component->adev = dev;
				found = 1;
				break;
			}
		}
	}

	if (found) {
		ret = ops->bind(dev);
		if (ret)
			return ret;
	}

	return 0;
}

#ifdef __HAVE_FDT

#include <linux/platform_device.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <machine/fdt.h>

LIST_HEAD(, platform_device) pdev_list = LIST_HEAD_INITIALIZER(pdev_list);

void
platform_device_register(struct platform_device *pdev)
{
	int i;

	pdev->num_resources = pdev->faa->fa_nreg;
	if (pdev->faa->fa_nreg > 0) {
		pdev->resource = mallocarray(pdev->faa->fa_nreg,
		    sizeof(*pdev->resource), M_DEVBUF, M_WAITOK | M_ZERO);
		for (i = 0; i < pdev->faa->fa_nreg; i++) {
			pdev->resource[i].start = pdev->faa->fa_reg[i].addr;
			pdev->resource[i].end = pdev->faa->fa_reg[i].addr +
			    pdev->faa->fa_reg[i].size - 1;
		}
	}

	pdev->parent = pdev->dev.dv_parent;
	pdev->node = pdev->faa->fa_node;
	pdev->iot = pdev->faa->fa_iot;
	pdev->dmat = pdev->faa->fa_dmat;
	LIST_INSERT_HEAD(&pdev_list, pdev, next);
}


struct resource *
platform_get_resource(struct platform_device *pdev, u_int type, u_int num)
{
	KASSERT(num < pdev->num_resources);
	return &pdev->resource[num];
}

void __iomem *
devm_platform_ioremap_resource_byname(struct platform_device *pdev,
				      const char *name)
{
	bus_space_handle_t ioh;
	int err, idx;

	idx = OF_getindex(pdev->node, name, "reg-names");
	if (idx == -1 || idx >= pdev->num_resources)
		return ERR_PTR(-EINVAL);

	err = bus_space_map(pdev->iot, pdev->resource[idx].start,
	    pdev->resource[idx].end - pdev->resource[idx].start + 1,
	    BUS_SPACE_MAP_LINEAR, &ioh);
	if (err)
		return ERR_PTR(-err);

	return bus_space_vaddr(pdev->iot, ioh);
}

#include <dev/ofw/ofw_clock.h>
#include <linux/clk.h>

struct clk *
devm_clk_get(struct device *dev, const char *name)
{
	struct platform_device *pdev = (struct platform_device *)dev;
	struct clk *clk;

	clk = malloc(sizeof(*clk), M_DEVBUF, M_WAITOK);
	clk->freq = clock_get_frequency(pdev->node, name);
	return clk;
}

u_long
clk_get_rate(struct clk *clk)
{
	return clk->freq;
}

#include <linux/gpio/consumer.h>
#include <dev/ofw/ofw_gpio.h>

struct gpio_desc {
	uint32_t gpios[4];
};

struct gpio_desc *
devm_gpiod_get_optional(struct device *dev, const char *name, int flags)
{
	struct platform_device *pdev = (struct platform_device *)dev;
	struct gpio_desc *desc;
	char fullname[128];
	int len;

	snprintf(fullname, sizeof(fullname), "%s-gpios", name);

	desc = malloc(sizeof(*desc), M_DEVBUF, M_WAITOK | M_ZERO);
	len = OF_getpropintarray(pdev->node, fullname, desc->gpios,
	     sizeof(desc->gpios));
	KASSERT(len <= sizeof(desc->gpios));
	if (len < 0) {
		free(desc, M_DEVBUF, sizeof(*desc));
		return NULL;
	}

	switch (flags) {
	case GPIOD_IN:
		gpio_controller_config_pin(desc->gpios, GPIO_CONFIG_INPUT);
		break;
	case GPIOD_OUT_HIGH:
		gpio_controller_config_pin(desc->gpios, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(desc->gpios, 1);
		break;
	default:
		panic("%s: unimplemented flags 0x%x", __func__, flags);
	}

	return desc;
}

int
gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	return gpio_controller_get_pin(((struct gpio_desc *)desc)->gpios);
}

struct phy {
	int node;
	const char *name;
};

struct phy *
devm_phy_optional_get(struct device *dev, const char *name)
{
	struct platform_device *pdev = (struct platform_device *)dev;
	struct phy *phy;
	int idx;

	idx = OF_getindex(pdev->node, name, "phy-names");
	if (idx == -1)
		return NULL;

	phy = malloc(sizeof(*phy), M_DEVBUF, M_WAITOK);
	phy->node = pdev->node;
	phy->name = name;

	return phy;
}

struct bus_type platform_bus_type;

#include <dev/ofw/ofw_misc.h>

#include <linux/of.h>
#include <linux/platform_device.h>

struct device_node *
__of_devnode(void *arg)
{
	struct device *dev = container_of(arg, struct device, of_node);
	struct platform_device *pdev = (struct platform_device *)dev;

	return (struct device_node *)(uintptr_t)pdev->node;
}

int
__of_device_is_compatible(struct device_node *np, const char *compatible)
{
	return OF_is_compatible((uintptr_t)np, compatible);
}

int
__of_property_present(struct device_node *np, const char *propname)
{
	return OF_getpropbool((uintptr_t)np, (char *)propname);
}

int
__of_property_read_variable_u32_array(struct device_node *np,
    const char *propname, uint32_t *out_values, size_t sz_min, size_t sz_max)
{
	int len;

	len = OF_getpropintarray((uintptr_t)np, (char *)propname, out_values,
	    sz_max * sizeof(*out_values));
	if (len < 0)
		return -EINVAL;
	if (len == 0)
		return -ENODATA;
	if (len < sz_min * sizeof(*out_values) ||
	    len > sz_max * sizeof(*out_values))
		return -EOVERFLOW;
	if (sz_min == 1 && sz_max == 1)
		return 0;
	return len / sizeof(*out_values);
}

int
__of_property_read_variable_u64_array(struct device_node *np,
    const char *propname, uint64_t *out_values, size_t sz_min, size_t sz_max)
{
	int len;

	len = OF_getpropint64array((uintptr_t)np, (char *)propname, out_values,
	    sz_max * sizeof(*out_values));
	if (len < 0)
		return -EINVAL;
	if (len == 0)
		return -ENODATA;
	if (len < sz_min * sizeof(*out_values) ||
	    len > sz_max * sizeof(*out_values))
		return -EOVERFLOW;
	if (sz_min == 1 && sz_max == 1)
		return 0;
	return len / sizeof(*out_values);
}

int
__of_property_match_string(struct device_node *np,
    const char *propname, const char *str)
{
	int idx;

	idx = OF_getindex((uintptr_t)np, str, propname);
	if (idx == -1)
		return -ENODATA;
	return idx;
}

struct device_node *
__of_parse_phandle(struct device_node *np, const char *propname, int idx)
{
	uint32_t phandles[16] = {};
	int len, node;

	len = OF_getpropintarray((uintptr_t)np, (char *)propname, phandles,
	    sizeof(phandles));
	if (len < (idx + 1) * sizeof(uint32_t))
		return NULL;

	node = OF_getnodebyphandle(phandles[idx]);
	if (node == 0)
		return NULL;

	return (struct device_node *)(uintptr_t)node;
}

int
__of_parse_phandle_with_args(struct device_node *np, const char *propname,
    const char *cellsname, int idx, struct of_phandle_args *args)
{
	uint32_t phandles[16] = {};
	int i, len, node;

	len = OF_getpropintarray((uintptr_t)np, (char *)propname, phandles,
	    sizeof(phandles));
	if (len < (idx + 1) * sizeof(uint32_t))
		return -ENOENT;

	node = OF_getnodebyphandle(phandles[idx]);
	if (node == 0)
		return -ENOENT;

	args->np = (struct device_node *)(uintptr_t)node;
	args->args_count = OF_getpropint(node, (char *)cellsname, 0);
	for (i = 0; i < args->args_count; i++)
		args->args[i] = phandles[i + 1];

	return 0;
}

int
of_address_to_resource(struct device_node *np, int idx, struct resource *res)
{
	uint64_t reg[16] = {};
	int len;

	KASSERT(idx < 8);

	len = OF_getpropint64array((uintptr_t)np, "reg", reg, sizeof(reg));
	if (len < 0 || idx >= (len / (2 * sizeof(uint64_t))))
		return -EINVAL;

	res->start = reg[2 * idx];
	res->end = reg[2 * idx] + reg[2 * idx + 1] - 1;

	return 0;
}

static int
next_node(int node)
{
	int peer = OF_peer(node);

	while (node && !peer) {
		node = OF_parent(node);
		if (node)
			peer = OF_peer(node);
	}

	return peer;
}

static int
find_matching_node(int node, const struct of_device_id *id)
{
	int child, match;
	int i;

	for (child = OF_child(node); child; child = OF_peer(child)) {
		match = find_matching_node(child, id);
		if (match)
			return match;
	}

	for (i = 0; id[i].compatible; i++) {
		if (OF_is_compatible(node, id[i].compatible))
			return node;
	}

	return 0;
}

struct device_node *
__matching_node(struct device_node *np, const struct of_device_id *id)
{
	int node = OF_peer(0);
	int match;

	if (np)
		node = next_node((uintptr_t)np);
	while (node) {
		match = find_matching_node(node, id);
		if (match)
			return (struct device_node *)(uintptr_t)match;
		node = next_node(node);
	}

	return NULL;
}

struct platform_device *
of_platform_device_create(struct device_node *np, const char *bus_id,
    struct device *parent)
{
	struct platform_device *pdev;

	pdev = malloc(sizeof(*pdev), M_DEVBUF, M_WAITOK | M_ZERO);
	pdev->node = (intptr_t)np;
	pdev->parent = parent;

	LIST_INSERT_HEAD(&pdev_list, pdev, next);

	return pdev;
}

struct platform_device *
of_find_device_by_node(struct device_node *np)
{
	struct platform_device *pdev;

	LIST_FOREACH(pdev, &pdev_list, next) {
		if (pdev->node == (intptr_t)np)
			return pdev;
	}

	return NULL;
}

int
of_device_is_available(struct device_node *np)
{
	char status[32];

	if (OF_getprop((uintptr_t)np, "status", status, sizeof(status)) > 0 &&
	    strcmp(status, "disabled") == 0)
		return 0;

	return 1;
}

int
of_dma_configure(struct device *dev, struct device_node *np, int force_dma)
{
	struct platform_device *pdev = (struct platform_device *)dev;
	bus_dma_tag_t dmat = dma_tag_lookup(pdev->parent);

	pdev->dmat = iommu_device_map(pdev->node, dmat);
	return 0;
}

struct device_node *
__of_get_compatible_child(void *p, const char *compat)
{
	struct device *dev = container_of(p, struct device, of_node);
	struct platform_device *pdev = (struct platform_device *)dev;
	int child;

	for (child = OF_child(pdev->node); child; child = OF_peer(child)) {
		if (OF_is_compatible(child, compat))
			return (struct device_node *)(uintptr_t)child;
	}
	return NULL;
}

struct device_node *
__of_get_child_by_name(void *p, const char *name)
{
	struct device *dev = container_of(p, struct device, of_node);
	struct platform_device *pdev = (struct platform_device *)dev;
	int child;

	child = OF_getnodebyname(pdev->node, name);
	if (child == 0)
		return NULL;
	return (struct device_node *)(uintptr_t)child;
}

int
component_compare_of(struct device *dev, void *data)
{
	struct platform_device *pdev = (struct platform_device *)dev;

	return (pdev->node == (intptr_t)data);
}

void
drm_of_component_match_add(struct device *master,
			   struct component_match **matchptr,
			   int (*compare)(struct device *, void *),
			   struct device_node *np)
{
	struct component_match *match = *matchptr;

	if (match == NULL) {
		match = malloc(sizeof(struct component_match),
		    M_DEVBUF, M_WAITOK | M_ZERO);
		*matchptr = match;
	}

	KASSERT(match->nmatches < nitems(match->match));
	match->match[match->nmatches].compare = compare;
	match->match[match->nmatches].data = np;
	match->nmatches++;
}

#endif
