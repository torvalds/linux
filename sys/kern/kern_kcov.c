/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2018 The FreeBSD Foundation. All rights reserved.
 * Copyright (C) 2018, 2019 Andrew Turner
 *
 * This software was developed by Mitchell Horne under sponsorship of
 * the FreeBSD Foundation.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kcov.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>

MALLOC_DEFINE(M_KCOV_INFO, "kcovinfo", "KCOV info type");

#define	KCOV_ELEMENT_SIZE	sizeof(uint64_t)

/*
 * To know what the code can safely perform at any point in time we use a
 * state machine. In the normal case the state transitions are:
 *
 * OPEN -> READY -> RUNNING -> DYING
 *  |       | ^        |        ^ ^
 *  |       | +--------+        | |
 *  |       +-------------------+ |
 *  +-----------------------------+
 *
 * The states are:
 *  OPEN:   The kcov fd has been opened, but no buffer is available to store
 *          coverage data.
 *  READY:  The buffer to store coverage data has been allocated. Userspace
 *          can set this by using ioctl(fd, KIOSETBUFSIZE, entries);. When
 *          this has been set the buffer can be written to by the kernel,
 *          and mmaped by userspace.
 * RUNNING: The coverage probes are able to store coverage data in the buffer.
 *          This is entered with ioctl(fd, KIOENABLE, mode);. The READY state
 *          can be exited by ioctl(fd, KIODISABLE); or exiting the thread to
 *          return to the READY state to allow tracing to be reused, or by
 *          closing the kcov fd to enter the DYING state.
 * DYING:   The fd has been closed. All states can enter into this state when
 *          userspace closes the kcov fd.
 *
 * We need to be careful when moving into and out of the RUNNING state. As
 * an interrupt may happen while this is happening the ordering of memory
 * operations is important so struct kcov_info is valid for the tracing
 * functions.
 *
 * When moving into the RUNNING state prior stores to struct kcov_info need
 * to be observed before the state is set. This allows for interrupts that
 * may call into one of the coverage functions to fire at any point while
 * being enabled and see a consistent struct kcov_info.
 *
 * When moving out of the RUNNING state any later stores to struct kcov_info
 * need to be observed after the state is set. As with entering this is to
 * present a consistent struct kcov_info to interrupts.
 */
typedef enum {
	KCOV_STATE_INVALID,
	KCOV_STATE_OPEN,	/* The device is open, but with no buffer */
	KCOV_STATE_READY,	/* The buffer has been allocated */
	KCOV_STATE_RUNNING,	/* Recording trace data */
	KCOV_STATE_DYING,	/* The fd was closed */
} kcov_state_t;

/*
 * (l) Set while holding the kcov_lock mutex and not in the RUNNING state.
 * (o) Only set once while in the OPEN state. Cleaned up while in the DYING
 *     state, and with no thread associated with the struct kcov_info.
 * (s) Set atomically to enter or exit the RUNNING state, non-atomically
 *     otherwise. See above for a description of the other constraints while
 *     moving into or out of the RUNNING state.
 */
struct kcov_info {
	struct thread	*thread;	/* (l) */
	vm_object_t	bufobj;		/* (o) */
	vm_offset_t	kvaddr;		/* (o) */
	size_t		entries;	/* (o) */
	size_t		bufsize;	/* (o) */
	kcov_state_t	state;		/* (s) */
	int		mode;		/* (l) */
};

/* Prototypes */
static d_open_t		kcov_open;
static d_close_t	kcov_close;
static d_mmap_single_t	kcov_mmap_single;
static d_ioctl_t	kcov_ioctl;

static int  kcov_alloc(struct kcov_info *info, size_t entries);
static void kcov_free(struct kcov_info *info);
static void kcov_init(const void *unused);

static struct cdevsw kcov_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	kcov_open,
	.d_close =	kcov_close,
	.d_mmap_single = kcov_mmap_single,
	.d_ioctl =	kcov_ioctl,
	.d_name =	"kcov",
};

SYSCTL_NODE(_kern, OID_AUTO, kcov, CTLFLAG_RW, 0, "Kernel coverage");

static u_int kcov_max_entries = KCOV_MAXENTRIES;
SYSCTL_UINT(_kern_kcov, OID_AUTO, max_entries, CTLFLAG_RW,
    &kcov_max_entries, 0,
    "Maximum number of entries in the kcov buffer");

static struct mtx kcov_lock;
static int active_count;

static struct kcov_info *
get_kinfo(struct thread *td)
{
	struct kcov_info *info;

	/* We might have a NULL thread when releasing the secondary CPUs */
	if (td == NULL)
		return (NULL);

	/*
	 * We are in an interrupt, stop tracing as it is not explicitly
	 * part of a syscall.
	 */
	if (td->td_intr_nesting_level > 0 || td->td_intr_frame != NULL)
		return (NULL);

	/*
	 * If info is NULL or the state is not running we are not tracing.
	 */
	info = td->td_kcov_info;
	if (info == NULL ||
	    atomic_load_acq_int(&info->state) != KCOV_STATE_RUNNING)
		return (NULL);

	return (info);
}

static void
trace_pc(uintptr_t ret)
{
	struct thread *td;
	struct kcov_info *info;
	uint64_t *buf, index;

	td = curthread;
	info = get_kinfo(td);
	if (info == NULL)
		return;

	/*
	 * Check we are in the PC-trace mode.
	 */
	if (info->mode != KCOV_MODE_TRACE_PC)
		return;

	KASSERT(info->kvaddr != 0,
	    ("__sanitizer_cov_trace_pc: NULL buf while running"));

	buf = (uint64_t *)info->kvaddr;

	/* The first entry of the buffer holds the index */
	index = buf[0];
	if (index + 2 > info->entries)
		return;

	buf[index + 1] = ret;
	buf[0] = index + 1;
}

static bool
trace_cmp(uint64_t type, uint64_t arg1, uint64_t arg2, uint64_t ret)
{
	struct thread *td;
	struct kcov_info *info;
	uint64_t *buf, index;

	td = curthread;
	info = get_kinfo(td);
	if (info == NULL)
		return (false);

	/*
	 * Check we are in the comparison-trace mode.
	 */
	if (info->mode != KCOV_MODE_TRACE_CMP)
		return (false);

	KASSERT(info->kvaddr != 0,
	    ("__sanitizer_cov_trace_pc: NULL buf while running"));

	buf = (uint64_t *)info->kvaddr;

	/* The first entry of the buffer holds the index */
	index = buf[0];

	/* Check we have space to store all elements */
	if (index * 4 + 4 + 1 > info->entries)
		return (false);

	while (1) {
		buf[index * 4 + 1] = type;
		buf[index * 4 + 2] = arg1;
		buf[index * 4 + 3] = arg2;
		buf[index * 4 + 4] = ret;

		if (atomic_cmpset_64(&buf[0], index, index + 1))
			break;
		buf[0] = index;
	}

	return (true);
}

/*
 * The fd is being closed, cleanup everything we can.
 */
static void
kcov_mmap_cleanup(void *arg)
{
	struct kcov_info *info = arg;
	struct thread *thread;

	mtx_lock_spin(&kcov_lock);
	/*
	 * Move to KCOV_STATE_DYING to stop adding new entries.
	 *
	 * If the thread is running we need to wait until thread exit to
	 * clean up as it may currently be adding a new entry. If this is
	 * the case being in KCOV_STATE_DYING will signal that the buffer
	 * needs to be cleaned up.
	 */
	atomic_store_int(&info->state, KCOV_STATE_DYING);
	atomic_thread_fence_seq_cst();
	thread = info->thread;
	mtx_unlock_spin(&kcov_lock);

	if (thread != NULL)
		return;

	/*
	 * We can safely clean up the info struct as it is in the
	 * KCOV_STATE_DYING state with no thread associated.
	 *
	 * The KCOV_STATE_DYING stops new threads from using it.
	 * The lack of a thread means nothing is currently using the buffers.
	 */
	kcov_free(info);
}

static int
kcov_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct kcov_info *info;
	int error;

	info = malloc(sizeof(struct kcov_info), M_KCOV_INFO, M_ZERO | M_WAITOK);
	info->state = KCOV_STATE_OPEN;
	info->thread = NULL;
	info->mode = -1;

	if ((error = devfs_set_cdevpriv(info, kcov_mmap_cleanup)) != 0)
		kcov_mmap_cleanup(info);

	return (error);
}

static int
kcov_close(struct cdev *dev, int fflag, int devtype, struct thread *td)
{
	struct kcov_info *info;
	int error;


	if ((error = devfs_get_cdevpriv((void **)&info)) != 0)
		return (error);

	KASSERT(info != NULL, ("kcov_close with no kcov_info structure"));

	/* Trying to close, but haven't disabled */
	if (info->state == KCOV_STATE_RUNNING)
		return (EBUSY);

	return (0);
}

static int
kcov_mmap_single(struct cdev *dev, vm_ooffset_t *offset, vm_size_t size,
    struct vm_object **object, int nprot)
{
	struct kcov_info *info;
	int error;

	if ((nprot & (PROT_EXEC | PROT_READ | PROT_WRITE)) !=
	    (PROT_READ | PROT_WRITE))
		return (EINVAL);

	if ((error = devfs_get_cdevpriv((void **)&info)) != 0)
		return (error);

	if (info->kvaddr == 0 || size / KCOV_ELEMENT_SIZE != info->entries)
		return (EINVAL);

	vm_object_reference(info->bufobj);
	*offset = 0;
	*object = info->bufobj;
	return (0);
}

static int
kcov_alloc(struct kcov_info *info, size_t entries)
{
	size_t n, pages;
	vm_page_t m;

	KASSERT(info->kvaddr == 0, ("kcov_alloc: Already have a buffer"));
	KASSERT(info->state == KCOV_STATE_OPEN,
	    ("kcov_alloc: Not in open state (%x)", info->state));

	if (entries < 2 || entries > kcov_max_entries)
		return (EINVAL);

	/* Align to page size so mmap can't access other kernel memory */
	info->bufsize = roundup2(entries * KCOV_ELEMENT_SIZE, PAGE_SIZE);
	pages = info->bufsize / PAGE_SIZE;

	if ((info->kvaddr = kva_alloc(info->bufsize)) == 0)
		return (ENOMEM);

	info->bufobj = vm_pager_allocate(OBJT_PHYS, 0, info->bufsize,
	    PROT_READ | PROT_WRITE, 0, curthread->td_ucred);

	VM_OBJECT_WLOCK(info->bufobj);
	for (n = 0; n < pages; n++) {
		m = vm_page_grab(info->bufobj, n,
		    VM_ALLOC_NOBUSY | VM_ALLOC_ZERO | VM_ALLOC_WIRED);
		m->valid = VM_PAGE_BITS_ALL;
		pmap_qenter(info->kvaddr + n * PAGE_SIZE, &m, 1);
	}
	VM_OBJECT_WUNLOCK(info->bufobj);

	info->entries = entries;

	return (0);
}

static void
kcov_free(struct kcov_info *info)
{
	vm_page_t m;
	size_t i;

	if (info->kvaddr != 0) {
		pmap_qremove(info->kvaddr, info->bufsize / PAGE_SIZE);
		kva_free(info->kvaddr, info->bufsize);
	}
	if (info->bufobj != NULL) {
		VM_OBJECT_WLOCK(info->bufobj);
		m = vm_page_lookup(info->bufobj, 0);
		for (i = 0; i < info->bufsize / PAGE_SIZE; i++) {
			vm_page_lock(m);
			vm_page_unwire_noq(m);
			vm_page_unlock(m);

			m = vm_page_next(m);
		}
		VM_OBJECT_WUNLOCK(info->bufobj);
		vm_object_deallocate(info->bufobj);
	}
	free(info, M_KCOV_INFO);
}

static int
kcov_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag __unused,
    struct thread *td)
{
	struct kcov_info *info;
	int mode, error;

	if ((error = devfs_get_cdevpriv((void **)&info)) != 0)
		return (error);

	if (cmd == KIOSETBUFSIZE) {
		/*
		 * Set the size of the coverage buffer. Should be called
		 * before enabling coverage collection for that thread.
		 */
		if (info->state != KCOV_STATE_OPEN) {
			return (EBUSY);
		}
		error = kcov_alloc(info, *(u_int *)data);
		if (error == 0)
			info->state = KCOV_STATE_READY;
		return (error);
	}

	mtx_lock_spin(&kcov_lock);
	switch (cmd) {
	case KIOENABLE:
		if (info->state != KCOV_STATE_READY) {
			error = EBUSY;
			break;
		}
		if (td->td_kcov_info != NULL) {
			error = EINVAL;
			break;
		}
		mode = *(int *)data;
		if (mode != KCOV_MODE_TRACE_PC && mode != KCOV_MODE_TRACE_CMP) {
			error = EINVAL;
			break;
		}

		/* Lets hope nobody opens this 2 billion times */
		KASSERT(active_count < INT_MAX,
		    ("%s: Open too many times", __func__));
		active_count++;
		if (active_count == 1) {
			cov_register_pc(&trace_pc);
			cov_register_cmp(&trace_cmp);
		}

		KASSERT(info->thread == NULL,
		    ("Enabling kcov when already enabled"));
		info->thread = td;
		info->mode = mode;
		/*
		 * Ensure the mode has been set before starting coverage
		 * tracing.
		 */
		atomic_store_rel_int(&info->state, KCOV_STATE_RUNNING);
		td->td_kcov_info = info;
		break;
	case KIODISABLE:
		/* Only the currently enabled thread may disable itself */
		if (info->state != KCOV_STATE_RUNNING ||
		    info != td->td_kcov_info) {
			error = EINVAL;
			break;
		}
		KASSERT(active_count > 0, ("%s: Open count is zero", __func__));
		active_count--;
		if (active_count == 0) {
			cov_unregister_pc();
			cov_unregister_cmp();
		}

		td->td_kcov_info = NULL;
		atomic_store_int(&info->state, KCOV_STATE_READY);
		/*
		 * Ensure we have exited the READY state before clearing the
		 * rest of the info struct.
		 */
		atomic_thread_fence_rel();
		info->mode = -1;
		info->thread = NULL;
		break;
	default:
		error = EINVAL;
		break;
	}
	mtx_unlock_spin(&kcov_lock);

	return (error);
}

static void
kcov_thread_dtor(void *arg __unused, struct thread *td)
{
	struct kcov_info *info;

	info = td->td_kcov_info;
	if (info == NULL)
		return;

	mtx_lock_spin(&kcov_lock);
	KASSERT(active_count > 0, ("%s: Open count is zero", __func__));
	active_count--;
	if (active_count == 0) {
		cov_unregister_pc();
		cov_unregister_cmp();
	}
	td->td_kcov_info = NULL;
	if (info->state != KCOV_STATE_DYING) {
		/*
		 * The kcov file is still open. Mark it as unused and
		 * wait for it to be closed before cleaning up.
		 */
		atomic_store_int(&info->state, KCOV_STATE_READY);
		atomic_thread_fence_seq_cst();
		/* This info struct is unused */
		info->thread = NULL;
		mtx_unlock_spin(&kcov_lock);
		return;
	}
	mtx_unlock_spin(&kcov_lock);

	/*
	 * We can safely clean up the info struct as it is in the
	 * KCOV_STATE_DYING state where the info struct is associated with
	 * the current thread that's about to exit.
	 *
	 * The KCOV_STATE_DYING stops new threads from using it.
	 * It also stops the current thread from trying to use the info struct.
	 */
	kcov_free(info);
}

static void
kcov_init(const void *unused)
{
	struct make_dev_args args;
	struct cdev *dev;

	mtx_init(&kcov_lock, "kcov lock", NULL, MTX_SPIN);

	make_dev_args_init(&args);
	args.mda_devsw = &kcov_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_WHEEL;
	args.mda_mode = 0600;
	if (make_dev_s(&args, &dev, "kcov") != 0) {
		printf("%s", "Failed to create kcov device");
		return;
	}

	EVENTHANDLER_REGISTER(thread_dtor, kcov_thread_dtor, NULL,
	    EVENTHANDLER_PRI_ANY);
}

SYSINIT(kcovdev, SI_SUB_LAST, SI_ORDER_ANY, kcov_init, NULL);
