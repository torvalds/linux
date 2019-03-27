/*      $NetBSD: ppc_reloc.c,v 1.10 2001/09/10 06:09:41 mycroft Exp $   */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (C) 1998   Tsubai Masanari
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
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/mman.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <machine/cpu.h>
#include <machine/md_var.h>

#include "debug.h"
#include "rtld.h"

#if !defined(_CALL_ELF) || _CALL_ELF == 1
struct funcdesc {
	Elf_Addr addr;
	Elf_Addr toc;
	Elf_Addr env;
};
#endif

/*
 * Process the R_PPC_COPY relocations
 */
int
do_copy_relocations(Obj_Entry *dstobj)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	/*
	 * COPY relocs are invalid outside of the main program
	 */
	assert(dstobj->mainprog);

	relalim = (const Elf_Rela *)((const char *) dstobj->rela +
	    dstobj->relasize);
	for (rela = dstobj->rela;  rela < relalim;  rela++) {
		void *dstaddr;
		const Elf_Sym *dstsym;
		const char *name;
		size_t size;
		const void *srcaddr;
		const Elf_Sym *srcsym = NULL;
		const Obj_Entry *srcobj, *defobj;
		SymLook req;
		int res;

		if (ELF_R_TYPE(rela->r_info) != R_PPC_COPY) {
			continue;
		}

		dstaddr = (void *)(dstobj->relocbase + rela->r_offset);
		dstsym = dstobj->symtab + ELF_R_SYM(rela->r_info);
		name = dstobj->strtab + dstsym->st_name;
		size = dstsym->st_size;
		symlook_init(&req, name);
		req.ventry = fetch_ventry(dstobj, ELF_R_SYM(rela->r_info));
		req.flags = SYMLOOK_EARLY;

		for (srcobj = globallist_next(dstobj); srcobj != NULL;
		     srcobj = globallist_next(srcobj)) {
			res = symlook_obj(&req, srcobj);
			if (res == 0) {
				srcsym = req.sym_out;
				defobj = req.defobj_out;
				break;
			}
		}

		if (srcobj == NULL) {
			_rtld_error("Undefined symbol \"%s\" "
				    " referenced from COPY"
				    " relocation in %s", name, dstobj->path);
			return (-1);
		}

		srcaddr = (const void *)(defobj->relocbase+srcsym->st_value);
		memcpy(dstaddr, srcaddr, size);
		dbg("copy_reloc: src=%p,dst=%p,size=%zd\n",srcaddr,dstaddr,size);
	}

	return (0);
}


/*
 * Perform early relocation of the run-time linker image
 */
void
reloc_non_plt_self(Elf_Dyn *dynp, Elf_Addr relocbase)
{
	const Elf_Rela *rela = NULL, *relalim;
	Elf_Addr relasz = 0;
	Elf_Addr *where;

	/*
	 * Extract the rela/relasz values from the dynamic section
	 */
	for (; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_RELA:
			rela = (const Elf_Rela *)(relocbase+dynp->d_un.d_ptr);
			break;
		case DT_RELASZ:
			relasz = dynp->d_un.d_val;
			break;
		}
	}

	/*
	 * Relocate these values
	 */
	relalim = (const Elf_Rela *)((const char *)rela + relasz);
	for (; rela < relalim; rela++) {
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		*where = (Elf_Addr)(relocbase + rela->r_addend);
	}
}


/*
 * Relocate a non-PLT object with addend.
 */
static int
reloc_nonplt_object(Obj_Entry *obj_rtld __unused, Obj_Entry *obj,
    const Elf_Rela *rela, SymCache *cache, int flags, RtldLockState *lockstate)
{
	Elf_Addr        *where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
	const Elf_Sym   *def;
	const Obj_Entry *defobj;
	Elf_Addr         tmp;

	switch (ELF_R_TYPE(rela->r_info)) {

	case R_PPC_NONE:
		break;

        case R_PPC64_UADDR64:    /* doubleword64 S + A */
        case R_PPC64_ADDR64:
        case R_PPC_GLOB_DAT:
		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
		    flags, cache, lockstate);
		if (def == NULL) {
			return (-1);
		}

                tmp = (Elf_Addr)(defobj->relocbase + def->st_value +
                    rela->r_addend);

		/* Don't issue write if unnecessary; avoid COW page fault */
                if (*where != tmp) {
                        *where = tmp;
		}
                break;

        case R_PPC_RELATIVE:  /* doubleword64 B + A */
		tmp = (Elf_Addr)(obj->relocbase + rela->r_addend);

		/* As above, don't issue write unnecessarily */
		if (*where != tmp) {
			*where = tmp;
		}
		break;

	case R_PPC_COPY:
		/*
		 * These are deferred until all other relocations
		 * have been done.  All we do here is make sure
		 * that the COPY relocation is not in a shared
		 * library.  They are allowed only in executable
		 * files.
		 */
		if (!obj->mainprog) {
			_rtld_error("%s: Unexpected R_COPY "
				    " relocation in shared library",
				    obj->path);
			return (-1);
		}
		break;

	case R_PPC_JMP_SLOT:
		/*
		 * These will be handled by the plt/jmpslot routines
		 */
		break;

	case R_PPC64_DTPMOD64:
		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
		    flags, cache, lockstate);

		if (def == NULL)
			return (-1);

		*where = (Elf_Addr) defobj->tlsindex;

		break;

	case R_PPC64_TPREL64:
		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
		    flags, cache, lockstate);

		if (def == NULL)
			return (-1);

		/*
		 * We lazily allocate offsets for static TLS as we
		 * see the first relocation that references the
		 * TLS block. This allows us to support (small
		 * amounts of) static TLS in dynamically loaded
		 * modules. If we run out of space, we generate an
		 * error.
		 */
		if (!defobj->tls_done) {
			if (!allocate_tls_offset(
				    __DECONST(Obj_Entry *, defobj))) {
				_rtld_error("%s: No space available for static "
				    "Thread Local Storage", obj->path);
				return (-1);
			}
		}

		*(Elf_Addr **)where = *where * sizeof(Elf_Addr)
		    + (Elf_Addr *)(def->st_value + rela->r_addend
		    + defobj->tlsoffset - TLS_TP_OFFSET - TLS_TCB_SIZE);

		break;

	case R_PPC64_DTPREL64:
		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
		    flags, cache, lockstate);

		if (def == NULL)
			return (-1);

		*where += (Elf_Addr)(def->st_value + rela->r_addend
		    - TLS_DTV_OFFSET);

		break;

	default:
		_rtld_error("%s: Unsupported relocation type %ld"
			    " in non-PLT relocations\n", obj->path,
			    ELF_R_TYPE(rela->r_info));
		return (-1);
        }
	return (0);
}


/*
 * Process non-PLT relocations
 */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld, int flags,
    RtldLockState *lockstate)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Phdr *phdr;
	SymCache *cache;
	int bytes = obj->dynsymcount * sizeof(SymCache);
	int r = -1;

	if ((flags & SYMLOOK_IFUNC) != 0)
		/* XXX not implemented */
		return (0);

	/*
	 * The dynamic loader may be called from a thread, we have
	 * limited amounts of stack available so we cannot use alloca().
	 */
	if (obj != obj_rtld) {
		cache = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_ANON,
		    -1, 0);
		if (cache == MAP_FAILED)
			cache = NULL;
	} else
		cache = NULL;

	/*
	 * From the SVR4 PPC ABI:
	 * "The PowerPC family uses only the Elf32_Rela relocation
	 *  entries with explicit addends."
	 */
	relalim = (const Elf_Rela *)((const char *)obj->rela + obj->relasize);
	for (rela = obj->rela; rela < relalim; rela++) {
		if (reloc_nonplt_object(obj_rtld, obj, rela, cache, flags,
		    lockstate) < 0)
			goto done;
	}
	r = 0;
done:
	if (cache)
		munmap(cache, bytes);

	/*
	 * Synchronize icache for executable segments in case we made
	 * any changes.
	 */
	for (phdr = obj->phdr;
	    (const char *)phdr < (const char *)obj->phdr + obj->phsize;
	    phdr++) {
		if (phdr->p_type == PT_LOAD && (phdr->p_flags & PF_X) != 0) {
			__syncicache(obj->relocbase + phdr->p_vaddr,
			    phdr->p_memsz);
		}
	}

	return (r);
}


/*
 * Initialise a PLT slot to the resolving trampoline
 */
static int
reloc_plt_object(Obj_Entry *obj, const Elf_Rela *rela)
{
	Elf_Addr *where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
	long reloff;

	reloff = rela - obj->pltrela;

	dbg(" reloc_plt_object: where=%p,reloff=%lx,glink=%#lx", (void *)where,
	    reloff, obj->glink);

#if !defined(_CALL_ELF) || _CALL_ELF == 1
	/* Glink code is 3 instructions after the first 32k, 2 before */
	*where = (Elf_Addr)obj->glink + 32 + 
	    8*((reloff < 0x8000) ? reloff : 0x8000) + 
	    12*((reloff < 0x8000) ? 0 : (reloff - 0x8000));
#else
	*where = (Elf_Addr)obj->glink + 4*reloff + 32;
#endif

	return (0);
}


/*
 * Process the PLT relocations.
 */
int
reloc_plt(Obj_Entry *obj, int flags __unused, RtldLockState *lockstate __unused)
{
	const Elf_Rela *relalim;
	const Elf_Rela *rela;

	if (obj->pltrelasize != 0) {
		relalim = (const Elf_Rela *)((const char *)obj->pltrela +
		    obj->pltrelasize);
		for (rela = obj->pltrela;  rela < relalim;  rela++) {
			assert(ELF_R_TYPE(rela->r_info) == R_PPC_JMP_SLOT);

			if (reloc_plt_object(obj, rela) < 0) {
				return (-1);
			}
		}
	}

	return (0);
}


/*
 * LD_BIND_NOW was set - force relocation for all jump slots
 */
int
reloc_jmpslots(Obj_Entry *obj, int flags, RtldLockState *lockstate)
{
	const Obj_Entry *defobj;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	const Elf_Sym *def;
	Elf_Addr *where;
	Elf_Addr target;

	relalim = (const Elf_Rela *)((const char *)obj->pltrela +
	    obj->pltrelasize);
	for (rela = obj->pltrela; rela < relalim; rela++) {
		assert(ELF_R_TYPE(rela->r_info) == R_PPC_JMP_SLOT);
		where = (Elf_Addr *)(obj->relocbase + rela->r_offset);
		def = find_symdef(ELF_R_SYM(rela->r_info), obj, &defobj,
		    SYMLOOK_IN_PLT | flags, NULL, lockstate);
		if (def == NULL) {
			dbg("reloc_jmpslots: sym not found");
			return (-1);
		}

		target = (Elf_Addr)(defobj->relocbase + def->st_value);

		if (def == &sym_zero) {
			/* Zero undefined weak symbols */
#if !defined(_CALL_ELF) || _CALL_ELF == 1
			bzero(where, sizeof(struct funcdesc));
#else
			*where = 0;
#endif
		} else {
			reloc_jmpslot(where, target, defobj, obj,
			    (const Elf_Rel *) rela);
		}
	}

	obj->jmpslots_done = true;

	return (0);
}


/*
 * Update the value of a PLT jump slot.
 */
Elf_Addr
reloc_jmpslot(Elf_Addr *wherep, Elf_Addr target, const Obj_Entry *defobj __unused,
    const Obj_Entry *obj __unused, const Elf_Rel *rel __unused)
{

	/*
	 * At the PLT entry pointed at by `wherep', construct
	 * a direct transfer to the now fully resolved function
	 * address.
	 */

#if !defined(_CALL_ELF) || _CALL_ELF == 1
	dbg(" reloc_jmpslot: where=%p, target=%p (%#lx + %#lx)",
	    (void *)wherep, (void *)target, *(Elf_Addr *)target,
	    (Elf_Addr)defobj->relocbase);

	if (ld_bind_not)
		goto out;

	/*
	 * For the trampoline, the second two elements of the function
	 * descriptor are unused, so we are fine replacing those at any time
	 * with the real ones with no thread safety implications. However, we
	 * need to make sure the main entry point pointer ([0]) is seen to be
	 * modified *after* the second two elements. This can't be done in
	 * general, since there are no barriers in the reading code, but put in
	 * some isyncs to at least make it a little better.
	 */
	memcpy(wherep, (void *)target, sizeof(struct funcdesc));
	wherep[2] = ((Elf_Addr *)target)[2];
	wherep[1] = ((Elf_Addr *)target)[1];
	__asm __volatile ("isync" : : : "memory");
	wherep[0] = ((Elf_Addr *)target)[0];
	__asm __volatile ("isync" : : : "memory");

	if (((struct funcdesc *)(wherep))->addr < (Elf_Addr)defobj->relocbase) {
		/*
		 * It is possible (LD_BIND_NOW) that the function
		 * descriptor we are copying has not yet been relocated.
		 * If this happens, fix it. Don't worry about threading in
		 * this case since LD_BIND_NOW makes it irrelevant.
		 */

		((struct funcdesc *)(wherep))->addr +=
		    (Elf_Addr)defobj->relocbase;
		((struct funcdesc *)(wherep))->toc +=
		    (Elf_Addr)defobj->relocbase;
	}
out:
#else
	dbg(" reloc_jmpslot: where=%p, target=%p", (void *)wherep,
	    (void *)target);

	if (!ld_bind_not)
		*wherep = target;
#endif

	return (target);
}

int
reloc_iresolve(Obj_Entry *obj __unused,
    struct Struct_RtldLockState *lockstate __unused)
{

	/* XXX not implemented */
	return (0);
}

int
reloc_gnu_ifunc(Obj_Entry *obj __unused, int flags __unused,
    struct Struct_RtldLockState *lockstate __unused)
{

	/* XXX not implemented */
	return (0);
}

void
init_pltgot(Obj_Entry *obj)
{
	Elf_Addr *pltcall;

	pltcall = obj->pltgot;

	if (pltcall == NULL) {
		return;
	}

#if defined(_CALL_ELF) && _CALL_ELF == 2
	pltcall[0] = (Elf_Addr)&_rtld_bind_start; 
	pltcall[1] = (Elf_Addr)obj;
#else
	memcpy(pltcall, _rtld_bind_start, sizeof(struct funcdesc));
	pltcall[2] = (Elf_Addr)obj;
#endif
}

void
ifunc_init(Elf_Auxinfo aux_info[__min_size(AT_COUNT)] __unused)
{

}

void
pre_init(void)
{

}

void
allocate_initial_tls(Obj_Entry *list)
{
	Elf_Addr **tp;

	/*
	* Fix the size of the static TLS block by using the maximum
	* offset allocated so far and adding a bit for dynamic modules to
	* use.
	*/

	tls_static_space = tls_last_offset + tls_last_size + RTLD_STATIC_TLS_EXTRA;

	tp = (Elf_Addr **)((char *)allocate_tls(list, NULL, TLS_TCB_SIZE, 16)
	    + TLS_TP_OFFSET + TLS_TCB_SIZE);

	__asm __volatile("mr 13,%0" :: "r"(tp));
}

void*
__tls_get_addr(tls_index* ti)
{
	Elf_Addr **tp;
	char *p;

	__asm __volatile("mr %0,13" : "=r"(tp));
	p = tls_get_addr_common((Elf_Addr**)((Elf_Addr)tp - TLS_TP_OFFSET 
	    - TLS_TCB_SIZE), ti->ti_module, ti->ti_offset);

	return (p + TLS_DTV_OFFSET);
}
