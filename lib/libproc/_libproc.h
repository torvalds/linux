/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 John Birrell (jb@freebsd.org)
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
 *
 * $FreeBSD$
 */

#ifndef __LIBPROC_H_
#define	__LIBPROC_H_

#include <sys/types.h>
#include <sys/ptrace.h>

#include <libelf.h>
#include <rtld_db.h>

#include "libproc.h"

struct procstat;

struct symtab {
	Elf_Data *data;
	u_int	nsyms;
	u_int	*index;
	u_long	stridx;
};

struct file_info {
	Elf	*elf;
	int	fd;
	u_int	refs;
	GElf_Ehdr ehdr;

	/* Symbol tables, sorted by value. */
	struct symtab dynsymtab;
	struct symtab symtab;
};

struct map_info {
	prmap_t	map;
	struct file_info *file;
};

struct proc_handle {
	struct proc_handle_public public; /* Public fields. */
	int	flags;			/* Process flags. */
	int	status;			/* Process status (PS_*). */
	int	wstat;			/* Process wait status. */
	int	model;			/* Process data model. */
	rd_agent_t *rdap;		/* librtld_db agent */
	struct map_info *mappings;	/* File mappings for proc. */
	size_t	maparrsz;		/* Map array size. */
	size_t	nmappings;		/* Number of mappings. */
	size_t	exec_map;		/* Executable text mapping index. */
	lwpstatus_t lwps;		/* Process status. */
	struct procstat *procstat;	/* libprocstat handle. */
	char	execpath[PATH_MAX];	/* Path to program executable. */
};

#ifdef DEBUG
#define	DPRINTF(...) 	warn(__VA_ARGS__)
#define	DPRINTFX(...)	warnx(__VA_ARGS__)
#else
#define	DPRINTF(...)    do { } while (0)
#define	DPRINTFX(...)   do { } while (0)
#endif

#endif /* __LIBPROC_H_ */
