/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998-2000 Doug Rabson
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

#include "opt_ddb.h"
#include "opt_gdb.h"

#include <sys/param.h>
#include <sys/systm.h>
#ifdef GPROF
#include <sys/gmon.h>
#endif
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mount.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/linker.h>

#include <machine/elf.h>

#include <net/vnet.h>

#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#ifdef SPARSE_MAPPING
#include <vm/vm_object.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#endif
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/link_elf.h>

#ifdef DDB_CTF
#include <sys/zlib.h>
#endif

#include "linker_if.h"

#define MAXSEGS 4

typedef struct elf_file {
	struct linker_file lf;		/* Common fields */
	int		preloaded;	/* Was file pre-loaded */
	caddr_t		address;	/* Relocation address */
#ifdef SPARSE_MAPPING
	vm_object_t	object;		/* VM object to hold file pages */
#endif
	Elf_Dyn		*dynamic;	/* Symbol table etc. */
	Elf_Hashelt	nbuckets;	/* DT_HASH info */
	Elf_Hashelt	nchains;
	const Elf_Hashelt *buckets;
	const Elf_Hashelt *chains;
	caddr_t		hash;
	caddr_t		strtab;		/* DT_STRTAB */
	int		strsz;		/* DT_STRSZ */
	const Elf_Sym	*symtab;		/* DT_SYMTAB */
	Elf_Addr	*got;		/* DT_PLTGOT */
	const Elf_Rel	*pltrel;	/* DT_JMPREL */
	int		pltrelsize;	/* DT_PLTRELSZ */
	const Elf_Rela	*pltrela;	/* DT_JMPREL */
	int		pltrelasize;	/* DT_PLTRELSZ */
	const Elf_Rel	*rel;		/* DT_REL */
	int		relsize;	/* DT_RELSZ */
	const Elf_Rela	*rela;		/* DT_RELA */
	int		relasize;	/* DT_RELASZ */
	caddr_t		modptr;
	const Elf_Sym	*ddbsymtab;	/* The symbol table we are using */
	long		ddbsymcnt;	/* Number of symbols */
	caddr_t		ddbstrtab;	/* String table */
	long		ddbstrcnt;	/* number of bytes in string table */
	caddr_t		symbase;	/* malloc'ed symbold base */
	caddr_t		strbase;	/* malloc'ed string base */
	caddr_t		ctftab;		/* CTF table */
	long		ctfcnt;		/* number of bytes in CTF table */
	caddr_t		ctfoff;		/* CTF offset table */
	caddr_t		typoff;		/* Type offset table */
	long		typlen;		/* Number of type entries. */
	Elf_Addr	pcpu_start;	/* Pre-relocation pcpu set start. */
	Elf_Addr	pcpu_stop;	/* Pre-relocation pcpu set stop. */
	Elf_Addr	pcpu_base;	/* Relocated pcpu set address. */
#ifdef VIMAGE
	Elf_Addr	vnet_start;	/* Pre-relocation vnet set start. */
	Elf_Addr	vnet_stop;	/* Pre-relocation vnet set stop. */
	Elf_Addr	vnet_base;	/* Relocated vnet set address. */
#endif
#ifdef GDB
	struct link_map	gdb;		/* hooks for gdb */
#endif
} *elf_file_t;

struct elf_set {
	Elf_Addr	es_start;
	Elf_Addr	es_stop;
	Elf_Addr	es_base;
	TAILQ_ENTRY(elf_set)	es_link;
};

TAILQ_HEAD(elf_set_head, elf_set);

#include <kern/kern_ctf.c>

static int	link_elf_link_common_finish(linker_file_t);
static int	link_elf_link_preload(linker_class_t cls,
				      const char *, linker_file_t *);
static int	link_elf_link_preload_finish(linker_file_t);
static int	link_elf_load_file(linker_class_t, const char *,
		    linker_file_t *);
static int	link_elf_lookup_symbol(linker_file_t, const char *,
		    c_linker_sym_t *);
static int	link_elf_symbol_values(linker_file_t, c_linker_sym_t,
		    linker_symval_t *);
static int	link_elf_search_symbol(linker_file_t, caddr_t,
		    c_linker_sym_t *, long *);

static void	link_elf_unload_file(linker_file_t);
static void	link_elf_unload_preload(linker_file_t);
static int	link_elf_lookup_set(linker_file_t, const char *,
		    void ***, void ***, int *);
static int	link_elf_each_function_name(linker_file_t,
		    int (*)(const char *, void *), void *);
static int	link_elf_each_function_nameval(linker_file_t,
		    linker_function_nameval_callback_t, void *);
static void	link_elf_reloc_local(linker_file_t);
static long	link_elf_symtab_get(linker_file_t, const Elf_Sym **);
static long	link_elf_strtab_get(linker_file_t, caddr_t *);
static int	elf_lookup(linker_file_t, Elf_Size, int, Elf_Addr *);

static kobj_method_t link_elf_methods[] = {
	KOBJMETHOD(linker_lookup_symbol,	link_elf_lookup_symbol),
	KOBJMETHOD(linker_symbol_values,	link_elf_symbol_values),
	KOBJMETHOD(linker_search_symbol,	link_elf_search_symbol),
	KOBJMETHOD(linker_unload,		link_elf_unload_file),
	KOBJMETHOD(linker_load_file,		link_elf_load_file),
	KOBJMETHOD(linker_link_preload,		link_elf_link_preload),
	KOBJMETHOD(linker_link_preload_finish,	link_elf_link_preload_finish),
	KOBJMETHOD(linker_lookup_set,		link_elf_lookup_set),
	KOBJMETHOD(linker_each_function_name,	link_elf_each_function_name),
	KOBJMETHOD(linker_each_function_nameval, link_elf_each_function_nameval),
	KOBJMETHOD(linker_ctf_get,		link_elf_ctf_get),
	KOBJMETHOD(linker_symtab_get,		link_elf_symtab_get),
	KOBJMETHOD(linker_strtab_get,		link_elf_strtab_get),
	{ 0, 0 }
};

static struct linker_class link_elf_class = {
#if ELF_TARG_CLASS == ELFCLASS32
	"elf32",
#else
	"elf64",
#endif
	link_elf_methods, sizeof(struct elf_file)
};

typedef int (*elf_reloc_fn)(linker_file_t lf, Elf_Addr relocbase,
    const void *data, int type, elf_lookup_fn lookup);

static int	parse_dynamic(elf_file_t);
static int	relocate_file(elf_file_t);
static int	relocate_file1(elf_file_t ef, elf_lookup_fn lookup,
		    elf_reloc_fn reloc, bool ifuncs);
static int	link_elf_preload_parse_symbols(elf_file_t);

static struct elf_set_head set_pcpu_list;
#ifdef VIMAGE
static struct elf_set_head set_vnet_list;
#endif

static void
elf_set_add(struct elf_set_head *list, Elf_Addr start, Elf_Addr stop, Elf_Addr base)
{
	struct elf_set *set, *iter;

	set = malloc(sizeof(*set), M_LINKER, M_WAITOK);
	set->es_start = start;
	set->es_stop = stop;
	set->es_base = base;

	TAILQ_FOREACH(iter, list, es_link) {

		KASSERT((set->es_start < iter->es_start && set->es_stop < iter->es_stop) ||
		    (set->es_start > iter->es_start && set->es_stop > iter->es_stop),
		    ("linker sets intersection: to insert: 0x%jx-0x%jx; inserted: 0x%jx-0x%jx",
		    (uintmax_t)set->es_start, (uintmax_t)set->es_stop,
		    (uintmax_t)iter->es_start, (uintmax_t)iter->es_stop));

		if (iter->es_start > set->es_start) {
			TAILQ_INSERT_BEFORE(iter, set, es_link);
			break;
		}
	}

	if (iter == NULL)
		TAILQ_INSERT_TAIL(list, set, es_link);
}

static int
elf_set_find(struct elf_set_head *list, Elf_Addr addr, Elf_Addr *start, Elf_Addr *base)
{
	struct elf_set *set;

	TAILQ_FOREACH(set, list, es_link) {
		if (addr < set->es_start)
			return (0);
		if (addr < set->es_stop) {
			*start = set->es_start;
			*base = set->es_base;
			return (1);
		}
	}

	return (0);
}

static void
elf_set_delete(struct elf_set_head *list, Elf_Addr start)
{
	struct elf_set *set;

	TAILQ_FOREACH(set, list, es_link) {
		if (start < set->es_start)
			break;
		if (start == set->es_start) {
			TAILQ_REMOVE(list, set, es_link);
			free(set, M_LINKER);
			return;
		}
	}
	KASSERT(0, ("deleting unknown linker set (start = 0x%jx)",
	    (uintmax_t)start));
}

#ifdef GDB
static void	r_debug_state(struct r_debug *, struct link_map *);

/*
 * A list of loaded modules for GDB to use for loading symbols.
 */
struct r_debug r_debug;

#define GDB_STATE(s) do {				\
	r_debug.r_state = s; r_debug_state(NULL, NULL);	\
} while (0)

/*
 * Function for the debugger to set a breakpoint on to gain control.
 */
static void
r_debug_state(struct r_debug *dummy_one __unused,
	      struct link_map *dummy_two __unused)
{
}

static void
link_elf_add_gdb(struct link_map *l)
{
	struct link_map *prev;

	l->l_next = NULL;

	if (r_debug.r_map == NULL) {
		/* Add first. */
		l->l_prev = NULL;
		r_debug.r_map = l;
	} else {
		/* Append to list. */
		for (prev = r_debug.r_map;
		    prev->l_next != NULL;
		    prev = prev->l_next)
			;
		l->l_prev = prev;
		prev->l_next = l;
	}
}

static void
link_elf_delete_gdb(struct link_map *l)
{
	if (l->l_prev == NULL) {
		/* Remove first. */
		if ((r_debug.r_map = l->l_next) != NULL)
			l->l_next->l_prev = NULL;
	} else {
		/* Remove any but first. */
		if ((l->l_prev->l_next = l->l_next) != NULL)
			l->l_next->l_prev = l->l_prev;
	}
}
#endif /* GDB */

/*
 * The kernel symbol table starts here.
 */
extern struct _dynamic _DYNAMIC;

static void
link_elf_error(const char *filename, const char *s)
{
	if (filename == NULL)
		printf("kldload: %s\n", s);
	else
		printf("kldload: %s: %s\n", filename, s);
}

static void
link_elf_invoke_ctors(caddr_t addr, size_t size)
{
	void (**ctor)(void);
	size_t i, cnt;

	if (addr == NULL || size == 0)
		return;
	cnt = size / sizeof(*ctor);
	ctor = (void *)addr;
	for (i = 0; i < cnt; i++) {
		if (ctor[i] != NULL)
			(*ctor[i])();
	}
}

/*
 * Actions performed after linking/loading both the preloaded kernel and any
 * modules; whether preloaded or dynamicly loaded.
 */
static int
link_elf_link_common_finish(linker_file_t lf)
{
#ifdef GDB
	elf_file_t ef = (elf_file_t)lf;
	char *newfilename;
#endif
	int error;

	/* Notify MD code that a module is being loaded. */
	error = elf_cpu_load_file(lf);
	if (error != 0)
		return (error);

#ifdef GDB
	GDB_STATE(RT_ADD);
	ef->gdb.l_addr = lf->address;
	newfilename = malloc(strlen(lf->filename) + 1, M_LINKER, M_WAITOK);
	strcpy(newfilename, lf->filename);
	ef->gdb.l_name = newfilename;
	ef->gdb.l_ld = ef->dynamic;
	link_elf_add_gdb(&ef->gdb);
	GDB_STATE(RT_CONSISTENT);
#endif

	/* Invoke .ctors */
	link_elf_invoke_ctors(lf->ctors_addr, lf->ctors_size);
	return (0);
}

extern vm_offset_t __startkernel, __endkernel;

static void
link_elf_init(void* arg)
{
	Elf_Dyn *dp;
	Elf_Addr *ctors_addrp;
	Elf_Size *ctors_sizep;
	caddr_t modptr, baseptr, sizeptr;
	elf_file_t ef;
	char *modname;

	linker_add_class(&link_elf_class);

	dp = (Elf_Dyn *)&_DYNAMIC;
	modname = NULL;
	modptr = preload_search_by_type("elf" __XSTRING(__ELF_WORD_SIZE) " kernel");
	if (modptr == NULL)
		modptr = preload_search_by_type("elf kernel");
	modname = (char *)preload_search_info(modptr, MODINFO_NAME);
	if (modname == NULL)
		modname = "kernel";
	linker_kernel_file = linker_make_file(modname, &link_elf_class);
	if (linker_kernel_file == NULL)
		panic("%s: Can't create linker structures for kernel",
		    __func__);

	ef = (elf_file_t) linker_kernel_file;
	ef->preloaded = 1;
#ifdef __powerpc__
	ef->address = (caddr_t) (__startkernel - KERNBASE);
#else
	ef->address = 0;
#endif
#ifdef SPARSE_MAPPING
	ef->object = 0;
#endif
	ef->dynamic = dp;

	if (dp != NULL)
		parse_dynamic(ef);
#ifdef __powerpc__
	linker_kernel_file->address = (caddr_t)__startkernel;
	linker_kernel_file->size = (intptr_t)(__endkernel - __startkernel);
#else
	linker_kernel_file->address += KERNBASE;
	linker_kernel_file->size = -(intptr_t)linker_kernel_file->address;
#endif

	if (modptr != NULL) {
		ef->modptr = modptr;
		baseptr = preload_search_info(modptr, MODINFO_ADDR);
		if (baseptr != NULL)
			linker_kernel_file->address = *(caddr_t *)baseptr;
		sizeptr = preload_search_info(modptr, MODINFO_SIZE);
		if (sizeptr != NULL)
			linker_kernel_file->size = *(size_t *)sizeptr;
		ctors_addrp = (Elf_Addr *)preload_search_info(modptr,
			MODINFO_METADATA | MODINFOMD_CTORS_ADDR);
		ctors_sizep = (Elf_Size *)preload_search_info(modptr,
			MODINFO_METADATA | MODINFOMD_CTORS_SIZE);
		if (ctors_addrp != NULL && ctors_sizep != NULL) {
			linker_kernel_file->ctors_addr = ef->address +
			    *ctors_addrp;
			linker_kernel_file->ctors_size = *ctors_sizep;
		}
	}
	(void)link_elf_preload_parse_symbols(ef);

#ifdef GDB
	r_debug.r_map = NULL;
	r_debug.r_brk = r_debug_state;
	r_debug.r_state = RT_CONSISTENT;
#endif

	(void)link_elf_link_common_finish(linker_kernel_file);
	linker_kernel_file->flags |= LINKER_FILE_LINKED;
	TAILQ_INIT(&set_pcpu_list);
#ifdef VIMAGE
	TAILQ_INIT(&set_vnet_list);
#endif
}

SYSINIT(link_elf, SI_SUB_KLD, SI_ORDER_THIRD, link_elf_init, NULL);

static int
link_elf_preload_parse_symbols(elf_file_t ef)
{
	caddr_t pointer;
	caddr_t ssym, esym, base;
	caddr_t strtab;
	int strcnt;
	Elf_Sym *symtab;
	int symcnt;

	if (ef->modptr == NULL)
		return (0);
	pointer = preload_search_info(ef->modptr,
	    MODINFO_METADATA | MODINFOMD_SSYM);
	if (pointer == NULL)
		return (0);
	ssym = *(caddr_t *)pointer;
	pointer = preload_search_info(ef->modptr,
	    MODINFO_METADATA | MODINFOMD_ESYM);
	if (pointer == NULL)
		return (0);
	esym = *(caddr_t *)pointer;

	base = ssym;

	symcnt = *(long *)base;
	base += sizeof(long);
	symtab = (Elf_Sym *)base;
	base += roundup(symcnt, sizeof(long));

	if (base > esym || base < ssym) {
		printf("Symbols are corrupt!\n");
		return (EINVAL);
	}

	strcnt = *(long *)base;
	base += sizeof(long);
	strtab = base;
	base += roundup(strcnt, sizeof(long));

	if (base > esym || base < ssym) {
		printf("Symbols are corrupt!\n");
		return (EINVAL);
	}

	ef->ddbsymtab = symtab;
	ef->ddbsymcnt = symcnt / sizeof(Elf_Sym);
	ef->ddbstrtab = strtab;
	ef->ddbstrcnt = strcnt;

	return (0);
}

static int
parse_dynamic(elf_file_t ef)
{
	Elf_Dyn *dp;
	int plttype = DT_REL;

	for (dp = ef->dynamic; dp->d_tag != DT_NULL; dp++) {
		switch (dp->d_tag) {
		case DT_HASH:
		{
			/* From src/libexec/rtld-elf/rtld.c */
			const Elf_Hashelt *hashtab = (const Elf_Hashelt *)
			    (ef->address + dp->d_un.d_ptr);
			ef->nbuckets = hashtab[0];
			ef->nchains = hashtab[1];
			ef->buckets = hashtab + 2;
			ef->chains = ef->buckets + ef->nbuckets;
			break;
		}
		case DT_STRTAB:
			ef->strtab = (caddr_t) (ef->address + dp->d_un.d_ptr);
			break;
		case DT_STRSZ:
			ef->strsz = dp->d_un.d_val;
			break;
		case DT_SYMTAB:
			ef->symtab = (Elf_Sym*) (ef->address + dp->d_un.d_ptr);
			break;
		case DT_SYMENT:
			if (dp->d_un.d_val != sizeof(Elf_Sym))
				return (ENOEXEC);
			break;
		case DT_PLTGOT:
			ef->got = (Elf_Addr *) (ef->address + dp->d_un.d_ptr);
			break;
		case DT_REL:
			ef->rel = (const Elf_Rel *) (ef->address + dp->d_un.d_ptr);
			break;
		case DT_RELSZ:
			ef->relsize = dp->d_un.d_val;
			break;
		case DT_RELENT:
			if (dp->d_un.d_val != sizeof(Elf_Rel))
				return (ENOEXEC);
			break;
		case DT_JMPREL:
			ef->pltrel = (const Elf_Rel *) (ef->address + dp->d_un.d_ptr);
			break;
		case DT_PLTRELSZ:
			ef->pltrelsize = dp->d_un.d_val;
			break;
		case DT_RELA:
			ef->rela = (const Elf_Rela *) (ef->address + dp->d_un.d_ptr);
			break;
		case DT_RELASZ:
			ef->relasize = dp->d_un.d_val;
			break;
		case DT_RELAENT:
			if (dp->d_un.d_val != sizeof(Elf_Rela))
				return (ENOEXEC);
			break;
		case DT_PLTREL:
			plttype = dp->d_un.d_val;
			if (plttype != DT_REL && plttype != DT_RELA)
				return (ENOEXEC);
			break;
#ifdef GDB
		case DT_DEBUG:
			dp->d_un.d_ptr = (Elf_Addr)&r_debug;
			break;
#endif
		}
	}

	if (plttype == DT_RELA) {
		ef->pltrela = (const Elf_Rela *)ef->pltrel;
		ef->pltrel = NULL;
		ef->pltrelasize = ef->pltrelsize;
		ef->pltrelsize = 0;
	}

	ef->ddbsymtab = ef->symtab;
	ef->ddbsymcnt = ef->nchains;
	ef->ddbstrtab = ef->strtab;
	ef->ddbstrcnt = ef->strsz;

	return (0);
}

static int
parse_dpcpu(elf_file_t ef)
{
	int error, size;

	ef->pcpu_start = 0;
	ef->pcpu_stop = 0;
	error = link_elf_lookup_set(&ef->lf, "pcpu", (void ***)&ef->pcpu_start,
	    (void ***)&ef->pcpu_stop, NULL);
	/* Error just means there is no pcpu set to relocate. */
	if (error != 0)
		return (0);
	size = (uintptr_t)ef->pcpu_stop - (uintptr_t)ef->pcpu_start;
	/* Empty set? */
	if (size < 1)
		return (0);
	/*
	 * Allocate space in the primary pcpu area.  Copy in our
	 * initialization from the data section and then initialize
	 * all per-cpu storage from that.
	 */
	ef->pcpu_base = (Elf_Addr)(uintptr_t)dpcpu_alloc(size);
	if (ef->pcpu_base == 0) {
		printf("%s: pcpu module space is out of space; "
		    "cannot allocate %d for %s\n",
		    __func__, size, ef->lf.pathname);
		return (ENOSPC);
	}
	memcpy((void *)ef->pcpu_base, (void *)ef->pcpu_start, size);
	dpcpu_copy((void *)ef->pcpu_base, size);
	elf_set_add(&set_pcpu_list, ef->pcpu_start, ef->pcpu_stop,
	    ef->pcpu_base);

	return (0);
}

#ifdef VIMAGE
static int
parse_vnet(elf_file_t ef)
{
	int error, size;

	ef->vnet_start = 0;
	ef->vnet_stop = 0;
	error = link_elf_lookup_set(&ef->lf, "vnet", (void ***)&ef->vnet_start,
	    (void ***)&ef->vnet_stop, NULL);
	/* Error just means there is no vnet data set to relocate. */
	if (error != 0)
		return (0);
	size = (uintptr_t)ef->vnet_stop - (uintptr_t)ef->vnet_start;
	/* Empty set? */
	if (size < 1)
		return (0);
	/*
	 * Allocate space in the primary vnet area.  Copy in our
	 * initialization from the data section and then initialize
	 * all per-vnet storage from that.
	 */
	ef->vnet_base = (Elf_Addr)(uintptr_t)vnet_data_alloc(size);
	if (ef->vnet_base == 0) {
		printf("%s: vnet module space is out of space; "
		    "cannot allocate %d for %s\n",
		    __func__, size, ef->lf.pathname);
		return (ENOSPC);
	}
	memcpy((void *)ef->vnet_base, (void *)ef->vnet_start, size);
	vnet_data_copy((void *)ef->vnet_base, size);
	elf_set_add(&set_vnet_list, ef->vnet_start, ef->vnet_stop,
	    ef->vnet_base);

	return (0);
}
#endif

static int
link_elf_link_preload(linker_class_t cls,
    const char* filename, linker_file_t *result)
{
	Elf_Addr *ctors_addrp;
	Elf_Size *ctors_sizep;
	caddr_t modptr, baseptr, sizeptr, dynptr;
	char *type;
	elf_file_t ef;
	linker_file_t lf;
	int error;
	vm_offset_t dp;

	/* Look to see if we have the file preloaded */
	modptr = preload_search_by_name(filename);
	if (modptr == NULL)
		return (ENOENT);

	type = (char *)preload_search_info(modptr, MODINFO_TYPE);
	baseptr = preload_search_info(modptr, MODINFO_ADDR);
	sizeptr = preload_search_info(modptr, MODINFO_SIZE);
	dynptr = preload_search_info(modptr,
	    MODINFO_METADATA | MODINFOMD_DYNAMIC);
	if (type == NULL ||
	    (strcmp(type, "elf" __XSTRING(__ELF_WORD_SIZE) " module") != 0 &&
	     strcmp(type, "elf module") != 0))
		return (EFTYPE);
	if (baseptr == NULL || sizeptr == NULL || dynptr == NULL)
		return (EINVAL);

	lf = linker_make_file(filename, &link_elf_class);
	if (lf == NULL)
		return (ENOMEM);

	ef = (elf_file_t) lf;
	ef->preloaded = 1;
	ef->modptr = modptr;
	ef->address = *(caddr_t *)baseptr;
#ifdef SPARSE_MAPPING
	ef->object = 0;
#endif
	dp = (vm_offset_t)ef->address + *(vm_offset_t *)dynptr;
	ef->dynamic = (Elf_Dyn *)dp;
	lf->address = ef->address;
	lf->size = *(size_t *)sizeptr;

	ctors_addrp = (Elf_Addr *)preload_search_info(modptr,
	    MODINFO_METADATA | MODINFOMD_CTORS_ADDR);
	ctors_sizep = (Elf_Size *)preload_search_info(modptr,
	    MODINFO_METADATA | MODINFOMD_CTORS_SIZE);
	if (ctors_addrp != NULL && ctors_sizep != NULL) {
		lf->ctors_addr = ef->address + *ctors_addrp;
		lf->ctors_size = *ctors_sizep;
	}

	error = parse_dynamic(ef);
	if (error == 0)
		error = parse_dpcpu(ef);
#ifdef VIMAGE
	if (error == 0)
		error = parse_vnet(ef);
#endif
	if (error != 0) {
		linker_file_unload(lf, LINKER_UNLOAD_FORCE);
		return (error);
	}
	link_elf_reloc_local(lf);
	*result = lf;
	return (0);
}

static int
link_elf_link_preload_finish(linker_file_t lf)
{
	elf_file_t ef;
	int error;

	ef = (elf_file_t) lf;
	error = relocate_file(ef);
	if (error != 0)
		return (error);
	(void)link_elf_preload_parse_symbols(ef);

	return (link_elf_link_common_finish(lf));
}

static int
link_elf_load_file(linker_class_t cls, const char* filename,
    linker_file_t* result)
{
	struct nameidata nd;
	struct thread* td = curthread;	/* XXX */
	Elf_Ehdr *hdr;
	caddr_t firstpage;
	int nbytes, i;
	Elf_Phdr *phdr;
	Elf_Phdr *phlimit;
	Elf_Phdr *segs[MAXSEGS];
	int nsegs;
	Elf_Phdr *phdyn;
	caddr_t mapbase;
	size_t mapsize;
	Elf_Addr base_vaddr;
	Elf_Addr base_vlimit;
	int error = 0;
	ssize_t resid;
	int flags;
	elf_file_t ef;
	linker_file_t lf;
	Elf_Shdr *shdr;
	int symtabindex;
	int symstrindex;
	int shstrindex;
	int symcnt;
	int strcnt;
	char *shstrs;

	shdr = NULL;
	lf = NULL;
	shstrs = NULL;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_SYSSPACE, filename, td);
	flags = FREAD;
	error = vn_open(&nd, &flags, 0, NULL);
	if (error != 0)
		return (error);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (nd.ni_vp->v_type != VREG) {
		error = ENOEXEC;
		firstpage = NULL;
		goto out;
	}
#ifdef MAC
	error = mac_kld_check_load(curthread->td_ucred, nd.ni_vp);
	if (error != 0) {
		firstpage = NULL;
		goto out;
	}
#endif

	/*
	 * Read the elf header from the file.
	 */
	firstpage = malloc(PAGE_SIZE, M_LINKER, M_WAITOK);
	hdr = (Elf_Ehdr *)firstpage;
	error = vn_rdwr(UIO_READ, nd.ni_vp, firstpage, PAGE_SIZE, 0,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	nbytes = PAGE_SIZE - resid;
	if (error != 0)
		goto out;

	if (!IS_ELF(*hdr)) {
		error = ENOEXEC;
		goto out;
	}

	if (hdr->e_ident[EI_CLASS] != ELF_TARG_CLASS ||
	    hdr->e_ident[EI_DATA] != ELF_TARG_DATA) {
		link_elf_error(filename, "Unsupported file layout");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_ident[EI_VERSION] != EV_CURRENT ||
	    hdr->e_version != EV_CURRENT) {
		link_elf_error(filename, "Unsupported file version");
		error = ENOEXEC;
		goto out;
	}
	if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
		error = ENOSYS;
		goto out;
	}
	if (hdr->e_machine != ELF_TARG_MACH) {
		link_elf_error(filename, "Unsupported machine");
		error = ENOEXEC;
		goto out;
	}

	/*
	 * We rely on the program header being in the first page.
	 * This is not strictly required by the ABI specification, but
	 * it seems to always true in practice.  And, it simplifies
	 * things considerably.
	 */
	if (!((hdr->e_phentsize == sizeof(Elf_Phdr)) &&
	      (hdr->e_phoff + hdr->e_phnum*sizeof(Elf_Phdr) <= PAGE_SIZE) &&
	      (hdr->e_phoff + hdr->e_phnum*sizeof(Elf_Phdr) <= nbytes)))
		link_elf_error(filename, "Unreadable program headers");

	/*
	 * Scan the program header entries, and save key information.
	 *
	 * We rely on there being exactly two load segments, text and data,
	 * in that order.
	 */
	phdr = (Elf_Phdr *) (firstpage + hdr->e_phoff);
	phlimit = phdr + hdr->e_phnum;
	nsegs = 0;
	phdyn = NULL;
	while (phdr < phlimit) {
		switch (phdr->p_type) {
		case PT_LOAD:
			if (nsegs == MAXSEGS) {
				link_elf_error(filename, "Too many sections");
				error = ENOEXEC;
				goto out;
			}
			/*
			 * XXX: We just trust they come in right order ??
			 */
			segs[nsegs] = phdr;
			++nsegs;
			break;

		case PT_DYNAMIC:
			phdyn = phdr;
			break;

		case PT_INTERP:
			error = ENOSYS;
			goto out;
		}

		++phdr;
	}
	if (phdyn == NULL) {
		link_elf_error(filename, "Object is not dynamically-linked");
		error = ENOEXEC;
		goto out;
	}
	if (nsegs == 0) {
		link_elf_error(filename, "No sections");
		error = ENOEXEC;
		goto out;
	}

	/*
	 * Allocate the entire address space of the object, to stake
	 * out our contiguous region, and to establish the base
	 * address for relocation.
	 */
	base_vaddr = trunc_page(segs[0]->p_vaddr);
	base_vlimit = round_page(segs[nsegs - 1]->p_vaddr +
	    segs[nsegs - 1]->p_memsz);
	mapsize = base_vlimit - base_vaddr;

	lf = linker_make_file(filename, &link_elf_class);
	if (lf == NULL) {
		error = ENOMEM;
		goto out;
	}

	ef = (elf_file_t) lf;
#ifdef SPARSE_MAPPING
	ef->object = vm_object_allocate(OBJT_DEFAULT, mapsize >> PAGE_SHIFT);
	if (ef->object == NULL) {
		error = ENOMEM;
		goto out;
	}
	ef->address = (caddr_t) vm_map_min(kernel_map);
	error = vm_map_find(kernel_map, ef->object, 0,
	    (vm_offset_t *) &ef->address, mapsize, 0, VMFS_OPTIMAL_SPACE,
	    VM_PROT_ALL, VM_PROT_ALL, 0);
	if (error != 0) {
		vm_object_deallocate(ef->object);
		ef->object = 0;
		goto out;
	}
#else
	ef->address = malloc(mapsize, M_LINKER, M_EXEC | M_WAITOK);
#endif
	mapbase = ef->address;

	/*
	 * Read the text and data sections and zero the bss.
	 */
	for (i = 0; i < nsegs; i++) {
		caddr_t segbase = mapbase + segs[i]->p_vaddr - base_vaddr;
		error = vn_rdwr(UIO_READ, nd.ni_vp,
		    segbase, segs[i]->p_filesz, segs[i]->p_offset,
		    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
		    &resid, td);
		if (error != 0)
			goto out;
		bzero(segbase + segs[i]->p_filesz,
		    segs[i]->p_memsz - segs[i]->p_filesz);

#ifdef SPARSE_MAPPING
		/*
		 * Wire down the pages
		 */
		error = vm_map_wire(kernel_map,
		    (vm_offset_t) segbase,
		    (vm_offset_t) segbase + segs[i]->p_memsz,
		    VM_MAP_WIRE_SYSTEM|VM_MAP_WIRE_NOHOLES);
		if (error != KERN_SUCCESS) {
			error = ENOMEM;
			goto out;
		}
#endif
	}

#ifdef GPROF
	/* Update profiling information with the new text segment. */
	mtx_lock(&Giant);
	kmupetext((uintfptr_t)(mapbase + segs[0]->p_vaddr - base_vaddr +
	    segs[0]->p_memsz));
	mtx_unlock(&Giant);
#endif

	ef->dynamic = (Elf_Dyn *) (mapbase + phdyn->p_vaddr - base_vaddr);

	lf->address = ef->address;
	lf->size = mapsize;

	error = parse_dynamic(ef);
	if (error != 0)
		goto out;
	error = parse_dpcpu(ef);
	if (error != 0)
		goto out;
#ifdef VIMAGE
	error = parse_vnet(ef);
	if (error != 0)
		goto out;
#endif
	link_elf_reloc_local(lf);

	VOP_UNLOCK(nd.ni_vp, 0);
	error = linker_load_dependencies(lf);
	vn_lock(nd.ni_vp, LK_EXCLUSIVE | LK_RETRY);
	if (error != 0)
		goto out;
	error = relocate_file(ef);
	if (error != 0)
		goto out;

	/*
	 * Try and load the symbol table if it's present.  (you can
	 * strip it!)
	 */
	nbytes = hdr->e_shnum * hdr->e_shentsize;
	if (nbytes == 0 || hdr->e_shoff == 0)
		goto nosyms;
	shdr = malloc(nbytes, M_LINKER, M_WAITOK | M_ZERO);
	error = vn_rdwr(UIO_READ, nd.ni_vp,
	    (caddr_t)shdr, nbytes, hdr->e_shoff,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error != 0)
		goto out;

	/* Read section string table */
	shstrindex = hdr->e_shstrndx;
	if (shstrindex != 0 && shdr[shstrindex].sh_type == SHT_STRTAB &&
	    shdr[shstrindex].sh_size != 0) {
		nbytes = shdr[shstrindex].sh_size;
		shstrs = malloc(nbytes, M_LINKER, M_WAITOK | M_ZERO);
		error = vn_rdwr(UIO_READ, nd.ni_vp, (caddr_t)shstrs, nbytes,
		    shdr[shstrindex].sh_offset, UIO_SYSSPACE, IO_NODELOCKED,
		    td->td_ucred, NOCRED, &resid, td);
		if (error)
			goto out;
	}

	symtabindex = -1;
	symstrindex = -1;
	for (i = 0; i < hdr->e_shnum; i++) {
		if (shdr[i].sh_type == SHT_SYMTAB) {
			symtabindex = i;
			symstrindex = shdr[i].sh_link;
		} else if (shstrs != NULL && shdr[i].sh_name != 0 &&
		    strcmp(shstrs + shdr[i].sh_name, ".ctors") == 0) {
			/* Record relocated address and size of .ctors. */
			lf->ctors_addr = mapbase + shdr[i].sh_addr - base_vaddr;
			lf->ctors_size = shdr[i].sh_size;
		}
	}
	if (symtabindex < 0 || symstrindex < 0)
		goto nosyms;

	symcnt = shdr[symtabindex].sh_size;
	ef->symbase = malloc(symcnt, M_LINKER, M_WAITOK);
	strcnt = shdr[symstrindex].sh_size;
	ef->strbase = malloc(strcnt, M_LINKER, M_WAITOK);

	error = vn_rdwr(UIO_READ, nd.ni_vp,
	    ef->symbase, symcnt, shdr[symtabindex].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error != 0)
		goto out;
	error = vn_rdwr(UIO_READ, nd.ni_vp,
	    ef->strbase, strcnt, shdr[symstrindex].sh_offset,
	    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED,
	    &resid, td);
	if (error != 0)
		goto out;

	ef->ddbsymcnt = symcnt / sizeof(Elf_Sym);
	ef->ddbsymtab = (const Elf_Sym *)ef->symbase;
	ef->ddbstrcnt = strcnt;
	ef->ddbstrtab = ef->strbase;

nosyms:
	error = link_elf_link_common_finish(lf);
	if (error != 0)
		goto out;

	*result = lf;

out:
	VOP_UNLOCK(nd.ni_vp, 0);
	vn_close(nd.ni_vp, FREAD, td->td_ucred, td);
	if (error != 0 && lf != NULL)
		linker_file_unload(lf, LINKER_UNLOAD_FORCE);
	free(shdr, M_LINKER);
	free(firstpage, M_LINKER);
	free(shstrs, M_LINKER);

	return (error);
}

Elf_Addr
elf_relocaddr(linker_file_t lf, Elf_Addr x)
{
	elf_file_t ef;

	ef = (elf_file_t)lf;
	if (x >= ef->pcpu_start && x < ef->pcpu_stop)
		return ((x - ef->pcpu_start) + ef->pcpu_base);
#ifdef VIMAGE
	if (x >= ef->vnet_start && x < ef->vnet_stop)
		return ((x - ef->vnet_start) + ef->vnet_base);
#endif
	return (x);
}


static void
link_elf_unload_file(linker_file_t file)
{
	elf_file_t ef = (elf_file_t) file;

	if (ef->pcpu_base != 0) {
		dpcpu_free((void *)ef->pcpu_base,
		    ef->pcpu_stop - ef->pcpu_start);
		elf_set_delete(&set_pcpu_list, ef->pcpu_start);
	}
#ifdef VIMAGE
	if (ef->vnet_base != 0) {
		vnet_data_free((void *)ef->vnet_base,
		    ef->vnet_stop - ef->vnet_start);
		elf_set_delete(&set_vnet_list, ef->vnet_start);
	}
#endif
#ifdef GDB
	if (ef->gdb.l_ld != NULL) {
		GDB_STATE(RT_DELETE);
		free((void *)(uintptr_t)ef->gdb.l_name, M_LINKER);
		link_elf_delete_gdb(&ef->gdb);
		GDB_STATE(RT_CONSISTENT);
	}
#endif

	/* Notify MD code that a module is being unloaded. */
	elf_cpu_unload_file(file);

	if (ef->preloaded) {
		link_elf_unload_preload(file);
		return;
	}

#ifdef SPARSE_MAPPING
	if (ef->object != NULL) {
		vm_map_remove(kernel_map, (vm_offset_t) ef->address,
		    (vm_offset_t) ef->address
		    + (ef->object->size << PAGE_SHIFT));
	}
#else
	free(ef->address, M_LINKER);
#endif
	free(ef->symbase, M_LINKER);
	free(ef->strbase, M_LINKER);
	free(ef->ctftab, M_LINKER);
	free(ef->ctfoff, M_LINKER);
	free(ef->typoff, M_LINKER);
}

static void
link_elf_unload_preload(linker_file_t file)
{
	if (file->pathname != NULL)
		preload_delete_name(file->pathname);
}

static const char *
symbol_name(elf_file_t ef, Elf_Size r_info)
{
	const Elf_Sym *ref;

	if (ELF_R_SYM(r_info)) {
		ref = ef->symtab + ELF_R_SYM(r_info);
		return (ef->strtab + ref->st_name);
	}
	return (NULL);
}

static int
symbol_type(elf_file_t ef, Elf_Size r_info)
{
	const Elf_Sym *ref;

	if (ELF_R_SYM(r_info)) {
		ref = ef->symtab + ELF_R_SYM(r_info);
		return (ELF_ST_TYPE(ref->st_info));
	}
	return (STT_NOTYPE);
}

static int
relocate_file1(elf_file_t ef, elf_lookup_fn lookup, elf_reloc_fn reloc,
    bool ifuncs)
{
	const Elf_Rel *rel;
	const Elf_Rela *rela;
	const char *symname;

#define	APPLY_RELOCS(iter, tbl, tblsize, type) do {			\
	for ((iter) = (tbl); (iter) != NULL &&				\
	    (iter) < (tbl) + (tblsize) / sizeof(*(iter)); (iter)++) {	\
		if ((symbol_type(ef, (iter)->r_info) ==			\
		    STT_GNU_IFUNC ||					\
		    elf_is_ifunc_reloc((iter)->r_info)) != ifuncs)	\
			continue;					\
		if (reloc(&ef->lf, (Elf_Addr)ef->address,		\
		    (iter), (type), lookup)) {				\
			symname = symbol_name(ef, (iter)->r_info);	\
			printf("link_elf: symbol %s undefined\n",	\
			    symname);					\
			return (ENOENT);				\
		}							\
	}								\
} while (0)

	APPLY_RELOCS(rel, ef->rel, ef->relsize, ELF_RELOC_REL);
	APPLY_RELOCS(rela, ef->rela, ef->relasize, ELF_RELOC_RELA);
	APPLY_RELOCS(rel, ef->pltrel, ef->pltrelsize, ELF_RELOC_REL);
	APPLY_RELOCS(rela, ef->pltrela, ef->pltrelasize, ELF_RELOC_RELA);

#undef APPLY_RELOCS

	return (0);
}

static int
relocate_file(elf_file_t ef)
{
	int error;

	error = relocate_file1(ef, elf_lookup, elf_reloc, false);
	if (error == 0)
		error = relocate_file1(ef, elf_lookup, elf_reloc, true);
	return (error);
}

/*
 * Hash function for symbol table lookup.  Don't even think about changing
 * this.  It is specified by the System V ABI.
 */
static unsigned long
elf_hash(const char *name)
{
	const unsigned char *p = (const unsigned char *) name;
	unsigned long h = 0;
	unsigned long g;

	while (*p != '\0') {
		h = (h << 4) + *p++;
		if ((g = h & 0xf0000000) != 0)
			h ^= g >> 24;
		h &= ~g;
	}
	return (h);
}

static int
link_elf_lookup_symbol(linker_file_t lf, const char *name, c_linker_sym_t *sym)
{
	elf_file_t ef = (elf_file_t) lf;
	unsigned long symnum;
	const Elf_Sym* symp;
	const char *strp;
	unsigned long hash;
	int i;

	/* If we don't have a hash, bail. */
	if (ef->buckets == NULL || ef->nbuckets == 0) {
		printf("link_elf_lookup_symbol: missing symbol hash table\n");
		return (ENOENT);
	}

	/* First, search hashed global symbols */
	hash = elf_hash(name);
	symnum = ef->buckets[hash % ef->nbuckets];

	while (symnum != STN_UNDEF) {
		if (symnum >= ef->nchains) {
			printf("%s: corrupt symbol table\n", __func__);
			return (ENOENT);
		}

		symp = ef->symtab + symnum;
		if (symp->st_name == 0) {
			printf("%s: corrupt symbol table\n", __func__);
			return (ENOENT);
		}

		strp = ef->strtab + symp->st_name;

		if (strcmp(name, strp) == 0) {
			if (symp->st_shndx != SHN_UNDEF ||
			    (symp->st_value != 0 &&
			    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
			    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC))) {
				*sym = (c_linker_sym_t) symp;
				return (0);
			}
			return (ENOENT);
		}

		symnum = ef->chains[symnum];
	}

	/* If we have not found it, look at the full table (if loaded) */
	if (ef->symtab == ef->ddbsymtab)
		return (ENOENT);

	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		strp = ef->ddbstrtab + symp->st_name;
		if (strcmp(name, strp) == 0) {
			if (symp->st_shndx != SHN_UNDEF ||
			    (symp->st_value != 0 &&
			    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
			    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC))) {
				*sym = (c_linker_sym_t) symp;
				return (0);
			}
			return (ENOENT);
		}
	}

	return (ENOENT);
}

static int
link_elf_symbol_values(linker_file_t lf, c_linker_sym_t sym,
    linker_symval_t *symval)
{
	elf_file_t ef;
	const Elf_Sym *es;
	caddr_t val;

	ef = (elf_file_t)lf;
	es = (const Elf_Sym *)sym;
	if (es >= ef->symtab && es < (ef->symtab + ef->nchains)) {
		symval->name = ef->strtab + es->st_name;
		val = (caddr_t)ef->address + es->st_value;
		if (ELF_ST_TYPE(es->st_info) == STT_GNU_IFUNC)
			val = ((caddr_t (*)(void))val)();
		symval->value = val;
		symval->size = es->st_size;
		return (0);
	}
	if (ef->symtab == ef->ddbsymtab)
		return (ENOENT);
	if (es >= ef->ddbsymtab && es < (ef->ddbsymtab + ef->ddbsymcnt)) {
		symval->name = ef->ddbstrtab + es->st_name;
		val = (caddr_t)ef->address + es->st_value;
		if (ELF_ST_TYPE(es->st_info) == STT_GNU_IFUNC)
			val = ((caddr_t (*)(void))val)();
		symval->value = val;
		symval->size = es->st_size;
		return (0);
	}
	return (ENOENT);
}

static int
link_elf_search_symbol(linker_file_t lf, caddr_t value,
    c_linker_sym_t *sym, long *diffp)
{
	elf_file_t ef = (elf_file_t) lf;
	u_long off = (uintptr_t) (void *) value;
	u_long diff = off;
	u_long st_value;
	const Elf_Sym* es;
	const Elf_Sym* best = NULL;
	int i;

	for (i = 0, es = ef->ddbsymtab; i < ef->ddbsymcnt; i++, es++) {
		if (es->st_name == 0)
			continue;
		st_value = es->st_value + (uintptr_t) (void *) ef->address;
		if (off >= st_value) {
			if (off - st_value < diff) {
				diff = off - st_value;
				best = es;
				if (diff == 0)
					break;
			} else if (off - st_value == diff) {
				best = es;
			}
		}
	}
	if (best == NULL)
		*diffp = off;
	else
		*diffp = diff;
	*sym = (c_linker_sym_t) best;

	return (0);
}

/*
 * Look up a linker set on an ELF system.
 */
static int
link_elf_lookup_set(linker_file_t lf, const char *name,
    void ***startp, void ***stopp, int *countp)
{
	c_linker_sym_t sym;
	linker_symval_t symval;
	char *setsym;
	void **start, **stop;
	int len, error = 0, count;

	len = strlen(name) + sizeof("__start_set_"); /* sizeof includes \0 */
	setsym = malloc(len, M_LINKER, M_WAITOK);

	/* get address of first entry */
	snprintf(setsym, len, "%s%s", "__start_set_", name);
	error = link_elf_lookup_symbol(lf, setsym, &sym);
	if (error != 0)
		goto out;
	link_elf_symbol_values(lf, sym, &symval);
	if (symval.value == 0) {
		error = ESRCH;
		goto out;
	}
	start = (void **)symval.value;

	/* get address of last entry */
	snprintf(setsym, len, "%s%s", "__stop_set_", name);
	error = link_elf_lookup_symbol(lf, setsym, &sym);
	if (error != 0)
		goto out;
	link_elf_symbol_values(lf, sym, &symval);
	if (symval.value == 0) {
		error = ESRCH;
		goto out;
	}
	stop = (void **)symval.value;

	/* and the number of entries */
	count = stop - start;

	/* and copy out */
	if (startp != NULL)
		*startp = start;
	if (stopp != NULL)
		*stopp = stop;
	if (countp != NULL)
		*countp = count;

out:
	free(setsym, M_LINKER);
	return (error);
}

static int
link_elf_each_function_name(linker_file_t file,
  int (*callback)(const char *, void *), void *opaque)
{
	elf_file_t ef = (elf_file_t)file;
	const Elf_Sym *symp;
	int i, error;

	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		if (symp->st_value != 0 &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
		    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC)) {
			error = callback(ef->ddbstrtab + symp->st_name, opaque);
			if (error != 0)
				return (error);
		}
	}
	return (0);
}

static int
link_elf_each_function_nameval(linker_file_t file,
    linker_function_nameval_callback_t callback, void *opaque)
{
	linker_symval_t symval;
	elf_file_t ef = (elf_file_t)file;
	const Elf_Sym* symp;
	int i, error;

	/* Exhaustive search */
	for (i = 0, symp = ef->ddbsymtab; i < ef->ddbsymcnt; i++, symp++) {
		if (symp->st_value != 0 &&
		    (ELF_ST_TYPE(symp->st_info) == STT_FUNC ||
		    ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC)) {
			error = link_elf_symbol_values(file,
			    (c_linker_sym_t) symp, &symval);
			if (error != 0)
				return (error);
			error = callback(file, i, &symval, opaque);
			if (error != 0)
				return (error);
		}
	}
	return (0);
}

const Elf_Sym *
elf_get_sym(linker_file_t lf, Elf_Size symidx)
{
	elf_file_t ef = (elf_file_t)lf;

	if (symidx >= ef->nchains)
		return (NULL);
	return (ef->symtab + symidx);
}

const char *
elf_get_symname(linker_file_t lf, Elf_Size symidx)
{
	elf_file_t ef = (elf_file_t)lf;
	const Elf_Sym *sym;

	if (symidx >= ef->nchains)
		return (NULL);
	sym = ef->symtab + symidx;
	return (ef->strtab + sym->st_name);
}

/*
 * Symbol lookup function that can be used when the symbol index is known (ie
 * in relocations). It uses the symbol index instead of doing a fully fledged
 * hash table based lookup when such is valid. For example for local symbols.
 * This is not only more efficient, it's also more correct. It's not always
 * the case that the symbol can be found through the hash table.
 */
static int
elf_lookup(linker_file_t lf, Elf_Size symidx, int deps, Elf_Addr *res)
{
	elf_file_t ef = (elf_file_t)lf;
	const Elf_Sym *sym;
	const char *symbol;
	Elf_Addr addr, start, base;

	/* Don't even try to lookup the symbol if the index is bogus. */
	if (symidx >= ef->nchains) {
		*res = 0;
		return (EINVAL);
	}

	sym = ef->symtab + symidx;

	/*
	 * Don't do a full lookup when the symbol is local. It may even
	 * fail because it may not be found through the hash table.
	 */
	if (ELF_ST_BIND(sym->st_info) == STB_LOCAL) {
		/* Force lookup failure when we have an insanity. */
		if (sym->st_shndx == SHN_UNDEF || sym->st_value == 0) {
			*res = 0;
			return (EINVAL);
		}
		*res = ((Elf_Addr)ef->address + sym->st_value);
		return (0);
	}

	/*
	 * XXX we can avoid doing a hash table based lookup for global
	 * symbols as well. This however is not always valid, so we'll
	 * just do it the hard way for now. Performance tweaks can
	 * always be added.
	 */

	symbol = ef->strtab + sym->st_name;

	/* Force a lookup failure if the symbol name is bogus. */
	if (*symbol == 0) {
		*res = 0;
		return (EINVAL);
	}

	addr = ((Elf_Addr)linker_file_lookup_symbol(lf, symbol, deps));
	if (addr == 0 && ELF_ST_BIND(sym->st_info) != STB_WEAK) {
		*res = 0;
		return (EINVAL);
	}

	if (elf_set_find(&set_pcpu_list, addr, &start, &base))
		addr = addr - start + base;
#ifdef VIMAGE
	else if (elf_set_find(&set_vnet_list, addr, &start, &base))
		addr = addr - start + base;
#endif
	*res = addr;
	return (0);
}

static void
link_elf_reloc_local(linker_file_t lf)
{
	const Elf_Rel *rellim;
	const Elf_Rel *rel;
	const Elf_Rela *relalim;
	const Elf_Rela *rela;
	elf_file_t ef = (elf_file_t)lf;

	/* Perform relocations without addend if there are any: */
	if ((rel = ef->rel) != NULL) {
		rellim = (const Elf_Rel *)((const char *)ef->rel + ef->relsize);
		while (rel < rellim) {
			elf_reloc_local(lf, (Elf_Addr)ef->address, rel,
			    ELF_RELOC_REL, elf_lookup);
			rel++;
		}
	}

	/* Perform relocations with addend if there are any: */
	if ((rela = ef->rela) != NULL) {
		relalim = (const Elf_Rela *)
		    ((const char *)ef->rela + ef->relasize);
		while (rela < relalim) {
			elf_reloc_local(lf, (Elf_Addr)ef->address, rela,
			    ELF_RELOC_RELA, elf_lookup);
			rela++;
		}
	}
}

static long
link_elf_symtab_get(linker_file_t lf, const Elf_Sym **symtab)
{
	elf_file_t ef = (elf_file_t)lf;

	*symtab = ef->ddbsymtab;

	if (*symtab == NULL)
		return (0);

	return (ef->ddbsymcnt);
}

static long
link_elf_strtab_get(linker_file_t lf, caddr_t *strtab)
{
	elf_file_t ef = (elf_file_t)lf;

	*strtab = ef->ddbstrtab;

	if (*strtab == NULL)
		return (0);

	return (ef->ddbstrcnt);
}

#if defined(__i386__) || defined(__amd64__) || defined(__aarch64__)
/*
 * Use this lookup routine when performing relocations early during boot.
 * The generic lookup routine depends on kobj, which is not initialized
 * at that point.
 */
static int
elf_lookup_ifunc(linker_file_t lf, Elf_Size symidx, int deps __unused,
    Elf_Addr *res)
{
	elf_file_t ef;
	const Elf_Sym *symp;
	caddr_t val;

	ef = (elf_file_t)lf;
	symp = ef->symtab + symidx;
	if (ELF_ST_TYPE(symp->st_info) == STT_GNU_IFUNC) {
		val = (caddr_t)ef->address + symp->st_value;
		*res = ((Elf_Addr (*)(void))val)();
		return (0);
	}
	return (ENOENT);
}

void
link_elf_ireloc(caddr_t kmdp)
{
	struct elf_file eff;
	elf_file_t ef;

	ef = &eff;

	bzero_early(ef, sizeof(*ef));

	ef->modptr = kmdp;
	ef->dynamic = (Elf_Dyn *)&_DYNAMIC;
	parse_dynamic(ef);
	ef->address = 0;
	link_elf_preload_parse_symbols(ef);
	relocate_file1(ef, elf_lookup_ifunc, elf_reloc, true);
}
#endif
