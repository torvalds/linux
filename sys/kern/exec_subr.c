/*	$OpenBSD: exec_subr.c,v 1.70 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: exec_subr.c,v 1.9 1994/12/04 03:10:42 mycroft Exp $	*/

/*
 * Copyright (c) 1993, 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/exec.h>
#include <sys/mman.h>
#include <sys/resourcevar.h>

#include <uvm/uvm_extern.h>

/*
 * new_vmcmd():
 *	create a new vmcmd structure and fill in its fields based
 *	on function call arguments.  make sure objects ref'd by
 *	the vmcmd are 'held'.
 */

void
new_vmcmd(struct exec_vmcmd_set *evsp,
    int (*proc)(struct proc *, struct exec_vmcmd *), u_long len, u_long addr,
    struct vnode *vp, u_long offset, u_int prot, int flags)
{
	struct exec_vmcmd    *vcp;

	if (evsp->evs_used >= evsp->evs_cnt)
		vmcmdset_extend(evsp);
	vcp = &evsp->evs_cmds[evsp->evs_used++];
	vcp->ev_proc = proc;
	vcp->ev_len = len;
	vcp->ev_addr = addr;
	if ((vcp->ev_vp = vp) != NULL)
		vref(vp);
	vcp->ev_offset = offset;
	vcp->ev_prot = prot;
	vcp->ev_flags = flags;
}

void
vmcmdset_extend(struct exec_vmcmd_set *evsp)
{
	struct exec_vmcmd *nvcp;
	u_int ocnt;

#ifdef DIAGNOSTIC
	if (evsp->evs_used < evsp->evs_cnt)
		panic("vmcmdset_extend: not necessary");
#endif

	ocnt = evsp->evs_cnt;
	KASSERT(ocnt > 0);
	/* figure out number of entries in new set */
	evsp->evs_cnt += ocnt;

	/* reallocate the command set */
	nvcp = mallocarray(evsp->evs_cnt, sizeof(*nvcp), M_EXEC,
	    M_WAITOK);
	memcpy(nvcp, evsp->evs_cmds, ocnt * sizeof(*nvcp));
	if (evsp->evs_cmds != evsp->evs_start)
		free(evsp->evs_cmds, M_EXEC, ocnt * sizeof(*nvcp));
	evsp->evs_cmds = nvcp;
}

void
kill_vmcmds(struct exec_vmcmd_set *evsp)
{
	struct exec_vmcmd *vcp;
	int i;

	for (i = 0; i < evsp->evs_used; i++) {
		vcp = &evsp->evs_cmds[i];
		if (vcp->ev_vp != NULL)
			vrele(vcp->ev_vp);
	}

	/*
	 * Free old vmcmds and reset the array.
	 */
	evsp->evs_used = 0;
	if (evsp->evs_cmds != evsp->evs_start)
		free(evsp->evs_cmds, M_EXEC,
		    evsp->evs_cnt * sizeof(struct exec_vmcmd));
	evsp->evs_cmds = evsp->evs_start;
	evsp->evs_cnt = EXEC_DEFAULT_VMCMD_SETSIZE;
}

int
exec_process_vmcmds(struct proc *p, struct exec_package *epp)
{
	struct exec_vmcmd *base_vc = NULL;
	int error = 0;
	int i;

	for (i = 0; i < epp->ep_vmcmds.evs_used && !error; i++) {
		struct exec_vmcmd *vcp;

		vcp = &epp->ep_vmcmds.evs_cmds[i];

		if (vcp->ev_flags & VMCMD_RELATIVE) {
#ifdef DIAGNOSTIC
			if (base_vc == NULL)
				panic("exec_process_vmcmds: RELATIVE no base");
#endif
			vcp->ev_addr += base_vc->ev_addr;
		}
		error = (*vcp->ev_proc)(p, vcp);
		if (vcp->ev_flags & VMCMD_BASE) {
			base_vc = vcp;
		}
	}

	kill_vmcmds(&epp->ep_vmcmds);

	return (error);
}

/*
 * vmcmd_map_pagedvn():
 *	handle vmcmd which specifies that a vnode should be mmap'd.
 *	appropriate for handling demand-paged text and data segments.
 */

int
vmcmd_map_pagedvn(struct proc *p, struct exec_vmcmd *cmd)
{
	/*
	 * note that if you're going to map part of a process as being
	 * paged from a vnode, that vnode had damn well better be marked as
	 * VTEXT.  that's handled in the routine which sets up the vmcmd to
	 * call this routine.
	 */
	struct uvm_object *uobj;
	unsigned int flags = UVM_FLAG_COPYONW | UVM_FLAG_FIXED;
	int error;

	/*
	 * map the vnode in using uvm_map.
	 */

	if (cmd->ev_len == 0)
		return (0);
	if (cmd->ev_offset & PAGE_MASK)
		return (EINVAL);
	if (cmd->ev_addr & PAGE_MASK)
		return (EINVAL);
	if (cmd->ev_len & PAGE_MASK)
		return (EINVAL);

	/*
	 * first, attach to the object
	 */

	uobj = uvn_attach(cmd->ev_vp, PROT_READ | PROT_EXEC);
	if (uobj == NULL)
		return (ENOMEM);

	/*
	 * do the map
	 */
	error = uvm_map(&p->p_vmspace->vm_map, &cmd->ev_addr, cmd->ev_len,
	    uobj, cmd->ev_offset, 0,
	    UVM_MAPFLAG(cmd->ev_prot, PROT_MASK, MAP_INHERIT_COPY,
	    MADV_NORMAL, flags));

	/*
	 * check for error
	 */

	if (error) {
		/*
		 * error: detach from object
		 */
		uobj->pgops->pgo_detach(uobj);
	} else {
		if (cmd->ev_flags & VMCMD_IMMUTABLE)
			uvm_map_immutable(&p->p_vmspace->vm_map, cmd->ev_addr,
			    round_page(cmd->ev_addr + cmd->ev_len), 1);
#ifdef PMAP_CHECK_COPYIN
		if (PMAP_CHECK_COPYIN &&
		    ((cmd->ev_flags & VMCMD_IMMUTABLE) && (cmd->ev_prot & PROT_EXEC)))
			uvm_map_check_copyin_add(&p->p_vmspace->vm_map,
			    cmd->ev_addr, round_page(cmd->ev_addr + cmd->ev_len));
#endif
	}

	return (error);
}

/*
 * vmcmd_map_readvn():
 *	handle vmcmd which specifies that a vnode should be read from.
 *	appropriate for non-demand-paged text/data segments, i.e. impure
 *	objects (a la OMAGIC and NMAGIC).
 */

int
vmcmd_map_readvn(struct proc *p, struct exec_vmcmd *cmd)
{
	int error;
	vm_prot_t prot;

	if (cmd->ev_len == 0)
		return (0);

	prot = cmd->ev_prot;

	KASSERT((cmd->ev_addr & PAGE_MASK) == 0);
	error = uvm_map(&p->p_vmspace->vm_map, &cmd->ev_addr,
	    round_page(cmd->ev_len), NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(prot | PROT_WRITE, PROT_MASK, MAP_INHERIT_COPY,
	    MADV_NORMAL, UVM_FLAG_FIXED|UVM_FLAG_OVERLAY|UVM_FLAG_COPYONW));

	if (error)
		return (error);

	error = vn_rdwr(UIO_READ, cmd->ev_vp, (caddr_t)cmd->ev_addr,
	    cmd->ev_len, cmd->ev_offset, UIO_USERSPACE, IO_UNIT,
	    p->p_ucred, NULL, p);
	if (error)
		return (error);

	if ((prot & PROT_WRITE) == 0) {
		/*
		 * we had to map in the area at PROT_WRITE so that vn_rdwr()
		 * could write to it.   however, the caller seems to want
		 * it mapped read-only, so now we are going to have to call
		 * uvm_map_protect() to fix up the protection.  ICK.
		 */
		error = (uvm_map_protect(&p->p_vmspace->vm_map,
		    cmd->ev_addr, round_page(cmd->ev_addr + cmd->ev_len),
		    prot, 0, FALSE, TRUE));
	}
	if (error == 0) {
		if (cmd->ev_flags & VMCMD_IMMUTABLE)
			uvm_map_immutable(&p->p_vmspace->vm_map, cmd->ev_addr,
			    round_page(cmd->ev_addr + cmd->ev_len), 1);
	}
	return (error);
}

/*
 * vmcmd_map_zero():
 *	handle vmcmd which specifies a zero-filled address space region.
 */

int
vmcmd_map_zero(struct proc *p, struct exec_vmcmd *cmd)
{
	int error;

	if (cmd->ev_len == 0)
		return (0);

	KASSERT((cmd->ev_addr & PAGE_MASK) == 0);
	error = uvm_map(&p->p_vmspace->vm_map, &cmd->ev_addr,
	    round_page(cmd->ev_len), NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(cmd->ev_prot, PROT_MASK, MAP_INHERIT_COPY,
	    MADV_NORMAL, UVM_FLAG_FIXED|UVM_FLAG_COPYONW |
	    (cmd->ev_flags & VMCMD_STACK ? UVM_FLAG_STACK : 0)));
	if (cmd->ev_flags & VMCMD_IMMUTABLE)
		uvm_map_immutable(&p->p_vmspace->vm_map, cmd->ev_addr,
		    round_page(cmd->ev_addr + cmd->ev_len), 1);
	return error;
}

/*
 * vmcmd_mutable():
 *	handle vmcmd which changes an address space region.back to mutable
 */

int
vmcmd_mutable(struct proc *p, struct exec_vmcmd *cmd)
{
	if (cmd->ev_len == 0)
		return (0);

	/* ev_addr, ev_len may be misaligned, so maximize the region */
	uvm_map_immutable(&p->p_vmspace->vm_map, trunc_page(cmd->ev_addr),
	    round_page(cmd->ev_addr + cmd->ev_len), 0);
	return 0;
}

/*
 * vmcmd_randomize():
 *	handle vmcmd which specifies a randomized address space region.
 */
#define RANDOMIZE_CTX_THRESHOLD 512
int
vmcmd_randomize(struct proc *p, struct exec_vmcmd *cmd)
{
	int error;
	struct arc4random_ctx *ctx;
	char *buf;
	size_t sublen, off = 0;
	size_t len = cmd->ev_len;

	if (len == 0)
		return (0);
	if (len > ELF_RANDOMIZE_LIMIT)
		return (EINVAL);

	buf = malloc(PAGE_SIZE, M_TEMP, M_WAITOK);
	if (len < RANDOMIZE_CTX_THRESHOLD) {
		arc4random_buf(buf, len);
		error = copyout(buf, (void *)cmd->ev_addr, len);
		explicit_bzero(buf, len);
	} else {
		ctx = arc4random_ctx_new();
		do {
			sublen = MIN(len, PAGE_SIZE);
			arc4random_ctx_buf(ctx, buf, sublen);
			error = copyout(buf, (void *)cmd->ev_addr + off, sublen);
			if (error)
				break;
			off += sublen;
			len -= sublen;
			sched_pause(yield);
		} while (len);
		arc4random_ctx_free(ctx);
		explicit_bzero(buf, PAGE_SIZE);
	}
	free(buf, M_TEMP, PAGE_SIZE);
	return (error);
}

#ifndef MAXSSIZ_GUARD
#define MAXSSIZ_GUARD	(1024 * 1024)
#endif

/*
 * exec_setup_stack(): Set up the stack segment for an executable.
 *
 * Note that the ep_ssize parameter must be set to be the current stack
 * limit; this is adjusted in the body of execve() to yield the
 * appropriate stack segment usage once the argument length is
 * calculated.
 *
 * This function returns an int for uniformity with other (future) formats'
 * stack setup functions.  They might have errors to return.
 */

int
exec_setup_stack(struct proc *p, struct exec_package *epp)
{
	vsize_t dist = 0;

#ifdef MACHINE_STACK_GROWS_UP
	epp->ep_maxsaddr = USRSTACK;
	epp->ep_minsaddr = USRSTACK + MAXSSIZ;
#else
	epp->ep_maxsaddr = USRSTACK - MAXSSIZ - MAXSSIZ_GUARD;
	epp->ep_minsaddr = USRSTACK;
#endif
	epp->ep_ssize = round_page(lim_cur(RLIMIT_STACK));

#ifdef VM_MIN_STACK_ADDRESS
	dist = USRSTACK - MAXSSIZ - MAXSSIZ_GUARD - VM_MIN_STACK_ADDRESS;
	if (dist >> PAGE_SHIFT > 0xffffffff)
		dist = (vsize_t)arc4random() << PAGE_SHIFT;
	else
		dist = (vsize_t)arc4random_uniform(dist >> PAGE_SHIFT) << PAGE_SHIFT;
#else
	if (stackgap_random != 0) {
		dist = arc4random() & (stackgap_random - 1);
		dist = trunc_page(dist);
	}
#endif

#ifdef MACHINE_STACK_GROWS_UP
	epp->ep_maxsaddr += dist;
	epp->ep_minsaddr += dist;
#else
	epp->ep_maxsaddr -= dist;
	epp->ep_minsaddr -= dist;
#endif

	/*
	 * set up commands for stack.  note that this takes *two*, one to
	 * map the part of the stack which we can access, and one to map
	 * the part which we can't.
	 *
	 * arguably, it could be made into one, but that would require the
	 * addition of another mapping proc, which is unnecessary
	 *
	 * note that in memory, things assumed to be: 0 ....... ep_maxsaddr
	 * <stack> ep_minsaddr
	 */
#ifdef MACHINE_STACK_GROWS_UP
	NEW_VMCMD2(&epp->ep_vmcmds, vmcmd_map_zero,
	    ((epp->ep_minsaddr - epp->ep_ssize) - epp->ep_maxsaddr),
	    epp->ep_maxsaddr + epp->ep_ssize,
	    NULL, 0, PROT_NONE,  VMCMD_IMMUTABLE);
	NEW_VMCMD2(&epp->ep_vmcmds, vmcmd_map_zero, epp->ep_ssize,
	    epp->ep_maxsaddr,
	    NULL, 0, PROT_READ | PROT_WRITE, VMCMD_STACK | VMCMD_IMMUTABLE);
#else
	NEW_VMCMD2(&epp->ep_vmcmds, vmcmd_map_zero,
	    ((epp->ep_minsaddr - epp->ep_ssize) - epp->ep_maxsaddr),
	    epp->ep_maxsaddr,
	    NULL, 0, PROT_NONE, VMCMD_IMMUTABLE);
	NEW_VMCMD2(&epp->ep_vmcmds, vmcmd_map_zero, epp->ep_ssize,
	    (epp->ep_minsaddr - epp->ep_ssize),
	    NULL, 0, PROT_READ | PROT_WRITE, VMCMD_STACK | VMCMD_IMMUTABLE);
#endif

	return (0);
}
