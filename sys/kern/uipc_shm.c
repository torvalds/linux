/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006, 2011, 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
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
 */

/*
 * Support for shared swap-backed anonymous memory objects via
 * shm_open(2) and shm_unlink(2).  While most of the implementation is
 * here, vm_mmap.c contains mapping logic changes.
 *
 * TODO:
 *
 * (1) Need to export data to a userland tool via a sysctl.  Should ipcs(1)
 *     and ipcrm(1) be expanded or should new tools to manage both POSIX
 *     kernel semaphores and POSIX shared memory be written?
 *
 * (2) Add support for this file type to fstat(1).
 *
 * (3) Resource limits?  Does this need its own resource limits or are the
 *     existing limits in mmap(2) sufficient?
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/filio.h>
#include <sys/fnv_hash.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/signal.h>
#include <sys/jail.h>
#include <sys/ktrace.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/refcount.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/unistd.h>
#include <sys/user.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_kern.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/swap_pager.h>

struct shm_mapping {
	char		*sm_path;
	Fnv32_t		sm_fnv;
	struct shmfd	*sm_shmfd;
	LIST_ENTRY(shm_mapping) sm_link;
};

static MALLOC_DEFINE(M_SHMFD, "shmfd", "shared memory file descriptor");
static LIST_HEAD(, shm_mapping) *shm_dictionary;
static struct sx shm_dict_lock;
static struct mtx shm_timestamp_lock;
static u_long shm_hash;
static struct unrhdr64 shm_ino_unr;
static dev_t shm_dev_ino;

#define	SHM_HASH(fnv)	(&shm_dictionary[(fnv) & shm_hash])

static void	shm_init(void *arg);
static void	shm_insert(char *path, Fnv32_t fnv, struct shmfd *shmfd);
static struct shmfd *shm_lookup(char *path, Fnv32_t fnv);
static int	shm_remove(char *path, Fnv32_t fnv, struct ucred *ucred);

static fo_rdwr_t	shm_read;
static fo_rdwr_t	shm_write;
static fo_truncate_t	shm_truncate;
static fo_ioctl_t	shm_ioctl;
static fo_stat_t	shm_stat;
static fo_close_t	shm_close;
static fo_chmod_t	shm_chmod;
static fo_chown_t	shm_chown;
static fo_seek_t	shm_seek;
static fo_fill_kinfo_t	shm_fill_kinfo;
static fo_mmap_t	shm_mmap;

/* File descriptor operations. */
struct fileops shm_ops = {
	.fo_read = shm_read,
	.fo_write = shm_write,
	.fo_truncate = shm_truncate,
	.fo_ioctl = shm_ioctl,
	.fo_poll = invfo_poll,
	.fo_kqfilter = invfo_kqfilter,
	.fo_stat = shm_stat,
	.fo_close = shm_close,
	.fo_chmod = shm_chmod,
	.fo_chown = shm_chown,
	.fo_sendfile = vn_sendfile,
	.fo_seek = shm_seek,
	.fo_fill_kinfo = shm_fill_kinfo,
	.fo_mmap = shm_mmap,
	.fo_flags = DFLAG_PASSABLE | DFLAG_SEEKABLE
};

FEATURE(posix_shm, "POSIX shared memory");

static int
uiomove_object_page(vm_object_t obj, size_t len, struct uio *uio)
{
	vm_page_t m;
	vm_pindex_t idx;
	size_t tlen;
	int error, offset, rv;

	idx = OFF_TO_IDX(uio->uio_offset);
	offset = uio->uio_offset & PAGE_MASK;
	tlen = MIN(PAGE_SIZE - offset, len);

	VM_OBJECT_WLOCK(obj);

	/*
	 * Read I/O without either a corresponding resident page or swap
	 * page: use zero_region.  This is intended to avoid instantiating
	 * pages on read from a sparse region.
	 */
	if (uio->uio_rw == UIO_READ && vm_page_lookup(obj, idx) == NULL &&
	    !vm_pager_has_page(obj, idx, NULL, NULL)) {
		VM_OBJECT_WUNLOCK(obj);
		return (uiomove(__DECONST(void *, zero_region), tlen, uio));
	}

	/*
	 * Parallel reads of the page content from disk are prevented
	 * by exclusive busy.
	 *
	 * Although the tmpfs vnode lock is held here, it is
	 * nonetheless safe to sleep waiting for a free page.  The
	 * pageout daemon does not need to acquire the tmpfs vnode
	 * lock to page out tobj's pages because tobj is a OBJT_SWAP
	 * type object.
	 */
	m = vm_page_grab(obj, idx, VM_ALLOC_NORMAL | VM_ALLOC_NOBUSY);
	if (m->valid != VM_PAGE_BITS_ALL) {
		vm_page_xbusy(m);
		if (vm_pager_has_page(obj, idx, NULL, NULL)) {
			rv = vm_pager_get_pages(obj, &m, 1, NULL, NULL);
			if (rv != VM_PAGER_OK) {
				printf(
	    "uiomove_object: vm_obj %p idx %jd valid %x pager error %d\n",
				    obj, idx, m->valid, rv);
				vm_page_lock(m);
				vm_page_free(m);
				vm_page_unlock(m);
				VM_OBJECT_WUNLOCK(obj);
				return (EIO);
			}
		} else
			vm_page_zero_invalid(m, TRUE);
		vm_page_xunbusy(m);
	}
	vm_page_lock(m);
	vm_page_hold(m);
	if (vm_page_active(m))
		vm_page_reference(m);
	else
		vm_page_activate(m);
	vm_page_unlock(m);
	VM_OBJECT_WUNLOCK(obj);
	error = uiomove_fromphys(&m, offset, tlen, uio);
	if (uio->uio_rw == UIO_WRITE && error == 0) {
		VM_OBJECT_WLOCK(obj);
		vm_page_dirty(m);
		vm_pager_page_unswapped(m);
		VM_OBJECT_WUNLOCK(obj);
	}
	vm_page_lock(m);
	vm_page_unhold(m);
	vm_page_unlock(m);

	return (error);
}

int
uiomove_object(vm_object_t obj, off_t obj_size, struct uio *uio)
{
	ssize_t resid;
	size_t len;
	int error;

	error = 0;
	while ((resid = uio->uio_resid) > 0) {
		if (obj_size <= uio->uio_offset)
			break;
		len = MIN(obj_size - uio->uio_offset, resid);
		if (len == 0)
			break;
		error = uiomove_object_page(obj, len, uio);
		if (error != 0 || resid == uio->uio_resid)
			break;
	}
	return (error);
}

static int
shm_seek(struct file *fp, off_t offset, int whence, struct thread *td)
{
	struct shmfd *shmfd;
	off_t foffset;
	int error;

	shmfd = fp->f_data;
	foffset = foffset_lock(fp, 0);
	error = 0;
	switch (whence) {
	case L_INCR:
		if (foffset < 0 ||
		    (offset > 0 && foffset > OFF_MAX - offset)) {
			error = EOVERFLOW;
			break;
		}
		offset += foffset;
		break;
	case L_XTND:
		if (offset > 0 && shmfd->shm_size > OFF_MAX - offset) {
			error = EOVERFLOW;
			break;
		}
		offset += shmfd->shm_size;
		break;
	case L_SET:
		break;
	default:
		error = EINVAL;
	}
	if (error == 0) {
		if (offset < 0 || offset > shmfd->shm_size)
			error = EINVAL;
		else
			td->td_uretoff.tdu_off = offset;
	}
	foffset_unlock(fp, offset, error != 0 ? FOF_NOUPDATE : 0);
	return (error);
}

static int
shm_read(struct file *fp, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{
	struct shmfd *shmfd;
	void *rl_cookie;
	int error;

	shmfd = fp->f_data;
#ifdef MAC
	error = mac_posixshm_check_read(active_cred, fp->f_cred, shmfd);
	if (error)
		return (error);
#endif
	foffset_lock_uio(fp, uio, flags);
	rl_cookie = rangelock_rlock(&shmfd->shm_rl, uio->uio_offset,
	    uio->uio_offset + uio->uio_resid, &shmfd->shm_mtx);
	error = uiomove_object(shmfd->shm_object, shmfd->shm_size, uio);
	rangelock_unlock(&shmfd->shm_rl, rl_cookie, &shmfd->shm_mtx);
	foffset_unlock_uio(fp, uio, flags);
	return (error);
}

static int
shm_write(struct file *fp, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{
	struct shmfd *shmfd;
	void *rl_cookie;
	int error;

	shmfd = fp->f_data;
#ifdef MAC
	error = mac_posixshm_check_write(active_cred, fp->f_cred, shmfd);
	if (error)
		return (error);
#endif
	foffset_lock_uio(fp, uio, flags);
	if ((flags & FOF_OFFSET) == 0) {
		rl_cookie = rangelock_wlock(&shmfd->shm_rl, 0, OFF_MAX,
		    &shmfd->shm_mtx);
	} else {
		rl_cookie = rangelock_wlock(&shmfd->shm_rl, uio->uio_offset,
		    uio->uio_offset + uio->uio_resid, &shmfd->shm_mtx);
	}

	error = uiomove_object(shmfd->shm_object, shmfd->shm_size, uio);
	rangelock_unlock(&shmfd->shm_rl, rl_cookie, &shmfd->shm_mtx);
	foffset_unlock_uio(fp, uio, flags);
	return (error);
}

static int
shm_truncate(struct file *fp, off_t length, struct ucred *active_cred,
    struct thread *td)
{
	struct shmfd *shmfd;
#ifdef MAC
	int error;
#endif

	shmfd = fp->f_data;
#ifdef MAC
	error = mac_posixshm_check_truncate(active_cred, fp->f_cred, shmfd);
	if (error)
		return (error);
#endif
	return (shm_dotruncate(shmfd, length));
}

int
shm_ioctl(struct file *fp, u_long com, void *data, struct ucred *active_cred,
    struct thread *td)
{

	switch (com) {
	case FIONBIO:
	case FIOASYNC:
		/*
		 * Allow fcntl(fd, F_SETFL, O_NONBLOCK) to work,
		 * just like it would on an unlinked regular file
		 */
		return (0);
	default:
		return (ENOTTY);
	}
}

static int
shm_stat(struct file *fp, struct stat *sb, struct ucred *active_cred,
    struct thread *td)
{
	struct shmfd *shmfd;
#ifdef MAC
	int error;
#endif

	shmfd = fp->f_data;

#ifdef MAC
	error = mac_posixshm_check_stat(active_cred, fp->f_cred, shmfd);
	if (error)
		return (error);
#endif
	
	/*
	 * Attempt to return sanish values for fstat() on a memory file
	 * descriptor.
	 */
	bzero(sb, sizeof(*sb));
	sb->st_blksize = PAGE_SIZE;
	sb->st_size = shmfd->shm_size;
	sb->st_blocks = howmany(sb->st_size, sb->st_blksize);
	mtx_lock(&shm_timestamp_lock);
	sb->st_atim = shmfd->shm_atime;
	sb->st_ctim = shmfd->shm_ctime;
	sb->st_mtim = shmfd->shm_mtime;
	sb->st_birthtim = shmfd->shm_birthtime;
	sb->st_mode = S_IFREG | shmfd->shm_mode;		/* XXX */
	sb->st_uid = shmfd->shm_uid;
	sb->st_gid = shmfd->shm_gid;
	mtx_unlock(&shm_timestamp_lock);
	sb->st_dev = shm_dev_ino;
	sb->st_ino = shmfd->shm_ino;

	return (0);
}

static int
shm_close(struct file *fp, struct thread *td)
{
	struct shmfd *shmfd;

	shmfd = fp->f_data;
	fp->f_data = NULL;
	shm_drop(shmfd);

	return (0);
}

int
shm_dotruncate(struct shmfd *shmfd, off_t length)
{
	vm_object_t object;
	vm_page_t m;
	vm_pindex_t idx, nobjsize;
	vm_ooffset_t delta;
	int base, rv;

	KASSERT(length >= 0, ("shm_dotruncate: length < 0"));
	object = shmfd->shm_object;
	VM_OBJECT_WLOCK(object);
	if (length == shmfd->shm_size) {
		VM_OBJECT_WUNLOCK(object);
		return (0);
	}
	nobjsize = OFF_TO_IDX(length + PAGE_MASK);

	/* Are we shrinking?  If so, trim the end. */
	if (length < shmfd->shm_size) {
		/*
		 * Disallow any requests to shrink the size if this
		 * object is mapped into the kernel.
		 */
		if (shmfd->shm_kmappings > 0) {
			VM_OBJECT_WUNLOCK(object);
			return (EBUSY);
		}

		/*
		 * Zero the truncated part of the last page.
		 */
		base = length & PAGE_MASK;
		if (base != 0) {
			idx = OFF_TO_IDX(length);
retry:
			m = vm_page_lookup(object, idx);
			if (m != NULL) {
				if (vm_page_sleep_if_busy(m, "shmtrc"))
					goto retry;
			} else if (vm_pager_has_page(object, idx, NULL, NULL)) {
				m = vm_page_alloc(object, idx,
				    VM_ALLOC_NORMAL | VM_ALLOC_WAITFAIL);
				if (m == NULL)
					goto retry;
				rv = vm_pager_get_pages(object, &m, 1, NULL,
				    NULL);
				vm_page_lock(m);
				if (rv == VM_PAGER_OK) {
					/*
					 * Since the page was not resident,
					 * and therefore not recently
					 * accessed, immediately enqueue it
					 * for asynchronous laundering.  The
					 * current operation is not regarded
					 * as an access.
					 */
					vm_page_launder(m);
					vm_page_unlock(m);
					vm_page_xunbusy(m);
				} else {
					vm_page_free(m);
					vm_page_unlock(m);
					VM_OBJECT_WUNLOCK(object);
					return (EIO);
				}
			}
			if (m != NULL) {
				pmap_zero_page_area(m, base, PAGE_SIZE - base);
				KASSERT(m->valid == VM_PAGE_BITS_ALL,
				    ("shm_dotruncate: page %p is invalid", m));
				vm_page_dirty(m);
				vm_pager_page_unswapped(m);
			}
		}
		delta = IDX_TO_OFF(object->size - nobjsize);

		/* Toss in memory pages. */
		if (nobjsize < object->size)
			vm_object_page_remove(object, nobjsize, object->size,
			    0);

		/* Toss pages from swap. */
		if (object->type == OBJT_SWAP)
			swap_pager_freespace(object, nobjsize, delta);

		/* Free the swap accounted for shm */
		swap_release_by_cred(delta, object->cred);
		object->charge -= delta;
	} else {
		/* Try to reserve additional swap space. */
		delta = IDX_TO_OFF(nobjsize - object->size);
		if (!swap_reserve_by_cred(delta, object->cred)) {
			VM_OBJECT_WUNLOCK(object);
			return (ENOMEM);
		}
		object->charge += delta;
	}
	shmfd->shm_size = length;
	mtx_lock(&shm_timestamp_lock);
	vfs_timestamp(&shmfd->shm_ctime);
	shmfd->shm_mtime = shmfd->shm_ctime;
	mtx_unlock(&shm_timestamp_lock);
	object->size = nobjsize;
	VM_OBJECT_WUNLOCK(object);
	return (0);
}

/*
 * shmfd object management including creation and reference counting
 * routines.
 */
struct shmfd *
shm_alloc(struct ucred *ucred, mode_t mode)
{
	struct shmfd *shmfd;

	shmfd = malloc(sizeof(*shmfd), M_SHMFD, M_WAITOK | M_ZERO);
	shmfd->shm_size = 0;
	shmfd->shm_uid = ucred->cr_uid;
	shmfd->shm_gid = ucred->cr_gid;
	shmfd->shm_mode = mode;
	shmfd->shm_object = vm_pager_allocate(OBJT_DEFAULT, NULL,
	    shmfd->shm_size, VM_PROT_DEFAULT, 0, ucred);
	KASSERT(shmfd->shm_object != NULL, ("shm_create: vm_pager_allocate"));
	shmfd->shm_object->pg_color = 0;
	VM_OBJECT_WLOCK(shmfd->shm_object);
	vm_object_clear_flag(shmfd->shm_object, OBJ_ONEMAPPING);
	vm_object_set_flag(shmfd->shm_object, OBJ_COLORED | OBJ_NOSPLIT);
	VM_OBJECT_WUNLOCK(shmfd->shm_object);
	vfs_timestamp(&shmfd->shm_birthtime);
	shmfd->shm_atime = shmfd->shm_mtime = shmfd->shm_ctime =
	    shmfd->shm_birthtime;
	shmfd->shm_ino = alloc_unr64(&shm_ino_unr);
	refcount_init(&shmfd->shm_refs, 1);
	mtx_init(&shmfd->shm_mtx, "shmrl", NULL, MTX_DEF);
	rangelock_init(&shmfd->shm_rl);
#ifdef MAC
	mac_posixshm_init(shmfd);
	mac_posixshm_create(ucred, shmfd);
#endif

	return (shmfd);
}

struct shmfd *
shm_hold(struct shmfd *shmfd)
{

	refcount_acquire(&shmfd->shm_refs);
	return (shmfd);
}

void
shm_drop(struct shmfd *shmfd)
{

	if (refcount_release(&shmfd->shm_refs)) {
#ifdef MAC
		mac_posixshm_destroy(shmfd);
#endif
		rangelock_destroy(&shmfd->shm_rl);
		mtx_destroy(&shmfd->shm_mtx);
		vm_object_deallocate(shmfd->shm_object);
		free(shmfd, M_SHMFD);
	}
}

/*
 * Determine if the credentials have sufficient permissions for a
 * specified combination of FREAD and FWRITE.
 */
int
shm_access(struct shmfd *shmfd, struct ucred *ucred, int flags)
{
	accmode_t accmode;
	int error;

	accmode = 0;
	if (flags & FREAD)
		accmode |= VREAD;
	if (flags & FWRITE)
		accmode |= VWRITE;
	mtx_lock(&shm_timestamp_lock);
	error = vaccess(VREG, shmfd->shm_mode, shmfd->shm_uid, shmfd->shm_gid,
	    accmode, ucred, NULL);
	mtx_unlock(&shm_timestamp_lock);
	return (error);
}

/*
 * Dictionary management.  We maintain an in-kernel dictionary to map
 * paths to shmfd objects.  We use the FNV hash on the path to store
 * the mappings in a hash table.
 */
static void
shm_init(void *arg)
{

	mtx_init(&shm_timestamp_lock, "shm timestamps", NULL, MTX_DEF);
	sx_init(&shm_dict_lock, "shm dictionary");
	shm_dictionary = hashinit(1024, M_SHMFD, &shm_hash);
	new_unrhdr64(&shm_ino_unr, 1);
	shm_dev_ino = devfs_alloc_cdp_inode();
	KASSERT(shm_dev_ino > 0, ("shm dev inode not initialized"));
}
SYSINIT(shm_init, SI_SUB_SYSV_SHM, SI_ORDER_ANY, shm_init, NULL);

static struct shmfd *
shm_lookup(char *path, Fnv32_t fnv)
{
	struct shm_mapping *map;

	LIST_FOREACH(map, SHM_HASH(fnv), sm_link) {
		if (map->sm_fnv != fnv)
			continue;
		if (strcmp(map->sm_path, path) == 0)
			return (map->sm_shmfd);
	}

	return (NULL);
}

static void
shm_insert(char *path, Fnv32_t fnv, struct shmfd *shmfd)
{
	struct shm_mapping *map;

	map = malloc(sizeof(struct shm_mapping), M_SHMFD, M_WAITOK);
	map->sm_path = path;
	map->sm_fnv = fnv;
	map->sm_shmfd = shm_hold(shmfd);
	shmfd->shm_path = path;
	LIST_INSERT_HEAD(SHM_HASH(fnv), map, sm_link);
}

static int
shm_remove(char *path, Fnv32_t fnv, struct ucred *ucred)
{
	struct shm_mapping *map;
	int error;

	LIST_FOREACH(map, SHM_HASH(fnv), sm_link) {
		if (map->sm_fnv != fnv)
			continue;
		if (strcmp(map->sm_path, path) == 0) {
#ifdef MAC
			error = mac_posixshm_check_unlink(ucred, map->sm_shmfd);
			if (error)
				return (error);
#endif
			error = shm_access(map->sm_shmfd, ucred,
			    FREAD | FWRITE);
			if (error)
				return (error);
			map->sm_shmfd->shm_path = NULL;
			LIST_REMOVE(map, sm_link);
			shm_drop(map->sm_shmfd);
			free(map->sm_path, M_SHMFD);
			free(map, M_SHMFD);
			return (0);
		}
	}

	return (ENOENT);
}

int
kern_shm_open(struct thread *td, const char *userpath, int flags, mode_t mode,
    struct filecaps *fcaps)
{
	struct filedesc *fdp;
	struct shmfd *shmfd;
	struct file *fp;
	char *path;
	const char *pr_path;
	size_t pr_pathlen;
	Fnv32_t fnv;
	mode_t cmode;
	int fd, error;

#ifdef CAPABILITY_MODE
	/*
	 * shm_open(2) is only allowed for anonymous objects.
	 */
	if (IN_CAPABILITY_MODE(td) && (userpath != SHM_ANON))
		return (ECAPMODE);
#endif

	AUDIT_ARG_FFLAGS(flags);
	AUDIT_ARG_MODE(mode);

	if ((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_RDWR)
		return (EINVAL);

	if ((flags & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC)) != 0)
		return (EINVAL);

	fdp = td->td_proc->p_fd;
	cmode = (mode & ~fdp->fd_cmask) & ACCESSPERMS;

	error = falloc_caps(td, &fp, &fd, O_CLOEXEC, fcaps);
	if (error)
		return (error);

	/* A SHM_ANON path pointer creates an anonymous object. */
	if (userpath == SHM_ANON) {
		/* A read-only anonymous object is pointless. */
		if ((flags & O_ACCMODE) == O_RDONLY) {
			fdclose(td, fp, fd);
			fdrop(fp, td);
			return (EINVAL);
		}
		shmfd = shm_alloc(td->td_ucred, cmode);
	} else {
		path = malloc(MAXPATHLEN, M_SHMFD, M_WAITOK);
		pr_path = td->td_ucred->cr_prison->pr_path;

		/* Construct a full pathname for jailed callers. */
		pr_pathlen = strcmp(pr_path, "/") == 0 ? 0
		    : strlcpy(path, pr_path, MAXPATHLEN);
		error = copyinstr(userpath, path + pr_pathlen,
		    MAXPATHLEN - pr_pathlen, NULL);
#ifdef KTRACE
		if (error == 0 && KTRPOINT(curthread, KTR_NAMEI))
			ktrnamei(path);
#endif
		/* Require paths to start with a '/' character. */
		if (error == 0 && path[pr_pathlen] != '/')
			error = EINVAL;
		if (error) {
			fdclose(td, fp, fd);
			fdrop(fp, td);
			free(path, M_SHMFD);
			return (error);
		}

		AUDIT_ARG_UPATH1_CANON(path);
		fnv = fnv_32_str(path, FNV1_32_INIT);
		sx_xlock(&shm_dict_lock);
		shmfd = shm_lookup(path, fnv);
		if (shmfd == NULL) {
			/* Object does not yet exist, create it if requested. */
			if (flags & O_CREAT) {
#ifdef MAC
				error = mac_posixshm_check_create(td->td_ucred,
				    path);
				if (error == 0) {
#endif
					shmfd = shm_alloc(td->td_ucred, cmode);
					shm_insert(path, fnv, shmfd);
#ifdef MAC
				}
#endif
			} else {
				free(path, M_SHMFD);
				error = ENOENT;
			}
		} else {
			/*
			 * Object already exists, obtain a new
			 * reference if requested and permitted.
			 */
			free(path, M_SHMFD);
			if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
				error = EEXIST;
			else {
#ifdef MAC
				error = mac_posixshm_check_open(td->td_ucred,
				    shmfd, FFLAGS(flags & O_ACCMODE));
				if (error == 0)
#endif
				error = shm_access(shmfd, td->td_ucred,
				    FFLAGS(flags & O_ACCMODE));
			}

			/*
			 * Truncate the file back to zero length if
			 * O_TRUNC was specified and the object was
			 * opened with read/write.
			 */
			if (error == 0 &&
			    (flags & (O_ACCMODE | O_TRUNC)) ==
			    (O_RDWR | O_TRUNC)) {
#ifdef MAC
				error = mac_posixshm_check_truncate(
					td->td_ucred, fp->f_cred, shmfd);
				if (error == 0)
#endif
					shm_dotruncate(shmfd, 0);
			}
			if (error == 0)
				shm_hold(shmfd);
		}
		sx_xunlock(&shm_dict_lock);

		if (error) {
			fdclose(td, fp, fd);
			fdrop(fp, td);
			return (error);
		}
	}

	finit(fp, FFLAGS(flags & O_ACCMODE), DTYPE_SHM, shmfd, &shm_ops);

	td->td_retval[0] = fd;
	fdrop(fp, td);

	return (0);
}

/* System calls. */
int
sys_shm_open(struct thread *td, struct shm_open_args *uap)
{

	return (kern_shm_open(td, uap->path, uap->flags, uap->mode, NULL));
}

int
sys_shm_unlink(struct thread *td, struct shm_unlink_args *uap)
{
	char *path;
	const char *pr_path;
	size_t pr_pathlen;
	Fnv32_t fnv;
	int error;

	path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	pr_path = td->td_ucred->cr_prison->pr_path;
	pr_pathlen = strcmp(pr_path, "/") == 0 ? 0
	    : strlcpy(path, pr_path, MAXPATHLEN);
	error = copyinstr(uap->path, path + pr_pathlen, MAXPATHLEN - pr_pathlen,
	    NULL);
	if (error) {
		free(path, M_TEMP);
		return (error);
	}
#ifdef KTRACE
	if (KTRPOINT(curthread, KTR_NAMEI))
		ktrnamei(path);
#endif
	AUDIT_ARG_UPATH1_CANON(path);
	fnv = fnv_32_str(path, FNV1_32_INIT);
	sx_xlock(&shm_dict_lock);
	error = shm_remove(path, fnv, td->td_ucred);
	sx_xunlock(&shm_dict_lock);
	free(path, M_TEMP);

	return (error);
}

int
shm_mmap(struct file *fp, vm_map_t map, vm_offset_t *addr, vm_size_t objsize,
    vm_prot_t prot, vm_prot_t cap_maxprot, int flags,
    vm_ooffset_t foff, struct thread *td)
{
	struct shmfd *shmfd;
	vm_prot_t maxprot;
	int error;

	shmfd = fp->f_data;
	maxprot = VM_PROT_NONE;

	/* FREAD should always be set. */
	if ((fp->f_flag & FREAD) != 0)
		maxprot |= VM_PROT_EXECUTE | VM_PROT_READ;
	if ((fp->f_flag & FWRITE) != 0)
		maxprot |= VM_PROT_WRITE;

	/* Don't permit shared writable mappings on read-only descriptors. */
	if ((flags & MAP_SHARED) != 0 &&
	    (maxprot & VM_PROT_WRITE) == 0 &&
	    (prot & VM_PROT_WRITE) != 0)
		return (EACCES);
	maxprot &= cap_maxprot;

	/* See comment in vn_mmap(). */
	if (
#ifdef _LP64
	    objsize > OFF_MAX ||
#endif
	    foff < 0 || foff > OFF_MAX - objsize)
		return (EINVAL);

#ifdef MAC
	error = mac_posixshm_check_mmap(td->td_ucred, shmfd, prot, flags);
	if (error != 0)
		return (error);
#endif
	
	mtx_lock(&shm_timestamp_lock);
	vfs_timestamp(&shmfd->shm_atime);
	mtx_unlock(&shm_timestamp_lock);
	vm_object_reference(shmfd->shm_object);

	error = vm_mmap_object(map, addr, objsize, prot, maxprot, flags,
	    shmfd->shm_object, foff, FALSE, td);
	if (error != 0)
		vm_object_deallocate(shmfd->shm_object);
	return (error);
}

static int
shm_chmod(struct file *fp, mode_t mode, struct ucred *active_cred,
    struct thread *td)
{
	struct shmfd *shmfd;
	int error;

	error = 0;
	shmfd = fp->f_data;
	mtx_lock(&shm_timestamp_lock);
	/*
	 * SUSv4 says that x bits of permission need not be affected.
	 * Be consistent with our shm_open there.
	 */
#ifdef MAC
	error = mac_posixshm_check_setmode(active_cred, shmfd, mode);
	if (error != 0)
		goto out;
#endif
	error = vaccess(VREG, shmfd->shm_mode, shmfd->shm_uid,
	    shmfd->shm_gid, VADMIN, active_cred, NULL);
	if (error != 0)
		goto out;
	shmfd->shm_mode = mode & ACCESSPERMS;
out:
	mtx_unlock(&shm_timestamp_lock);
	return (error);
}

static int
shm_chown(struct file *fp, uid_t uid, gid_t gid, struct ucred *active_cred,
    struct thread *td)
{
	struct shmfd *shmfd;
	int error;

	error = 0;
	shmfd = fp->f_data;
	mtx_lock(&shm_timestamp_lock);
#ifdef MAC
	error = mac_posixshm_check_setowner(active_cred, shmfd, uid, gid);
	if (error != 0)
		goto out;
#endif
	if (uid == (uid_t)-1)
		uid = shmfd->shm_uid;
	if (gid == (gid_t)-1)
                 gid = shmfd->shm_gid;
	if (((uid != shmfd->shm_uid && uid != active_cred->cr_uid) ||
	    (gid != shmfd->shm_gid && !groupmember(gid, active_cred))) &&
	    (error = priv_check_cred(active_cred, PRIV_VFS_CHOWN)))
		goto out;
	shmfd->shm_uid = uid;
	shmfd->shm_gid = gid;
out:
	mtx_unlock(&shm_timestamp_lock);
	return (error);
}

/*
 * Helper routines to allow the backing object of a shared memory file
 * descriptor to be mapped in the kernel.
 */
int
shm_map(struct file *fp, size_t size, off_t offset, void **memp)
{
	struct shmfd *shmfd;
	vm_offset_t kva, ofs;
	vm_object_t obj;
	int rv;

	if (fp->f_type != DTYPE_SHM)
		return (EINVAL);
	shmfd = fp->f_data;
	obj = shmfd->shm_object;
	VM_OBJECT_WLOCK(obj);
	/*
	 * XXXRW: This validation is probably insufficient, and subject to
	 * sign errors.  It should be fixed.
	 */
	if (offset >= shmfd->shm_size ||
	    offset + size > round_page(shmfd->shm_size)) {
		VM_OBJECT_WUNLOCK(obj);
		return (EINVAL);
	}

	shmfd->shm_kmappings++;
	vm_object_reference_locked(obj);
	VM_OBJECT_WUNLOCK(obj);

	/* Map the object into the kernel_map and wire it. */
	kva = vm_map_min(kernel_map);
	ofs = offset & PAGE_MASK;
	offset = trunc_page(offset);
	size = round_page(size + ofs);
	rv = vm_map_find(kernel_map, obj, offset, &kva, size, 0,
	    VMFS_OPTIMAL_SPACE, VM_PROT_READ | VM_PROT_WRITE,
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	if (rv == KERN_SUCCESS) {
		rv = vm_map_wire(kernel_map, kva, kva + size,
		    VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);
		if (rv == KERN_SUCCESS) {
			*memp = (void *)(kva + ofs);
			return (0);
		}
		vm_map_remove(kernel_map, kva, kva + size);
	} else
		vm_object_deallocate(obj);

	/* On failure, drop our mapping reference. */
	VM_OBJECT_WLOCK(obj);
	shmfd->shm_kmappings--;
	VM_OBJECT_WUNLOCK(obj);

	return (vm_mmap_to_errno(rv));
}

/*
 * We require the caller to unmap the entire entry.  This allows us to
 * safely decrement shm_kmappings when a mapping is removed.
 */
int
shm_unmap(struct file *fp, void *mem, size_t size)
{
	struct shmfd *shmfd;
	vm_map_entry_t entry;
	vm_offset_t kva, ofs;
	vm_object_t obj;
	vm_pindex_t pindex;
	vm_prot_t prot;
	boolean_t wired;
	vm_map_t map;
	int rv;

	if (fp->f_type != DTYPE_SHM)
		return (EINVAL);
	shmfd = fp->f_data;
	kva = (vm_offset_t)mem;
	ofs = kva & PAGE_MASK;
	kva = trunc_page(kva);
	size = round_page(size + ofs);
	map = kernel_map;
	rv = vm_map_lookup(&map, kva, VM_PROT_READ | VM_PROT_WRITE, &entry,
	    &obj, &pindex, &prot, &wired);
	if (rv != KERN_SUCCESS)
		return (EINVAL);
	if (entry->start != kva || entry->end != kva + size) {
		vm_map_lookup_done(map, entry);
		return (EINVAL);
	}
	vm_map_lookup_done(map, entry);
	if (obj != shmfd->shm_object)
		return (EINVAL);
	vm_map_remove(map, kva, kva + size);
	VM_OBJECT_WLOCK(obj);
	KASSERT(shmfd->shm_kmappings > 0, ("shm_unmap: object not mapped"));
	shmfd->shm_kmappings--;
	VM_OBJECT_WUNLOCK(obj);
	return (0);
}

static int
shm_fill_kinfo(struct file *fp, struct kinfo_file *kif, struct filedesc *fdp)
{
	const char *path, *pr_path;
	struct shmfd *shmfd;
	size_t pr_pathlen;

	kif->kf_type = KF_TYPE_SHM;
	shmfd = fp->f_data;

	mtx_lock(&shm_timestamp_lock);
	kif->kf_un.kf_file.kf_file_mode = S_IFREG | shmfd->shm_mode;	/* XXX */
	mtx_unlock(&shm_timestamp_lock);
	kif->kf_un.kf_file.kf_file_size = shmfd->shm_size;
	if (shmfd->shm_path != NULL) {
		sx_slock(&shm_dict_lock);
		if (shmfd->shm_path != NULL) {
			path = shmfd->shm_path;
			pr_path = curthread->td_ucred->cr_prison->pr_path;
			if (strcmp(pr_path, "/") != 0) {
				/* Return the jail-rooted pathname. */
				pr_pathlen = strlen(pr_path);
				if (strncmp(path, pr_path, pr_pathlen) == 0 &&
				    path[pr_pathlen] == '/')
					path += pr_pathlen;
			}
			strlcpy(kif->kf_path, path, sizeof(kif->kf_path));
		}
		sx_sunlock(&shm_dict_lock);
	}
	return (0);
}
