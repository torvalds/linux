/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * Copyright (c) 2008 John Birrell (jb@freebsd.org)
 * All rights reserved.
 *
 * Portions of this software were developed by Rui Paulo under sponsorship
 * from the FreeBSD Foundation.
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

#ifndef	_LIBPROC_H_
#define	_LIBPROC_H_

#include <gelf.h>
#include <rtld_db.h>
#include <limits.h>

struct ctf_file;
struct proc_handle;

typedef void (*proc_child_func)(void *);

/* Values returned by proc_state(). */
#define PS_IDLE		1
#define PS_STOP		2
#define PS_RUN		3
#define PS_UNDEAD	4
#define PS_DEAD		5
#define PS_LOST		6

/* Flags for proc_attach(). */
#define	PATTACH_FORCE	0x01
#define	PATTACH_RDONLY	0x02
#define	PATTACH_NOSTOP	0x04

/* Reason values for proc_detach(). */
#define PRELEASE_HANG	1
#define PRELEASE_KILL	2

typedef struct prmap {
	uintptr_t	pr_vaddr;	/* Virtual address. */
	size_t		pr_size;	/* Mapping size in bytes */
	size_t		pr_offset;	/* Mapping offset in object */
	char		pr_mapname[PATH_MAX];	/* Mapping filename */
	uint8_t		pr_mflags;	/* Protection flags */
#define	MA_READ		0x01
#define	MA_WRITE	0x02
#define	MA_EXEC		0x04
#define	MA_COW		0x08
#define MA_NEEDS_COPY	0x10
#define	MA_NOCOREDUMP	0x20
} prmap_t;

typedef struct prsyminfo {
	u_int		prs_lmid;	/* Map id. */
	u_int		prs_id;		/* Symbol id. */
} prsyminfo_t;

typedef int proc_map_f(void *, const prmap_t *, const char *);
typedef int proc_sym_f(void *, const GElf_Sym *, const char *);

/* Values for ELF sections */
#define	PR_SYMTAB	1
#define PR_DYNSYM	2

/* Values for the 'mask' parameter in the iteration functions */
#define	BIND_LOCAL	0x0001
#define BIND_GLOBAL	0x0002
#define BIND_WEAK	0x0004
#define BIND_ANY	(BIND_LOCAL|BIND_GLOBAL|BIND_WEAK)
#define TYPE_NOTYPE	0x0100
#define TYPE_OBJECT	0x0200
#define TYPE_FUNC	0x0400
#define TYPE_SECTION	0x0800
#define TYPE_FILE	0x1000
#define TYPE_ANY	(TYPE_NOTYPE|TYPE_OBJECT|TYPE_FUNC|TYPE_SECTION|\
    			 TYPE_FILE)

typedef enum {
	REG_PC,
	REG_SP,
	REG_RVAL1,
	REG_RVAL2
} proc_reg_t;

#define SIG2STR_MAX	8

typedef struct lwpstatus {
	int pr_why;
#define PR_REQUESTED	1
#define PR_FAULTED	2
#define PR_SYSENTRY	3
#define PR_SYSEXIT	4
#define PR_SIGNALLED	5
	int pr_what;
#define FLTBPT		-1
} lwpstatus_t;

#define	PR_MODEL_ILP32	1
#define	PR_MODEL_LP64	2

struct proc_handle_public {
	pid_t		pid;
};

#define	proc_getpid(phdl)	(((struct proc_handle_public *)(phdl))->pid)

/* Function prototype definitions. */
__BEGIN_DECLS

prmap_t *proc_addr2map(struct proc_handle *, uintptr_t);
prmap_t *proc_name2map(struct proc_handle *, const char *);
char	*proc_objname(struct proc_handle *, uintptr_t, char *, size_t);
int	proc_iter_objs(struct proc_handle *, proc_map_f *, void *);
int	proc_iter_symbyaddr(struct proc_handle *, const char *, int,
	    int, proc_sym_f *, void *);
int	proc_addr2sym(struct proc_handle *, uintptr_t, char *, size_t, GElf_Sym *);
int	proc_attach(pid_t pid, int flags, struct proc_handle **pphdl);
int	proc_continue(struct proc_handle *);
int	proc_clearflags(struct proc_handle *, int);
int	proc_create(const char *, char * const *, char * const *,
	    proc_child_func *, void *, struct proc_handle **);
int	proc_detach(struct proc_handle *, int);
int	proc_getflags(struct proc_handle *);
int	proc_name2sym(struct proc_handle *, const char *, const char *,
	    GElf_Sym *, prsyminfo_t *);
struct ctf_file *proc_name2ctf(struct proc_handle *, const char *);
int	proc_setflags(struct proc_handle *, int);
int	proc_state(struct proc_handle *);
int	proc_getmodel(struct proc_handle *);
int	proc_wstatus(struct proc_handle *);
int	proc_getwstat(struct proc_handle *);
char *	proc_signame(int, char *, size_t);
int	proc_read(struct proc_handle *, void *, size_t, size_t);
const lwpstatus_t *proc_getlwpstatus(struct proc_handle *);
void	proc_free(struct proc_handle *);
rd_agent_t *proc_rdagent(struct proc_handle *);
void	proc_updatesyms(struct proc_handle *);
int	proc_bkptset(struct proc_handle *, uintptr_t, unsigned long *);
int	proc_bkptdel(struct proc_handle *, uintptr_t, unsigned long);
void	proc_bkptregadj(unsigned long *);
int	proc_bkptexec(struct proc_handle *, unsigned long);
int	proc_regget(struct proc_handle *, proc_reg_t, unsigned long *);
int	proc_regset(struct proc_handle *, proc_reg_t, unsigned long);

__END_DECLS

#endif /* _LIBPROC_H_ */
