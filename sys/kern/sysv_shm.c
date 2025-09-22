/*	$OpenBSD: sysv_shm.c,v 1.81 2024/11/05 15:34:30 mpi Exp $	*/
/*	$NetBSD: sysv_shm.c,v 1.50 1998/10/21 22:24:29 tron Exp $	*/

/*
 * Copyright (c) 2002 Todd C. Miller <millert@openbsd.org>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */
/*
 * Copyright (c) 1994 Adam Glass and Charles M. Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Adam Glass and Charles M.
 *	Hannum.
 * 4. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/shm.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/pool.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/stat.h>

#include <sys/mount.h>
#include <sys/syscallargs.h>

#include <uvm/uvm_extern.h>

extern struct shminfo shminfo;
struct shmid_ds **shmsegs;	/* linear mapping of shmid -> shmseg */
struct pool shm_pool;
unsigned short *shmseqs;	/* array of shm sequence numbers */

struct shmid_ds *shm_find_segment_by_shmid(int);

/*
 * Provides the following externally accessible functions:
 *
 * shminit(void);		                 initialization
 * shmexit(struct vmspace *)                     cleanup
 * shmfork(struct vmspace *, struct vmspace *)   fork handling
 * shmsys(arg1, arg2, arg3, arg4);         shm{at,ctl,dt,get}(arg2, arg3, arg4)
 *
 * Structures:
 * shmsegs (an array of 'struct shmid_ds *')
 * per proc 'struct shmmap_head' with an array of 'struct shmmap_state'
 */

#define	SHMSEG_REMOVED  	0x0200		/* can't overlap ACCESSPERMS */

int shm_last_free, shm_nused, shm_committed;

struct shm_handle {
	struct uvm_object *shm_object;
};

struct shmmap_state {
	vaddr_t va;
	int shmid;
};

struct shmmap_head {
	int shmseg;
	struct shmmap_state state[1];
};

int shm_find_segment_by_key(key_t);
void shm_deallocate_segment(struct shmid_ds *);
int shm_delete_mapping(struct vmspace *, struct shmmap_state *);
int shmget_existing(struct proc *, struct sys_shmget_args *,
			 int, int, register_t *);
int shmget_allocate_segment(struct proc *, struct sys_shmget_args *,
				 int, register_t *);

int
shm_find_segment_by_key(key_t key)
{
	struct shmid_ds *shmseg;
	int i;

	for (i = 0; i < shminfo.shmmni; i++) {
		shmseg = shmsegs[i];
		if (shmseg != NULL && shmseg->shm_perm.key == key)
			return (i);
	}
	return (-1);
}

struct shmid_ds *
shm_find_segment_by_shmid(int shmid)
{
	int segnum;
	struct shmid_ds *shmseg;

	segnum = IPCID_TO_IX(shmid);
	if (segnum < 0 || segnum >= shminfo.shmmni ||
	    (shmseg = shmsegs[segnum]) == NULL ||
	    shmseg->shm_perm.seq != IPCID_TO_SEQ(shmid))
		return (NULL);
	return (shmseg);
}

void
shm_deallocate_segment(struct shmid_ds *shmseg)
{
	struct shm_handle *shm_handle;
	size_t size;

	shm_handle = shmseg->shm_internal;
	size = round_page(shmseg->shm_segsz);
	uao_detach(shm_handle->shm_object);
	pool_put(&shm_pool, shmseg);
	shm_committed -= atop(size);
	shm_nused--;
}

int
shm_delete_mapping(struct vmspace *vm, struct shmmap_state *shmmap_s)
{
	struct shmid_ds *shmseg;
	int segnum, deallocate = 0;
	vaddr_t end;

	segnum = IPCID_TO_IX(shmmap_s->shmid);
	if (segnum < 0 || segnum >= shminfo.shmmni ||
	    (shmseg = shmsegs[segnum]) == NULL)
		return (EINVAL);
	if ((--shmseg->shm_nattch <= 0) &&
	    (shmseg->shm_perm.mode & SHMSEG_REMOVED)) {
	    	deallocate = 1;
		shm_last_free = segnum;
		shmsegs[shm_last_free] = NULL;
	}
	end = round_page(shmmap_s->va+shmseg->shm_segsz);
	uvm_unmap(&vm->vm_map, trunc_page(shmmap_s->va), end);
	shmmap_s->shmid = -1;
	shmseg->shm_dtime = gettime();
	if (deallocate)
		shm_deallocate_segment(shmseg);
	return (0);
}

int
sys_shmdt(struct proc *p, void *v, register_t *retval)
{
	struct sys_shmdt_args /* {
		syscallarg(const void *) shmaddr;
	} */ *uap = v;
	struct shmmap_head *shmmap_h;
	struct shmmap_state *shmmap_s;
	int i;

	shmmap_h = (struct shmmap_head *)p->p_vmspace->vm_shm;
	if (shmmap_h == NULL)
		return (EINVAL);

	for (i = 0, shmmap_s = shmmap_h->state; i < shmmap_h->shmseg;
	    i++, shmmap_s++)
		if (shmmap_s->shmid != -1 &&
		    shmmap_s->va == (vaddr_t)SCARG(uap, shmaddr))
			break;
	if (i == shmmap_h->shmseg)
		return (EINVAL);
	return (shm_delete_mapping(p->p_vmspace, shmmap_s));
}

int
sys_shmat(struct proc *p, void *v, register_t *retval)
{
	struct sys_shmat_args /* {
		syscallarg(int) shmid;
		syscallarg(const void *) shmaddr;
		syscallarg(int) shmflg;
	} */ *uap = v;
	int error, i, flags = 0;
	struct ucred *cred = p->p_ucred;
	struct shmid_ds *shmseg;
	struct shmmap_head *shmmap_h;
	struct shmmap_state *shmmap_s;
	struct shm_handle *shm_handle;
	vaddr_t attach_va;
	vm_prot_t prot;
	vsize_t size;

	shmmap_h = (struct shmmap_head *)p->p_vmspace->vm_shm;
	if (shmmap_h == NULL) {
		size = sizeof(int) +
		    shminfo.shmseg * sizeof(struct shmmap_state);
		shmmap_h = malloc(size, M_SHM, M_WAITOK | M_CANFAIL);
		if (shmmap_h == NULL)
			return (ENOMEM);
		shmmap_h->shmseg = shminfo.shmseg;
		for (i = 0, shmmap_s = shmmap_h->state; i < shmmap_h->shmseg;
		    i++, shmmap_s++)
			shmmap_s->shmid = -1;
		p->p_vmspace->vm_shm = (caddr_t)shmmap_h;
	}
	shmseg = shm_find_segment_by_shmid(SCARG(uap, shmid));
	if (shmseg == NULL)
		return (EINVAL);
	error = ipcperm(cred, &shmseg->shm_perm,
		    (SCARG(uap, shmflg) & SHM_RDONLY) ? IPC_R : IPC_R|IPC_W);
	if (error)
		return (error);
	for (i = 0, shmmap_s = shmmap_h->state; i < shmmap_h->shmseg; i++) {
		if (shmmap_s->shmid == -1)
			break;
		shmmap_s++;
	}
	if (i >= shmmap_h->shmseg)
		return (EMFILE);
	size = round_page(shmseg->shm_segsz);
	prot = PROT_READ;
	if ((SCARG(uap, shmflg) & SHM_RDONLY) == 0)
		prot |= PROT_WRITE;
	if (SCARG(uap, shmaddr)) {
		flags |= UVM_FLAG_FIXED;
		if (SCARG(uap, shmflg) & SHM_RND)
			attach_va =
			    (vaddr_t)SCARG(uap, shmaddr) & ~(SHMLBA-1);
		else if (((vaddr_t)SCARG(uap, shmaddr) & (SHMLBA-1)) == 0)
			attach_va = (vaddr_t)SCARG(uap, shmaddr);
		else
			return (EINVAL);
	} else
		attach_va = 0;
	/*
	 * Since uvm_map() could end up sleeping, grab a reference to prevent
	 * the segment from being deallocated while sleeping.
	 */
	shmseg->shm_nattch++;
	shm_handle = shmseg->shm_internal;
	uao_reference(shm_handle->shm_object);
	error = uvm_map(&p->p_vmspace->vm_map, &attach_va, size,
	    shm_handle->shm_object, 0, 0, UVM_MAPFLAG(prot, prot,
	    MAP_INHERIT_SHARE, MADV_RANDOM, flags));
	if (error) {
		if ((--shmseg->shm_nattch <= 0) &&
		    (shmseg->shm_perm.mode & SHMSEG_REMOVED)) {
			shm_deallocate_segment(shmseg);
			shm_last_free = IPCID_TO_IX(SCARG(uap, shmid));
			shmsegs[shm_last_free] = NULL;
		} else {
			uao_detach(shm_handle->shm_object);
		}
		return (error);
	}

	shmmap_s->va = attach_va;
	shmmap_s->shmid = SCARG(uap, shmid);
	shmseg->shm_lpid = p->p_p->ps_pid;
	shmseg->shm_atime = gettime();
	*retval = attach_va;
	return (0);
}

int
sys_shmctl(struct proc *p, void *v, register_t *retval)
{
	struct sys_shmctl_args /* {
		syscallarg(int) shmid;
		syscallarg(int) cmd;
		syscallarg(struct shmid_ds *) buf;
	} */ *uap = v;
	int		shmid = SCARG(uap, shmid);
	int		cmd = SCARG(uap, cmd);
	void		*buf = SCARG(uap, buf);
	struct ucred	*cred = p->p_ucred;
	struct shmid_ds	inbuf, *shmseg;
	int		error;

	if (cmd == IPC_SET) {
		error = copyin(buf, &inbuf, sizeof(inbuf));
		if (error)
			return (error);
	}

	shmseg = shm_find_segment_by_shmid(shmid);
	if (shmseg == NULL)
		return (EINVAL);
	switch (cmd) {
	case IPC_STAT:
		if ((error = ipcperm(cred, &shmseg->shm_perm, IPC_R)) != 0)
			return (error);
		error = copyout(shmseg, buf, sizeof(inbuf));
		if (error)
			return (error);
		break;
	case IPC_SET:
		if ((error = ipcperm(cred, &shmseg->shm_perm, IPC_M)) != 0)
			return (error);
		shmseg->shm_perm.uid = inbuf.shm_perm.uid;
		shmseg->shm_perm.gid = inbuf.shm_perm.gid;
		shmseg->shm_perm.mode =
		    (shmseg->shm_perm.mode & ~ACCESSPERMS) |
		    (inbuf.shm_perm.mode & ACCESSPERMS);
		shmseg->shm_ctime = gettime();
		break;
	case IPC_RMID:
		if ((error = ipcperm(cred, &shmseg->shm_perm, IPC_M)) != 0)
			return (error);
		shmseg->shm_perm.key = IPC_PRIVATE;
		shmseg->shm_perm.mode |= SHMSEG_REMOVED;
		if (shmseg->shm_nattch <= 0) {
			shm_deallocate_segment(shmseg);
			shm_last_free = IPCID_TO_IX(shmid);
			shmsegs[shm_last_free] = NULL;
		}
		break;
	case SHM_LOCK:
	case SHM_UNLOCK:
	default:
		return (EINVAL);
	}
	return (0);
}

int
shmget_existing(struct proc *p,
	struct sys_shmget_args /* {
		syscallarg(key_t) key;
		syscallarg(size_t) size;
		syscallarg(int) shmflg;
	} */ *uap,
	int mode, int segnum, register_t *retval)
{
	struct shmid_ds *shmseg;
	struct ucred *cred = p->p_ucred;
	int error;

	shmseg = shmsegs[segnum];	/* We assume the segnum is valid */
	if ((error = ipcperm(cred, &shmseg->shm_perm, mode)) != 0)
		return (error);
	if (SCARG(uap, size) && SCARG(uap, size) > shmseg->shm_segsz)
		return (EINVAL);
	if ((SCARG(uap, shmflg) & (IPC_CREAT | IPC_EXCL)) ==
	    (IPC_CREAT | IPC_EXCL))
		return (EEXIST);
	*retval = IXSEQ_TO_IPCID(segnum, shmseg->shm_perm);
	return (0);
}

int
shmget_allocate_segment(struct proc *p,
	struct sys_shmget_args /* {
		syscallarg(key_t) key;
		syscallarg(size_t) size;
		syscallarg(int) shmflg;
	} */ *uap,
	int mode, register_t *retval)
{
	size_t size;
	key_t key;
	int segnum;
	struct ucred *cred = p->p_ucred;
	struct shmid_ds *shmseg;
	struct shm_handle *shm_handle;
	int error = 0;

	if (SCARG(uap, size) < shminfo.shmmin ||
	    SCARG(uap, size) > shminfo.shmmax)
		return (EINVAL);
	if (shm_nused >= shminfo.shmmni) /* any shmids left? */
		return (ENOSPC);
	size = round_page(SCARG(uap, size));
	if (shm_committed + atop(size) > shminfo.shmall)
		return (ENOMEM);
	shm_nused++;
	shm_committed += atop(size);

	/*
	 * If a key has been specified and we had to wait for memory
	 * to be freed up we need to verify that no one has allocated
	 * the key we want in the meantime.  Yes, this is ugly.
	 */
	key = SCARG(uap, key);
	shmseg = pool_get(&shm_pool, key == IPC_PRIVATE ? PR_WAITOK :
	    PR_NOWAIT);
	if (shmseg == NULL) {
		shmseg = pool_get(&shm_pool, PR_WAITOK);
		if (shm_find_segment_by_key(key) != -1) {
			pool_put(&shm_pool, shmseg);
			shm_nused--;
			shm_committed -= atop(size);
			return (EAGAIN);
		}
	}

	/* XXX - hash shmids instead */
	if (shm_last_free < 0) {
		for (segnum = 0; segnum < shminfo.shmmni && shmsegs[segnum];
		    segnum++)
			;
		if (segnum == shminfo.shmmni)
			panic("shmseg free count inconsistent");
	} else {
		segnum = shm_last_free;
		if (++shm_last_free >= shminfo.shmmni || shmsegs[shm_last_free])
			shm_last_free = -1;
	}
	shmsegs[segnum] = shmseg;

	shm_handle = (struct shm_handle *)((caddr_t)shmseg + sizeof(*shmseg));
	shm_handle->shm_object = uao_create(size, 0);

	shmseg->shm_perm.cuid = shmseg->shm_perm.uid = cred->cr_uid;
	shmseg->shm_perm.cgid = shmseg->shm_perm.gid = cred->cr_gid;
	shmseg->shm_perm.mode = (mode & ACCESSPERMS);
	shmseg->shm_perm.seq = shmseqs[segnum] = (shmseqs[segnum] + 1) & 0x7fff;
	shmseg->shm_perm.key = key;
	shmseg->shm_segsz = SCARG(uap, size);
	shmseg->shm_cpid = p->p_p->ps_pid;
	shmseg->shm_lpid = shmseg->shm_nattch = 0;
	shmseg->shm_atime = shmseg->shm_dtime = 0;
	shmseg->shm_ctime = gettime();
	shmseg->shm_internal = shm_handle;

	*retval = IXSEQ_TO_IPCID(segnum, shmseg->shm_perm);
	return (error);
}

int
sys_shmget(struct proc *p, void *v, register_t *retval)
{
	struct sys_shmget_args /* {
		syscallarg(key_t) key;
		syscallarg(size_t) size;
		syscallarg(int) shmflg;
	} */ *uap = v;
	int segnum, mode, error;

	mode = SCARG(uap, shmflg) & ACCESSPERMS;

	if (SCARG(uap, key) != IPC_PRIVATE) {
	again:
		segnum = shm_find_segment_by_key(SCARG(uap, key));
		if (segnum >= 0)
			return (shmget_existing(p, uap, mode, segnum, retval));
		if ((SCARG(uap, shmflg) & IPC_CREAT) == 0)
			return (ENOENT);
	}
	error = shmget_allocate_segment(p, uap, mode, retval);
	if (error == EAGAIN)
		goto again;
	return (error);
}

void
shmfork(struct vmspace *vm1, struct vmspace *vm2)
{
	struct shmmap_head *shmmap_h;
	struct shmmap_state *shmmap_s;
	struct shmid_ds *shmseg;
	size_t size;
	int i;

	if (vm1->vm_shm == NULL) {
		vm2->vm_shm = NULL;
		return;
	}

	shmmap_h = (struct shmmap_head *)vm1->vm_shm;
	size = sizeof(int) + shmmap_h->shmseg * sizeof(struct shmmap_state);
	vm2->vm_shm = malloc(size, M_SHM, M_WAITOK);
	memcpy(vm2->vm_shm, vm1->vm_shm, size);
	for (i = 0, shmmap_s = shmmap_h->state; i < shmmap_h->shmseg;
	    i++, shmmap_s++) {
		if (shmmap_s->shmid != -1 &&
		    (shmseg = shmsegs[IPCID_TO_IX(shmmap_s->shmid)]) != NULL)
			shmseg->shm_nattch++;
	}
}

void
shmexit(struct vmspace *vm)
{
	struct shmmap_head *shmmap_h;
	struct shmmap_state *shmmap_s;
	size_t size;
	int i;

	shmmap_h = (struct shmmap_head *)vm->vm_shm;
	if (shmmap_h == NULL)
		return;
	size = sizeof(int) + shmmap_h->shmseg * sizeof(struct shmmap_state);
	for (i = 0, shmmap_s = shmmap_h->state; i < shmmap_h->shmseg;
	    i++, shmmap_s++)
		if (shmmap_s->shmid != -1)
			shm_delete_mapping(vm, shmmap_s);
	free(vm->vm_shm, M_SHM, size);
	vm->vm_shm = NULL;
}

void
shminit(void)
{

	pool_init(&shm_pool,
	    sizeof(struct shmid_ds) + sizeof(struct shm_handle), 0,
	    IPL_NONE, PR_WAITOK, "shmpl", NULL);
	shmsegs = mallocarray(shminfo.shmmni, sizeof(struct shmid_ds *),
	    M_SHM, M_WAITOK|M_ZERO);
	shmseqs = mallocarray(shminfo.shmmni, sizeof(unsigned short),
	    M_SHM, M_WAITOK|M_ZERO);

	shminfo.shmmax *= PAGE_SIZE;	/* actually in pages */
	shm_last_free = 0;
	shm_nused = 0;
	shm_committed = 0;
}

/* Expand shmsegs and shmseqs arrays */
void
shm_reallocate(int val)
{
	struct shmid_ds **newsegs;
	unsigned short *newseqs;

	newsegs = mallocarray(val, sizeof(struct shmid_ds *),
	    M_SHM, M_WAITOK | M_ZERO);
	memcpy(newsegs, shmsegs,
	    shminfo.shmmni * sizeof(struct shmid_ds *));
	free(shmsegs, M_SHM,
	    shminfo.shmmni * sizeof(struct shmid_ds *));
	shmsegs = newsegs;
	newseqs = mallocarray(val, sizeof(unsigned short), M_SHM,
	    M_WAITOK | M_ZERO);
	memcpy(newseqs, shmseqs,
	    shminfo.shmmni * sizeof(unsigned short));
	free(shmseqs, M_SHM, shminfo.shmmni * sizeof(unsigned short));
	shmseqs = newseqs;
	shminfo.shmmni = val;
}

/*
 * Userland access to struct shminfo.
 */
int
sysctl_sysvshm(int *name, u_int namelen, void *oldp, size_t *oldlenp,
	void *newp, size_t newlen)
{
	int error, val;

	if (namelen != 1)
                        return (ENOTDIR);       /* leaf-only */

	switch (name[0]) {
	case KERN_SHMINFO_SHMMAX:
		if ((error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &shminfo.shmmax, 0, INT_MAX)) || newp == NULL)
			return (error);

		/* If new shmmax > shmall, crank shmall */
		if (atop(round_page(shminfo.shmmax)) > shminfo.shmall)
			shminfo.shmall = atop(round_page(shminfo.shmmax));
		return (0);
	case KERN_SHMINFO_SHMMIN:
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &shminfo.shmmin, 1, INT_MAX));
	case KERN_SHMINFO_SHMMNI:
		val = shminfo.shmmni;
		/* can't decrease shmmni */
		error = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &val, val, 0xffff);
		/* returns success and skips reallocation if val is unchanged */
		if (error || val == shminfo.shmmni)
			return (error);
		shm_reallocate(val);
		return (0);
	case KERN_SHMINFO_SHMSEG:
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &shminfo.shmseg, 1, INT_MAX));
	case KERN_SHMINFO_SHMALL:
		/* can't decrease shmall */
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &shminfo.shmall, shminfo.shmall, INT_MAX));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}
