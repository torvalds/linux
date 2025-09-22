/*	$OpenBSD: uvm_vnode.c,v 1.140 2025/08/15 08:21:41 mpi Exp $	*/
/*	$NetBSD: uvm_vnode.c,v 1.36 2000/11/24 20:34:01 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.
 * Copyright (c) 1990 University of Utah.
 *
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)vnode_pager.c       8.8 (Berkeley) 2/13/94
 * from: Id: uvm_vnode.c,v 1.1.2.26 1998/02/02 20:38:07 chuck Exp
 */

/*
 * uvm_vnode.c: the vnode pager.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/disklabel.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/rwlock.h>
#include <sys/dkio.h>
#include <sys/specdev.h>

#include <uvm/uvm.h>
#include <uvm/uvm_vnode.h>

/*
 * private global data structure
 *
 * we keep a list of writeable active vnode-backed VM objects for sync op.
 * we keep a simpleq of vnodes that are currently being sync'd.
 */

LIST_HEAD(, uvm_vnode)		uvn_wlist;	/* [K] writeable uvns */
SIMPLEQ_HEAD(, uvm_vnode)	uvn_sync_q;	/* [S] sync'ing uvns */
struct rwlock uvn_sync_lock;			/* locks sync operation */

extern int rebooting;

/*
 * functions
 */
void		 uvn_cluster(struct uvm_object *, voff_t, voff_t *, voff_t *);
void		 uvn_detach(struct uvm_object *);
boolean_t	 uvn_flush(struct uvm_object *, voff_t, voff_t, int);
int		 uvn_get(struct uvm_object *, voff_t, vm_page_t *, int *, int,
		     vm_prot_t, int, int);
void		 uvn_init(void);
int		 uvn_io(struct uvm_vnode *, vm_page_t *, int, int, int);
int		 uvn_put(struct uvm_object *, vm_page_t *, int, boolean_t);
void		 uvn_reference(struct uvm_object *);

/*
 * master pager structure
 */
const struct uvm_pagerops uvm_vnodeops = {
	.pgo_init = uvn_init,
	.pgo_reference = uvn_reference,
	.pgo_detach = uvn_detach,
	.pgo_flush = uvn_flush,
	.pgo_get = uvn_get,
	.pgo_put = uvn_put,
	.pgo_cluster = uvn_cluster,
	/* use generic version of this: see uvm_pager.c */
	.pgo_mk_pcluster = uvm_mk_pcluster,
};

/*
 * the ops!
 */
/*
 * uvn_init
 *
 * init pager private data structures.
 */
void
uvn_init(void)
{

	LIST_INIT(&uvn_wlist);
	/* note: uvn_sync_q init'd in uvm_vnp_sync() */
	rw_init_flags(&uvn_sync_lock, "uvnsync", RWL_IS_VNODE);
}

/*
 * uvn_attach
 *
 * attach a vnode structure to a VM object.  if the vnode is already
 * attached, then just bump the reference count by one and return the
 * VM object.   if not already attached, attach and return the new VM obj.
 * the "accessprot" tells the max access the attaching thread wants to
 * our pages.
 *
 * => in fact, nothing should be locked so that we can sleep here.
 * => note that uvm_object is first thing in vnode structure, so their
 *    pointers are equiv.
 */
struct uvm_object *
uvn_attach(struct vnode *vp, vm_prot_t accessprot)
{
	struct uvm_vnode *uvn = vp->v_uvm;
	struct vattr vattr;
	int oldflags, result;
	struct partinfo pi;
	u_quad_t used_vnode_size = 0;

	/* if we're mapping a BLK device, make sure it is a disk. */
	if (vp->v_type == VBLK && bdevsw[major(vp->v_rdev)].d_type != D_DISK) {
		return NULL;
	}

	/* first get a lock on the uvn. */
	rw_enter(uvn->u_obj.vmobjlock, RW_WRITE);
	while (uvn->u_flags & UVM_VNODE_BLOCKED) {
		uvn->u_flags |= UVM_VNODE_WANTED;
		rwsleep_nsec(uvn, uvn->u_obj.vmobjlock, PVM, "uvn_attach",
		    INFSLP);
	}

	/*
	 * now uvn must not be in a blocked state.
	 * first check to see if it is already active, in which case
	 * we can bump the reference count, check to see if we need to
	 * add it to the writeable list, and then return.
	 */
	if (uvn->u_flags & UVM_VNODE_VALID) {	/* already active? */

		/* regain vref if we were persisting */
		if (uvn->u_obj.uo_refs == 0) {
			vref(vp);
		}
		uvn->u_obj.uo_refs++;		/* bump uvn ref! */

		/* check for new writeable uvn */
		if ((accessprot & PROT_WRITE) != 0 &&
		    (uvn->u_flags & UVM_VNODE_WRITEABLE) == 0) {
			uvn->u_flags |= UVM_VNODE_WRITEABLE;
			KERNEL_ASSERT_LOCKED();
			LIST_INSERT_HEAD(&uvn_wlist, uvn, u_wlist);
		}

		rw_exit(uvn->u_obj.vmobjlock);
		return (&uvn->u_obj);
	}

	/*
	 * need to call VOP_GETATTR() to get the attributes, but that could
	 * block (due to I/O), so we want to unlock the object before calling.
	 * however, we want to keep anyone else from playing with the object
	 * while it is unlocked.   to do this we set UVM_VNODE_ALOCK which
	 * prevents anyone from attaching to the vnode until we are done with
	 * it.
	 */
	uvn->u_flags = UVM_VNODE_ALOCK;
	rw_exit(uvn->u_obj.vmobjlock);

	if (vp->v_type == VBLK) {
		/*
		 * We could implement this as a specfs getattr call, but:
		 *
		 *	(1) VOP_GETATTR() would get the file system
		 *	    vnode operation, not the specfs operation.
		 *
		 *	(2) All we want is the size, anyhow.
		 */
		result = (*bdevsw[major(vp->v_rdev)].d_ioctl)(vp->v_rdev,
		    DIOCGPART, (caddr_t)&pi, FREAD, curproc);
		if (result == 0) {
			/* XXX should remember blocksize */
			used_vnode_size = (u_quad_t)pi.disklab->d_secsize *
			    (u_quad_t)DL_GETPSIZE(pi.part);
		}
	} else {
		result = VOP_GETATTR(vp, &vattr, curproc->p_ucred, curproc);
		if (result == 0)
			used_vnode_size = vattr.va_size;
	}

	if (result != 0) {
		rw_enter(uvn->u_obj.vmobjlock, RW_WRITE);
		if (uvn->u_flags & UVM_VNODE_WANTED)
			wakeup(uvn);
		uvn->u_flags = 0;
		rw_exit(uvn->u_obj.vmobjlock);
		return NULL;
	}

	/*
	 * make sure that the newsize fits within a vaddr_t
	 * XXX: need to revise addressing data types
	 */
#ifdef DEBUG
	if (vp->v_type == VBLK)
		printf("used_vnode_size = %llu\n", (long long)used_vnode_size);
#endif

	/* now set up the uvn. */
	KASSERT(uvn->u_obj.uo_refs == 0);
	uvn->u_obj.uo_refs++;
	oldflags = uvn->u_flags;
	uvn->u_flags = UVM_VNODE_VALID|UVM_VNODE_CANPERSIST;
	uvn->u_nio = 0;
	uvn->u_size = used_vnode_size;

	/*
	 * add a reference to the vnode.   this reference will stay as long
	 * as there is a valid mapping of the vnode.   dropped when the
	 * reference count goes to zero [and we either free or persist].
	 */
	vref(vp);

	/* if write access, we need to add it to the wlist */
	if (accessprot & PROT_WRITE) {
		uvn->u_flags |= UVM_VNODE_WRITEABLE;	/* we are on wlist! */
		KERNEL_ASSERT_LOCKED();
		LIST_INSERT_HEAD(&uvn_wlist, uvn, u_wlist);
	}

	if (oldflags & UVM_VNODE_WANTED)
		wakeup(uvn);

	return &uvn->u_obj;
}


/*
 * uvn_reference
 *
 * duplicate a reference to a VM object.  Note that the reference
 * count must already be at least one (the passed in reference) so
 * there is no chance of the uvn being killed out here.
 *
 * => caller must be using the same accessprot as was used at attach time
 */


void
uvn_reference(struct uvm_object *uobj)
{
#ifdef DEBUG
	struct uvm_vnode *uvn = (struct uvm_vnode *) uobj;
#endif

	rw_enter(uobj->vmobjlock, RW_WRITE);
#ifdef DEBUG
	if ((uvn->u_flags & UVM_VNODE_VALID) == 0) {
		printf("uvn_reference: ref=%d, flags=0x%x\n",
		    uobj->uo_refs, uvn->u_flags);
		panic("uvn_reference: invalid state");
	}
#endif
	uobj->uo_refs++;
	rw_exit(uobj->vmobjlock);
}

/*
 * uvn_detach
 *
 * remove a reference to a VM object.
 *
 * => caller must call with map locked.
 * => this starts the detach process, but doesn't have to finish it
 *    (async i/o could still be pending).
 */
void
uvn_detach(struct uvm_object *uobj)
{
	struct uvm_vnode *uvn;
	struct vnode *vp;
	int oldflags;

	rw_enter(uobj->vmobjlock, RW_WRITE);
	uobj->uo_refs--;			/* drop ref! */
	if (uobj->uo_refs) {			/* still more refs */
		rw_exit(uobj->vmobjlock);
		return;
	}

	KERNEL_LOCK();
	/* get other pointers ... */
	uvn = (struct uvm_vnode *) uobj;
	vp = uvn->u_vnode;

	/*
	 * clear VTEXT flag now that there are no mappings left (VTEXT is used
	 * to keep an active text file from being overwritten).
	 */
	vp->v_flag &= ~VTEXT;

	/*
	 * we just dropped the last reference to the uvn.   see if we can
	 * let it "stick around".
	 */
	if (uvn->u_flags & UVM_VNODE_CANPERSIST) {
		/* won't block */
		uvn_flush(uobj, 0, 0, PGO_DEACTIVATE|PGO_ALLPAGES);
		goto out;
	}

	/* its a goner! */
	uvn->u_flags |= UVM_VNODE_DYING;

	/*
	 * even though we may unlock in flush, no one can gain a reference
	 * to us until we clear the "dying" flag [because it blocks
	 * attaches].  we will not do that until after we've disposed of all
	 * the pages with uvn_flush().  note that before the flush the only
	 * pages that could be marked PG_BUSY are ones that are in async
	 * pageout by the daemon.  (there can't be any pending "get"'s
	 * because there are no references to the object).
	 */
	(void) uvn_flush(uobj, 0, 0, PGO_CLEANIT|PGO_FREE|PGO_ALLPAGES);

	/*
	 * given the structure of this pager, the above flush request will
	 * create the following state: all the pages that were in the object
	 * have either been free'd or they are marked PG_BUSY and in the 
	 * middle of an async io. If we still have pages we set the "relkill"
	 * state, so that in the case the vnode gets terminated we know 
	 * to leave it alone. Otherwise we'll kill the vnode when it's empty.
	 */
	uvn->u_flags |= UVM_VNODE_RELKILL;
	/* wait on any outstanding io */
	while (uobj->uo_npages && uvn->u_flags & UVM_VNODE_RELKILL) {
		uvn->u_flags |= UVM_VNODE_IOSYNC;
		rwsleep_nsec(&uvn->u_nio, uobj->vmobjlock, PVM, "uvn_term",
		    INFSLP);
	}

	if ((uvn->u_flags & UVM_VNODE_RELKILL) == 0) {
		rw_exit(uobj->vmobjlock);
		KERNEL_UNLOCK();
		return;
	}

	/*
	 * kill object now.   note that we can't be on the sync q because
	 * all references are gone.
	 */
	if (uvn->u_flags & UVM_VNODE_WRITEABLE) {
		LIST_REMOVE(uvn, u_wlist);
	}
	KASSERT(RBT_EMPTY(uvm_objtree, &uobj->memt));
	oldflags = uvn->u_flags;
	uvn->u_flags = 0;

	/* wake up any sleepers */
	if (oldflags & UVM_VNODE_WANTED)
		wakeup(uvn);
out:
	rw_exit(uobj->vmobjlock);

	/* drop our reference to the vnode. */
	vrele(vp);
	KERNEL_UNLOCK();
}

/*
 * uvm_vnp_terminate: external hook to clear out a vnode's VM
 *
 * called in two cases:
 *  [1] when a persisting vnode vm object (i.e. one with a zero reference
 *      count) needs to be freed so that a vnode can be reused.  this
 *      happens under "getnewvnode" in vfs_subr.c.   if the vnode from
 *      the free list is still attached (i.e. not VBAD) then vgone is
 *	called.   as part of the vgone trace this should get called to
 *	free the vm object.   this is the common case.
 *  [2] when a filesystem is being unmounted by force (MNT_FORCE,
 *	"umount -f") the vgone() function is called on active vnodes
 *	on the mounted file systems to kill their data (the vnodes become
 *	"dead" ones [see src/sys/miscfs/deadfs/...]).  that results in a
 *	call here (even if the uvn is still in use -- i.e. has a non-zero
 *	reference count).  this case happens at "umount -f" and during a
 *	"reboot/halt" operation.
 *
 * => the caller must XLOCK and VOP_LOCK the vnode before calling us
 *	[protects us from getting a vnode that is already in the DYING
 *	 state...]
 * => in case [2] the uvn is still alive after this call, but all I/O
 *	ops will fail (due to the backing vnode now being "dead").  this
 *	will prob. kill any process using the uvn due to pgo_get failing.
 */
void
uvm_vnp_terminate(struct vnode *vp)
{
	struct uvm_vnode *uvn = vp->v_uvm;
	struct uvm_object *uobj = &uvn->u_obj;
	int oldflags;

	/* check if it is valid */
	rw_enter(uobj->vmobjlock, RW_WRITE);
	if ((uvn->u_flags & UVM_VNODE_VALID) == 0) {
		rw_exit(uobj->vmobjlock);
		return;
	}

	/*
	 * must be a valid uvn that is not already dying (because XLOCK
	 * protects us from that).   the uvn can't in the ALOCK state
	 * because it is valid, and uvn's that are in the ALOCK state haven't
	 * been marked valid yet.
	 */
#ifdef DEBUG
	/*
	 * debug check: are we yanking the vnode out from under our uvn?
	 */
	if (uvn->u_obj.uo_refs) {
		printf("uvm_vnp_terminate(%p): terminating active vnode "
		    "(refs=%d)\n", uvn, uvn->u_obj.uo_refs);
	}
#endif

	/*
	 * it is possible that the uvn was detached and is in the relkill
	 * state [i.e. waiting for async i/o to finish].
	 * we take over the vnode now and cancel the relkill.
	 * we want to know when the i/o is done so we can recycle right
	 * away.   note that a uvn can only be in the RELKILL state if it
	 * has a zero reference count.
	 */
	if (uvn->u_flags & UVM_VNODE_RELKILL)
		uvn->u_flags &= ~UVM_VNODE_RELKILL;	/* cancel RELKILL */

	/*
	 * block the uvn by setting the dying flag, and then flush the
	 * pages.
	 *
	 * also, note that we tell I/O that we are already VOP_LOCK'd so
	 * that uvn_io doesn't attempt to VOP_LOCK again.
	 *
	 * XXXCDC: setting VNISLOCKED on an active uvn which is being terminated
	 *	due to a forceful unmount might not be a good idea.  maybe we
	 *	need a way to pass in this info to uvn_flush through a
	 *	pager-defined PGO_ constant [currently there are none].
	 */
	uvn->u_flags |= UVM_VNODE_DYING|UVM_VNODE_VNISLOCKED;

	(void) uvn_flush(&uvn->u_obj, 0, 0, PGO_CLEANIT|PGO_FREE|PGO_ALLPAGES);

	/*
	 * as we just did a flush we expect all the pages to be gone or in
	 * the process of going.  sleep to wait for the rest to go [via iosync].
	 */
	while (uvn->u_obj.uo_npages) {
#ifdef DEBUG
		struct vm_page *pp;
		RBT_FOREACH(pp, uvm_objtree, &uvn->u_obj.memt) {
			if ((pp->pg_flags & PG_BUSY) == 0)
				panic("uvm_vnp_terminate: detected unbusy pg");
		}
		if (uvn->u_nio == 0)
			panic("uvm_vnp_terminate: no I/O to wait for?");
		printf("uvm_vnp_terminate: waiting for I/O to fin.\n");
		/*
		 * XXXCDC: this is unlikely to happen without async i/o so we
		 * put a printf in just to keep an eye on it.
		 */
#endif
		uvn->u_flags |= UVM_VNODE_IOSYNC;
		rwsleep_nsec(&uvn->u_nio, uobj->vmobjlock, PVM, "uvn_term",
		    INFSLP);
	}

	/*
	 * done.   now we free the uvn if its reference count is zero
	 * (true if we are zapping a persisting uvn).   however, if we are
	 * terminating a uvn with active mappings we let it live ... future
	 * calls down to the vnode layer will fail.
	 */
	oldflags = uvn->u_flags;
	if (uvn->u_obj.uo_refs) {
		/*
		 * uvn must live on it is dead-vnode state until all references
		 * are gone.   restore flags.    clear CANPERSIST state.
		 */
		uvn->u_flags &= ~(UVM_VNODE_DYING|UVM_VNODE_VNISLOCKED|
		      UVM_VNODE_WANTED|UVM_VNODE_CANPERSIST);
	} else {
		/*
		 * free the uvn now.   note that the vref reference is already
		 * gone [it is dropped when we enter the persist state].
		 */
		if (uvn->u_flags & UVM_VNODE_IOSYNCWANTED)
			panic("uvm_vnp_terminate: io sync wanted bit set");

		if (uvn->u_flags & UVM_VNODE_WRITEABLE) {
			LIST_REMOVE(uvn, u_wlist);
		}
		uvn->u_flags = 0;	/* uvn is history, clear all bits */
	}

	if (oldflags & UVM_VNODE_WANTED)
		wakeup(uvn);

	rw_exit(uobj->vmobjlock);
}

/*
 * NOTE: currently we have to use VOP_READ/VOP_WRITE because they go
 * through the buffer cache and allow I/O in any size.  These VOPs use
 * synchronous i/o.  [vs. VOP_STRATEGY which can be async, but doesn't
 * go through the buffer cache or allow I/O sizes larger than a
 * block].  we will eventually want to change this.
 *
 * issues to consider:
 *   uvm provides the uvm_aiodesc structure for async i/o management.
 * there are two tailq's in the uvm. structure... one for pending async
 * i/o and one for "done" async i/o.   to do an async i/o one puts
 * an aiodesc on the "pending" list (protected by splbio()), starts the
 * i/o and returns VM_PAGER_PEND.    when the i/o is done, we expect
 * some sort of "i/o done" function to be called (at splbio(), interrupt
 * time).   this function should remove the aiodesc from the pending list
 * and place it on the "done" list and wakeup the daemon.   the daemon
 * will run at normal spl() and will remove all items from the "done"
 * list and call the "aiodone" hook for each done request (see uvm_pager.c).
 * [in the old vm code, this was done by calling the "put" routine with
 * null arguments which made the code harder to read and understand because
 * you had one function ("put") doing two things.]
 *
 * so the current pager needs:
 *   int uvn_aiodone(struct uvm_aiodesc *)
 *
 * => return 0 (aio finished, free it). otherwise requeue for later collection.
 * => called with pageq's locked by the daemon.
 *
 * general outline:
 * - drop "u_nio" (this req is done!)
 * - if (object->iosync && u_naio == 0) { wakeup &uvn->u_naio }
 * - get "page" structures (atop?).
 * - handle "wanted" pages
 * dont forget to look at "object" wanted flag in all cases.
 */

/*
 * uvn_flush: flush pages out of a uvm object.
 *
 * => if PGO_CLEANIT is set, we may block (due to I/O).   thus, a caller
 *	might want to unlock higher level resources (e.g. vm_map)
 *	before calling flush.
 * => if PGO_CLEANIT is not set, then we will not block
 * => if PGO_ALLPAGE is set, then all pages in the object are valid targets
 *	for flushing.
 * => NOTE: we are allowed to lock the page queues, so the caller
 *	must not be holding the lock on them [e.g. pagedaemon had
 *	better not call us with the queues locked]
 * => we return TRUE unless we encountered some sort of I/O error
 *
 * comment on "cleaning" object and PG_BUSY pages:
 *	this routine is holding the lock on the object.   the only time
 *	that it can run into a PG_BUSY page that it does not own is if
 *	some other process has started I/O on the page (e.g. either
 *	a pagein, or a pageout).    if the PG_BUSY page is being paged
 *	in, then it can not be dirty (!PG_CLEAN) because no one has
 *	had a chance to modify it yet.    if the PG_BUSY page is being
 *	paged out then it means that someone else has already started
 *	cleaning the page for us (how nice!).    in this case, if we
 *	have syncio specified, then after we make our pass through the
 *	object we need to wait for the other PG_BUSY pages to clear
 *	off (i.e. we need to do an iosync).   also note that once a
 *	page is PG_BUSY it must stay in its object until it is un-busyed.
 */
boolean_t
uvn_flush(struct uvm_object *uobj, voff_t start, voff_t stop, int flags)
{
	struct uvm_vnode *uvn = (struct uvm_vnode *) uobj;
	struct vm_page *pp, *ptmp;
	struct vm_page *pps[MAXBSIZE >> PAGE_SHIFT], **ppsp;
	int npages, result, lcv;
	boolean_t retval, need_iosync, needs_clean;
	voff_t curoff;

	KASSERT(rw_write_held(uobj->vmobjlock));

	/* get init vals and determine how we are going to traverse object */
	need_iosync = FALSE;
	retval = TRUE;		/* return value */
	if (flags & PGO_ALLPAGES) {
		start = 0;
		stop = round_page(uvn->u_size);
	} else {
		start = trunc_page(start);
		stop = MIN(round_page(stop), round_page(uvn->u_size));
	}

	/*
	 * PG_CLEANCHK: this bit is used by the pgo_mk_pcluster function as
	 * a _hint_ as to how up to date the PG_CLEAN bit is.   if the hint
	 * is wrong it will only prevent us from clustering... it won't break
	 * anything.   we clear all PG_CLEANCHK bits here, and pgo_mk_pcluster
	 * will set them as it syncs PG_CLEAN.   This is only an issue if we
	 * are looking at non-inactive pages (because inactive page's PG_CLEAN
	 * bit is always up to date since there are no mappings).
	 * [borrowed PG_CLEANCHK idea from FreeBSD VM]
	 */
	if ((flags & PGO_CLEANIT) != 0) {
		KASSERT(uobj->pgops->pgo_mk_pcluster != 0);
		for (curoff = start ; curoff < stop; curoff += PAGE_SIZE) {
			if ((pp = uvm_pagelookup(uobj, curoff)) != NULL)
				atomic_clearbits_int(&pp->pg_flags,
				    PG_CLEANCHK);
		}
	}

	ppsp = NULL;		/* XXX: shut up gcc */
	uvm_lock_pageq();
	/* locked: both page queues */
	for (curoff = start; curoff < stop; curoff += PAGE_SIZE) {
		if ((pp = uvm_pagelookup(uobj, curoff)) == NULL)
			continue;
		/*
		 * handle case where we do not need to clean page (either
		 * because we are not clean or because page is not dirty or
		 * is busy):
		 *
		 * NOTE: we are allowed to deactivate a non-wired active
		 * PG_BUSY page, but once a PG_BUSY page is on the inactive
		 * queue it must stay put until it is !PG_BUSY (so as not to
		 * confuse pagedaemon).
		 */
		if ((flags & PGO_CLEANIT) == 0 || (pp->pg_flags & PG_BUSY) != 0) {
			needs_clean = FALSE;
			if ((pp->pg_flags & PG_BUSY) != 0 &&
			    (flags & (PGO_CLEANIT|PGO_SYNCIO)) ==
			             (PGO_CLEANIT|PGO_SYNCIO))
				need_iosync = TRUE;
		} else {
			/*
			 * freeing: nuke all mappings so we can sync
			 * PG_CLEAN bit with no race
			 */
			if ((pp->pg_flags & PG_CLEAN) != 0 &&
			    (flags & PGO_FREE) != 0 &&
			    (pp->pg_flags & PQ_ACTIVE) != 0)
				pmap_page_protect(pp, PROT_NONE);
			if ((pp->pg_flags & PG_CLEAN) != 0 &&
			    pmap_is_modified(pp))
				atomic_clearbits_int(&pp->pg_flags, PG_CLEAN);
			atomic_setbits_int(&pp->pg_flags, PG_CLEANCHK);

			needs_clean = ((pp->pg_flags & PG_CLEAN) == 0);
		}

		/* if we don't need a clean, deactivate/free pages then cont. */
		if (!needs_clean) {
			if (flags & PGO_DEACTIVATE) {
				if (pp->wire_count == 0) {
					uvm_pagedeactivate(pp);
				}
			} else if (flags & PGO_FREE) {
				if (pp->pg_flags & PG_BUSY) {
					uvm_unlock_pageq();
					uvm_pagewait(pp, uobj->vmobjlock,
					    "uvn_flsh");
					rw_enter(uobj->vmobjlock, RW_WRITE);
					uvm_lock_pageq();
					curoff -= PAGE_SIZE;
					continue;
				} else {
					pmap_page_protect(pp, PROT_NONE);
					/* dequeue to prevent lock recursion */
					uvm_pagedequeue(pp);
					uvm_pagefree(pp);
				}
			}
			continue;
		}

		/*
		 * pp points to a page in the object that we are
		 * working on.  if it is !PG_CLEAN,!PG_BUSY and we asked
		 * for cleaning (PGO_CLEANIT).  we clean it now.
		 *
		 * let uvm_pager_put attempted a clustered page out.
		 * note: locked: page queues.
		 */
		atomic_setbits_int(&pp->pg_flags, PG_BUSY);
		UVM_PAGE_OWN(pp, "uvn_flush");
		pmap_page_protect(pp, PROT_READ);
		/* if we're async, free the page in aiodoned */
		if ((flags & (PGO_FREE|PGO_SYNCIO)) == PGO_FREE)
			atomic_setbits_int(&pp->pg_flags, PG_RELEASED);
ReTry:
		ppsp = pps;
		npages = sizeof(pps) / sizeof(struct vm_page *);

		result = uvm_pager_put(uobj, pp, &ppsp, &npages,
			   flags | PGO_DOACTCLUST, start, stop);

		/*
		 * if we did an async I/O it is remotely possible for the
		 * async i/o to complete and the page "pp" be freed or what
		 * not before we get a chance to relock the object. Therefore,
		 * we only touch it when it won't be freed, RELEASED took care
		 * of the rest.
		 */
		uvm_lock_pageq();

		/*
		 * VM_PAGER_AGAIN: given the structure of this pager, this
		 * can only happen when we are doing async I/O and can't
		 * map the pages into kernel memory (pager_map) due to lack
		 * of vm space.   if this happens we drop back to sync I/O.
		 */
		if (result == VM_PAGER_AGAIN) {
			/*
			 * it is unlikely, but page could have been released
			 * we ignore this now and retry the I/O.
			 * we will detect and
			 * handle the released page after the syncio I/O
			 * completes.
			 */
#ifdef DIAGNOSTIC
			if (flags & PGO_SYNCIO)
	panic("%s: PGO_SYNCIO return 'try again' error (impossible)", __func__);
#endif
			flags |= PGO_SYNCIO;
			if (flags & PGO_FREE)
				atomic_clearbits_int(&pp->pg_flags,
				    PG_RELEASED);

			goto ReTry;
		}

		/*
		 * the cleaning operation is now done.   finish up.  note that
		 * on error (!OK, !PEND) uvm_pager_put drops the cluster for us.
		 * if success (OK, PEND) then uvm_pager_put returns the cluster
		 * to us in ppsp/npages.
		 */
		/*
		 * for pending async i/o if we are not deactivating
		 * we can move on to the next page. aiodoned deals with
		 * the freeing case for us.
		 */
		if (result == VM_PAGER_PEND && (flags & PGO_DEACTIVATE) == 0)
			continue;

		/*
		 * need to look at each page of the I/O operation, and do what
		 * we gotta do.
		 */
		for (lcv = 0 ; lcv < npages; lcv++) {
			ptmp = ppsp[lcv];
			/*
			 * verify the page didn't get moved
			 */
			if (result == VM_PAGER_PEND && ptmp->uobject != uobj)
				continue;

			/*
			 * unbusy the page if I/O is done.   note that for
			 * pending I/O it is possible that the I/O op
			 * finished
			 * (in which case the page is no longer busy).
			 */
			if (result != VM_PAGER_PEND) {
				if (ptmp->pg_flags & PG_WANTED)
					wakeup(ptmp);

				atomic_clearbits_int(&ptmp->pg_flags,
				    PG_WANTED|PG_BUSY);
				UVM_PAGE_OWN(ptmp, NULL);
				atomic_setbits_int(&ptmp->pg_flags,
				    PG_CLEAN|PG_CLEANCHK);
				if ((flags & PGO_FREE) == 0)
					pmap_clear_modify(ptmp);
			}

			/* dispose of page */
			if (flags & PGO_DEACTIVATE) {
				if (ptmp->wire_count == 0) {
					uvm_pagedeactivate(ptmp);
				}
			} else if (flags & PGO_FREE &&
			    result != VM_PAGER_PEND) {
				if (result != VM_PAGER_OK) {
					static struct timeval lasttime;
					static const struct timeval interval =
					    { 5, 0 };

					if (ratecheck(&lasttime, &interval)) {
						printf("%s: obj=%p, "
						   "offset=0x%llx.  error "
						   "during pageout.\n",
						    __func__, pp->uobject,
						    (long long)pp->offset);
						printf("%s: WARNING: "
						    "changes to page may be "
						    "lost!\n", __func__);
					}
					retval = FALSE;
				}
				pmap_page_protect(ptmp, PROT_NONE);
				/* dequeue first to prevent lock recursion */
				uvm_pagedequeue(ptmp);
				uvm_pagefree(ptmp);
			}

		}		/* end of "lcv" for loop */

	}		/* end of "pp" for loop */

	/* done with pagequeues: unlock */
	uvm_unlock_pageq();

	/* now wait for all I/O if required. */
	if (need_iosync) {
		while (uvn->u_nio != 0) {
			uvn->u_flags |= UVM_VNODE_IOSYNC;
			rwsleep_nsec(&uvn->u_nio, uobj->vmobjlock, PVM,
			    "uvn_flush", INFSLP);
		}
		if (uvn->u_flags & UVM_VNODE_IOSYNCWANTED)
			wakeup(&uvn->u_flags);
		uvn->u_flags &= ~(UVM_VNODE_IOSYNC|UVM_VNODE_IOSYNCWANTED);
	}

	return retval;
}

/*
 * uvn_cluster
 *
 * we are about to do I/O in an object at offset.   this function is called
 * to establish a range of offsets around "offset" in which we can cluster
 * I/O.
 */

void
uvn_cluster(struct uvm_object *uobj, voff_t offset, voff_t *loffset,
    voff_t *hoffset)
{
	struct uvm_vnode *uvn = (struct uvm_vnode *) uobj;
	*loffset = offset;

	KASSERT(rw_write_held(uobj->vmobjlock));

	if (*loffset >= uvn->u_size)
		panic("uvn_cluster: offset out of range");

	/*
	 * XXX: old pager claims we could use VOP_BMAP to get maxcontig value.
	 */
	*hoffset = *loffset + MAXBSIZE;
	if (*hoffset > round_page(uvn->u_size))	/* past end? */
		*hoffset = round_page(uvn->u_size);
}

/*
 * uvn_put: flush page data to backing store.
 *
 * => prefer map unlocked (not required)
 * => flags: PGO_SYNCIO -- use sync. I/O
 * => note: caller must set PG_CLEAN and pmap_clear_modify (if needed)
 * => XXX: currently we use VOP_READ/VOP_WRITE which are only sync.
 *	[thus we never do async i/o!  see iodone comment]
 */
int
uvn_put(struct uvm_object *uobj, struct vm_page **pps, int npages, int flags)
{
	struct uvm_vnode *uvn = (struct uvm_vnode *)uobj;
	int dying, retval;

	KASSERT(rw_write_held(uobj->vmobjlock));

	/*
	 * Unless we're recycling this vnode, grab a reference to it
	 * to prevent it from being recycled from under our feet.
	 * This also makes sure we can don't panic if we end up in
	 * uvn_vnp_uncache() as a result of the I/O operation as that
	 * function assumes we hold a reference.
	 *
	 * If the vnode is in the process of being recycled by someone
	 * else, grabbing a reference will fail.  In that case the
	 * pages will already be written out by whoever is cleaning
	 * the vnode, so simply return VM_PAGER_AGAIN such that we
	 * skip these pages.
	 */
	dying = (uvn->u_flags & UVM_VNODE_DYING);
	if (!dying) {
		if (vget(uvn->u_vnode, LK_NOWAIT))
			return VM_PAGER_AGAIN;
	}

	retval = uvn_io((struct uvm_vnode*)uobj, pps, npages, flags, UIO_WRITE);

	if (!dying)
		vrele(uvn->u_vnode);

	return retval;
}

/*
 * uvn_get: get pages (synchronously) from backing store
 *
 * => prefer map unlocked (not required)
 * => flags: PGO_ALLPAGES: get all of the pages
 *           PGO_LOCKED: fault data structures are locked
 * => NOTE: offset is the offset of pps[0], _NOT_ pps[centeridx]
 * => NOTE: caller must check for released pages!!
 */
int
uvn_get(struct uvm_object *uobj, voff_t offset, struct vm_page **pps,
    int *npagesp, int centeridx, vm_prot_t access_type, int advice, int flags)
{
	voff_t current_offset;
	struct vm_page *ptmp;
	int lcv, result, gotpages;
	boolean_t done;

	KASSERT(rw_lock_held(uobj->vmobjlock));
	KASSERT(rw_write_held(uobj->vmobjlock) ||
	    ((flags & PGO_LOCKED) != 0 && (access_type & PROT_WRITE) == 0));

	/* step 1: handled the case where fault data structures are locked. */
	if (flags & PGO_LOCKED) {
		/*
		 * gotpages is the current number of pages we've gotten (which
		 * we pass back up to caller via *npagesp.
		 */
		gotpages = 0;

		/*
		 * step 1a: get pages that are already resident.   only do this
		 * if the data structures are locked (i.e. the first time
		 * through).
		 */
		done = TRUE;	/* be optimistic */

		for (lcv = 0, current_offset = offset ; lcv < *npagesp ;
		    lcv++, current_offset += PAGE_SIZE) {
			/* do we care about this page?  if not, skip it */
			if (pps[lcv] == PGO_DONTCARE)
				continue;

			/* lookup page */
			ptmp = uvm_pagelookup(uobj, current_offset);

			/*
			 * to be useful must get a non-busy page
			 */
			if (ptmp == NULL || (ptmp->pg_flags & PG_BUSY) != 0) {
				if (lcv == centeridx ||
				    (flags & PGO_ALLPAGES) != 0)
					/* need to do a wait or I/O! */
					done = FALSE;
				continue;
			}

			/*
			 * useful page: busy it and plug it in our
			 * result array
			 */
			pps[lcv] = ptmp;
			gotpages++;

		}

		/*
		 * XXX: given the "advice", should we consider async read-ahead?
		 * XXX: fault current does deactivate of pages behind us.  is
		 * this good (other callers might now).
		 */
		/*
		 * XXX: read-ahead currently handled by buffer cache (bread)
		 * level.
		 * XXX: no async i/o available.
		 * XXX: so we don't do anything now.
		 */

		/*
		 * step 1c: now we've either done everything needed or we to
		 * unlock and do some waiting or I/O.
		 */
		*npagesp = gotpages;		/* let caller know */
		return done ? VM_PAGER_OK : VM_PAGER_UNLOCK;
	}

	/*
	 * step 2: get non-resident or busy pages.
	 * data structures are unlocked.
	 *
	 * XXX: because we can't do async I/O at this level we get things
	 * page at a time (otherwise we'd chunk).   the VOP_READ() will do
	 * async-read-ahead for us at a lower level.
	 */
	for (lcv = 0, current_offset = offset;
			 lcv < *npagesp ; lcv++, current_offset += PAGE_SIZE) {

		/* skip over pages we've already gotten or don't want */
		/* skip over pages we don't _have_ to get */
		if (pps[lcv] != NULL || (lcv != centeridx &&
		    (flags & PGO_ALLPAGES) == 0))
			continue;

		/*
		 * we have yet to locate the current page (pps[lcv]).   we first
		 * look for a page that is already at the current offset.   if
		 * we fine a page, we check to see if it is busy or released.
		 * if that is the case, then we sleep on the page until it is
		 * no longer busy or released and repeat the lookup.    if the
		 * page we found is neither busy nor released, then we busy it
		 * (so we own it) and plug it into pps[lcv].   this breaks the
		 * following while loop and indicates we are ready to move on
		 * to the next page in the "lcv" loop above.
		 *
		 * if we exit the while loop with pps[lcv] still set to NULL,
		 * then it means that we allocated a new busy/fake/clean page
		 * ptmp in the object and we need to do I/O to fill in the data.
		 */
		while (pps[lcv] == NULL) {	/* top of "pps" while loop */
			/* look for a current page */
			ptmp = uvm_pagelookup(uobj, current_offset);

			/* nope?   allocate one now (if we can) */
			if (ptmp == NULL) {
				ptmp = uvm_pagealloc(uobj, current_offset,
				    NULL, 0);

				/* out of RAM? */
				if (ptmp == NULL) {
					uvm_wait("uvn_getpage");

					/* goto top of pps while loop */
					continue;
				}

				/*
				 * got new page ready for I/O.  break pps
				 * while loop.  pps[lcv] is still NULL.
				 */
				break;
			}

			/* page is there, see if we need to wait on it */
			if ((ptmp->pg_flags & PG_BUSY) != 0) {
				uvm_pagewait(ptmp, uobj->vmobjlock, "uvn_get");
				rw_enter(uobj->vmobjlock, RW_WRITE);
				continue;	/* goto top of pps while loop */
			}

			/*
			 * if we get here then the page has become resident
			 * and unbusy between steps 1 and 2.  we busy it
			 * now (so we own it) and set pps[lcv] (so that we
			 * exit the while loop).
			 */
			atomic_setbits_int(&ptmp->pg_flags, PG_BUSY);
			UVM_PAGE_OWN(ptmp, "uvn_get2");
			pps[lcv] = ptmp;
		}

		/*
		 * if we own the a valid page at the correct offset, pps[lcv]
		 * will point to it.   nothing more to do except go to the
		 * next page.
		 */
		if (pps[lcv])
			continue;			/* next lcv */

		/*
		 * we have a "fake/busy/clean" page that we just allocated.  do
		 * I/O to fill it with valid data.
		 */
		result = uvn_io((struct uvm_vnode *) uobj, &ptmp, 1,
		    PGO_SYNCIO|PGO_NOWAIT, UIO_READ);

		/*
		 * I/O done.  because we used syncio the result can not be
		 * PEND or AGAIN.
		 */
		if (result != VM_PAGER_OK) {
			if (ptmp->pg_flags & PG_WANTED)
				wakeup(ptmp);

			atomic_clearbits_int(&ptmp->pg_flags,
			    PG_WANTED|PG_BUSY);
			UVM_PAGE_OWN(ptmp, NULL);
			uvm_pagefree(ptmp);
			rw_exit(uobj->vmobjlock);
			return result;
		}

		/*
		 * we got the page!   clear the fake flag (indicates valid
		 * data now in page) and plug into our result array.   note
		 * that page is still busy.
		 *
		 * it is the callers job to:
		 * => check if the page is released
		 * => unbusy the page
		 * => activate the page
		 */

		/* data is valid ... */
		atomic_clearbits_int(&ptmp->pg_flags, PG_FAKE);
		pmap_clear_modify(ptmp);		/* ... and clean */
		pps[lcv] = ptmp;

	}


	rw_exit(uobj->vmobjlock);
	return (VM_PAGER_OK);
}

/*
 * uvn_io: do I/O to a vnode
 *
 * => prefer map unlocked (not required)
 * => flags: PGO_SYNCIO -- use sync. I/O
 * => XXX: currently we use VOP_READ/VOP_WRITE which are only sync.
 *	[thus we never do async i/o!  see iodone comment]
 */

int
uvn_io(struct uvm_vnode *uvn, vm_page_t *pps, int npages, int flags, int rw)
{
	struct uvm_object *uobj = &uvn->u_obj;
	struct vnode *vn;
	struct uio uio;
	struct iovec iov;
	vaddr_t kva;
	off_t file_offset;
	int waitf, result, mapinflags;
	size_t got, wanted;
	int vnlocked, netunlocked = 0;
	int lkflags = (flags & PGO_NOWAIT) ? LK_NOWAIT : 0;
	voff_t uvnsize;

	KASSERT(rw_write_held(uobj->vmobjlock));

	/* init values */
	waitf = (flags & PGO_SYNCIO) ? M_WAITOK : M_NOWAIT;
	vn = uvn->u_vnode;
	file_offset = pps[0]->offset;

	/* check for sync'ing I/O. */
	while (uvn->u_flags & UVM_VNODE_IOSYNC) {
		if (waitf == M_NOWAIT) {
			return VM_PAGER_AGAIN;
		}
		uvn->u_flags |= UVM_VNODE_IOSYNCWANTED;
		rwsleep_nsec(&uvn->u_flags, uobj->vmobjlock, PVM, "uvn_iosync",
		    INFSLP);
	}

	/* check size */
	if (file_offset >= uvn->u_size) {
		return VM_PAGER_BAD;
	}

	/* first try and map the pages in (without waiting) */
	mapinflags = (rw == UIO_READ) ?
	    UVMPAGER_MAPIN_READ : UVMPAGER_MAPIN_WRITE;

	kva = uvm_pagermapin(pps, npages, mapinflags);
	if (kva == 0 && waitf == M_NOWAIT) {
		return VM_PAGER_AGAIN;
	}

	/*
	 * ok, now bump u_nio up.   at this point we are done with uvn
	 * and can unlock it.   if we still don't have a kva, try again
	 * (this time with sleep ok).
	 */
	uvn->u_nio++;			/* we have an I/O in progress! */
	vnlocked = (uvn->u_flags & UVM_VNODE_VNISLOCKED);
	uvnsize = uvn->u_size;
	rw_exit(uobj->vmobjlock);
	if (kva == 0)
		kva = uvm_pagermapin(pps, npages,
		    mapinflags | UVMPAGER_MAPIN_WAITOK);

	/*
	 * ok, mapped in.  our pages are PG_BUSY so they are not going to
	 * get touched (so we can look at "offset" without having to lock
	 * the object).  set up for I/O.
	 */
	/* fill out uio/iov */
	iov.iov_base = (caddr_t) kva;
	wanted = (size_t)npages << PAGE_SHIFT;
	if (file_offset + wanted > uvnsize)
		wanted = uvnsize - file_offset;	/* XXX: needed? */
	iov.iov_len = wanted;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = file_offset;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = rw;
	uio.uio_resid = wanted;
	uio.uio_procp = curproc;

	/*
	 * This process may already have the NET_LOCK(), if we
	 * faulted in copyin() or copyout() in the network stack.
	 */
	if (rw_status(&netlock) == RW_WRITE) {
		NET_UNLOCK();
		netunlocked = 1;
	}

	/* do the I/O!  (XXX: curproc?) */
	/*
	 * This process may already have this vnode locked, if we faulted in
	 * copyin() or copyout() on a region backed by this vnode
	 * while doing I/O to the vnode.  If this is the case, don't
	 * panic.. instead, return the error to the user.
	 *
	 * XXX this is a stopgap to prevent a panic.
	 * Ideally, this kind of operation *should* work.
	 */
	result = 0;
	KERNEL_LOCK();
	if (!vnlocked)
		result = vn_lock(vn, LK_EXCLUSIVE | LK_RECURSEFAIL | lkflags);
	if (result == 0) {
		/* NOTE: vnode now locked! */
		if (rw == UIO_READ)
			result = VOP_READ(vn, &uio, 0, curproc->p_ucred);
		else
			result = VOP_WRITE(vn, &uio,
			    (flags & PGO_PDFREECLUST) ? IO_NOCACHE : 0,
			    curproc->p_ucred);

		if (!vnlocked)
			VOP_UNLOCK(vn);

	}
	KERNEL_UNLOCK();

	if (netunlocked)
		NET_LOCK();


	/* NOTE: vnode now unlocked (unless vnislocked) */
	/*
	 * result == unix style errno (0 == OK!)
	 *
	 * zero out rest of buffer (if needed)
	 */
	if (result == 0) {
		got = wanted - uio.uio_resid;

		if (wanted && got == 0) {
			result = EIO;		/* XXX: error? */
		} else if (got < PAGE_SIZE * npages && rw == UIO_READ) {
			memset((void *) (kva + got), 0,
			       ((size_t)npages << PAGE_SHIFT) - got);
		}
	}

	/* now remove pager mapping */
	uvm_pagermapout(kva, npages);

	/* now clean up the object (i.e. drop I/O count) */
	rw_enter(uobj->vmobjlock, RW_WRITE);
	uvn->u_nio--;			/* I/O DONE! */
	if ((uvn->u_flags & UVM_VNODE_IOSYNC) != 0 && uvn->u_nio == 0) {
		wakeup(&uvn->u_nio);
	}

	if (result == 0) {
		return VM_PAGER_OK;
	} else if (result == EBUSY) {
		KASSERT(flags & PGO_NOWAIT);
		return VM_PAGER_AGAIN;
	} else {
		if (rebooting) {
			KERNEL_LOCK();
			while (rebooting)
				tsleep_nsec(&rebooting, PVM, "uvndead", INFSLP);
			KERNEL_UNLOCK();
		}
		return VM_PAGER_ERROR;
	}
}

/*
 * uvm_vnp_uncache: disable "persisting" in a vnode... when last reference
 * is gone we will kill the object (flushing dirty pages back to the vnode
 * if needed).
 *
 * => returns TRUE if there was no uvm_object attached or if there was
 *	one and we killed it [i.e. if there is no active uvn]
 * => called with the vnode VOP_LOCK'd [we will unlock it for I/O, if
 *	needed]
 *
 * => XXX: given that we now kill uvn's when a vnode is recycled (without
 *	having to hold a reference on the vnode) and given a working
 *	uvm_vnp_sync(), how does that effect the need for this function?
 *      [XXXCDC: seems like it can die?]
 *
 * => XXX: this function should DIE once we merge the VM and buffer
 *	cache.
 *
 * research shows that this is called in the following places:
 * ext2fs_truncate, ffs_truncate, detrunc[msdosfs]: called when vnode
 *	changes sizes
 * ext2fs_write, WRITE [ufs_readwrite], msdosfs_write: called when we
 *	are written to
 * ex2fs_chmod, ufs_chmod: called if VTEXT vnode and the sticky bit
 *	is off
 * ffs_realloccg: when we can't extend the current block and have
 *	to allocate a new one we call this [XXX: why?]
 * nfsrv_rename, rename_files: called when the target filename is there
 *	and we want to remove it
 * nfsrv_remove, sys_unlink: called on file we are removing
 * nfsrv_access: if VTEXT and we want WRITE access and we don't uncache
 *	then return "text busy"
 * nfs_open: seems to uncache any file opened with nfs
 * vn_writechk: if VTEXT vnode and can't uncache return "text busy"
 * fusefs_open: uncaches any file that is opened
 * fusefs_write: uncaches on every write
 */

int
uvm_vnp_uncache(struct vnode *vp)
{
	struct uvm_vnode *uvn = vp->v_uvm;
	struct uvm_object *uobj = &uvn->u_obj;

	/* lock uvn part of the vnode and check if we need to do anything */

	rw_enter(uobj->vmobjlock, RW_WRITE);
	if ((uvn->u_flags & UVM_VNODE_VALID) == 0 ||
			(uvn->u_flags & UVM_VNODE_BLOCKED) != 0) {
		rw_exit(uobj->vmobjlock);
		return TRUE;
	}

	/*
	 * we have a valid, non-blocked uvn.   clear persist flag.
	 * if uvn is currently active we can return now.
	 */
	uvn->u_flags &= ~UVM_VNODE_CANPERSIST;
	if (uvn->u_obj.uo_refs) {
		rw_exit(uobj->vmobjlock);
		return FALSE;
	}

	/*
	 * uvn is currently persisting!   we have to gain a reference to
	 * it so that we can call uvn_detach to kill the uvn.
	 */
	vref(vp);			/* seems ok, even with VOP_LOCK */
	uvn->u_obj.uo_refs++;		/* value is now 1 */
	rw_exit(uobj->vmobjlock);

#ifdef VFSLCKDEBUG
	/*
	 * carry over sanity check from old vnode pager: the vnode should
	 * be VOP_LOCK'd, and we confirm it here.
	 */
	if ((vp->v_flag & VLOCKSWORK) && !VOP_ISLOCKED(vp))
		panic("uvm_vnp_uncache: vnode not locked!");
#endif

	/*
	 * now drop our reference to the vnode.   if we have the sole
	 * reference to the vnode then this will cause it to die [as we
	 * just cleared the persist flag].   we have to unlock the vnode
	 * while we are doing this as it may trigger I/O.
	 *
	 * XXX: it might be possible for uvn to get reclaimed while we are
	 * unlocked causing us to return TRUE when we should not.   we ignore
	 * this as a false-positive return value doesn't hurt us.
	 */
	VOP_UNLOCK(vp);
	uvn_detach(&uvn->u_obj);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	return TRUE;
}

/*
 * uvm_vnp_setsize: grow or shrink a vnode uvn
 *
 * grow   => just update size value
 * shrink => toss un-needed pages
 *
 * => we assume that the caller has a reference of some sort to the
 *	vnode in question so that it will not be yanked out from under
 *	us.
 *
 * called from:
 *  => truncate fns (ext2fs_truncate, ffs_truncate, detrunc[msdos],
 *     fusefs_setattr)
 *  => "write" fns (ext2fs_write, WRITE [ufs/ufs], msdosfs_write, nfs_write
 *     fusefs_write)
 *  => ffs_balloc [XXX: why? doesn't WRITE handle?]
 *  => NFS: nfs_loadattrcache, nfs_getattrcache, nfs_setattr
 *  => union fs: union_newsize
 */

void
uvm_vnp_setsize(struct vnode *vp, off_t newsize)
{
	struct uvm_vnode *uvn = vp->v_uvm;
	struct uvm_object *uobj = &uvn->u_obj;

	KERNEL_ASSERT_LOCKED();

	rw_enter(uobj->vmobjlock, RW_WRITE);

	/* lock uvn and check for valid object, and if valid: do it! */
	if (uvn->u_flags & UVM_VNODE_VALID) {

		/*
		 * now check if the size has changed: if we shrink we had better
		 * toss some pages...
		 */

		if (uvn->u_size > newsize) {
			(void)uvn_flush(&uvn->u_obj, newsize,
			    uvn->u_size, PGO_FREE);
		}
		uvn->u_size = newsize;
	}
	rw_exit(uobj->vmobjlock);
}

/*
 * uvm_vnp_sync: flush all dirty VM pages back to their backing vnodes.
 *
 * => called from sys_sync with no VM structures locked
 * => only one process can do a sync at a time (because the uvn
 *    structure only has one queue for sync'ing).  we ensure this
 *    by holding the uvn_sync_lock while the sync is in progress.
 *    other processes attempting a sync will sleep on this lock
 *    until we are done.
 */
void
uvm_vnp_sync(struct mount *mp)
{
	struct uvm_vnode *uvn;
	struct vnode *vp;

	/*
	 * step 1: ensure we are only ones using the uvn_sync_q by locking
	 * our lock...
	 */
	rw_enter_write(&uvn_sync_lock);

	/*
	 * step 2: build up a simpleq of uvns of interest based on the
	 * write list.   we gain a reference to uvns of interest. 
	 */
	SIMPLEQ_INIT(&uvn_sync_q);
	LIST_FOREACH(uvn, &uvn_wlist, u_wlist) {
		vp = uvn->u_vnode;
		if (mp && vp->v_mount != mp)
			continue;

		/*
		 * If the vnode is "blocked" it means it must be dying, which
		 * in turn means its in the process of being flushed out so
		 * we can safely skip it.
		 *
		 * note that uvn must already be valid because we found it on
		 * the wlist (this also means it can't be ALOCK'd).
		 */
		if ((uvn->u_flags & UVM_VNODE_BLOCKED) != 0)
			continue;

		/*
		 * gain reference.   watch out for persisting uvns (need to
		 * regain vnode REF).
		 */
		if (uvn->u_obj.uo_refs == 0)
			vref(vp);
		uvn->u_obj.uo_refs++;

		SIMPLEQ_INSERT_HEAD(&uvn_sync_q, uvn, u_syncq);
	}

	/* step 3: we now have a list of uvn's that may need cleaning. */
	SIMPLEQ_FOREACH(uvn, &uvn_sync_q, u_syncq) {
		rw_enter(uvn->u_obj.vmobjlock, RW_WRITE);
#ifdef DEBUG
		if (uvn->u_flags & UVM_VNODE_DYING) {
			printf("uvm_vnp_sync: dying vnode on sync list\n");
		}
#endif
		uvn_flush(&uvn->u_obj, 0, 0, PGO_CLEANIT|PGO_ALLPAGES|PGO_DOACTCLUST);

		/*
		 * if we have the only reference and we just cleaned the uvn,
		 * then we can pull it out of the UVM_VNODE_WRITEABLE state
		 * thus allowing us to avoid thinking about flushing it again
		 * on later sync ops.
		 */
		if (uvn->u_obj.uo_refs == 1 &&
		    (uvn->u_flags & UVM_VNODE_WRITEABLE)) {
			LIST_REMOVE(uvn, u_wlist);
			uvn->u_flags &= ~UVM_VNODE_WRITEABLE;
		}
		rw_exit(uvn->u_obj.vmobjlock);

		/* now drop our reference to the uvn */
		uvn_detach(&uvn->u_obj);
	}

	rw_exit_write(&uvn_sync_lock);
}
