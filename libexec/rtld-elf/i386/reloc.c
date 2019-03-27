/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1996, 1997, 1998, 1999 John D. Polstra.
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
 * $FreeBSD$
 */

/*
 * Dynamic linker for ELF.
 *
 * John Polstra <jdp@polstra.com>.
 */

#include <sys/param.h>
#include <sys/mman.h>
#include <machine/segments.h>
#include <machine/sysarch.h>

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "rtld.h"
#include "rtld_tls.h"

/*
 * Process the special R_386_COPY relocations in the main program.  These
 * copy data from a shared object into a region in the main program's BSS
 * segment.
 *
 * Returns 0 on success, -1 on failure.
 */
int
do_copy_relocations(Obj_Entry *dstobj)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    assert(dstobj->mainprog);	/* COPY relocations are invalid elsewhere */

    rellim = (const Elf_Rel *)((const char *)dstobj->rel + dstobj->relsize);
    for (rel = dstobj->rel;  rel < rellim;  rel++) {
	if (ELF_R_TYPE(rel->r_info) == R_386_COPY) {
	    void *dstaddr;
	    const Elf_Sym *dstsym;
	    const char *name;
	    size_t size;
	    const void *srcaddr;
	    const Elf_Sym *srcsym;
	    const Obj_Entry *srcobj, *defobj;
	    SymLook req;
	    int res;

	    dstaddr = (void *)(dstobj->relocbase + rel->r_offset);
	    dstsym = dstobj->symtab + ELF_R_SYM(rel->r_info);
	    name = dstobj->strtab + dstsym->st_name;
	    size = dstsym->st_size;
	    symlook_init(&req, name);
	    req.ventry = fetch_ventry(dstobj, ELF_R_SYM(rel->r_info));
	    req.flags = SYMLOOK_EARLY;

	    for (srcobj = globallist_next(dstobj);  srcobj != NULL;
	      srcobj = globallist_next(srcobj)) {
		res = symlook_obj(&req, srcobj);
		if (res == 0) {
		    srcsym = req.sym_out;
		    defobj = req.defobj_out;
		    break;
		}
	    }

	    if (srcobj == NULL) {
		_rtld_error("Undefined symbol \"%s\" referenced from COPY"
		  " relocation in %s", name, dstobj->path);
		return -1;
	    }

	    srcaddr = (const void *)(defobj->relocbase + srcsym->st_value);
	    memcpy(dstaddr, srcaddr, size);
	}
    }

    return 0;
}

/* Initialize the special GOT entries. */
void
init_pltgot(Obj_Entry *obj)
{
    if (obj->pltgot != NULL) {
	obj->pltgot[1] = (Elf_Addr) obj;
	obj->pltgot[2] = (Elf_Addr) &_rtld_bind_start;
    }
}

/* Process the non-PLT relocations. */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld, int flags,
    RtldLockState *lockstate)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	SymCache *cache;
	const Elf_Sym *def;
	const Obj_Entry *defobj;
	Elf_Addr *where, symval, add;
	int r;

	r = -1;
	/*
	 * The dynamic loader may be called from a thread, we have
	 * limited amounts of stack available so we cannot use alloca().
	 */
	if (obj != obj_rtld) {
		cache = calloc(obj->dynsymcount, sizeof(SymCache));
		/* No need to check for NULL here */
	} else
		cache = NULL;

	/* Appease some compilers. */
	symval = 0;
	def = NULL;

	rellim = (const Elf_Rel *)((const char *)obj->rel + obj->relsize);
	for (rel = obj->rel;  rel < rellim;  rel++) {
		switch (ELF_R_TYPE(rel->r_info)) {
		case R_386_32:
		case R_386_PC32:
		case R_386_GLOB_DAT:
		case R_386_TLS_TPOFF:
		case R_386_TLS_TPOFF32:
		case R_386_TLS_DTPMOD32:
		case R_386_TLS_DTPOFF32:
			def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
			    flags, cache, lockstate);
			if (def == NULL)
				goto done;
			if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC) {
				switch (ELF_R_TYPE(rel->r_info)) {
				case R_386_32:
				case R_386_PC32:
				case R_386_GLOB_DAT:
					if ((flags & SYMLOOK_IFUNC) == 0) {
						obj->non_plt_gnu_ifunc = true;
						continue;
					}
					symval = (Elf_Addr)rtld_resolve_ifunc(
					    defobj, def);
					break;
				case R_386_TLS_TPOFF:
				case R_386_TLS_TPOFF32:
				case R_386_TLS_DTPMOD32:
				case R_386_TLS_DTPOFF32:
					_rtld_error("%s: IFUNC for TLS reloc",
					    obj->path);
					goto done;
				}
			} else {
				if ((flags & SYMLOOK_IFUNC) != 0)
					continue;
				symval = (Elf_Addr)defobj->relocbase +
				    def->st_value;
			}
			break;
		default:
			if ((flags & SYMLOOK_IFUNC) != 0)
				continue;
			break;
		}
		where = (Elf_Addr *)(obj->relocbase + rel->r_offset);

		switch (ELF_R_TYPE(rel->r_info)) {
		case R_386_NONE:
			break;
		case R_386_32:
			*where += symval;
			break;
		case R_386_PC32:
		    /*
		     * I don't think the dynamic linker should ever
		     * see this type of relocation.  But the
		     * binutils-2.6 tools sometimes generate it.
		     */
		    *where += symval - (Elf_Addr)where;
		    break;
		case R_386_COPY:
			/*
			 * These are deferred until all other
			 * relocations have been done.  All we do here
			 * is make sure that the COPY relocation is
			 * not in a shared library.  They are allowed
			 * only in executable files.
			 */
			if (!obj->mainprog) {
				_rtld_error("%s: Unexpected R_386_COPY "
				    "relocation in shared library", obj->path);
				goto done;
			}
			break;
		case R_386_GLOB_DAT:
			*where = symval;
			break;
		case R_386_RELATIVE:
			*where += (Elf_Addr)obj->relocbase;
			break;
		case R_386_TLS_TPOFF:
		case R_386_TLS_TPOFF32:
			/*
			 * We lazily allocate offsets for static TLS
			 * as we see the first relocation that
			 * references the TLS block. This allows us to
			 * support (small amounts of) static TLS in
			 * dynamically loaded modules. If we run out
			 * of space, we generate an error.
			 */
			if (!defobj->tls_done) {
				if (!allocate_tls_offset(
				    __DECONST(Obj_Entry *, defobj))) {
					_rtld_error("%s: No space available "
					    "for static Thread Local Storage",
					    obj->path);
					goto done;
				}
			}
			add = (Elf_Addr)(def->st_value - defobj->tlsoffset);
			if (ELF_R_TYPE(rel->r_info) == R_386_TLS_TPOFF)
				*where += add;
			else
				*where -= add;
			break;
		case R_386_TLS_DTPMOD32:
			*where += (Elf_Addr)defobj->tlsindex;
			break;
		case R_386_TLS_DTPOFF32:
			*where += (Elf_Addr) def->st_value;
			break;
		default:
			_rtld_error("%s: Unsupported relocation type %d"
			    " in non-PLT relocations\n", obj->path,
			    ELF_R_TYPE(rel->r_info));
			goto done;
		}
	}
	r = 0;
done:
	free(cache);
	return (r);
}

/* Process the PLT relocations. */
int
reloc_plt(Obj_Entry *obj, int flags __unused, RtldLockState *lockstate __unused)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    rellim = (const Elf_Rel *)((const char *)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel;  rel < rellim;  rel++) {
	Elf_Addr *where/*, val*/;

	switch (ELF_R_TYPE(rel->r_info)) {
	case R_386_JMP_SLOT:
	  /* Relocate the GOT slot pointing into the PLT. */
	  where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	  *where += (Elf_Addr)obj->relocbase;
	  break;

	case R_386_IRELATIVE:
	  obj->irelative = true;
	  break;

	default:
	  _rtld_error("Unknown relocation type %x in PLT",
	    ELF_R_TYPE(rel->r_info));
	  return (-1);
	}
    }
    return 0;
}

/* Relocate the jump slots in an object. */
int
reloc_jmpslots(Obj_Entry *obj, int flags, RtldLockState *lockstate)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    if (obj->jmpslots_done)
	return 0;
    rellim = (const Elf_Rel *)((const char *)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel;  rel < rellim;  rel++) {
	Elf_Addr *where, target;
	const Elf_Sym *def;
	const Obj_Entry *defobj;

	switch (ELF_R_TYPE(rel->r_info)) {
	case R_386_JMP_SLOT:
	  where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	  def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
		SYMLOOK_IN_PLT | flags, NULL, lockstate);
	  if (def == NULL)
	      return (-1);
	  if (ELF_ST_TYPE(def->st_info) == STT_GNU_IFUNC) {
	      obj->gnu_ifunc = true;
	      continue;
	  }
	  target = (Elf_Addr)(defobj->relocbase + def->st_value);
	  reloc_jmpslot(where, target, defobj, obj, rel);
	  break;

	case R_386_IRELATIVE:
	  break;

	default:
	  _rtld_error("Unknown relocation type %x in PLT",
	    ELF_R_TYPE(rel->r_info));
	  return (-1);
	}
    }

    obj->jmpslots_done = true;
    return 0;
}

/* Fixup the jump slot at "where" to transfer control to "target". */
Elf_Addr
reloc_jmpslot(Elf_Addr *where, Elf_Addr target,
    const Obj_Entry *obj __unused, const Obj_Entry *refobj __unused,
    const Elf_Rel *rel __unused)
{
#ifdef dbg
	dbg("reloc_jmpslot: *%p = %p", where, (void *)target);
#endif
	if (!ld_bind_not)
		*where = target;
	return (target);
}

int
reloc_iresolve(Obj_Entry *obj, RtldLockState *lockstate)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;
    Elf_Addr *where, target;

    if (!obj->irelative)
	return (0);
    rellim = (const Elf_Rel *)((const char *)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel;  rel < rellim;  rel++) {
	switch (ELF_R_TYPE(rel->r_info)) {
	case R_386_IRELATIVE:
	  where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	  lock_release(rtld_bind_lock, lockstate);
	  target = call_ifunc_resolver(obj->relocbase + *where);
	  wlock_acquire(rtld_bind_lock, lockstate);
	  *where = target;
	  break;
	}
    }
    obj->irelative = false;
    return (0);
}

int
reloc_gnu_ifunc(Obj_Entry *obj, int flags, RtldLockState *lockstate)
{
    const Elf_Rel *rellim;
    const Elf_Rel *rel;

    if (!obj->gnu_ifunc)
	return (0);
    rellim = (const Elf_Rel *)((const char *)obj->pltrel + obj->pltrelsize);
    for (rel = obj->pltrel;  rel < rellim;  rel++) {
	Elf_Addr *where, target;
	const Elf_Sym *def;
	const Obj_Entry *defobj;

	switch (ELF_R_TYPE(rel->r_info)) {
	case R_386_JMP_SLOT:
	  where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	  def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
		SYMLOOK_IN_PLT | flags, NULL, lockstate);
	  if (def == NULL)
	      return (-1);
	  if (ELF_ST_TYPE(def->st_info) != STT_GNU_IFUNC)
	      continue;
	  lock_release(rtld_bind_lock, lockstate);
	  target = (Elf_Addr)rtld_resolve_ifunc(defobj, def);
	  wlock_acquire(rtld_bind_lock, lockstate);
	  reloc_jmpslot(where, target, defobj, obj, rel);
	  break;
	}
    }

    obj->gnu_ifunc = false;
    return (0);
}

uint32_t cpu_feature, cpu_feature2, cpu_stdext_feature, cpu_stdext_feature2;

static void
rtld_cpuid_count(int idx, int cnt, u_int *p)
{

	__asm __volatile(
	    "	pushl	%%ebx\n"
	    "	cpuid\n"
	    "	movl	%%ebx,%1\n"
	    "	popl	%%ebx\n"
	    : "=a" (p[0]), "=r" (p[1]), "=c" (p[2]), "=d" (p[3])
	    :  "0" (idx), "2" (cnt));
}

void
ifunc_init(Elf_Auxinfo aux_info[__min_size(AT_COUNT)] __unused)
{
	u_int p[4], cpu_high;
	int cpuid_supported;

	__asm __volatile(
	    "	pushfl\n"
	    "	popl	%%eax\n"
	    "	movl    %%eax,%%ecx\n"
	    "	xorl    $0x200000,%%eax\n"
	    "	pushl	%%eax\n"
	    "	popfl\n"
	    "	pushfl\n"
	    "	popl    %%eax\n"
	    "	xorl    %%eax,%%ecx\n"
	    "	je	1f\n"
	    "	movl	$1,%0\n"
	    "	jmp	2f\n"
	    "1:	movl	$0,%0\n"
	    "2:\n"
	    : "=r" (cpuid_supported) : : "eax", "ecx");
	if (!cpuid_supported)
		return;

	rtld_cpuid_count(1, 0, p);
	cpu_feature = p[3];
	cpu_feature2 = p[2];
	rtld_cpuid_count(0, 0, p);
	cpu_high = p[0];
	if (cpu_high >= 7) {
		rtld_cpuid_count(7, 0, p);
		cpu_stdext_feature = p[1];
		cpu_stdext_feature2 = p[2];
	}
}

void
pre_init(void)
{

}

void
allocate_initial_tls(Obj_Entry *objs)
{
    void* tls;

    /*
     * Fix the size of the static TLS block by using the maximum
     * offset allocated so far and adding a bit for dynamic modules to
     * use.
     */
    tls_static_space = tls_last_offset + RTLD_STATIC_TLS_EXTRA;
    tls = allocate_tls(objs, NULL, 3*sizeof(Elf_Addr), sizeof(Elf_Addr));
    i386_set_gsbase(tls);
}

/* GNU ABI */
__attribute__((__regparm__(1)))
void *___tls_get_addr(tls_index *ti)
{
    Elf_Addr** segbase;

    __asm __volatile("movl %%gs:0, %0" : "=r" (segbase));

    return tls_get_addr_common(&segbase[1], ti->ti_module, ti->ti_offset);
}

/* Sun ABI */
void *__tls_get_addr(tls_index *ti)
{
    Elf_Addr** segbase;

    __asm __volatile("movl %%gs:0, %0" : "=r" (segbase));

    return tls_get_addr_common(&segbase[1], ti->ti_module, ti->ti_offset);
}
