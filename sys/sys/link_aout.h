/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1993 Paul Kranenburg
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
 *      This product includes software developed by Paul Kranenburg.
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
 *
 * $FreeBSD$
 */

/*
 * RRS section definitions.
 *
 * The layout of some data structures defined in this header file is
 * such that we can provide compatibility with the SunOS 4.x shared
 * library scheme.
 */

#ifndef _SYS_LINK_AOUT_H_
#define _SYS_LINK_AOUT_H_

struct dl_info;

/*
 * A `Shared Object Descriptor' describes a shared object that is needed
 * to complete the link edit process of the object containing it.
 * A list of such objects (chained through `sod_next') is pointed at
 * by `sdt_sods' in the section_dispatch_table structure.
 */

struct sod {	/* Shared Object Descriptor */
	long	sod_name;		/* name (relative to load address) */
	u_int	sod_library  : 1,	/* Searched for by library rules */
		sod_reserved : 31;
	short	sod_major;		/* major version number */
	short	sod_minor;		/* minor version number */
	long	sod_next;		/* next sod */
};

/*
 * `Shared Object Map's are used by the run-time link editor (ld.so) to
 * keep track of all shared objects loaded into a process' address space.
 * These structures are only used at run-time and do not occur within
 * the text or data segment of an executable or shared library.
 */
struct so_map {		/* Shared Object Map */
	caddr_t		som_addr;	/* Address at which object mapped */
	char 		*som_path;	/* Path to mmap'ed file */
	struct so_map	*som_next;	/* Next map in chain */
	struct sod	*som_sod;	/* Sod responsible for this map */
	caddr_t		som_sodbase;	/* Base address of this sod */
	u_int		som_write : 1;	/* Text is currently writable */
	struct _dynamic	*som_dynamic;	/* _dynamic structure */
	caddr_t		som_spd;	/* Private data */
};

/*
 * Symbol description with size. This is simply an `nlist' with
 * one field (nz_size) added.
 * Used to convey size information on items in the data segment
 * of shared objects. An array of these live in the shared object's
 * text segment and is addressed by the `sdt_nzlist' field.
 */
struct nzlist {
	struct nlist	nlist;
	u_long		nz_size;
};

#define nz_un		nlist.n_un
#define nz_strx		nlist.n_un.n_strx
#define nz_name		nlist.n_un.n_name
#define nz_type		nlist.n_type
#define nz_value	nlist.n_value
#define nz_desc		nlist.n_desc
#define nz_other	nlist.n_other

/*
 * The `section_dispatch_table' structure contains offsets to various data
 * structures needed to do run-time relocation.
 */
struct section_dispatch_table {
	struct so_map *sdt_loaded;	/* List of loaded objects */
	long	sdt_sods;		/* List of shared objects descriptors */
	long	sdt_paths;		/* Library search paths */
	long	sdt_got;		/* Global offset table */
	long	sdt_plt;		/* Procedure linkage table */
	long	sdt_rel;		/* Relocation table */
	long	sdt_hash;		/* Symbol hash table */
	long	sdt_nzlist;		/* Symbol table itself */
	long	sdt_filler2;		/* Unused (was: stab_hash) */
	long	sdt_buckets;		/* Number of hash buckets */
	long	sdt_strings;		/* Symbol strings */
	long	sdt_str_sz;		/* Size of symbol strings */
	long	sdt_text_sz;		/* Size of text area */
	long	sdt_plt_sz;		/* Size of procedure linkage table */
};

/*
 * RRS symbol hash table, addressed by `sdt_hash' in section_dispatch_table.
 * Used to quickly lookup symbols of the shared object by hashing
 * on the symbol's name. `rh_symbolnum' is the index of the symbol
 * in the shared object's symbol list (`sdt_nzlist'), `rh_next' is
 * the next symbol in the hash bucket (in case of collisions).
 */
struct rrs_hash {
	int	rh_symbolnum;		/* Symbol number */
	int	rh_next;		/* Next hash entry */
};

/*
 * `rt_symbols' is used to keep track of run-time allocated commons
 * and data items copied from shared objects.
 */
struct rt_symbol {
	struct nzlist		*rt_sp;		/* The symbol */
	struct rt_symbol	*rt_next;	/* Next in linear list */
	struct rt_symbol	*rt_link;	/* Next in bucket */
	caddr_t			rt_srcaddr;	/* Address of "master" copy */
	struct so_map		*rt_smp;	/* Originating map */
};

/*
 * Debugger interface structure.
 */
struct so_debug {
	int	dd_version;		/* Version # of interface */
	int	dd_in_debugger;		/* Set when run by debugger */
	int	dd_sym_loaded;		/* Run-time linking brought more
					   symbols into scope */
	char   	 *dd_bpt_addr;		/* Address of rtld-generated bpt */
	int	dd_bpt_shadow;		/* Original contents of bpt */
	struct rt_symbol *dd_cc;	/* Allocated commons/copied data */
};

/*
 * Version returned to crt0 from ld.so
 */
#define LDSO_VERSION_NONE	0	/* FreeBSD2.0, 2.0.5 */
#define LDSO_VERSION_HAS_DLEXIT	1	/* includes dlexit in ld_entry */
#define LDSO_VERSION_HAS_DLSYM3	2	/* includes 3-argument dlsym */
#define LDSO_VERSION_HAS_DLADDR	3	/* includes dladdr in ld_entry */

/*
 * Entry points into ld.so - user interface to the run-time linker.
 * Entries are valid for the given version numbers returned by ld.so
 * to crt0.
 */
struct ld_entry {
	void	*(*dlopen)(const char *, int);	/* NONE */
	int	(*dlclose)(void *);		/* NONE */
	void	*(*dlsym)(void *, const char *);	/* NONE */
	const char *(*dlerror)(void);		/* NONE */
	void	(*dlexit)(void);			/* HAS_DLEXIT */
	void	*(*dlsym3)(void *, const char *, void *); /* HAS_DLSYM3 */
	int	 (*dladdr)(const void *, struct dl_info *); /* HAS_DLADDR */
};

/*
 * This is the structure pointed at by the __DYNAMIC symbol if an
 * executable requires the attention of the run-time link editor.
 * __DYNAMIC is given the value zero if no run-time linking needs to
 * be done (it is always present in shared objects).
 * The union `d_un' provides for different versions of the dynamic
 * linking mechanism (switched on by `d_version'). The last version
 * used by Sun is 3. We leave some room here and go to version number
 * 8 for NetBSD, the main difference lying in the support for the
 * `nz_list' type of symbols.
 */

struct _dynamic {
	int		d_version;	/* version # of this interface */
	struct so_debug	*d_debug;
	union {
		struct section_dispatch_table *d_sdt;
	} d_un;
	struct ld_entry *d_entry;	/* XXX */
};

#define LD_VERSION_SUN		(3)
#define LD_VERSION_BSD		(8)
#define LD_VERSION_NZLIST_P(v)	((v) >= 8)

#define LD_GOT(x)	((x)->d_un.d_sdt->sdt_got)
#define LD_PLT(x)	((x)->d_un.d_sdt->sdt_plt)
#define LD_REL(x)	((x)->d_un.d_sdt->sdt_rel)
#define LD_SYMBOL(x)	((x)->d_un.d_sdt->sdt_nzlist)
#define LD_HASH(x)	((x)->d_un.d_sdt->sdt_hash)
#define LD_STRINGS(x)	((x)->d_un.d_sdt->sdt_strings)
#define LD_NEED(x)	((x)->d_un.d_sdt->sdt_sods)
#define LD_BUCKETS(x)	((x)->d_un.d_sdt->sdt_buckets)
#define LD_PATHS(x)	((x)->d_un.d_sdt->sdt_paths)

#define LD_GOTSZ(x)	((x)->d_un.d_sdt->sdt_plt - (x)->d_un.d_sdt->sdt_got)
#define LD_RELSZ(x)	((x)->d_un.d_sdt->sdt_hash - (x)->d_un.d_sdt->sdt_rel)
#define LD_HASHSZ(x)	((x)->d_un.d_sdt->sdt_nzlist - (x)->d_un.d_sdt->sdt_hash)
#define LD_STABSZ(x)	((x)->d_un.d_sdt->sdt_strings - (x)->d_un.d_sdt->sdt_nzlist)
#define LD_PLTSZ(x)	((x)->d_un.d_sdt->sdt_plt_sz)
#define LD_STRSZ(x)	((x)->d_un.d_sdt->sdt_str_sz)
#define LD_TEXTSZ(x)	((x)->d_un.d_sdt->sdt_text_sz)

/*
 * Interface to ld.so
 */
struct crt_ldso {
	int		crt_ba;		/* Base address of ld.so */
	int		crt_dzfd;	/* "/dev/zero" file descriptor (SunOS) */
	int		crt_ldfd;	/* ld.so file descriptor */
	struct _dynamic	*crt_dp;	/* Main's __DYNAMIC */
	char		**crt_ep;	/* environment strings */
	caddr_t		crt_bp;		/* Breakpoint if run from debugger */
	char		*crt_prog;	/* Program name (v3) */
	char		*crt_ldso;	/* Link editor name (v4) */
	struct ld_entry	*crt_ldentry;	/* dl*() access (v4) */
	char		**crt_argv;	/* argument strings (v5) */
};

/*
 * Version passed from crt0 to ld.so (1st argument to _rtld()).
 */
#define CRT_VERSION_SUN		1
#define CRT_VERSION_BSD_2	2
#define CRT_VERSION_BSD_3	3
#define CRT_VERSION_BSD_4	4
#define CRT_VERSION_BSD_5	5

/*
 * Maximum number of recognized shared object version numbers.
 */
#define MAXDEWEY	8

/*
 * Header of the hints file.
 */
struct hints_header {
	long		hh_magic;
#define HH_MAGIC	011421044151
	long		hh_version;	/* Interface version number */
#define LD_HINTS_VERSION_1	1
#define LD_HINTS_VERSION_2	2
	long		hh_hashtab;	/* Location of hash table */
	long		hh_nbucket;	/* Number of buckets in hashtab */
	long		hh_strtab;	/* Location of strings */
	long		hh_strtab_sz;	/* Size of strings */
	long		hh_ehints;	/* End of hints (max offset in file) */
	long		hh_dirlist;	/* Colon-separated list of srch dirs */
};

#define HH_BADMAG(hdr)	((hdr).hh_magic != HH_MAGIC)

/*
 * Hash table element in hints file.
 */
struct hints_bucket {
	/* namex and pathx are indices into the string table */
	int		hi_namex;		/* Library name */
	int		hi_pathx;		/* Full path */
	int		hi_dewey[MAXDEWEY];	/* The versions */
	int		hi_ndewey;		/* Number of version numbers */
#define hi_major hi_dewey[0]
#define hi_minor hi_dewey[1]
	int		hi_next;		/* Next in this bucket */
};

#define _PATH_LD_HINTS		"/var/run/ld.so.hints"

#endif /* _SYS_LINK_AOUT_H_ */
