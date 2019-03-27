/*-
 * Copyright (c) 1999-2002, 2008-2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mac.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

static int	mac_mmap_revocation = 1;
SYSCTL_INT(_security_mac, OID_AUTO, mmap_revocation, CTLFLAG_RW,
    &mac_mmap_revocation, 0, "Revoke mmap access to files on subject "
    "relabel");

static int	mac_mmap_revocation_via_cow = 0;
SYSCTL_INT(_security_mac, OID_AUTO, mmap_revocation_via_cow, CTLFLAG_RW,
    &mac_mmap_revocation_via_cow, 0, "Revoke mmap access to files via "
    "copy-on-write semantics, or by removing all write access");

static void	mac_proc_vm_revoke_recurse(struct thread *td,
		    struct ucred *cred, struct vm_map *map);

static struct label *
mac_proc_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(proc_init_label, label);
	return (label);
}

void
mac_proc_init(struct proc *p)
{

	if (mac_labeled & MPC_OBJECT_PROC)
		p->p_label = mac_proc_label_alloc();
	else
		p->p_label = NULL;
}

static void
mac_proc_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(proc_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_proc_destroy(struct proc *p)
{

	if (p->p_label != NULL) {
		mac_proc_label_free(p->p_label);
		p->p_label = NULL;
	}
}

void
mac_thread_userret(struct thread *td)
{

	MAC_POLICY_PERFORM(thread_userret, td);
}

int
mac_execve_enter(struct image_params *imgp, struct mac *mac_p)
{
	struct label *label;
	struct mac mac;
	char *buffer;
	int error;

	if (mac_p == NULL)
		return (0);

	if (!(mac_labeled & MPC_OBJECT_CRED))
		return (EINVAL);

	error = copyin(mac_p, &mac, sizeof(mac));
	if (error)
		return (error);

	error = mac_check_structmac_consistent(&mac);
	if (error)
		return (error);

	buffer = malloc(mac.m_buflen, M_MACTEMP, M_WAITOK);
	error = copyinstr(mac.m_string, buffer, mac.m_buflen, NULL);
	if (error) {
		free(buffer, M_MACTEMP);
		return (error);
	}

	label = mac_cred_label_alloc();
	error = mac_cred_internalize_label(label, buffer);
	free(buffer, M_MACTEMP);
	if (error) {
		mac_cred_label_free(label);
		return (error);
	}
	imgp->execlabel = label;
	return (0);
}

void
mac_execve_exit(struct image_params *imgp)
{
	if (imgp->execlabel != NULL) {
		mac_cred_label_free(imgp->execlabel);
		imgp->execlabel = NULL;
	}
}

void
mac_execve_interpreter_enter(struct vnode *interpvp,
    struct label **interpvplabel)
{

	if (mac_labeled & MPC_OBJECT_VNODE) {
		*interpvplabel = mac_vnode_label_alloc();
		mac_vnode_copy_label(interpvp->v_label, *interpvplabel);
	} else
		*interpvplabel = NULL;
}

void
mac_execve_interpreter_exit(struct label *interpvplabel)
{

	if (interpvplabel != NULL)
		mac_vnode_label_free(interpvplabel);
}

/*
 * When relabeling a process, call out to the policies for the maximum
 * permission allowed for each object type we know about in its memory space,
 * and revoke access (in the least surprising ways we know) when necessary.
 * The process lock is not held here.
 */
void
mac_proc_vm_revoke(struct thread *td)
{
	struct ucred *cred;

	PROC_LOCK(td->td_proc);
	cred = crhold(td->td_proc->p_ucred);
	PROC_UNLOCK(td->td_proc);

	/* XXX freeze all other threads */
	mac_proc_vm_revoke_recurse(td, cred,
	    &td->td_proc->p_vmspace->vm_map);
	/* XXX allow other threads to continue */

	crfree(cred);
}

static __inline const char *
prot2str(vm_prot_t prot)
{

	switch (prot & VM_PROT_ALL) {
	case VM_PROT_READ:
		return ("r--");
	case VM_PROT_READ | VM_PROT_WRITE:
		return ("rw-");
	case VM_PROT_READ | VM_PROT_EXECUTE:
		return ("r-x");
	case VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE:
		return ("rwx");
	case VM_PROT_WRITE:
		return ("-w-");
	case VM_PROT_EXECUTE:
		return ("--x");
	case VM_PROT_WRITE | VM_PROT_EXECUTE:
		return ("-wx");
	default:
		return ("---");
	}
}

static void
mac_proc_vm_revoke_recurse(struct thread *td, struct ucred *cred,
    struct vm_map *map)
{
	vm_map_entry_t vme;
	int result;
	vm_prot_t revokeperms;
	vm_object_t backing_object, object;
	vm_ooffset_t offset;
	struct vnode *vp;
	struct mount *mp;

	if (!mac_mmap_revocation)
		return;

	vm_map_lock(map);
	for (vme = map->header.next; vme != &map->header; vme = vme->next) {
		if (vme->eflags & MAP_ENTRY_IS_SUB_MAP) {
			mac_proc_vm_revoke_recurse(td, cred,
			    vme->object.sub_map);
			continue;
		}
		/*
		 * Skip over entries that obviously are not shared.
		 */
		if (vme->eflags & (MAP_ENTRY_COW | MAP_ENTRY_NOSYNC) ||
		    !vme->max_protection)
			continue;
		/*
		 * Drill down to the deepest backing object.
		 */
		offset = vme->offset;
		object = vme->object.vm_object;
		if (object == NULL)
			continue;
		VM_OBJECT_RLOCK(object);
		while ((backing_object = object->backing_object) != NULL) {
			VM_OBJECT_RLOCK(backing_object);
			offset += object->backing_object_offset;
			VM_OBJECT_RUNLOCK(object);
			object = backing_object;
		}
		VM_OBJECT_RUNLOCK(object);
		/*
		 * At the moment, vm_maps and objects aren't considered by
		 * the MAC system, so only things with backing by a normal
		 * object (read: vnodes) are checked.
		 */
		if (object->type != OBJT_VNODE)
			continue;
		vp = (struct vnode *)object->handle;
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		result = vme->max_protection;
		mac_vnode_check_mmap_downgrade(cred, vp, &result);
		VOP_UNLOCK(vp, 0);
		/*
		 * Find out what maximum protection we may be allowing now
		 * but a policy needs to get removed.
		 */
		revokeperms = vme->max_protection & ~result;
		if (!revokeperms)
			continue;
		printf("pid %ld: revoking %s perms from %#lx:%ld "
		    "(max %s/cur %s)\n", (long)td->td_proc->p_pid,
		    prot2str(revokeperms), (u_long)vme->start,
		    (long)(vme->end - vme->start),
		    prot2str(vme->max_protection), prot2str(vme->protection));
		/*
		 * This is the really simple case: if a map has more
		 * max_protection than is allowed, but it's not being
		 * actually used (that is, the current protection is still
		 * allowed), we can just wipe it out and do nothing more.
		 */
		if ((vme->protection & revokeperms) == 0) {
			vme->max_protection -= revokeperms;
		} else {
			if (revokeperms & VM_PROT_WRITE) {
				/*
				 * In the more complicated case, flush out all
				 * pending changes to the object then turn it
				 * copy-on-write.
				 */
				vm_object_reference(object);
				(void) vn_start_write(vp, &mp, V_WAIT);
				vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
				VM_OBJECT_WLOCK(object);
				vm_object_page_clean(object, offset, offset +
				    vme->end - vme->start, OBJPC_SYNC);
				VM_OBJECT_WUNLOCK(object);
				VOP_UNLOCK(vp, 0);
				vn_finished_write(mp);
				vm_object_deallocate(object);
				/*
				 * Why bother if there's no read permissions
				 * anymore?  For the rest, we need to leave
				 * the write permissions on for COW, or
				 * remove them entirely if configured to.
				 */
				if (!mac_mmap_revocation_via_cow) {
					vme->max_protection &= ~VM_PROT_WRITE;
					vme->protection &= ~VM_PROT_WRITE;
				} if ((revokeperms & VM_PROT_READ) == 0)
					vme->eflags |= MAP_ENTRY_COW |
					    MAP_ENTRY_NEEDS_COPY;
			}
			if (revokeperms & VM_PROT_EXECUTE) {
				vme->max_protection &= ~VM_PROT_EXECUTE;
				vme->protection &= ~VM_PROT_EXECUTE;
			}
			if (revokeperms & VM_PROT_READ) {
				vme->max_protection = 0;
				vme->protection = 0;
			}
			pmap_protect(map->pmap, vme->start, vme->end,
			    vme->protection & ~revokeperms);
			vm_map_simplify_entry(map, vme);
		}
	}
	vm_map_unlock(map);
}

MAC_CHECK_PROBE_DEFINE2(proc_check_debug, "struct ucred *", "struct proc *");

int
mac_proc_check_debug(struct ucred *cred, struct proc *p)
{
	int error;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(proc_check_debug, cred, p);
	MAC_CHECK_PROBE2(proc_check_debug, error, cred, p);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(proc_check_sched, "struct ucred *", "struct proc *");

int
mac_proc_check_sched(struct ucred *cred, struct proc *p)
{
	int error;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(proc_check_sched, cred, p);
	MAC_CHECK_PROBE2(proc_check_sched, error, cred, p);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(proc_check_signal, "struct ucred *", "struct proc *",
    "int");

int
mac_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{
	int error;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(proc_check_signal, cred, p, signum);
	MAC_CHECK_PROBE3(proc_check_signal, error, cred, p, signum);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(proc_check_wait, "struct ucred *", "struct proc *");

int
mac_proc_check_wait(struct ucred *cred, struct proc *p)
{
	int error;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(proc_check_wait, cred, p);
	MAC_CHECK_PROBE2(proc_check_wait, error, cred, p);

	return (error);
}
