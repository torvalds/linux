/*	$OpenBSD: exec_elf.c,v 1.194 2025/09/20 13:53:36 mpi Exp $	*/

/*
 * Copyright (c) 1996 Per Fogelstrom
 * All rights reserved.
 *
 * Copyright (c) 1994 Christos Zoulas
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
 * 3. The name of the author may not be used to endorse or promote products
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
 *
 */

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/core.h>
#include <sys/exec.h>
#include <sys/exec_elf.h>
#include <sys/fcntl.h>
#include <sys/ptrace.h>
#include <sys/signalvar.h>
#include <sys/pledge.h>
#include <sys/syscall.h>

#include <sys/mman.h>

#include <uvm/uvm_extern.h>

#include <machine/reg.h>
#include <machine/exec.h>
#include <machine/elf.h>

int	elf_load_file(struct proc *, char *, struct exec_package *,
	    struct elf_args *);
int	elf_check_header(Elf_Ehdr *);
int	elf_read_from(struct proc *, struct vnode *, u_long, void *, int);
void	elf_load_psection(struct exec_vmcmd_set *, struct vnode *,
	    Elf_Phdr *, Elf_Addr *, Elf_Addr *, int *, int);
int	elf_os_pt_note_name(Elf_Note *, int *);
int	elf_os_pt_note(struct proc *, struct exec_package *, Elf_Ehdr *, int *);
int	elf_read_pintable(struct proc *p, struct vnode *vp, Elf_Phdr *pp,
	    u_int **pinp, int is_ldso, size_t len);

/* round up and down to page boundaries. */
#define ELF_ROUND(a, b)		(((a) + (b) - 1) & ~((b) - 1))
#define ELF_TRUNC(a, b)		((a) & ~((b) - 1))

/*
 * We limit the number of program headers to 32, this should
 * be a reasonable limit for ELF, the most we have seen so far is 12
 */
#define ELF_MAX_VALID_PHDR 32

#define ELF_NOTE_NAME_OPENBSD	0x01

struct elf_note_name {
	char *name;
	int id;
} elf_note_names[] = {
	{ "OpenBSD",	ELF_NOTE_NAME_OPENBSD },
};

/*
 * Check header for validity; return 0 for ok, ENOEXEC if error
 */
int
elf_check_header(Elf_Ehdr *ehdr)
{
	/*
	 * We need to check magic, class size, endianness, and version before
	 * we look at the rest of the Elf_Ehdr structure. These few elements
	 * are represented in a machine independent fashion.
	 */
	if (!IS_ELF(*ehdr) ||
	    ehdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    ehdr->e_ident[EI_DATA] != ELF_TARG_DATA ||
	    ehdr->e_ident[EI_VERSION] != ELF_TARG_VER)
		return (ENOEXEC);

	/* Now check the machine dependent header */
	if (ehdr->e_machine != ELF_TARG_MACH ||
	    ehdr->e_version != ELF_TARG_VER)
		return (ENOEXEC);

	/* Don't allow an insane amount of sections. */
	if (ehdr->e_phnum > ELF_MAX_VALID_PHDR)
		return (ENOEXEC);

	return (0);
}

/*
 * Load a psection at the appropriate address
 */
void
elf_load_psection(struct exec_vmcmd_set *vcset, struct vnode *vp,
    Elf_Phdr *ph, Elf_Addr *addr, Elf_Addr *size, int *prot, int flags)
{
	u_long msize, lsize, psize, rm, rf;
	long diff, offset, bdiff;
	Elf_Addr base;

	/*
	 * If the user specified an address, then we load there.
	 */
	if (*addr != ELF_NO_ADDR) {
		if (ph->p_align > 1) {
			*addr = ELF_TRUNC(*addr, ph->p_align);
			diff = ph->p_vaddr - ELF_TRUNC(ph->p_vaddr, ph->p_align);
			/* page align vaddr */
			base = *addr + trunc_page(ph->p_vaddr) 
			    - ELF_TRUNC(ph->p_vaddr, ph->p_align);
		} else {
			diff = 0;
			base = *addr + trunc_page(ph->p_vaddr) - ph->p_vaddr;
		}
	} else {
		*addr = ph->p_vaddr;
		if (ph->p_align > 1)
			*addr = ELF_TRUNC(*addr, ph->p_align);
		base = trunc_page(ph->p_vaddr);
		diff = ph->p_vaddr - *addr;
	}
	bdiff = ph->p_vaddr - trunc_page(ph->p_vaddr);

	/*
	 * Enforce W^X and map W|X segments without X permission
	 * initially.  The dynamic linker will make these read-only
	 * and add back X permission after relocation processing.
	 * Static executables with W|X segments will probably crash.
	 */
	*prot |= (ph->p_flags & PF_R) ? PROT_READ : 0;
	*prot |= (ph->p_flags & PF_W) ? PROT_WRITE : 0;
	if ((ph->p_flags & PF_W) == 0)
		*prot |= (ph->p_flags & PF_X) ? PROT_EXEC : 0;

	/*
	 * Apply immutability as much as possible, but not text/rodata
	 * segments of textrel binaries, or RELRO or PT_OPENBSD_MUTABLE
	 * sections, or LOADS marked PF_OPENBSD_MUTABLE, or LOADS which
	 * violate W^X.
	 * Userland (meaning crt0 or ld.so) will repair those regions.
	 */
	if ((ph->p_flags & (PF_X | PF_W)) != (PF_X | PF_W) &&
	    ((ph->p_flags & PF_OPENBSD_MUTABLE) == 0))
		flags |= VMCMD_IMMUTABLE;
	if ((flags & VMCMD_TEXTREL) && (ph->p_flags & PF_W) == 0)
		flags &= ~VMCMD_IMMUTABLE;

	msize = ph->p_memsz + diff;
	offset = ph->p_offset - bdiff;
	lsize = ph->p_filesz + bdiff;
	psize = round_page(lsize);

	/*
	 * Because the pagedvn pager can't handle zero fill of the last
	 * data page if it's not page aligned we map the last page readvn.
	 */
	if (ph->p_flags & PF_W) {
		psize = trunc_page(lsize);
		if (psize > 0)
			NEW_VMCMD2(vcset, vmcmd_map_pagedvn, psize, base, vp,
			    offset, *prot, flags);
		if (psize != lsize) {
			NEW_VMCMD2(vcset, vmcmd_map_readvn, lsize - psize,
			    base + psize, vp, offset + psize, *prot, flags);
		}
	} else {
		NEW_VMCMD2(vcset, vmcmd_map_pagedvn, psize, base, vp, offset,
		    *prot, flags);
	}

	/*
	 * Check if we need to extend the size of the segment
	 */
	rm = round_page(*addr + ph->p_memsz + diff);
	rf = round_page(*addr + ph->p_filesz + diff);

	if (rm != rf) {
		NEW_VMCMD2(vcset, vmcmd_map_zero, rm - rf, rf, NULL, 0,
		    *prot, flags);
	}
	*size = msize;
}

/*
 * Read from vnode into buffer at offset.
 */
int
elf_read_from(struct proc *p, struct vnode *vp, u_long off, void *buf,
    int size)
{
	int error;
	size_t resid;

	if ((error = vn_rdwr(UIO_READ, vp, buf, size, off, UIO_SYSSPACE,
	    0, p->p_ucred, &resid, p)) != 0)
		return error;
	/*
	 * See if we got all of it
	 */
	if (resid != 0)
		return (ENOEXEC);
	return (0);
}

/*
 * rebase the pin offsets inside a base,len window for the text segment only.
 */
void
elf_adjustpins(vaddr_t *basep, size_t *lenp, u_int *pins, int npins, u_int offset)
{
	int i;

	/* Adjust offsets, base, len */
	for (i = 0; i < npins; i++) {
		if (pins[i] == -1 || pins[i] == 0)
			continue;
		pins[i] -= offset;
	}
	*basep += offset;
	*lenp -= offset;
}

int
elf_read_pintable(struct proc *p, struct vnode *vp, Elf_Phdr *pp,
    u_int **pinp, int is_ldso, size_t len)
{
	struct pinsyscalls {
		u_int offset;
		u_int sysno;
	} *syscalls = NULL;
	int i, nsyscalls = 0, npins = 0;
	u_int *pins = NULL;

	if (pp->p_filesz > SYS_MAXSYSCALL * 2 * sizeof(*syscalls) ||
	    pp->p_filesz % sizeof(*syscalls) != 0)
		goto bad;
	nsyscalls = pp->p_filesz / sizeof(*syscalls);
	syscalls = malloc(pp->p_filesz, M_PINSYSCALL, M_WAITOK);
	if (elf_read_from(p, vp, pp->p_offset, syscalls,
	    pp->p_filesz) != 0)
		goto bad;

	/* Validate, and calculate pintable size */
	for (i = 0; i < nsyscalls; i++) {
		if (syscalls[i].sysno <= 0 ||
		    syscalls[i].sysno >= SYS_MAXSYSCALL ||
		    syscalls[i].offset > len) {
			npins = 0;
			goto bad;
		}
		npins = MAX(npins, syscalls[i].sysno);
	}
	if (is_ldso)
		npins = MAX(npins, SYS_kbind);	/* XXX see ld.so/loader.c */
	npins++;

	/* Fill pintable: 0 = invalid, -1 = allowed, else offset from base */
	pins = mallocarray(npins, sizeof(u_int), M_PINSYSCALL, M_WAITOK|M_ZERO);
	for (i = 0; i < nsyscalls; i++) {
		if (pins[syscalls[i].sysno])
			pins[syscalls[i].sysno] = -1;	/* duplicated */
		else
			pins[syscalls[i].sysno] = syscalls[i].offset;
	}
	if (is_ldso)
		pins[SYS_kbind] = -1;	/* XXX see ld.so/loader.c */
	*pinp = pins;
	pins = NULL;
bad:
	free(syscalls, M_PINSYSCALL, nsyscalls * sizeof(*syscalls));
	free(pins, M_PINSYSCALL, npins * sizeof(u_int));
	return npins;
}

/*
 * Load a file (interpreter/library) pointed to by path [stolen from
 * coff_load_shlib()]. Made slightly generic so it might be used externally.
 */
int
elf_load_file(struct proc *p, char *path, struct exec_package *epp,
    struct elf_args *ap)
{
	int error, i;
	struct nameidata nd;
	Elf_Ehdr eh;
	Elf_Phdr *ph = NULL, *syscall_ph = NULL;
	u_long phsize = 0;
	Elf_Addr addr;
	struct vnode *vp;
	Elf_Phdr *base_ph = NULL;
	struct interp_ld_sec {
		Elf_Addr vaddr;
		u_long memsz;
	} loadmap[ELF_MAX_VALID_PHDR];
	int nload, idx = 0;
	Elf_Addr pos;
	int file_align;
	int loop;
	size_t randomizequota = ELF_RANDOMIZE_LIMIT;
	vaddr_t text_start = -1, text_end = 0;

	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE, path, p);
	nd.ni_pledge = PLEDGE_RPATH;
	nd.ni_unveil = UNVEIL_READ;
	if ((error = namei(&nd)) != 0) {
		return (error);
	}
	vp = nd.ni_vp;
	if (vp->v_type != VREG) {
		error = EACCES;
		goto bad;
	}
	if ((error = VOP_GETATTR(vp, epp->ep_vap, p->p_ucred, p)) != 0)
		goto bad;
	if (vp->v_mount->mnt_flag & MNT_NOEXEC) {
		error = EACCES;
		goto bad;
	}
	if ((error = VOP_ACCESS(vp, VREAD, p->p_ucred, p)) != 0)
		goto bad1;
	if ((error = elf_read_from(p, nd.ni_vp, 0, &eh, sizeof(eh))) != 0)
		goto bad1;

	if (elf_check_header(&eh) || eh.e_type != ET_DYN) {
		error = ENOEXEC;
		goto bad1;
	}

	ph = mallocarray(eh.e_phnum, sizeof(Elf_Phdr), M_TEMP, M_WAITOK);
	phsize = eh.e_phnum * sizeof(Elf_Phdr);

	if ((error = elf_read_from(p, nd.ni_vp, eh.e_phoff, ph, phsize)) != 0)
		goto bad1;

	for (i = 0; i < eh.e_phnum; i++) {
		if ((ph[i].p_align > 1) && !powerof2(ph[i].p_align)) {
			error = EINVAL;
			goto bad1;
		}

		if (ph[i].p_type == PT_LOAD) {
			if (ph[i].p_filesz > ph[i].p_memsz ||
			    ph[i].p_memsz == 0) {
				error = EINVAL;
				goto bad1;
			}
			loadmap[idx].vaddr = trunc_page(ph[i].p_vaddr);
			loadmap[idx].memsz = round_page (ph[i].p_vaddr +
			    ph[i].p_memsz - loadmap[idx].vaddr);
			file_align = ph[i].p_align;
			idx++;
		}
	}
	nload = idx;

	/*
	 * Load the interpreter where a non-fixed mmap(NULL, ...)
	 * would (i.e. something safely out of the way).
	 */
	pos = uvm_map_hint(p->p_vmspace, PROT_EXEC, VM_MIN_ADDRESS,
	    VM_MAXUSER_ADDRESS);
	pos = ELF_ROUND(pos, file_align);

	loop = 0;
	for (i = 0; i < nload;/**/) {
		vaddr_t	addr;
		struct	uvm_object *uobj;
		off_t	uoff;
		size_t	size;

#ifdef this_needs_fixing
		if (i == 0) {
			uobj = &vp->v_uvm.u_obj;
			/* need to fix uoff */
		} else {
#endif
			uobj = NULL;
			uoff = 0;
#ifdef this_needs_fixing
		}
#endif

		addr = trunc_page(pos + loadmap[i].vaddr);
		size =  round_page(addr + loadmap[i].memsz) - addr;

		/* CRAP - map_findspace does not avoid daddr+BRKSIZ */
		if ((addr + size > (vaddr_t)p->p_vmspace->vm_daddr) &&
		    (addr < (vaddr_t)p->p_vmspace->vm_daddr + BRKSIZ))
			addr = round_page((vaddr_t)p->p_vmspace->vm_daddr +
			    BRKSIZ);

		if (uvm_map_mquery(&p->p_vmspace->vm_map, &addr, size,
		    (i == 0 ? uoff : UVM_UNKNOWN_OFFSET), 0) != 0) {
			if (loop == 0) {
				loop = 1;
				i = 0;
				pos = 0;
				continue;
			}
			error = ENOMEM;
			goto bad1;
		}
		if (addr != pos + loadmap[i].vaddr) {
			/* base changed. */
			pos = addr - trunc_page(loadmap[i].vaddr);
			pos = ELF_ROUND(pos,file_align);
			i = 0;
			continue;
		}

		i++;
	}

	/*
	 * Load all the necessary sections
	 */
	for (i = 0; i < eh.e_phnum; i++) {
		Elf_Addr size = 0;
		int prot = 0;
		int flags;

		switch (ph[i].p_type) {
		case PT_LOAD:
			if (base_ph == NULL) {
				flags = VMCMD_BASE;
				addr = pos;
				base_ph = &ph[i];
			} else {
				flags = VMCMD_RELATIVE;
				addr = ph[i].p_vaddr - base_ph->p_vaddr;
			}
			elf_load_psection(&epp->ep_vmcmds, nd.ni_vp,
			    &ph[i], &addr, &size, &prot, flags);
			/* If entry is within this section it must be text */
			if (eh.e_entry >= ph[i].p_vaddr &&
			    eh.e_entry < (ph[i].p_vaddr + size)) {
				/* LOAD containing e_entry may not be writable */
				if (prot & PROT_WRITE) {
					error = ENOEXEC;
					goto bad1;
				}
 				epp->ep_entry = addr + eh.e_entry -
				    ELF_TRUNC(ph[i].p_vaddr,ph[i].p_align);
				if (flags == VMCMD_RELATIVE)
					epp->ep_entry += pos;
				ap->arg_interp = pos;
			}
			if (prot & PROT_EXEC) {
				if (addr < text_start)
					text_start = addr;
				if (addr+size >= text_end)
					text_end = addr + size;
			}
			addr += size;
			break;

		case PT_PHDR:
		case PT_NOTE:
			break;

		case PT_OPENBSD_RANDOMIZE:
			if (ph[i].p_memsz > randomizequota) {
				error = ENOMEM;
				goto bad1;
			}
			randomizequota -= ph[i].p_memsz;
			NEW_VMCMD(&epp->ep_vmcmds, vmcmd_randomize,
			    ph[i].p_memsz, ph[i].p_vaddr + pos, NULL, 0, 0);
			break;

		case PT_DYNAMIC:
#if defined (__mips__)
			/* DT_DEBUG is not ready on mips */
			NEW_VMCMD(&epp->ep_vmcmds, vmcmd_mutable,
			    ph[i].p_memsz, ph[i].p_vaddr + pos, NULL, 0, 0);
#endif
			break;
		case PT_GNU_RELRO:
		case PT_OPENBSD_MUTABLE:
			NEW_VMCMD(&epp->ep_vmcmds, vmcmd_mutable,
			    ph[i].p_memsz, ph[i].p_vaddr + pos, NULL, 0, 0);
			break;
		case PT_OPENBSD_SYSCALLS:
			syscall_ph = &ph[i];
			break;
		default:
			break;
		}
	}

	if (syscall_ph) {
		struct process *pr = p->p_p;
		vaddr_t base = pos;
		size_t len = text_end;
		u_int *pins;
		int npins;

		npins = elf_read_pintable(p, nd.ni_vp, syscall_ph,
		    &pins, 1, len);
		if (npins) {
			elf_adjustpins(&base, &len, pins, npins,
			    text_start);
			pr->ps_pin.pn_start = base;
			pr->ps_pin.pn_end = base + len;
			pr->ps_pin.pn_pins = pins;
			pr->ps_pin.pn_npins = npins;
		}
	} else {
		error = EINVAL;	/* no pin table */
		goto bad1;
	}

	vn_marktext(nd.ni_vp);

bad1:
	VOP_CLOSE(nd.ni_vp, FREAD, p->p_ucred, p);
bad:
	free(ph, M_TEMP, phsize);

	vput(nd.ni_vp);
	return (error);
}

/*
 * Prepare an Elf binary's exec package
 *
 * First, set of the various offsets/lengths in the exec package.
 *
 * Then, mark the text image busy (so it can be demand paged) or error out if
 * this is not possible.  Finally, set up vmcmds for the text, data, bss, and
 * stack segments.
 */
int
exec_elf_makecmds(struct proc *p, struct exec_package *epp)
{
	Elf_Ehdr *eh = epp->ep_hdr;
	Elf_Phdr *ph, *pp, *base_ph = NULL, *syscall_ph = NULL;
	Elf_Addr phdr = 0, exe_base = 0, exe_end = 0;
	int error, i, has_phdr = 0, names = 0, textrel = 0;
	char *interp = NULL;
	u_long phsize;
	size_t randomizequota = ELF_RANDOMIZE_LIMIT;

	if (epp->ep_hdrvalid < sizeof(Elf_Ehdr))
		return (ENOEXEC);

	if (elf_check_header(eh) ||
	   (eh->e_type != ET_EXEC && eh->e_type != ET_DYN))
		return (ENOEXEC);

	/*
	 * check if vnode is in open for writing, because we want to demand-
	 * page out of it.  if it is, don't do it, for various reasons.
	 */
	if (epp->ep_vp->v_writecount != 0) {
#ifdef DIAGNOSTIC
		if (epp->ep_vp->v_flag & VTEXT)
			panic("exec: a VTEXT vnode has writecount != 0");
#endif
		return (ETXTBSY);
	}
	/*
	 * Allocate space to hold all the program headers, and read them
	 * from the file
	 */
	ph = mallocarray(eh->e_phnum, sizeof(Elf_Phdr), M_TEMP, M_WAITOK);
	phsize = eh->e_phnum * sizeof(Elf_Phdr);

	if ((error = elf_read_from(p, epp->ep_vp, eh->e_phoff, ph,
	    phsize)) != 0)
		goto bad;

	epp->ep_tsize = ELF_NO_ADDR;
	epp->ep_dsize = ELF_NO_ADDR;

	for (i = 0, pp = ph; i < eh->e_phnum; i++, pp++) {
		if ((pp->p_align > 1) && !powerof2(pp->p_align)) {
			error = EINVAL;
			goto bad;
		}

		if (pp->p_type == PT_INTERP && !interp) {
			if (pp->p_filesz < 2 || pp->p_filesz > MAXPATHLEN)
				goto bad;
			interp = pool_get(&namei_pool, PR_WAITOK);
			if ((error = elf_read_from(p, epp->ep_vp,
			    pp->p_offset, interp, pp->p_filesz)) != 0) {
				goto bad;
			}
			if (interp[pp->p_filesz - 1] != '\0')
				goto bad;
		} else if (pp->p_type == PT_LOAD) {
			if (pp->p_filesz > pp->p_memsz ||
			    pp->p_memsz == 0) {
				error = EINVAL;
				goto bad;
			}
			if (base_ph == NULL)
				base_ph = pp;
		} else if (pp->p_type == PT_PHDR) {
			has_phdr = 1;
		}
	}

	/*
	 * Verify this is an OpenBSD executable.  If it's marked that way
	 * via a PT_NOTE then also check for a PT_OPENBSD_WXNEEDED segment.
	 */
	if ((error = elf_os_pt_note(p, epp, epp->ep_hdr, &names)) != 0)
		goto bad;
	if (eh->e_ident[EI_OSABI] == ELFOSABI_OPENBSD)
		names |= ELF_NOTE_NAME_OPENBSD;

	if (eh->e_type == ET_DYN) {
		/* need phdr and load sections for PIE */
		if (!has_phdr || base_ph == NULL || base_ph->p_vaddr != 0) {
			error = EINVAL;
			goto bad;
		}
		/* randomize exe_base for PIE */
		exe_base = uvm_map_pie(base_ph->p_align);

		/*
		 * Check if DYNAMIC contains DT_TEXTREL
		 */
		for (i = 0, pp = ph; i < eh->e_phnum; i++, pp++) {
			Elf_Dyn *dt;
			int j;

			switch (pp->p_type) {
			case PT_DYNAMIC:
				if (pp->p_filesz > 64*1024)
					break;
				dt = malloc(pp->p_filesz, M_TEMP, M_WAITOK);
				error = vn_rdwr(UIO_READ, epp->ep_vp,
				    (caddr_t)dt, pp->p_filesz, pp->p_offset,
				    UIO_SYSSPACE, IO_UNIT, p->p_ucred, NULL, p);
				if (error) {
					free(dt, M_TEMP, pp->p_filesz);
					break;
				}
				for (j = 0; j < pp->p_filesz / sizeof(*dt); j++) {
					if (dt[j].d_tag == DT_TEXTREL) {
						textrel = VMCMD_TEXTREL;
						break;
					}
				}
				free(dt, M_TEMP, pp->p_filesz);
				break;
			default:
				break;
			}
		}
	}

	/*
	 * Load all the necessary sections
	 */
	for (i = 0, pp = ph; i < eh->e_phnum; i++, pp++) {
		Elf_Addr addr, size = 0;
		int prot = 0;
		int flags = 0;

		switch (pp->p_type) {
		case PT_LOAD:
			if (exe_base != 0) {
				if (pp == base_ph) {
					flags = VMCMD_BASE;
					addr = exe_base;
				} else {
					flags = VMCMD_RELATIVE;
					addr = pp->p_vaddr - base_ph->p_vaddr;
				}
			} else
				addr = ELF_NO_ADDR;

			/* Static binaries may not call pinsyscalls() */
			if (interp == NULL)
				p->p_vmspace->vm_map.flags |= VM_MAP_PINSYSCALL_ONCE;

			/*
			 * Calculates size of text and data segments
			 * by starting at first and going to end of last.
			 * 'rwx' sections are treated as data.
			 * this is correct for BSS_PLT, but may not be
			 * for DATA_PLT, is fine for TEXT_PLT.
			 */
			elf_load_psection(&epp->ep_vmcmds, epp->ep_vp,
			    pp, &addr, &size, &prot, flags | textrel);

			/*
			 * Update exe_base in case alignment was off.
			 * For PIE, addr is relative to exe_base so
			 * adjust it (non PIE exe_base is 0 so no change).
			 */
			if (flags == VMCMD_BASE)
				exe_base = addr;
			else
				addr += exe_base;

			/*
			 * Decide whether it's text or data by looking
			 * at the protection of the section
			 */
			if (prot & PROT_WRITE) {
				/* data section */
				if (epp->ep_dsize == ELF_NO_ADDR) {
					epp->ep_daddr = addr;
					epp->ep_dsize = size;
				} else {
					if (addr < epp->ep_daddr) {
						epp->ep_dsize =
						    epp->ep_dsize +
						    epp->ep_daddr -
						    addr;
						epp->ep_daddr = addr;
					} else
						epp->ep_dsize = addr+size -
						    epp->ep_daddr;
				}
			} else if (prot & PROT_EXEC) {
				/* text section */
				if (epp->ep_tsize == ELF_NO_ADDR) {
					epp->ep_taddr = addr;
					epp->ep_tsize = size;
				} else {
					if (addr < epp->ep_taddr) {
						epp->ep_tsize =
						    epp->ep_tsize +
						    epp->ep_taddr -
						    addr;
						epp->ep_taddr = addr;
					} else
						epp->ep_tsize = addr+size -
						    epp->ep_taddr;
				}
				if (interp == NULL)
					exe_end = epp->ep_taddr +
					    epp->ep_tsize;	/* end of TEXT */
			}
			break;

		case PT_SHLIB:
			error = ENOEXEC;
			goto bad;

		case PT_INTERP:
			/* Already did this one */
		case PT_NOTE:
			break;

		case PT_PHDR:
			/* Note address of program headers (in text segment) */
			phdr = pp->p_vaddr;
			break;

		case PT_OPENBSD_RANDOMIZE:
			if (ph[i].p_memsz > randomizequota) {
				error = ENOMEM;
				goto bad;
			}
			randomizequota -= ph[i].p_memsz;
			NEW_VMCMD(&epp->ep_vmcmds, vmcmd_randomize,
			    ph[i].p_memsz, ph[i].p_vaddr + exe_base, NULL, 0, 0);
			break;

		case PT_DYNAMIC:
#if defined (__mips__)
			/* DT_DEBUG is not ready on mips */
			NEW_VMCMD(&epp->ep_vmcmds, vmcmd_mutable,
			    ph[i].p_memsz, ph[i].p_vaddr + exe_base, NULL, 0, 0);
#endif
			break;
		case PT_GNU_RELRO:
		case PT_OPENBSD_MUTABLE:
			NEW_VMCMD(&epp->ep_vmcmds, vmcmd_mutable,
			    ph[i].p_memsz, ph[i].p_vaddr + exe_base, NULL, 0, 0);
			break;
		case PT_OPENBSD_SYSCALLS:
			if (interp == NULL)
				syscall_ph = &ph[i];
			break;
		default:
			/*
			 * Not fatal, we don't need to understand everything
			 * :-)
			 */
			break;
		}
	}

	if (syscall_ph) {
		vaddr_t base = exe_base;
		size_t len = exe_end - exe_base;
		u_int *pins;
		int npins;

		npins = elf_read_pintable(p, epp->ep_vp, syscall_ph,
		    &pins, 0, len);
		if (npins) {
			elf_adjustpins(&base, &len, pins, npins,
			    epp->ep_taddr - exe_base);
			epp->ep_pinstart = base;
			epp->ep_pinend = base + len;
			epp->ep_pins = pins;
			epp->ep_npins = npins;
		}
	}

	phdr += exe_base;

	/*
	 * Strangely some linux programs may have all load sections marked
	 * writeable, in this case, textsize is not -1, but rather 0;
	 */
	if (epp->ep_tsize == ELF_NO_ADDR)
		epp->ep_tsize = 0;
	/*
	 * Another possibility is that it has all load sections marked
	 * read-only.  Fake a zero-sized data segment right after the
	 * text segment.
	 */
	if (epp->ep_dsize == ELF_NO_ADDR) {
		epp->ep_daddr = round_page(epp->ep_taddr + epp->ep_tsize);
		epp->ep_dsize = 0;
	}

	epp->ep_interp = interp;
	epp->ep_entry = eh->e_entry + exe_base;

	/*
	 * Check if we found a dynamically linked binary and arrange to load
	 * its interpreter when the exec file is released.
	 */
	if (interp || eh->e_type == ET_DYN) {
		struct elf_args *ap;

		ap = malloc(sizeof(*ap), M_TEMP, M_WAITOK);

		ap->arg_phaddr = phdr;
		ap->arg_phentsize = eh->e_phentsize;
		ap->arg_phnum = eh->e_phnum;
		ap->arg_entry = eh->e_entry + exe_base;
		ap->arg_interp = exe_base;

		epp->ep_args = ap;
	}

	free(ph, M_TEMP, phsize);
	vn_marktext(epp->ep_vp);
	return (exec_setup_stack(p, epp));

bad:
	if (interp)
		pool_put(&namei_pool, interp);
	free(ph, M_TEMP, phsize);
	kill_vmcmds(&epp->ep_vmcmds);
	if (error == 0)
		return (ENOEXEC);
	return (error);
}

#ifdef __HAVE_CPU_HWCAP
unsigned long hwcap;
#endif /* __HAVE_CPU_HWCAP */

#ifdef __HAVE_CPU_HWCAP2
unsigned long hwcap2;
#endif /* __HAVE_CPU_HWCAP2 */

/*
 * Phase II of load. It is now safe to load the interpreter. Info collected
 * when loading the program is available for setup of the interpreter.
 */
int
exec_elf_fixup(struct proc *p, struct exec_package *epp)
{
	char	*interp;
	int	error = 0;
	struct	elf_args *ap;
	AuxInfo ai[ELF_AUX_ENTRIES], *a;

	ap = epp->ep_args;
	if (ap == NULL) {
		return (0);
	}

	interp = epp->ep_interp;

	/* disable kbind in programs that don't use ld.so */
	if (interp == NULL)
		p->p_p->ps_kbind_addr = BOGO_PC;

	if (interp &&
	    (error = elf_load_file(p, interp, epp, ap)) != 0) {
		uprintf("execve: cannot load %s\n", interp);
		free(ap, M_TEMP, sizeof *ap);
		pool_put(&namei_pool, interp);
		kill_vmcmds(&epp->ep_vmcmds);
		return (error);
	}
	/*
	 * We have to do this ourselves...
	 */
	error = exec_process_vmcmds(p, epp);

	/*
	 * Push extra arguments on the stack needed by dynamically
	 * linked binaries
	 */
	if (error == 0) {
		memset(&ai, 0, sizeof ai);
		a = ai;

		a->au_id = AUX_phdr;
		a->au_v = ap->arg_phaddr;
		a++;

		a->au_id = AUX_phent;
		a->au_v = ap->arg_phentsize;
		a++;

		a->au_id = AUX_phnum;
		a->au_v = ap->arg_phnum;
		a++;

		a->au_id = AUX_pagesz;
		a->au_v = PAGE_SIZE;
		a++;

		a->au_id = AUX_base;
		a->au_v = ap->arg_interp;
		a++;

		a->au_id = AUX_flags;
		a->au_v = 0;
		a++;

		a->au_id = AUX_entry;
		a->au_v = ap->arg_entry;
		a++;

#ifdef __HAVE_CPU_HWCAP
		a->au_id = AUX_hwcap;
		a->au_v = hwcap;
		a++;
#endif /* __HAVE_CPU_HWCAP */

#ifdef __HAVE_CPU_HWCAP2
		a->au_id = AUX_hwcap2;
		a->au_v = hwcap2;
		a++;
#endif /* __HAVE_CPU_HWCAP2 */

		a->au_id = AUX_openbsd_timekeep;
		a->au_v = p->p_p->ps_timekeep;
		a++;

		a->au_id = AUX_null;
		a->au_v = 0;
		a++;

		error = copyout(ai, epp->ep_auxinfo, sizeof ai);
	}
	free(ap, M_TEMP, sizeof *ap);
	if (interp)
		pool_put(&namei_pool, interp);
	return (error);
}

int
elf_os_pt_note_name(Elf_Note *np, int *typep)
{
	int i, j;

	for (i = 0; i < nitems(elf_note_names); i++) {
		size_t namlen = strlen(elf_note_names[i].name);
		if (np->namesz < namlen)
			continue;
		/* verify name padding (after the NUL) is NUL */
		for (j = namlen + 1; j < elfround(np->namesz); j++)
			if (((char *)(np + 1))[j] != '\0')
				continue;
		/* verify desc padding is NUL */
		for (j = np->descsz; j < elfround(np->descsz); j++)
			if (((char *)(np + 1))[j] != '\0')
				continue;
		if (strcmp((char *)(np + 1), elf_note_names[i].name) == 0) {
			*typep = np->type;
			return elf_note_names[i].id;
		}
	}
	return (0);
}

int
elf_os_pt_note(struct proc *p, struct exec_package *epp, Elf_Ehdr *eh, int *namesp)
{
	Elf_Phdr *hph, *ph;
	Elf_Note *np = NULL;
	size_t phsize, offset, pfilesz = 0, total;
	int error, names = 0;

	hph = mallocarray(eh->e_phnum, sizeof(Elf_Phdr), M_TEMP, M_WAITOK);
	phsize = eh->e_phnum * sizeof(Elf_Phdr);
	if ((error = elf_read_from(p, epp->ep_vp, eh->e_phoff,
	    hph, phsize)) != 0)
		goto out1;

	for (ph = hph;  ph < &hph[eh->e_phnum]; ph++) {
		if (ph->p_type == PT_OPENBSD_WXNEEDED) {
			epp->ep_flags |= EXEC_WXNEEDED;
			continue;
		}
		if (ph->p_type == PT_OPENBSD_NOBTCFI) {
			epp->ep_flags |= EXEC_NOBTCFI;
			continue;
		}

		if (ph->p_type != PT_NOTE || ph->p_filesz > 1024)
			continue;

		if (np && ph->p_filesz != pfilesz) {
			free(np, M_TEMP, pfilesz);
			np = NULL;
		}
		if (!np)
			np = malloc(ph->p_filesz, M_TEMP, M_WAITOK);
		pfilesz = ph->p_filesz;
		if ((error = elf_read_from(p, epp->ep_vp, ph->p_offset,
		    np, ph->p_filesz)) != 0)
			goto out2;

		for (offset = 0; offset < ph->p_filesz; offset += total) {
			Elf_Note *np2 = (Elf_Note *)((char *)np + offset);
			int name, type;

			if (offset + sizeof(Elf_Note) > ph->p_filesz)
				break;
			total = sizeof(Elf_Note) + elfround(np2->namesz) +
			    elfround(np2->descsz);
			if (offset + total > ph->p_filesz)
				break;
			name = elf_os_pt_note_name(np2, &type);
			if (name == ELF_NOTE_NAME_OPENBSD &&
			    type == NT_OPENBSD_PROF)
				epp->ep_flags |= EXEC_PROFILE;
			names |= name;
		}
	}

out2:
	free(np, M_TEMP, pfilesz);
out1:
	free(hph, M_TEMP, phsize);
	*namesp = names;
	return ((names & ELF_NOTE_NAME_OPENBSD) ? 0 : ENOEXEC);
}

/*
 * Start of routines related to dumping core
 */

#ifdef SMALL_KERNEL
int
coredump_elf(struct proc *p, void *cookie)
{
	return EPERM;
}
#else /* !SMALL_KERNEL */

struct writesegs_state {
	off_t	notestart;
	off_t	secstart;
	off_t	secoff;
	struct	proc *p;
	void	*iocookie;
	Elf_Phdr *psections;
	size_t	psectionslen;
	size_t	notesize;
	int	npsections;
};

uvm_coredump_setup_cb	coredump_setup_elf;
uvm_coredump_walk_cb	coredump_walk_elf;

int	coredump_notes_elf(struct proc *, void *, size_t *);
int	coredump_note_elf(struct proc *, void *, size_t *);

extern vaddr_t sigcode_va;
extern vsize_t sigcode_sz;

int
coredump_elf(struct proc *p, void *cookie)
{
#ifdef DIAGNOSTIC
	off_t offset;
#endif
	struct writesegs_state ws;
	size_t notesize;
	int error, i;

	ws.p = p;
	ws.iocookie = cookie;
	ws.psections = NULL;

	/*
	 * Walk the map to get all the segment offsets and lengths,
	 * write out the ELF header.
	 */
	error = uvm_coredump_walkmap(p, coredump_setup_elf,
	    coredump_walk_elf, &ws);
	if (error)
		goto out;

	error = coredump_write(cookie, UIO_SYSSPACE, ws.psections,
	    ws.psectionslen, 0);
	if (error)
		goto out;

	/* Write out the notes. */
	error = coredump_notes_elf(p, cookie, &notesize);
	if (error)
		goto out;

#ifdef DIAGNOSTIC
	if (notesize != ws.notesize)
		panic("coredump: notesize changed: %zu != %zu",
		    ws.notesize, notesize);
	offset = ws.notestart + notesize;
	if (offset != ws.secstart)
		panic("coredump: offset %lld != secstart %lld",
		    (long long) offset, (long long) ws.secstart);
#endif

	/* Pass 3: finally, write the sections themselves. */
	for (i = 0; i < ws.npsections - 1; i++) {
		Elf_Phdr *pent = &ws.psections[i];
		if (pent->p_filesz == 0)
			continue;

#ifdef DIAGNOSTIC
		if (offset != pent->p_offset)
			panic("coredump: offset %lld != p_offset[%d] %lld",
			    (long long) offset, i,
			    (long long) pent->p_filesz);
#endif

		/*
		 * Since the sigcode is mapped execute-only, we can't
		 * read it.  So use the kernel mapping for it instead.
		 */
		if (pent->p_vaddr == p->p_p->ps_sigcode &&
		    pent->p_filesz == sigcode_sz) {
			error = coredump_write(cookie, UIO_SYSSPACE,
			    (void *)sigcode_va, sigcode_sz, 0);
		} else {
			error = coredump_write(cookie, UIO_USERSPACE,
			    (void *)(vaddr_t)pent->p_vaddr, pent->p_filesz,
			    (pent->p_flags & PF_ISVNODE));
		}
		if (error)
			goto out;

		coredump_unmap(cookie, (vaddr_t)pent->p_vaddr,
		    (vaddr_t)pent->p_vaddr + pent->p_filesz);

#ifdef DIAGNOSTIC
		offset += ws.psections[i].p_filesz;
#endif
	}

out:
	free(ws.psections, M_TEMP, ws.psectionslen);
	return (error);
}


/*
 * Normally we lay out core files like this:
 *	[ELF Header] [Program headers] [Notes] [data for PT_LOAD segments]
 *
 * However, if there's >= 65535 segments then it overflows the field
 * in the ELF header, so the standard specifies putting a magic
 * number there and saving the real count in the .sh_info field of
 * the first *section* header...which requires generating a section
 * header.  To avoid confusing tools, we include an .shstrtab section
 * as well so all the indexes look valid.  So in this case we lay
 * out the core file like this:
 *	[ELF Header] [Section Headers] [.shstrtab] [Program headers] \
 *	[Notes] [data for PT_LOAD segments]
 *
 * The 'shstrtab' structure below is data for the second of the two
 * section headers, plus the .shstrtab itself, in one const buffer.
 */
static const struct {
    Elf_Shdr	shdr;
    char	shstrtab[sizeof(ELF_SHSTRTAB) + 1];
} shstrtab = {
    .shdr = {
	.sh_name = 1,			/* offset in .shstrtab below */
	.sh_type = SHT_STRTAB,
	.sh_offset = sizeof(Elf_Ehdr) + 2*sizeof(Elf_Shdr),
	.sh_size = sizeof(ELF_SHSTRTAB) + 1,
	.sh_addralign = 1,
    },
    .shstrtab = "\0" ELF_SHSTRTAB,
};

int
coredump_setup_elf(int segment_count, void *cookie)
{
	Elf_Ehdr ehdr;
	struct writesegs_state *ws = cookie;
	Elf_Phdr *note;
	int error;

	/* Get the count of segments, plus one for the PT_NOTE */
	ws->npsections = segment_count + 1;

	/* Get the size of the notes. */
	error = coredump_notes_elf(ws->p, NULL, &ws->notesize);
	if (error)
		return error;

	/* Setup the ELF header */
	memset(&ehdr, 0, sizeof(ehdr));
	memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
	ehdr.e_ident[EI_CLASS] = ELF_TARG_CLASS;
	ehdr.e_ident[EI_DATA] = ELF_TARG_DATA;
	ehdr.e_ident[EI_VERSION] = EV_CURRENT;
	/* XXX Should be the OSABI/ABI version of the executable. */
	ehdr.e_ident[EI_OSABI] = ELFOSABI_SYSV;
	ehdr.e_ident[EI_ABIVERSION] = 0;
	ehdr.e_type = ET_CORE;
	/* XXX This should be the e_machine of the executable. */
	ehdr.e_machine = ELF_TARG_MACH;
	ehdr.e_version = EV_CURRENT;
	ehdr.e_entry = 0;
	ehdr.e_flags = 0;
	ehdr.e_ehsize = sizeof(ehdr);
	ehdr.e_phentsize = sizeof(Elf_Phdr);

	if (ws->npsections < PN_XNUM) {
		ehdr.e_phoff = sizeof(ehdr);
		ehdr.e_shoff = 0;
		ehdr.e_phnum = ws->npsections;
		ehdr.e_shentsize = 0;
		ehdr.e_shnum = 0;
		ehdr.e_shstrndx = 0;
	} else {
		/* too many segments, use extension setup */
		ehdr.e_shoff = sizeof(ehdr);
		ehdr.e_phnum = PN_XNUM;
		ehdr.e_shentsize = sizeof(Elf_Shdr);
		ehdr.e_shnum = 2;
		ehdr.e_shstrndx = 1;
		ehdr.e_phoff = shstrtab.shdr.sh_offset + shstrtab.shdr.sh_size;
	}

	/* Write out the ELF header. */
	error = coredump_write(ws->iocookie, UIO_SYSSPACE, &ehdr, sizeof(ehdr), 0);
	if (error)
		return error;

	/*
	 * If an section header is needed to store extension info, write
	 * it out after the ELF header and before the program header.
	 */
	if (ehdr.e_shnum != 0) {
		Elf_Shdr shdr = { .sh_info = ws->npsections };
		error = coredump_write(ws->iocookie, UIO_SYSSPACE, &shdr,
		    sizeof shdr, 0);
		if (error)
			return error;
		error = coredump_write(ws->iocookie, UIO_SYSSPACE, &shstrtab,
		    sizeof(shstrtab.shdr) + sizeof(shstrtab.shstrtab), 0);
		if (error)
			return error;
	}

	/*
	 * Allocate the segment header array and setup to collect
	 * the section sizes and offsets
	 */
	ws->psections = mallocarray(ws->npsections, sizeof(Elf_Phdr),
	    M_TEMP, M_WAITOK|M_CANFAIL|M_ZERO);
	if (ws->psections == NULL)
		return ENOMEM;
	ws->psectionslen = ws->npsections * sizeof(Elf_Phdr);

	ws->notestart = ehdr.e_phoff + ws->psectionslen;
	ws->secstart = ws->notestart + ws->notesize;
	ws->secoff = ws->secstart;

	/* Fill in the PT_NOTE segment header in the last slot */
	note = &ws->psections[ws->npsections - 1];
	note->p_type = PT_NOTE;
	note->p_offset = ws->notestart;
	note->p_vaddr = 0;
	note->p_paddr = 0;
	note->p_filesz = ws->notesize;
	note->p_memsz = 0;
	note->p_flags = PF_R;
	note->p_align = ELFROUNDSIZE;

	return (0);
}

int
coredump_walk_elf(vaddr_t start, vaddr_t realend, vaddr_t end, vm_prot_t prot,
    int isvnode, int nsegment, void *cookie)
{
	struct writesegs_state *ws = cookie;
	Elf_Phdr phdr;
	vsize_t size, realsize;

	size = end - start;
	realsize = realend - start;

	phdr.p_type = PT_LOAD;
	phdr.p_offset = ws->secoff;
	phdr.p_vaddr = start;
	phdr.p_paddr = 0;
	phdr.p_filesz = realsize;
	phdr.p_memsz = size;
	phdr.p_flags = 0;
	if (prot & PROT_READ)
		phdr.p_flags |= PF_R;
	if (prot & PROT_WRITE)
		phdr.p_flags |= PF_W;
	if (prot & PROT_EXEC)
		phdr.p_flags |= PF_X;
	if (isvnode)
		phdr.p_flags |= PF_ISVNODE;
	phdr.p_align = PAGE_SIZE;

	ws->secoff += phdr.p_filesz;
	ws->psections[nsegment] = phdr;

	return (0);
}

int
coredump_notes_elf(struct proc *p, void *iocookie, size_t *sizep)
{
	struct elfcore_procinfo cpi;
	Elf_Note nhdr;
	struct process *pr = p->p_p;
	struct proc *q;
	size_t size, notesize;
	int error;

	KASSERT(!P_HASSIBLING(p) || pr->ps_single != NULL);
	size = 0;

	/* First, write an elfcore_procinfo. */
	notesize = sizeof(nhdr) + elfround(sizeof("OpenBSD")) +
	    elfround(sizeof(cpi));
	if (iocookie) {
		memset(&cpi, 0, sizeof(cpi));

		cpi.cpi_version = ELFCORE_PROCINFO_VERSION;
		cpi.cpi_cpisize = sizeof(cpi);
		cpi.cpi_signo = p->p_sisig;
		cpi.cpi_sigcode = p->p_sicode;

		cpi.cpi_sigpend = p->p_siglist | pr->ps_siglist;
		cpi.cpi_sigmask = p->p_sigmask;
		cpi.cpi_sigignore = pr->ps_sigacts->ps_sigignore;
		cpi.cpi_sigcatch = pr->ps_sigacts->ps_sigcatch;

		cpi.cpi_pid = pr->ps_pid;
		cpi.cpi_ppid = pr->ps_ppid;
		cpi.cpi_pgrp = pr->ps_pgid;
		if (pr->ps_session->s_leader)
			cpi.cpi_sid = pr->ps_session->s_leader->ps_pid;
		else
			cpi.cpi_sid = 0;

		cpi.cpi_ruid = p->p_ucred->cr_ruid;
		cpi.cpi_euid = p->p_ucred->cr_uid;
		cpi.cpi_svuid = p->p_ucred->cr_svuid;

		cpi.cpi_rgid = p->p_ucred->cr_rgid;
		cpi.cpi_egid = p->p_ucred->cr_gid;
		cpi.cpi_svgid = p->p_ucred->cr_svgid;

		(void)strlcpy(cpi.cpi_name, pr->ps_comm, sizeof(cpi.cpi_name));

		nhdr.namesz = sizeof("OpenBSD");
		nhdr.descsz = sizeof(cpi);
		nhdr.type = NT_OPENBSD_PROCINFO;

		error = coredump_writenote_elf(p, iocookie, &nhdr,
		    "OpenBSD", &cpi);
		if (error)
			return (error);
	}
	size += notesize;

	/* Second, write an NT_OPENBSD_AUXV note. */
	notesize = sizeof(nhdr) + elfround(sizeof("OpenBSD")) +
	    elfround(ELF_AUX_WORDS * sizeof(char *));
	if (iocookie && pr->ps_auxinfo) {

		nhdr.namesz = sizeof("OpenBSD");
		nhdr.descsz = ELF_AUX_WORDS * sizeof(char *);
		nhdr.type = NT_OPENBSD_AUXV;

		error = coredump_write(iocookie, UIO_SYSSPACE,
		    &nhdr, sizeof(nhdr), 0);
		if (error)
			return (error);

		error = coredump_write(iocookie, UIO_SYSSPACE,
		    "OpenBSD", elfround(nhdr.namesz), 0);
		if (error)
			return (error);

		error = coredump_write(iocookie, UIO_USERSPACE,
		    (caddr_t)pr->ps_auxinfo, nhdr.descsz, 0);
		if (error)
			return (error);
	}
	size += notesize;

#ifdef PT_WCOOKIE
	notesize = sizeof(nhdr) + elfround(sizeof("OpenBSD")) +
	    elfround(sizeof(register_t));
	if (iocookie) {
		register_t wcookie;

		nhdr.namesz = sizeof("OpenBSD");
		nhdr.descsz = sizeof(register_t);
		nhdr.type = NT_OPENBSD_WCOOKIE;

		wcookie = process_get_wcookie(p);
		error = coredump_writenote_elf(p, iocookie, &nhdr,
		    "OpenBSD", &wcookie);
		if (error)
			return (error);
	}
	size += notesize;
#endif

	/*
	 * Now write the register info for the thread that caused the
	 * coredump.
	 */
	error = coredump_note_elf(p, iocookie, &notesize);
	if (error)
		return (error);
	size += notesize;

	/*
	 * Now, for each thread, write the register info and any other
	 * per-thread notes.  Since we're dumping core, all the other
	 * threads in the process have been stopped and the list can't
	 * change.
	 */
	TAILQ_FOREACH(q, &pr->ps_threads, p_thr_link) {
		if (q == p)		/* we've taken care of this thread */
			continue;
		error = coredump_note_elf(q, iocookie, &notesize);
		if (error)
			return (error);
		size += notesize;
	}

	*sizep = size;
	return (0);
}

int
coredump_note_elf(struct proc *p, void *iocookie, size_t *sizep)
{
	Elf_Note nhdr;
	int size, notesize, error;
	int namesize;
	char name[64+ELFROUNDSIZE];
	struct reg intreg;
#ifdef PT_GETFPREGS
	struct fpreg freg;
#endif

	size = 0;

	snprintf(name, sizeof(name)-ELFROUNDSIZE, "%s@%d",
	    "OpenBSD", p->p_tid + THREAD_PID_OFFSET);
	namesize = strlen(name) + 1;
	memset(name + namesize, 0, elfround(namesize) - namesize);

	notesize = sizeof(nhdr) + elfround(namesize) + elfround(sizeof(intreg));
	if (iocookie) {
		error = process_read_regs(p, &intreg);
		if (error)
			return (error);

		nhdr.namesz = namesize;
		nhdr.descsz = sizeof(intreg);
		nhdr.type = NT_OPENBSD_REGS;

		error = coredump_writenote_elf(p, iocookie, &nhdr,
		    name, &intreg);
		if (error)
			return (error);

	}
	size += notesize;

#ifdef PT_GETFPREGS
	notesize = sizeof(nhdr) + elfround(namesize) + elfround(sizeof(freg));
	if (iocookie) {
		error = process_read_fpregs(p, &freg);
		if (error)
			return (error);

		nhdr.namesz = namesize;
		nhdr.descsz = sizeof(freg);
		nhdr.type = NT_OPENBSD_FPREGS;

		error = coredump_writenote_elf(p, iocookie, &nhdr, name, &freg);
		if (error)
			return (error);
	}
	size += notesize;
#endif

	*sizep = size;

#ifdef __HAVE_COREDUMP_NOTE_ELF_MD
	/* Add machdep per-thread notes. */
	return coredump_note_elf_md(p, iocookie, name, sizep);
#else
	return 0;
#endif
}

int
coredump_writenote_elf(struct proc *p, void *cookie, Elf_Note *nhdr,
    const char *name, void *data)
{
	int error;

	error = coredump_write(cookie, UIO_SYSSPACE, nhdr, sizeof(*nhdr), 0);
	if (error)
		return error;

	error = coredump_write(cookie, UIO_SYSSPACE, name,
	    elfround(nhdr->namesz), 0);
	if (error)
		return error;

	return coredump_write(cookie, UIO_SYSSPACE, data, nhdr->descsz, 0);
}
#endif /* !SMALL_KERNEL */
