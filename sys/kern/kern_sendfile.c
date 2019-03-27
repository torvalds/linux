/*-
 * Copyright (c) 2013-2015 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 1998, David Greenman. All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/rwlock.h>
#include <sys/sf_buf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <net/vnet.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>

#define	EXT_FLAG_SYNC		EXT_FLAG_VENDOR1
#define	EXT_FLAG_NOCACHE	EXT_FLAG_VENDOR2

/*
 * Structure describing a single sendfile(2) I/O, which may consist of
 * several underlying pager I/Os.
 *
 * The syscall context allocates the structure and initializes 'nios'
 * to 1.  As sendfile_swapin() runs through pages and starts asynchronous
 * paging operations, it increments 'nios'.
 *
 * Every I/O completion calls sendfile_iodone(), which decrements the 'nios',
 * and the syscall also calls sendfile_iodone() after allocating all mbufs,
 * linking them and sending to socket.  Whoever reaches zero 'nios' is
 * responsible to * call pru_ready on the socket, to notify it of readyness
 * of the data.
 */
struct sf_io {
	volatile u_int	nios;
	u_int		error;
	int		npages;
	struct socket	*so;
	struct mbuf	*m;
	vm_page_t	pa[];
};

/*
 * Structure used to track requests with SF_SYNC flag.
 */
struct sendfile_sync {
	struct mtx	mtx;
	struct cv	cv;
	unsigned	count;
};

counter_u64_t sfstat[sizeof(struct sfstat) / sizeof(uint64_t)];

static void
sfstat_init(const void *unused)
{

	COUNTER_ARRAY_ALLOC(sfstat, sizeof(struct sfstat) / sizeof(uint64_t),
	    M_WAITOK);
}
SYSINIT(sfstat, SI_SUB_MBUF, SI_ORDER_FIRST, sfstat_init, NULL);

static int
sfstat_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct sfstat s;

	COUNTER_ARRAY_COPY(sfstat, &s, sizeof(s) / sizeof(uint64_t));
	if (req->newptr)
		COUNTER_ARRAY_ZERO(sfstat, sizeof(s) / sizeof(uint64_t));
	return (SYSCTL_OUT(req, &s, sizeof(s)));
}
SYSCTL_PROC(_kern_ipc, OID_AUTO, sfstat, CTLTYPE_OPAQUE | CTLFLAG_RW,
    NULL, 0, sfstat_sysctl, "I", "sendfile statistics");

/*
 * Detach mapped page and release resources back to the system.  Called
 * by mbuf(9) code when last reference to a page is freed.
 */
static void
sendfile_free_page(vm_page_t pg, bool nocache)
{
	bool freed;

	vm_page_lock(pg);
	/*
	 * In either case check for the object going away on us.  This can
	 * happen since we don't hold a reference to it.  If so, we're
	 * responsible for freeing the page.  In 'noncache' case try to free
	 * the page, but only if it is cheap to.
	 */
	if (vm_page_unwire_noq(pg)) {
		vm_object_t obj;

		if ((obj = pg->object) == NULL)
			vm_page_free(pg);
		else {
			freed = false;
			if (nocache && !vm_page_xbusied(pg) &&
			    VM_OBJECT_TRYWLOCK(obj)) {
				/* Only free unmapped pages. */
				if (obj->ref_count == 0 ||
				    !pmap_page_is_mapped(pg))
					/*
					 * The busy test before the object is
					 * locked cannot be relied upon.
					 */
					freed = vm_page_try_to_free(pg);
				VM_OBJECT_WUNLOCK(obj);
			}
			if (!freed) {
				/*
				 * If we were asked to not cache the page, place
				 * it near the head of the inactive queue so
				 * that it is reclaimed sooner.  Otherwise,
				 * maintain LRU.
				 */
				if (nocache)
					vm_page_deactivate_noreuse(pg);
				else if (vm_page_active(pg))
					vm_page_reference(pg);
				else
					vm_page_deactivate(pg);
			}
		}
	}
	vm_page_unlock(pg);
}

static void
sendfile_free_mext(struct mbuf *m)
{
	struct sf_buf *sf;
	vm_page_t pg;
	bool nocache;

	KASSERT(m->m_flags & M_EXT && m->m_ext.ext_type == EXT_SFBUF,
	    ("%s: m %p !M_EXT or !EXT_SFBUF", __func__, m));

	sf = m->m_ext.ext_arg1;
	pg = sf_buf_page(sf);
	nocache = m->m_ext.ext_flags & EXT_FLAG_NOCACHE;

	sf_buf_free(sf);
	sendfile_free_page(pg, nocache);

	if (m->m_ext.ext_flags & EXT_FLAG_SYNC) {
		struct sendfile_sync *sfs = m->m_ext.ext_arg2;

		mtx_lock(&sfs->mtx);
		KASSERT(sfs->count > 0, ("Sendfile sync botchup count == 0"));
		if (--sfs->count == 0)
			cv_signal(&sfs->cv);
		mtx_unlock(&sfs->mtx);
	}
}

/*
 * Helper function to calculate how much data to put into page i of n.
 * Only first and last pages are special.
 */
static inline off_t
xfsize(int i, int n, off_t off, off_t len)
{

	if (i == 0)
		return (omin(PAGE_SIZE - (off & PAGE_MASK), len));

	if (i == n - 1 && ((off + len) & PAGE_MASK) > 0)
		return ((off + len) & PAGE_MASK);

	return (PAGE_SIZE);
}

/*
 * Helper function to get offset within object for i page.
 */
static inline vm_ooffset_t
vmoff(int i, off_t off)
{

	if (i == 0)
		return ((vm_ooffset_t)off);

	return (trunc_page(off + i * PAGE_SIZE));
}

/*
 * Helper function used when allocation of a page or sf_buf failed.
 * Pretend as if we don't have enough space, subtract xfsize() of
 * all pages that failed.
 */
static inline void
fixspace(int old, int new, off_t off, int *space)
{

	KASSERT(old > new, ("%s: old %d new %d", __func__, old, new));

	/* Subtract last one. */
	*space -= xfsize(old - 1, old, off, *space);
	old--;

	if (new == old)
		/* There was only one page. */
		return;

	/* Subtract first one. */
	if (new == 0) {
		*space -= xfsize(0, old, off, *space);
		new++;
	}

	/* Rest of pages are full sized. */
	*space -= (old - new) * PAGE_SIZE;

	KASSERT(*space >= 0, ("%s: space went backwards", __func__));
}

/*
 * I/O completion callback.
 */
static void
sendfile_iodone(void *arg, vm_page_t *pg, int count, int error)
{
	struct sf_io *sfio = arg;
	struct socket *so = sfio->so;

	for (int i = 0; i < count; i++)
		if (pg[i] != bogus_page)
			vm_page_xunbusy(pg[i]);

	if (error)
		sfio->error = error;

	if (!refcount_release(&sfio->nios))
		return;

	CURVNET_SET(so->so_vnet);
	if (sfio->error) {
		struct mbuf *m;

		/*
		 * I/O operation failed.  The state of data in the socket
		 * is now inconsistent, and all what we can do is to tear
		 * it down. Protocol abort method would tear down protocol
		 * state, free all ready mbufs and detach not ready ones.
		 * We will free the mbufs corresponding to this I/O manually.
		 *
		 * The socket would be marked with EIO and made available
		 * for read, so that application receives EIO on next
		 * syscall and eventually closes the socket.
		 */
		so->so_proto->pr_usrreqs->pru_abort(so);
		so->so_error = EIO;

		m = sfio->m;
		for (int i = 0; i < sfio->npages; i++)
			m = m_free(m);
	} else
		(void )(so->so_proto->pr_usrreqs->pru_ready)(so, sfio->m,
		    sfio->npages);

	SOCK_LOCK(so);
	sorele(so);
	CURVNET_RESTORE();
	free(sfio, M_TEMP);
}

/*
 * Iterate through pages vector and request paging for non-valid pages.
 */
static int
sendfile_swapin(vm_object_t obj, struct sf_io *sfio, off_t off, off_t len,
    int npages, int rhpages, int flags)
{
	vm_page_t *pa = sfio->pa;
	int grabbed, nios;

	nios = 0;
	flags = (flags & SF_NODISKIO) ? VM_ALLOC_NOWAIT : 0;

	/*
	 * First grab all the pages and wire them.  Note that we grab
	 * only required pages.  Readahead pages are dealt with later.
	 */
	VM_OBJECT_WLOCK(obj);

	grabbed = vm_page_grab_pages(obj, OFF_TO_IDX(off),
	    VM_ALLOC_NORMAL | VM_ALLOC_WIRED | flags, pa, npages);
	if (grabbed < npages) {
		for (int i = grabbed; i < npages; i++)
			pa[i] = NULL;
		npages = grabbed;
		rhpages = 0;
	}

	for (int i = 0; i < npages;) {
		int j, a, count, rv __unused;

		/* Skip valid pages. */
		if (vm_page_is_valid(pa[i], vmoff(i, off) & PAGE_MASK,
		    xfsize(i, npages, off, len))) {
			vm_page_xunbusy(pa[i]);
			SFSTAT_INC(sf_pages_valid);
			i++;
			continue;
		}

		/*
		 * Next page is invalid.  Check if it belongs to pager.  It
		 * may not be there, which is a regular situation for shmem
		 * pager.  For vnode pager this happens only in case of
		 * a sparse file.
		 *
		 * Important feature of vm_pager_has_page() is the hint
		 * stored in 'a', about how many pages we can pagein after
		 * this page in a single I/O.
		 */
		if (!vm_pager_has_page(obj, OFF_TO_IDX(vmoff(i, off)), NULL,
		    &a)) {
			pmap_zero_page(pa[i]);
			pa[i]->valid = VM_PAGE_BITS_ALL;
			MPASS(pa[i]->dirty == 0);
			vm_page_xunbusy(pa[i]);
			i++;
			continue;
		}

		/*
		 * We want to pagein as many pages as possible, limited only
		 * by the 'a' hint and actual request.
		 */
		count = min(a + 1, npages - i);

		/*
		 * We should not pagein into a valid page, thus we first trim
		 * any valid pages off the end of request, and substitute
		 * to bogus_page those, that are in the middle.
		 */
		for (j = i + count - 1; j > i; j--) {
			if (vm_page_is_valid(pa[j], vmoff(j, off) & PAGE_MASK,
			    xfsize(j, npages, off, len))) {
				count--;
				rhpages = 0;
			} else
				break;
		}
		for (j = i + 1; j < i + count - 1; j++)
			if (vm_page_is_valid(pa[j], vmoff(j, off) & PAGE_MASK,
			    xfsize(j, npages, off, len))) {
				vm_page_xunbusy(pa[j]);
				SFSTAT_INC(sf_pages_valid);
				SFSTAT_INC(sf_pages_bogus);
				pa[j] = bogus_page;
			}

		refcount_acquire(&sfio->nios);
		rv = vm_pager_get_pages_async(obj, pa + i, count, NULL,
		    i + count == npages ? &rhpages : NULL,
		    &sendfile_iodone, sfio);
		KASSERT(rv == VM_PAGER_OK, ("%s: pager fail obj %p page %p",
		    __func__, obj, pa[i]));

		SFSTAT_INC(sf_iocnt);
		SFSTAT_ADD(sf_pages_read, count);
		if (i + count == npages)
			SFSTAT_ADD(sf_rhpages_read, rhpages);

		/*
		 * Restore the valid page pointers.  They are already
		 * unbusied, but still wired.
		 */
		for (j = i; j < i + count; j++)
			if (pa[j] == bogus_page) {
				pa[j] = vm_page_lookup(obj,
				    OFF_TO_IDX(vmoff(j, off)));
				KASSERT(pa[j], ("%s: page %p[%d] disappeared",
				    __func__, pa, j));

			}
		i += count;
		nios++;
	}

	VM_OBJECT_WUNLOCK(obj);

	if (nios == 0 && npages != 0)
		SFSTAT_INC(sf_noiocnt);

	return (nios);
}

static int
sendfile_getobj(struct thread *td, struct file *fp, vm_object_t *obj_res,
    struct vnode **vp_res, struct shmfd **shmfd_res, off_t *obj_size,
    int *bsize)
{
	struct vattr va;
	vm_object_t obj;
	struct vnode *vp;
	struct shmfd *shmfd;
	int error;

	vp = *vp_res = NULL;
	obj = NULL;
	shmfd = *shmfd_res = NULL;
	*bsize = 0;

	/*
	 * The file descriptor must be a regular file and have a
	 * backing VM object.
	 */
	if (fp->f_type == DTYPE_VNODE) {
		vp = fp->f_vnode;
		vn_lock(vp, LK_SHARED | LK_RETRY);
		if (vp->v_type != VREG) {
			error = EINVAL;
			goto out;
		}
		*bsize = vp->v_mount->mnt_stat.f_iosize;
		error = VOP_GETATTR(vp, &va, td->td_ucred);
		if (error != 0)
			goto out;
		*obj_size = va.va_size;
		obj = vp->v_object;
		if (obj == NULL) {
			error = EINVAL;
			goto out;
		}
	} else if (fp->f_type == DTYPE_SHM) {
		error = 0;
		shmfd = fp->f_data;
		obj = shmfd->shm_object;
		*obj_size = shmfd->shm_size;
	} else {
		error = EINVAL;
		goto out;
	}

	VM_OBJECT_WLOCK(obj);
	if ((obj->flags & OBJ_DEAD) != 0) {
		VM_OBJECT_WUNLOCK(obj);
		error = EBADF;
		goto out;
	}

	/*
	 * Temporarily increase the backing VM object's reference
	 * count so that a forced reclamation of its vnode does not
	 * immediately destroy it.
	 */
	vm_object_reference_locked(obj);
	VM_OBJECT_WUNLOCK(obj);
	*obj_res = obj;
	*vp_res = vp;
	*shmfd_res = shmfd;

out:
	if (vp != NULL)
		VOP_UNLOCK(vp, 0);
	return (error);
}

static int
sendfile_getsock(struct thread *td, int s, struct file **sock_fp,
    struct socket **so)
{
	int error;

	*sock_fp = NULL;
	*so = NULL;

	/*
	 * The socket must be a stream socket and connected.
	 */
	error = getsock_cap(td, s, &cap_send_rights,
	    sock_fp, NULL, NULL);
	if (error != 0)
		return (error);
	*so = (*sock_fp)->f_data;
	if ((*so)->so_type != SOCK_STREAM)
		return (EINVAL);
	if (SOLISTENING(*so))
		return (ENOTCONN);
	return (0);
}

int
vn_sendfile(struct file *fp, int sockfd, struct uio *hdr_uio,
    struct uio *trl_uio, off_t offset, size_t nbytes, off_t *sent, int flags,
    struct thread *td)
{
	struct file *sock_fp;
	struct vnode *vp;
	struct vm_object *obj;
	struct socket *so;
	struct mbuf *m, *mh, *mhtail;
	struct sf_buf *sf;
	struct shmfd *shmfd;
	struct sendfile_sync *sfs;
	struct vattr va;
	off_t off, sbytes, rem, obj_size;
	int error, softerr, bsize, hdrlen;

	obj = NULL;
	so = NULL;
	m = mh = NULL;
	sfs = NULL;
	hdrlen = sbytes = 0;
	softerr = 0;

	error = sendfile_getobj(td, fp, &obj, &vp, &shmfd, &obj_size, &bsize);
	if (error != 0)
		return (error);

	error = sendfile_getsock(td, sockfd, &sock_fp, &so);
	if (error != 0)
		goto out;

#ifdef MAC
	error = mac_socket_check_send(td->td_ucred, so);
	if (error != 0)
		goto out;
#endif

	SFSTAT_INC(sf_syscalls);
	SFSTAT_ADD(sf_rhpages_requested, SF_READAHEAD(flags));

	if (flags & SF_SYNC) {
		sfs = malloc(sizeof *sfs, M_TEMP, M_WAITOK | M_ZERO);
		mtx_init(&sfs->mtx, "sendfile", NULL, MTX_DEF);
		cv_init(&sfs->cv, "sendfile");
	}

	rem = nbytes ? omin(nbytes, obj_size - offset) : obj_size - offset;

	/*
	 * Protect against multiple writers to the socket.
	 *
	 * XXXRW: Historically this has assumed non-interruptibility, so now
	 * we implement that, but possibly shouldn't.
	 */
	(void)sblock(&so->so_snd, SBL_WAIT | SBL_NOINTR);

	/*
	 * Loop through the pages of the file, starting with the requested
	 * offset. Get a file page (do I/O if necessary), map the file page
	 * into an sf_buf, attach an mbuf header to the sf_buf, and queue
	 * it on the socket.
	 * This is done in two loops.  The inner loop turns as many pages
	 * as it can, up to available socket buffer space, without blocking
	 * into mbufs to have it bulk delivered into the socket send buffer.
	 * The outer loop checks the state and available space of the socket
	 * and takes care of the overall progress.
	 */
	for (off = offset; rem > 0; ) {
		struct sf_io *sfio;
		vm_page_t *pa;
		struct mbuf *mtail;
		int nios, space, npages, rhpages;

		mtail = NULL;
		/*
		 * Check the socket state for ongoing connection,
		 * no errors and space in socket buffer.
		 * If space is low allow for the remainder of the
		 * file to be processed if it fits the socket buffer.
		 * Otherwise block in waiting for sufficient space
		 * to proceed, or if the socket is nonblocking, return
		 * to userland with EAGAIN while reporting how far
		 * we've come.
		 * We wait until the socket buffer has significant free
		 * space to do bulk sends.  This makes good use of file
		 * system read ahead and allows packet segmentation
		 * offloading hardware to take over lots of work.  If
		 * we were not careful here we would send off only one
		 * sfbuf at a time.
		 */
		SOCKBUF_LOCK(&so->so_snd);
		if (so->so_snd.sb_lowat < so->so_snd.sb_hiwat / 2)
			so->so_snd.sb_lowat = so->so_snd.sb_hiwat / 2;
retry_space:
		if (so->so_snd.sb_state & SBS_CANTSENDMORE) {
			error = EPIPE;
			SOCKBUF_UNLOCK(&so->so_snd);
			goto done;
		} else if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			SOCKBUF_UNLOCK(&so->so_snd);
			goto done;
		}
		if ((so->so_state & SS_ISCONNECTED) == 0) {
			SOCKBUF_UNLOCK(&so->so_snd);
			error = ENOTCONN;
			goto done;
		}

		space = sbspace(&so->so_snd);
		if (space < rem &&
		    (space <= 0 ||
		     space < so->so_snd.sb_lowat)) {
			if (so->so_state & SS_NBIO) {
				SOCKBUF_UNLOCK(&so->so_snd);
				error = EAGAIN;
				goto done;
			}
			/*
			 * sbwait drops the lock while sleeping.
			 * When we loop back to retry_space the
			 * state may have changed and we retest
			 * for it.
			 */
			error = sbwait(&so->so_snd);
			/*
			 * An error from sbwait usually indicates that we've
			 * been interrupted by a signal. If we've sent anything
			 * then return bytes sent, otherwise return the error.
			 */
			if (error != 0) {
				SOCKBUF_UNLOCK(&so->so_snd);
				goto done;
			}
			goto retry_space;
		}
		SOCKBUF_UNLOCK(&so->so_snd);

		/*
		 * At the beginning of the first loop check if any headers
		 * are specified and copy them into mbufs.  Reduce space in
		 * the socket buffer by the size of the header mbuf chain.
		 * Clear hdr_uio here and hdrlen at the end of the first loop.
		 */
		if (hdr_uio != NULL && hdr_uio->uio_resid > 0) {
			hdr_uio->uio_td = td;
			hdr_uio->uio_rw = UIO_WRITE;
			mh = m_uiotombuf(hdr_uio, M_WAITOK, space, 0, 0);
			hdrlen = m_length(mh, &mhtail);
			space -= hdrlen;
			/*
			 * If header consumed all the socket buffer space,
			 * don't waste CPU cycles and jump to the end.
			 */
			if (space == 0) {
				sfio = NULL;
				nios = 0;
				goto prepend_header;
			}
			hdr_uio = NULL;
		}

		if (vp != NULL) {
			error = vn_lock(vp, LK_SHARED);
			if (error != 0)
				goto done;
			error = VOP_GETATTR(vp, &va, td->td_ucred);
			if (error != 0 || off >= va.va_size) {
				VOP_UNLOCK(vp, 0);
				goto done;
			}
			if (va.va_size != obj_size) {
				obj_size = va.va_size;
				rem = nbytes ?
				    omin(nbytes + offset, obj_size) : obj_size;
				rem -= off;
			}
		}

		if (space > rem)
			space = rem;

		npages = howmany(space + (off & PAGE_MASK), PAGE_SIZE);

		/*
		 * Calculate maximum allowed number of pages for readahead
		 * at this iteration.  If SF_USER_READAHEAD was set, we don't
		 * do any heuristics and use exactly the value supplied by
		 * application.  Otherwise, we allow readahead up to "rem".
		 * If application wants more, let it be, but there is no
		 * reason to go above MAXPHYS.  Also check against "obj_size",
		 * since vm_pager_has_page() can hint beyond EOF.
		 */
		if (flags & SF_USER_READAHEAD) {
			rhpages = SF_READAHEAD(flags);
		} else {
			rhpages = howmany(rem + (off & PAGE_MASK), PAGE_SIZE) -
			    npages;
			rhpages += SF_READAHEAD(flags);
		}
		rhpages = min(howmany(MAXPHYS, PAGE_SIZE), rhpages);
		rhpages = min(howmany(obj_size - trunc_page(off), PAGE_SIZE) -
		    npages, rhpages);

		sfio = malloc(sizeof(struct sf_io) +
		    npages * sizeof(vm_page_t), M_TEMP, M_WAITOK);
		refcount_init(&sfio->nios, 1);
		sfio->so = so;
		sfio->error = 0;

		nios = sendfile_swapin(obj, sfio, off, space, npages, rhpages,
		    flags);

		/*
		 * Loop and construct maximum sized mbuf chain to be bulk
		 * dumped into socket buffer.
		 */
		pa = sfio->pa;
		for (int i = 0; i < npages; i++) {
			struct mbuf *m0;

			/*
			 * If a page wasn't grabbed successfully, then
			 * trim the array. Can happen only with SF_NODISKIO.
			 */
			if (pa[i] == NULL) {
				SFSTAT_INC(sf_busy);
				fixspace(npages, i, off, &space);
				npages = i;
				softerr = EBUSY;
				break;
			}

			/*
			 * Get a sendfile buf.  When allocating the
			 * first buffer for mbuf chain, we usually
			 * wait as long as necessary, but this wait
			 * can be interrupted.  For consequent
			 * buffers, do not sleep, since several
			 * threads might exhaust the buffers and then
			 * deadlock.
			 */
			sf = sf_buf_alloc(pa[i],
			    m != NULL ? SFB_NOWAIT : SFB_CATCH);
			if (sf == NULL) {
				SFSTAT_INC(sf_allocfail);
				for (int j = i; j < npages; j++) {
					vm_page_lock(pa[j]);
					vm_page_unwire(pa[j], PQ_INACTIVE);
					vm_page_unlock(pa[j]);
				}
				if (m == NULL)
					softerr = ENOBUFS;
				fixspace(npages, i, off, &space);
				npages = i;
				break;
			}

			m0 = m_get(M_WAITOK, MT_DATA);
			m0->m_ext.ext_buf = (char *)sf_buf_kva(sf);
			m0->m_ext.ext_size = PAGE_SIZE;
			m0->m_ext.ext_arg1 = sf;
			m0->m_ext.ext_type = EXT_SFBUF;
			m0->m_ext.ext_flags = EXT_FLAG_EMBREF;
			m0->m_ext.ext_free = sendfile_free_mext;
			/*
			 * SF_NOCACHE sets the page as being freed upon send.
			 * However, we ignore it for the last page in 'space',
			 * if the page is truncated, and we got more data to
			 * send (rem > space), or if we have readahead
			 * configured (rhpages > 0).
			 */
			if ((flags & SF_NOCACHE) &&
			    (i != npages - 1 ||
			    !((off + space) & PAGE_MASK) ||
			    !(rem > space || rhpages > 0)))
				m0->m_ext.ext_flags |= EXT_FLAG_NOCACHE;
			if (sfs != NULL) {
				m0->m_ext.ext_flags |= EXT_FLAG_SYNC;
				m0->m_ext.ext_arg2 = sfs;
				mtx_lock(&sfs->mtx);
				sfs->count++;
				mtx_unlock(&sfs->mtx);
			}
			m0->m_ext.ext_count = 1;
			m0->m_flags |= (M_EXT | M_RDONLY);
			if (nios)
				m0->m_flags |= M_NOTREADY;
			m0->m_data = (char *)sf_buf_kva(sf) +
			    (vmoff(i, off) & PAGE_MASK);
			m0->m_len = xfsize(i, npages, off, space);

			if (i == 0)
				sfio->m = m0;

			/* Append to mbuf chain. */
			if (mtail != NULL)
				mtail->m_next = m0;
			else
				m = m0;
			mtail = m0;
		}

		if (vp != NULL)
			VOP_UNLOCK(vp, 0);

		/* Keep track of bytes processed. */
		off += space;
		rem -= space;

		/* Prepend header, if any. */
		if (hdrlen) {
prepend_header:
			mhtail->m_next = m;
			m = mh;
			mh = NULL;
		}

		if (m == NULL) {
			KASSERT(softerr, ("%s: m NULL, no error", __func__));
			error = softerr;
			free(sfio, M_TEMP);
			goto done;
		}

		/* Add the buffer chain to the socket buffer. */
		KASSERT(m_length(m, NULL) == space + hdrlen,
		    ("%s: mlen %u space %d hdrlen %d",
		    __func__, m_length(m, NULL), space, hdrlen));

		CURVNET_SET(so->so_vnet);
		if (nios == 0) {
			/*
			 * If sendfile_swapin() didn't initiate any I/Os,
			 * which happens if all data is cached in VM, then
			 * we can send data right now without the
			 * PRUS_NOTREADY flag.
			 */
			free(sfio, M_TEMP);
			error = (*so->so_proto->pr_usrreqs->pru_send)
			    (so, 0, m, NULL, NULL, td);
		} else {
			sfio->npages = npages;
			soref(so);
			error = (*so->so_proto->pr_usrreqs->pru_send)
			    (so, PRUS_NOTREADY, m, NULL, NULL, td);
			sendfile_iodone(sfio, NULL, 0, 0);
		}
		CURVNET_RESTORE();

		m = NULL;	/* pru_send always consumes */
		if (error)
			goto done;
		sbytes += space + hdrlen;
		if (hdrlen)
			hdrlen = 0;
		if (softerr) {
			error = softerr;
			goto done;
		}
	}

	/*
	 * Send trailers. Wimp out and use writev(2).
	 */
	if (trl_uio != NULL) {
		sbunlock(&so->so_snd);
		error = kern_writev(td, sockfd, trl_uio);
		if (error == 0)
			sbytes += td->td_retval[0];
		goto out;
	}

done:
	sbunlock(&so->so_snd);
out:
	/*
	 * If there was no error we have to clear td->td_retval[0]
	 * because it may have been set by writev.
	 */
	if (error == 0) {
		td->td_retval[0] = 0;
	}
	if (sent != NULL) {
		(*sent) = sbytes;
	}
	if (obj != NULL)
		vm_object_deallocate(obj);
	if (so)
		fdrop(sock_fp, td);
	if (m)
		m_freem(m);
	if (mh)
		m_freem(mh);

	if (sfs != NULL) {
		mtx_lock(&sfs->mtx);
		if (sfs->count != 0)
			cv_wait(&sfs->cv, &sfs->mtx);
		KASSERT(sfs->count == 0, ("sendfile sync still busy"));
		cv_destroy(&sfs->cv);
		mtx_destroy(&sfs->mtx);
		free(sfs, M_TEMP);
	}

	if (error == ERESTART)
		error = EINTR;

	return (error);
}

static int
sendfile(struct thread *td, struct sendfile_args *uap, int compat)
{
	struct sf_hdtr hdtr;
	struct uio *hdr_uio, *trl_uio;
	struct file *fp;
	off_t sbytes;
	int error;

	/*
	 * File offset must be positive.  If it goes beyond EOF
	 * we send only the header/trailer and no payload data.
	 */
	if (uap->offset < 0)
		return (EINVAL);

	sbytes = 0;
	hdr_uio = trl_uio = NULL;

	if (uap->hdtr != NULL) {
		error = copyin(uap->hdtr, &hdtr, sizeof(hdtr));
		if (error != 0)
			goto out;
		if (hdtr.headers != NULL) {
			error = copyinuio(hdtr.headers, hdtr.hdr_cnt,
			    &hdr_uio);
			if (error != 0)
				goto out;
#ifdef COMPAT_FREEBSD4
			/*
			 * In FreeBSD < 5.0 the nbytes to send also included
			 * the header.  If compat is specified subtract the
			 * header size from nbytes.
			 */
			if (compat) {
				if (uap->nbytes > hdr_uio->uio_resid)
					uap->nbytes -= hdr_uio->uio_resid;
				else
					uap->nbytes = 0;
			}
#endif
		}
		if (hdtr.trailers != NULL) {
			error = copyinuio(hdtr.trailers, hdtr.trl_cnt,
			    &trl_uio);
			if (error != 0)
				goto out;
		}
	}

	AUDIT_ARG_FD(uap->fd);

	/*
	 * sendfile(2) can start at any offset within a file so we require
	 * CAP_READ+CAP_SEEK = CAP_PREAD.
	 */
	if ((error = fget_read(td, uap->fd, &cap_pread_rights, &fp)) != 0)
		goto out;

	error = fo_sendfile(fp, uap->s, hdr_uio, trl_uio, uap->offset,
	    uap->nbytes, &sbytes, uap->flags, td);
	fdrop(fp, td);

	if (uap->sbytes != NULL)
		copyout(&sbytes, uap->sbytes, sizeof(off_t));

out:
	free(hdr_uio, M_IOV);
	free(trl_uio, M_IOV);
	return (error);
}

/*
 * sendfile(2)
 * 
 * int sendfile(int fd, int s, off_t offset, size_t nbytes,
 *       struct sf_hdtr *hdtr, off_t *sbytes, int flags)
 * 
 * Send a file specified by 'fd' and starting at 'offset' to a socket
 * specified by 's'. Send only 'nbytes' of the file or until EOF if nbytes ==
 * 0.  Optionally add a header and/or trailer to the socket output.  If
 * specified, write the total number of bytes sent into *sbytes.
 */
int
sys_sendfile(struct thread *td, struct sendfile_args *uap)
{
 
	return (sendfile(td, uap, 0));
}

#ifdef COMPAT_FREEBSD4
int
freebsd4_sendfile(struct thread *td, struct freebsd4_sendfile_args *uap)
{
	struct sendfile_args args;

	args.fd = uap->fd;
	args.s = uap->s;
	args.offset = uap->offset;
	args.nbytes = uap->nbytes;
	args.hdtr = uap->hdtr;
	args.sbytes = uap->sbytes;
	args.flags = uap->flags;

	return (sendfile(td, &args, 1));
}
#endif /* COMPAT_FREEBSD4 */
