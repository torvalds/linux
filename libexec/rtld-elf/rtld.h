/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1996, 1997, 1998, 1999, 2000 John D. Polstra.
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

#ifndef RTLD_H /* { */
#define RTLD_H 1

#include <machine/elf.h>
#include <sys/types.h>
#include <sys/queue.h>

#include <elf-hints.h>
#include <link.h>
#include <stdarg.h>
#include <setjmp.h>
#include <stddef.h>

#include "rtld_lock.h"
#include "rtld_machdep.h"

#define NEW(type)	((type *) xmalloc(sizeof(type)))
#define CNEW(type)	((type *) xcalloc(1, sizeof(type)))

/* We might as well do booleans like C++. */
typedef unsigned char bool;
#define false	0
#define true	1

extern size_t tls_last_offset;
extern size_t tls_last_size;
extern size_t tls_static_space;
extern Elf_Addr tls_dtv_generation;
extern int tls_max_index;

extern int npagesizes;
extern size_t *pagesizes;

extern int main_argc;
extern char **main_argv;
extern char **environ;

struct stat;
struct Struct_Obj_Entry;

/* Lists of shared objects */
typedef struct Struct_Objlist_Entry {
    STAILQ_ENTRY(Struct_Objlist_Entry) link;
    struct Struct_Obj_Entry *obj;
} Objlist_Entry;

typedef STAILQ_HEAD(Struct_Objlist, Struct_Objlist_Entry) Objlist;

/* Types of init and fini functions */
typedef void (*InitFunc)(void);
typedef void (*InitArrFunc)(int, char **, char **);

/* Lists of shared object dependencies */
typedef struct Struct_Needed_Entry {
    struct Struct_Needed_Entry *next;
    struct Struct_Obj_Entry *obj;
    unsigned long name;		/* Offset of name in string table */
} Needed_Entry;

typedef struct Struct_Name_Entry {
    STAILQ_ENTRY(Struct_Name_Entry) link;
    char   name[1];
} Name_Entry;

/* Lock object */
typedef struct Struct_LockInfo {
    void *context;		/* Client context for creating locks */
    void *thelock;		/* The one big lock */
    /* Debugging aids. */
    volatile int rcount;	/* Number of readers holding lock */
    volatile int wcount;	/* Number of writers holding lock */
    /* Methods */
    void *(*lock_create)(void *context);
    void (*rlock_acquire)(void *lock);
    void (*wlock_acquire)(void *lock);
    void (*rlock_release)(void *lock);
    void (*wlock_release)(void *lock);
    void (*lock_destroy)(void *lock);
    void (*context_destroy)(void *context);
} LockInfo;

typedef struct Struct_Ver_Entry {
	Elf_Word     hash;
	unsigned int flags;
	const char  *name;
	const char  *file;
} Ver_Entry;

typedef struct Struct_Sym_Match_Result {
    const Elf_Sym *sym_out;
    const Elf_Sym *vsymp;
    int vcount;
} Sym_Match_Result;

#define VER_INFO_HIDDEN	0x01

/*
 * Shared object descriptor.
 *
 * Items marked with "(%)" are dynamically allocated, and must be freed
 * when the structure is destroyed.
 *
 * CAUTION: It appears that the JDK port peeks into these structures.
 * It looks at "next" and "mapbase" at least.  Don't add new members
 * near the front, until this can be straightened out.
 */
typedef struct Struct_Obj_Entry {
    /*
     * These two items have to be set right for compatibility with the
     * original ElfKit crt1.o.
     */
    Elf_Size magic;		/* Magic number (sanity check) */
    Elf_Size version;		/* Version number of struct format */

    TAILQ_ENTRY(Struct_Obj_Entry) next;
    char *path;			/* Pathname of underlying file (%) */
    char *origin_path;		/* Directory path of origin file */
    int refcount;		/* DAG references */
    int holdcount;		/* Count of transient references */
    int dl_refcount;		/* Number of times loaded by dlopen */

    /* These items are computed by map_object() or by digest_phdr(). */
    caddr_t mapbase;		/* Base address of mapped region */
    size_t mapsize;		/* Size of mapped region in bytes */
    Elf_Addr vaddrbase;		/* Base address in shared object file */
    caddr_t relocbase;		/* Relocation constant = mapbase - vaddrbase */
    const Elf_Dyn *dynamic;	/* Dynamic section */
    caddr_t entry;		/* Entry point */
    const Elf_Phdr *phdr;	/* Program header if it is mapped, else NULL */
    size_t phsize;		/* Size of program header in bytes */
    const char *interp;		/* Pathname of the interpreter, if any */
    Elf_Word stack_flags;

    /* TLS information */
    int tlsindex;		/* Index in DTV for this module */
    void *tlsinit;		/* Base address of TLS init block */
    size_t tlsinitsize;		/* Size of TLS init block for this module */
    size_t tlssize;		/* Size of TLS block for this module */
    size_t tlsoffset;		/* Offset of static TLS block for this module */
    size_t tlsalign;		/* Alignment of static TLS block */

    caddr_t relro_page;
    size_t relro_size;

    /* Items from the dynamic section. */
    Elf_Addr *pltgot;		/* PLT or GOT, depending on architecture */
    const Elf_Rel *rel;		/* Relocation entries */
    unsigned long relsize;	/* Size in bytes of relocation info */
    const Elf_Rela *rela;	/* Relocation entries with addend */
    unsigned long relasize;	/* Size in bytes of addend relocation info */
    const Elf_Rel *pltrel;	/* PLT relocation entries */
    unsigned long pltrelsize;	/* Size in bytes of PLT relocation info */
    const Elf_Rela *pltrela;	/* PLT relocation entries with addend */
    unsigned long pltrelasize;	/* Size in bytes of PLT addend reloc info */
    const Elf_Sym *symtab;	/* Symbol table */
    const char *strtab;		/* String table */
    unsigned long strsize;	/* Size in bytes of string table */
#ifdef __mips__
    Elf_Word local_gotno;	/* Number of local GOT entries */
    Elf_Word symtabno;		/* Number of dynamic symbols */
    Elf_Word gotsym;		/* First dynamic symbol in GOT */
    Elf_Addr *mips_pltgot;	/* Second PLT GOT */
#endif
#ifdef __powerpc64__
    Elf_Addr glink;		/* GLINK PLT call stub section */
#endif

    const Elf_Verneed *verneed; /* Required versions. */
    Elf_Word verneednum;	/* Number of entries in verneed table */
    const Elf_Verdef  *verdef;	/* Provided versions. */
    Elf_Word verdefnum;		/* Number of entries in verdef table */
    const Elf_Versym *versyms;  /* Symbol versions table */

    const Elf_Hashelt *buckets;	/* Hash table buckets array */
    unsigned long nbuckets;	/* Number of buckets */
    const Elf_Hashelt *chains;	/* Hash table chain array */
    unsigned long nchains;	/* Number of entries in chain array */

    Elf32_Word nbuckets_gnu;		/* Number of GNU hash buckets*/
    Elf32_Word symndx_gnu;		/* 1st accessible symbol on dynsym table */
    Elf32_Word maskwords_bm_gnu;  	/* Bloom filter words - 1 (bitmask) */
    Elf32_Word shift2_gnu;		/* Bloom filter shift count */
    Elf32_Word dynsymcount;		/* Total entries in dynsym table */
    const Elf_Addr *bloom_gnu;		/* Bloom filter used by GNU hash func */
    const Elf_Hashelt *buckets_gnu;	/* GNU hash table bucket array */
    const Elf_Hashelt *chain_zero_gnu;	/* GNU hash table value array (Zeroed) */

    const char *rpath;		/* Search path specified in object */
    const char *runpath;	/* Search path with different priority */
    Needed_Entry *needed;	/* Shared objects needed by this one (%) */
    Needed_Entry *needed_filtees;
    Needed_Entry *needed_aux_filtees;

    STAILQ_HEAD(, Struct_Name_Entry) names; /* List of names for this object we
					       know about. */
    Ver_Entry *vertab;		/* Versions required /defined by this object */
    int vernum;			/* Number of entries in vertab */

    Elf_Addr init;		/* Initialization function to call */
    Elf_Addr fini;		/* Termination function to call */
    Elf_Addr preinit_array;	/* Pre-initialization array of functions */
    Elf_Addr init_array;	/* Initialization array of functions */
    Elf_Addr fini_array;	/* Termination array of functions */
    int preinit_array_num;	/* Number of entries in preinit_array */
    int init_array_num; 	/* Number of entries in init_array */
    int fini_array_num; 	/* Number of entries in fini_array */

    int32_t osrel;		/* OSREL note value */
    uint32_t fctl0;		/* FEATURE_CONTROL note desc[0] value */

    bool mainprog : 1;		/* True if this is the main program */
    bool rtld : 1;		/* True if this is the dynamic linker */
    bool relocated : 1;		/* True if processed by relocate_objects() */
    bool ver_checked : 1;	/* True if processed by rtld_verify_object_versions */
    bool textrel : 1;		/* True if there are relocations to text seg */
    bool symbolic : 1;		/* True if generated with "-Bsymbolic" */
    bool bind_now : 1;		/* True if all relocations should be made first */
    bool traced : 1;		/* Already printed in ldd trace output */
    bool jmpslots_done : 1;	/* Already have relocated the jump slots */
    bool init_done : 1;		/* Already have added object to init list */
    bool tls_done : 1;		/* Already allocated offset for static TLS */
    bool phdr_alloc : 1;	/* Phdr is allocated and needs to be freed. */
    bool z_origin : 1;		/* Process rpath and soname tokens */
    bool z_nodelete : 1;	/* Do not unload the object and dependencies */
    bool z_noopen : 1;		/* Do not load on dlopen */
    bool z_loadfltr : 1;	/* Immediately load filtees */
    bool z_interpose : 1;	/* Interpose all objects but main */
    bool z_nodeflib : 1;	/* Don't search default library path */
    bool z_global : 1;		/* Make the object global */
    bool ref_nodel : 1;		/* Refcount increased to prevent dlclose */
    bool init_scanned: 1;	/* Object is already on init list. */
    bool on_fini_list: 1;	/* Object is already on fini list. */
    bool dag_inited : 1;	/* Object has its DAG initialized. */
    bool filtees_loaded : 1;	/* Filtees loaded */
    bool irelative : 1;		/* Object has R_MACHDEP_IRELATIVE relocs */
    bool gnu_ifunc : 1;		/* Object has references to STT_GNU_IFUNC */
    bool non_plt_gnu_ifunc : 1;	/* Object has non-plt IFUNC references */
    bool ifuncs_resolved : 1;	/* Object ifuncs were already resolved */
    bool crt_no_init : 1;	/* Object' crt does not call _init/_fini */
    bool valid_hash_sysv : 1;	/* A valid System V hash hash tag is available */
    bool valid_hash_gnu : 1;	/* A valid GNU hash tag is available */
    bool dlopened : 1;		/* dlopen()-ed (vs. load statically) */
    bool marker : 1;		/* marker on the global obj list */
    bool unholdfree : 1;	/* unmap upon last unhold */
    bool doomed : 1;		/* Object cannot be referenced */

    struct link_map linkmap;	/* For GDB and dlinfo() */
    Objlist dldags;		/* Object belongs to these dlopened DAGs (%) */
    Objlist dagmembers;		/* DAG has these members (%) */
    dev_t dev;			/* Object's filesystem's device */
    ino_t ino;			/* Object's inode number */
    void *priv;			/* Platform-dependent */
} Obj_Entry;

#define RTLD_MAGIC	0xd550b87a
#define RTLD_VERSION	1

TAILQ_HEAD(obj_entry_q, Struct_Obj_Entry);

#define RTLD_STATIC_TLS_EXTRA	128

/* Flags to be passed into symlook_ family of functions. */
#define SYMLOOK_IN_PLT	0x01	/* Lookup for PLT symbol */
#define SYMLOOK_DLSYM	0x02	/* Return newest versioned symbol. Used by
				   dlsym. */
#define	SYMLOOK_EARLY	0x04	/* Symlook is done during initialization. */
#define	SYMLOOK_IFUNC	0x08	/* Allow IFUNC processing in
				   reloc_non_plt(). */

/* Flags for load_object(). */
#define	RTLD_LO_NOLOAD	0x01	/* dlopen() specified RTLD_NOLOAD. */
#define	RTLD_LO_DLOPEN	0x02	/* Load_object() called from dlopen(). */
#define	RTLD_LO_TRACE	0x04	/* Only tracing. */
#define	RTLD_LO_NODELETE 0x08	/* Loaded object cannot be closed. */
#define	RTLD_LO_FILTEES 0x10	/* Loading filtee. */
#define	RTLD_LO_EARLY	0x20	/* Do not call ctors, postpone it to the
				   initialization during the image start. */

/*
 * Symbol cache entry used during relocation to avoid multiple lookups
 * of the same symbol.
 */
typedef struct Struct_SymCache {
    const Elf_Sym *sym;		/* Symbol table entry */
    const Obj_Entry *obj;	/* Shared object which defines it */
} SymCache;

/*
 * This structure provides a reentrant way to keep a list of objects and
 * check which ones have already been processed in some way.
 */
typedef struct Struct_DoneList {
    const Obj_Entry **objs;		/* Array of object pointers */
    unsigned int num_alloc;		/* Allocated size of the array */
    unsigned int num_used;		/* Number of array slots used */
} DoneList;

struct Struct_RtldLockState {
	int lockstate;
	sigjmp_buf env;
};

struct fill_search_info_args {
	int request;
	unsigned int flags;
	struct dl_serinfo *serinfo;
	struct dl_serpath *serpath;
	char *strspace;
};

/*
 * The pack of arguments and results for the symbol lookup functions.
 */
typedef struct Struct_SymLook {
    const char *name;
    unsigned long hash;
    uint32_t hash_gnu;
    const Ver_Entry *ventry;
    int flags;
    const Obj_Entry *defobj_out;
    const Elf_Sym *sym_out;
    struct Struct_RtldLockState *lockstate;
} SymLook;

void _rtld_error(const char *, ...) __printflike(1, 2) __exported;
void rtld_die(void) __dead2;
const char *rtld_strerror(int);
Obj_Entry *map_object(int, const char *, const struct stat *);
void *xcalloc(size_t, size_t);
void *xmalloc(size_t);
char *xstrdup(const char *);
void *malloc_aligned(size_t size, size_t align);
void free_aligned(void *ptr);
extern Elf_Addr _GLOBAL_OFFSET_TABLE_[];
extern Elf_Sym sym_zero;	/* For resolving undefined weak refs. */
extern bool ld_bind_not;

void dump_relocations(Obj_Entry *);
void dump_obj_relocations(Obj_Entry *);
void dump_Elf_Rel(Obj_Entry *, const Elf_Rel *, u_long);
void dump_Elf_Rela(Obj_Entry *, const Elf_Rela *, u_long);

/*
 * Function declarations.
 */
unsigned long elf_hash(const char *);
const Elf_Sym *find_symdef(unsigned long, const Obj_Entry *,
  const Obj_Entry **, int, SymCache *, struct Struct_RtldLockState *);
void lockdflt_init(void);
void digest_notes(Obj_Entry *, Elf_Addr, Elf_Addr);
Obj_Entry *globallist_curr(const Obj_Entry *obj);
Obj_Entry *globallist_next(const Obj_Entry *obj);
void obj_free(Obj_Entry *);
Obj_Entry *obj_new(void);
void _rtld_bind_start(void);
void *rtld_resolve_ifunc(const Obj_Entry *obj, const Elf_Sym *def);
void symlook_init(SymLook *, const char *);
int symlook_obj(SymLook *, const Obj_Entry *);
void *tls_get_addr_common(Elf_Addr** dtvp, int index, size_t offset);
void *allocate_tls(Obj_Entry *, void *, size_t, size_t);
void free_tls(void *, size_t, size_t);
void *allocate_module_tls(int index);
bool allocate_tls_offset(Obj_Entry *obj);
void free_tls_offset(Obj_Entry *obj);
const Ver_Entry *fetch_ventry(const Obj_Entry *obj, unsigned long);
int convert_prot(int elfflags);

/*
 * MD function declarations.
 */
int do_copy_relocations(Obj_Entry *);
int reloc_non_plt(Obj_Entry *, Obj_Entry *, int flags,
    struct Struct_RtldLockState *);
int reloc_plt(Obj_Entry *, int flags, struct Struct_RtldLockState *);
int reloc_jmpslots(Obj_Entry *, int flags, struct Struct_RtldLockState *);
int reloc_iresolve(Obj_Entry *, struct Struct_RtldLockState *);
int reloc_gnu_ifunc(Obj_Entry *, int flags, struct Struct_RtldLockState *);
void ifunc_init(Elf_Auxinfo[__min_size(AT_COUNT)]);
void pre_init(void);
void init_pltgot(Obj_Entry *);
void allocate_initial_tls(Obj_Entry *);

void *__crt_calloc(size_t num, size_t size);
void __crt_free(void *cp);
void *__crt_malloc(size_t nbytes);
void *__crt_realloc(void *cp, size_t nbytes);

#endif /* } */
