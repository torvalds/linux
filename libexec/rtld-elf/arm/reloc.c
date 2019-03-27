/*	$NetBSD: mdreloc.c,v 1.23 2003/07/26 15:04:38 mrg Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "machine/sysarch.h"

#include "debug.h"
#include "rtld.h"
#include "paths.h"

#ifdef __ARM_FP
/*
 * On processors that have hard floating point supported, we also support
 * running soft float binaries. If we're being built with hard float support,
 * check the ELF headers to make sure that this is a hard float binary. If it is
 * a soft float binary, force the dynamic linker to use the alternative soft
 * float path.
 */
void
arm_abi_variant_hook(Elf_Auxinfo **aux_info)
{
	Elf_Word ehdr;

	/*
	 * If we're running an old kernel that doesn't provide any data fail
	 * safe by doing nothing.
	 */
	if (aux_info[AT_EHDRFLAGS] == NULL)
		return;
	ehdr = aux_info[AT_EHDRFLAGS]->a_un.a_val;

	/*
	 * Hard float ABI binaries are the default, and use the default paths
	 * and such.
	 */
	if ((ehdr & EF_ARM_VFP_FLOAT) != 0)
		return;

	/*
	 * This is a soft float ABI binary. We need to use the soft float
	 * settings.
	 */
	ld_elf_hints_default = _PATH_SOFT_ELF_HINTS;
	ld_path_libmap_conf = _PATH_SOFT_LIBMAP_CONF;
	ld_path_rtld = _PATH_SOFT_RTLD;
	ld_standard_library_path = SOFT_STANDARD_LIBRARY_PATH;
	ld_env_prefix = LD_SOFT_;
}
#endif

void
init_pltgot(Obj_Entry *obj)
{       
	if (obj->pltgot != NULL) {
		obj->pltgot[1] = (Elf_Addr) obj;
		obj->pltgot[2] = (Elf_Addr) &_rtld_bind_start;
	}
}

int             
do_copy_relocations(Obj_Entry *dstobj)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;

	assert(dstobj->mainprog);	/* COPY relocations are invalid elsewhere */

	rellim = (const Elf_Rel *)((const char *) dstobj->rel + dstobj->relsize);
	for (rel = dstobj->rel;  rel < rellim;  rel++) {
		if (ELF_R_TYPE(rel->r_info) == R_ARM_COPY) {
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
			req.ventry = fetch_ventry(dstobj,
			    ELF_R_SYM(rel->r_info));
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
				_rtld_error(
"Undefined symbol \"%s\" referenced from COPY relocation in %s",
				    name, dstobj->path);
				return (-1);
			}
			
			srcaddr = (const void *)(defobj->relocbase +
			    srcsym->st_value);
			memcpy(dstaddr, srcaddr, size);
		}
	}
	return 0;			     
}

void _rtld_bind_start(void);
void _rtld_relocate_nonplt_self(Elf_Dyn *, Elf_Addr);

void
_rtld_relocate_nonplt_self(Elf_Dyn *dynp, Elf_Addr relocbase)
{
	const Elf_Rel *rel = NULL, *rellim;
	Elf_Addr relsz = 0;
	Elf_Addr *where;
	uint32_t size;

	for (; dynp->d_tag != DT_NULL; dynp++) {
		switch (dynp->d_tag) {
		case DT_REL:
			rel = (const Elf_Rel *)(relocbase + dynp->d_un.d_ptr);
			break;
		case DT_RELSZ:
			relsz = dynp->d_un.d_val;
			break;
		}
	}
	rellim = (const Elf_Rel *)((const char *)rel + relsz);
	size = (rellim - 1)->r_offset - rel->r_offset;
	for (; rel < rellim; rel++) {
		where = (Elf_Addr *)(relocbase + rel->r_offset);
		
		*where += (Elf_Addr)relocbase;
	}
}
/*
 * It is possible for the compiler to emit relocations for unaligned data.
 * We handle this situation with these inlines.
 */
#define	RELOC_ALIGNED_P(x) \
	(((uintptr_t)(x) & (sizeof(void *) - 1)) == 0)

static __inline Elf_Addr
load_ptr(void *where)
{
	Elf_Addr res;

	memcpy(&res, where, sizeof(res));

	return (res);
}

static __inline void
store_ptr(void *where, Elf_Addr val)
{

	memcpy(where, &val, sizeof(val));
}

static int
reloc_nonplt_object(Obj_Entry *obj, const Elf_Rel *rel, SymCache *cache,
    int flags, RtldLockState *lockstate)
{
	Elf_Addr        *where;
	const Elf_Sym   *def;
	const Obj_Entry *defobj;
	Elf_Addr         tmp;
	unsigned long	 symnum;

	where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
	symnum = ELF_R_SYM(rel->r_info);

	switch (ELF_R_TYPE(rel->r_info)) {
	case R_ARM_NONE:
		break;
		
#if 1 /* XXX should not occur */
	case R_ARM_PC24: {	/* word32 S - P + A */
		Elf32_Sword addend;
		
		/*
		 * Extract addend and sign-extend if needed.
		 */
		addend = *where;
		if (addend & 0x00800000)
			addend |= 0xff000000;
		
		def = find_symdef(symnum, obj, &defobj, flags, cache,
		    lockstate);
		if (def == NULL)
				return -1;
			tmp = (Elf_Addr)obj->relocbase + def->st_value
			    - (Elf_Addr)where + (addend << 2);
			if ((tmp & 0xfe000000) != 0xfe000000 &&
			    (tmp & 0xfe000000) != 0) {
				_rtld_error(
				"%s: R_ARM_PC24 relocation @ %p to %s failed "
				"(displacement %ld (%#lx) out of range)",
				    obj->path, where,
				    obj->strtab + obj->symtab[symnum].st_name,
				    (long) tmp, (long) tmp);
				return -1;
			}
			tmp >>= 2;
			*where = (*where & 0xff000000) | (tmp & 0x00ffffff);
			dbg("PC24 %s in %s --> %p @ %p in %s",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)*where, where, defobj->path);
			break;
		}
#endif

		case R_ARM_ABS32:	/* word32 B + S + A */
		case R_ARM_GLOB_DAT:	/* word32 B + S */
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return -1;
			if (__predict_true(RELOC_ALIGNED_P(where))) {
				tmp =  *where + (Elf_Addr)defobj->relocbase +
				    def->st_value;
				*where = tmp;
			} else {
				tmp = load_ptr(where) +
				    (Elf_Addr)defobj->relocbase +
				    def->st_value;
				store_ptr(where, tmp);
			}
			dbg("ABS32/GLOB_DAT %s in %s --> %p @ %p in %s",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)tmp, where, defobj->path);
			break;

		case R_ARM_RELATIVE:	/* word32 B + A */
			if (__predict_true(RELOC_ALIGNED_P(where))) {
				tmp = *where + (Elf_Addr)obj->relocbase;
				*where = tmp;
			} else {
				tmp = load_ptr(where) +
				    (Elf_Addr)obj->relocbase;
				store_ptr(where, tmp);
			}
			dbg("RELATIVE in %s --> %p", obj->path,
			    (void *)tmp);
			break;

		case R_ARM_COPY:
			/*
			 * These are deferred until all other relocations have
			 * been done.  All we do here is make sure that the
			 * COPY relocation is not in a shared library.  They
			 * are allowed only in executable files.
			 */
			if (!obj->mainprog) {
				_rtld_error(
			"%s: Unexpected R_COPY relocation in shared library",
				    obj->path);
				return -1;
			}
			dbg("COPY (avoid in main)");
			break;

		case R_ARM_TLS_DTPOFF32:
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return -1;

			tmp = (Elf_Addr)(def->st_value);
			if (__predict_true(RELOC_ALIGNED_P(where)))
				*where = tmp;
			else
				store_ptr(where, tmp);

			dbg("TLS_DTPOFF32 %s in %s --> %p",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)tmp);

			break;
		case R_ARM_TLS_DTPMOD32:
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return -1;

			tmp = (Elf_Addr)(defobj->tlsindex);
			if (__predict_true(RELOC_ALIGNED_P(where)))
				*where = tmp;
			else
				store_ptr(where, tmp);

			dbg("TLS_DTPMOD32 %s in %s --> %p",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)tmp);

			break;

		case R_ARM_TLS_TPOFF32:
			def = find_symdef(symnum, obj, &defobj, flags, cache,
			    lockstate);
			if (def == NULL)
				return -1;

			if (!defobj->tls_done && allocate_tls_offset(obj))
				return -1;

			tmp = (Elf_Addr)def->st_value + defobj->tlsoffset;
			if (__predict_true(RELOC_ALIGNED_P(where)))
				*where = tmp;
			else
				store_ptr(where, tmp);
			dbg("TLS_TPOFF32 %s in %s --> %p",
			    obj->strtab + obj->symtab[symnum].st_name,
			    obj->path, (void *)tmp);
			break;


		default:
			dbg("sym = %lu, type = %lu, offset = %p, "
			    "contents = %p, symbol = %s",
			    symnum, (u_long)ELF_R_TYPE(rel->r_info),
			    (void *)rel->r_offset, (void *)load_ptr(where),
			    obj->strtab + obj->symtab[symnum].st_name);
			_rtld_error("%s: Unsupported relocation type %ld "
			    "in non-PLT relocations\n",
			    obj->path, (u_long) ELF_R_TYPE(rel->r_info));
			return -1;
	}
	return 0;
}

/*
 *  * Process non-PLT relocations
 *   */
int
reloc_non_plt(Obj_Entry *obj, Obj_Entry *obj_rtld, int flags,
    RtldLockState *lockstate)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	SymCache *cache;
	int r = -1;
	
	/* The relocation for the dynamic loader has already been done. */
	if (obj == obj_rtld)
		return (0);
	if ((flags & SYMLOOK_IFUNC) != 0)
		/* XXX not implemented */
		return (0);

	/*
 	 * The dynamic loader may be called from a thread, we have
	 * limited amounts of stack available so we cannot use alloca().
	 */
	cache = calloc(obj->dynsymcount, sizeof(SymCache));
	/* No need to check for NULL here */

	rellim = (const Elf_Rel *)((const char *)obj->rel + obj->relsize);
	for (rel = obj->rel; rel < rellim; rel++) {
		if (reloc_nonplt_object(obj, rel, cache, flags, lockstate) < 0)
			goto done;
	}
	r = 0;
done:
	if (cache != NULL)
		free(cache);
	return (r);
}

/*
 *  * Process the PLT relocations.
 *   */
int
reloc_plt(Obj_Entry *obj, int flags __unused, RtldLockState *lockstate __unused)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
		
	rellim = (const Elf_Rel *)((const char *)obj->pltrel +
	    obj->pltrelsize);
	for (rel = obj->pltrel;  rel < rellim;  rel++) {
		Elf_Addr *where;

		assert(ELF_R_TYPE(rel->r_info) == R_ARM_JUMP_SLOT);
		
		where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
		*where += (Elf_Addr )obj->relocbase;
	}
	
	return (0);
}

/*
 *  * LD_BIND_NOW was set - force relocation for all jump slots
 *   */
int
reloc_jmpslots(Obj_Entry *obj, int flags, RtldLockState *lockstate)
{
	const Obj_Entry *defobj;
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Sym *def;
	Elf_Addr *where;
	Elf_Addr target;
	
	rellim = (const Elf_Rel *)((const char *)obj->pltrel + obj->pltrelsize);
	for (rel = obj->pltrel; rel < rellim; rel++) {
		assert(ELF_R_TYPE(rel->r_info) == R_ARM_JUMP_SLOT);
		where = (Elf_Addr *)(obj->relocbase + rel->r_offset);
		def = find_symdef(ELF_R_SYM(rel->r_info), obj, &defobj,
		    SYMLOOK_IN_PLT | flags, NULL, lockstate);
		if (def == NULL) {
			dbg("reloc_jmpslots: sym not found");
			return (-1);
		}
		
		target = (Elf_Addr)(defobj->relocbase + def->st_value);		
		reloc_jmpslot(where, target, defobj, obj,
		    (const Elf_Rel *) rel);
	}
	
	obj->jmpslots_done = true;
	
	return (0);
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

Elf_Addr
reloc_jmpslot(Elf_Addr *where, Elf_Addr target,
    const Obj_Entry *defobj __unused, const Obj_Entry *obj __unused,
    const Elf_Rel *rel)
{

	assert(ELF_R_TYPE(rel->r_info) == R_ARM_JUMP_SLOT);

	if (*where != target && !ld_bind_not)
		*where = target;
	return (target);
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
allocate_initial_tls(Obj_Entry *objs)
{
#ifdef ARM_TP_ADDRESS
	void **_tp = (void **)ARM_TP_ADDRESS;
#endif

	/*
	* Fix the size of the static TLS block by using the maximum
	* offset allocated so far and adding a bit for dynamic modules to
	* use.
	*/

	tls_static_space = tls_last_offset + tls_last_size + RTLD_STATIC_TLS_EXTRA;

#ifdef ARM_TP_ADDRESS
	(*_tp) = (void *) allocate_tls(objs, NULL, TLS_TCB_SIZE, 8);
#else
	sysarch(ARM_SET_TP, allocate_tls(objs, NULL, TLS_TCB_SIZE, 8));
#endif
}

void *
__tls_get_addr(tls_index* ti)
{
	char *p;
#ifdef ARM_TP_ADDRESS
	void **_tp = (void **)ARM_TP_ADDRESS;

	p = tls_get_addr_common((Elf_Addr **)(*_tp), ti->ti_module, ti->ti_offset);
#else
	void *_tp;
	__asm __volatile("mrc  p15, 0, %0, c13, c0, 3"		\
	    : "=r" (_tp));
	p = tls_get_addr_common((Elf_Addr **)(_tp), ti->ti_module, ti->ti_offset);
#endif

	return (p);
}
