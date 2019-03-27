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

#ifndef _SYS_LINK_ELF_H_
#define	_SYS_LINK_ELF_H_

#include <sys/elf.h>

/*
 * Flags that describe the origin of the entries in Dl_serinfo.
 * SunOS has these in <sys/link.h>, we follow the suit.
 */
#define	LA_SER_ORIG	0x01	/* original (needed) name */
#define	LA_SER_LIBPATH	0x02	/* LD_LIBRARY_PATH entry prepended */
#define	LA_SER_RUNPATH	0x04	/* runpath entry prepended */
#define	LA_SER_CONFIG	0x08	/* configuration entry prepended */
#define	LA_SER_DEFAULT	0x40	/* default path prepended */
#define	LA_SER_SECURE	0x80	/* default (secure) path prepended */

typedef struct link_map {
	caddr_t		l_addr;			/* Base Address of library */
#ifdef __mips__
	caddr_t		l_offs;			/* Load Offset of library */
#endif
	const char	*l_name;		/* Absolute Path to Library */
	const void	*l_ld;			/* Pointer to .dynamic in memory */
	struct link_map	*l_next, *l_prev;	/* linked list of of mapped libs */
} Link_map;

struct r_debug {
	int		r_version;		/* not used */
	struct link_map *r_map;			/* list of loaded images */
	void		(*r_brk)(struct r_debug *, struct link_map *);
						/* pointer to break point */
	enum {
		RT_CONSISTENT,			/* things are stable */
		RT_ADD,				/* adding a shared library */
		RT_DELETE			/* removing a shared library */
	}		r_state;
};

struct dl_phdr_info
{
	Elf_Addr dlpi_addr;			/* module relocation base */
	const char *dlpi_name;			/* module name */
	const Elf_Phdr *dlpi_phdr;		/* pointer to module's phdr */
	Elf_Half dlpi_phnum;			/* number of entries in phdr */
	unsigned long long int dlpi_adds;	/* total # of loads */
	unsigned long long int dlpi_subs;	/* total # of unloads */
	size_t dlpi_tls_modid;
	void *dlpi_tls_data;
};

__BEGIN_DECLS

typedef int (*__dl_iterate_hdr_callback)(struct dl_phdr_info *, size_t, void *);
extern int dl_iterate_phdr(__dl_iterate_hdr_callback, void *);
int _rtld_addr_phdr(const void *, struct dl_phdr_info *);
int _rtld_get_stack_prot(void);
int _rtld_is_dlopened(void *);

#ifdef __ARM_EABI__
void * dl_unwind_find_exidx(const void *, int *);
#endif

__END_DECLS

#endif /* _SYS_LINK_ELF_H_ */
