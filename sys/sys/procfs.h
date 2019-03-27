/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 John D. Polstra.
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

#ifndef _SYS_PROCFS_H_
#define _SYS_PROCFS_H_

#include <sys/param.h>
#include <machine/reg.h>

typedef struct reg gregset_t;
typedef struct fpreg fpregset_t;

/*
 * These structures define an interface between core files and the debugger.
 * Never change or delete any elements.  If you add elements, add them to
 * the end of the structure, and increment the value of its version field.
 * This will help to ensure that today's core dump will still be usable
 * with next year's debugger.
 *
 * A lot more things should be added to these structures.  At present,
 * they contain the absolute bare minimum required to allow GDB to work
 * with ELF core dumps.
 */

/*
 * The parenthsized numbers like (1) indicate the minimum version number
 * for which each element exists in the structure.  The version number is
 * not bumped when adding new fields to the end, only if the meaning of
 * an existing field changes.  Additional fields are annotated as (1a),
 * (1b), etc. to indicate the groupings of additions.
 */

#define PRSTATUS_VERSION	1	/* Current version of prstatus_t */

typedef struct prstatus {
    int		pr_version;	/* Version number of struct (1) */
    size_t	pr_statussz;	/* sizeof(prstatus_t) (1) */
    size_t	pr_gregsetsz;	/* sizeof(gregset_t) (1) */
    size_t	pr_fpregsetsz;	/* sizeof(fpregset_t) (1) */
    int		pr_osreldate;	/* Kernel version (1) */
    int		pr_cursig;	/* Current signal (1) */
    pid_t	pr_pid;		/* LWP (Thread) ID (1) */
    gregset_t	pr_reg;		/* General purpose registers (1) */
} prstatus_t;

typedef gregset_t prgregset_t[1];
typedef fpregset_t prfpregset_t;

#define PRFNAMESZ	16	/* Maximum command length saved */
#define PRARGSZ		80	/* Maximum argument bytes saved */

#define PRPSINFO_VERSION	1	/* Current version of prpsinfo_t */

typedef struct prpsinfo {
    int		pr_version;	/* Version number of struct (1) */
    size_t	pr_psinfosz;	/* sizeof(prpsinfo_t) (1) */
    char	pr_fname[PRFNAMESZ+1];	/* Command name, null terminated (1) */
    char	pr_psargs[PRARGSZ+1];	/* Arguments, null terminated (1) */
    pid_t	pr_pid;		/* Process ID (1a) */
} prpsinfo_t;

typedef struct thrmisc {
    char	pr_tname[MAXCOMLEN+1];	/* Thread name, null terminated (1) */
    u_int	_pad;			/* Convenience pad, 0-filled (1) */
} thrmisc_t;

typedef uint64_t psaddr_t;	/* An address in the target process. */

#ifdef __HAVE_REG32
typedef struct prstatus32 {
	int32_t	pr_version;
	uint32_t pr_statussz;
	uint32_t pr_gregsetsz;
	uint32_t pr_fpregsetsz;
	int32_t	pr_osreldate;
	int32_t	pr_cursig;
	int32_t	pr_pid;
	struct reg32 pr_reg;
} prstatus32_t;

typedef struct prpsinfo32 {
	int32_t	pr_version;
	uint32_t pr_psinfosz;
	char	pr_fname[PRFNAMESZ+1];
	char	pr_psargs[PRARGSZ+1];
	int32_t	pr_pid;
} prpsinfo32_t;

struct thrmisc32 {
	char	pr_tname[MAXCOMLEN+1];
	uint32_t _pad;
};
#endif /* __HAVE_REG32 */

#endif /* _SYS_PROCFS_H_ */
