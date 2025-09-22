/*	$OpenBSD: link.h,v 1.15 2013/10/19 09:00:18 deraadt Exp $	*/
/*	$NetBSD: link.h,v 1.10 1996/01/09 00:00:11 pk Exp $	*/

/*
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
 */


#ifndef _LINK_H_
#define _LINK_H_

#ifdef __ELF__
#include <link_elf.h>
#endif

/*
 * A `Shared Object Descriptor' describes a shared object that is needed
 * to complete the link edit process of the object containing it.
 * A list of such objects (chained through `sod_next') is pointed at
 * by `sdt_sods' in the section_dispatch_table structure.
 */

struct sod {	/* Shared Object Descriptor */
	long		sod_name;	/* name (relative to load address) */
	unsigned int 	sod_library : 1,/* Searched for by library rules */
			sod_reserved : 31;
	short		sod_major;	/* major version number */
	short		sod_minor;	/* minor version number */
	long		sod_next;	/* next sod */
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
	unsigned int	som_write : 1;	/* Text is currently writable */
	struct _dynamic	*som_dynamic;	/* _dynamic structure */
	caddr_t		som_spd;	/* Private data */
};


/*
 *	Debug rendezvous struct. Pointer to this is set up in the
 *	target code pointed by the DT_DEBUG tag. If it is
 *	defined.
 */
struct r_debug {
	int	r_version;		/* Protocol version. */
	struct link_map *r_map;		/* Head of list of loaded objects. */

	/*
	 * This is the address of a function internal to the run-time linker,
	 * that will always be called when the linker begins to map in a
	 * library or unmap it, and again when the mapping change is complete.
	 * The debugger can set a breakpoint at this address if it wants to
	 * notice shared object mapping changes.
	 */
	unsigned long r_brk;
	enum {
		/*
		 * This state value describes the mapping change taking place
		 * when the `r_brk' address is called.
		 */
		RT_CONSISTENT,		/* Mapping change is complete.  */
		RT_ADD,			/* Adding a new object.  */
		RT_DELETE		/* Removing an object mapping.  */
	} r_state;

	unsigned long r_ldbase;		/* Base address the linker is loaded at.  */
};



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

#endif /* _LINK_H_ */

