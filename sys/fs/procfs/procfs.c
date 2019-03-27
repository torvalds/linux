/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Dag-Erling Smørgrav
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)procfs_vfsops.c	8.7 (Berkeley) 5/10/95
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/exec.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/sysproto.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <fs/pseudofs/pseudofs.h>
#include <fs/procfs/procfs.h>

/*
 * Filler function for proc/pid/file
 */
int
procfs_doprocfile(PFS_FILL_ARGS)
{
	char *fullpath;
	char *freepath;
	struct vnode *textvp;
	int error;

	freepath = NULL;
	PROC_LOCK(p);
	textvp = p->p_textvp;
	vhold(textvp);
	PROC_UNLOCK(p);
	error = vn_fullpath(td, textvp, &fullpath, &freepath);
	vdrop(textvp);
	if (error == 0)
		sbuf_printf(sb, "%s", fullpath);
	if (freepath != NULL)
		free(freepath, M_TEMP);
	return (error);
}

/*
 * Filler function for proc/curproc
 */
int
procfs_docurproc(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "%ld", (long)td->td_proc->p_pid);
	return (0);
}

static int
procfs_attr(PFS_ATTR_ARGS, int mode) {

	vap->va_mode = mode;
	if (p != NULL) {
		PROC_LOCK_ASSERT(p, MA_OWNED);

		if ((p->p_flag & P_SUGID) && pn->pn_type != pfstype_procdir)
			vap->va_mode = 0;
	}

	return (0);
}

int
procfs_attr_all_rx(PFS_ATTR_ARGS)
{

	return (procfs_attr(td, p, pn, vap, 0555));
}

int
procfs_attr_rw(PFS_ATTR_ARGS)
{

	return (procfs_attr(td, p, pn, vap, 0600));
}

int
procfs_attr_w(PFS_ATTR_ARGS)
{

	return (procfs_attr(td, p, pn, vap, 0200));
}

/*
 * Visibility: some files only exist for non-system processes
 * Non-static because linprocfs uses it.
 */
int
procfs_notsystem(PFS_VIS_ARGS)
{
	PROC_LOCK_ASSERT(p, MA_OWNED);
	return ((p->p_flag & P_SYSTEM) == 0);
}

/*
 * Visibility: some files are only visible to process that can debug
 * the target process.
 */
int
procfs_candebug(PFS_VIS_ARGS)
{
	PROC_LOCK_ASSERT(p, MA_OWNED);
	return ((p->p_flag & P_SYSTEM) == 0 && p_candebug(td, p) == 0);
}

/*
 * Constructor
 */
static int
procfs_init(PFS_INIT_ARGS)
{
	struct pfs_node *root;
	struct pfs_node *dir;
	struct pfs_node *node;

	root = pi->pi_root;

	pfs_create_link(root, "curproc", procfs_docurproc,
	    NULL, NULL, NULL, 0);

	dir = pfs_create_dir(root, "pid",
	    procfs_attr_all_rx, NULL, NULL, PFS_PROCDEP);
	pfs_create_file(dir, "cmdline", procfs_doproccmdline,
	    NULL, NULL, NULL, PFS_RD);
	pfs_create_file(dir, "dbregs", procfs_doprocdbregs,
	    procfs_attr_rw, procfs_candebug, NULL, PFS_RDWR | PFS_RAW);
	pfs_create_file(dir, "etype", procfs_doproctype,
	    NULL, NULL, NULL, PFS_RD);
	pfs_create_file(dir, "fpregs", procfs_doprocfpregs,
	    procfs_attr_rw, procfs_candebug, NULL, PFS_RDWR | PFS_RAW);
	pfs_create_file(dir, "map", procfs_doprocmap,
	    NULL, procfs_notsystem, NULL, PFS_RD);
	node = pfs_create_file(dir, "mem", procfs_doprocmem,
	    procfs_attr_rw, procfs_candebug, NULL, PFS_RDWR | PFS_RAW);
	node->pn_ioctl = procfs_ioctl;
	node->pn_close = procfs_close;
	pfs_create_file(dir, "note", procfs_doprocnote,
	    procfs_attr_w, procfs_candebug, NULL, PFS_WR);
	pfs_create_file(dir, "notepg", procfs_doprocnote,
	    procfs_attr_w, procfs_candebug, NULL, PFS_WR);
	pfs_create_file(dir, "regs", procfs_doprocregs,
	    procfs_attr_rw, procfs_candebug, NULL, PFS_RDWR | PFS_RAW);
	pfs_create_file(dir, "rlimit", procfs_doprocrlimit,
	    NULL, NULL, NULL, PFS_RD);
	pfs_create_file(dir, "status", procfs_doprocstatus,
	    NULL, NULL, NULL, PFS_RD);
	pfs_create_file(dir, "osrel", procfs_doosrel,
	    procfs_attr_rw, procfs_candebug, NULL, PFS_RDWR);

	pfs_create_link(dir, "file", procfs_doprocfile,
	    NULL, procfs_notsystem, NULL, 0);

	return (0);
}

/*
 * Destructor
 */
static int
procfs_uninit(PFS_INIT_ARGS)
{
	/* nothing to do, pseudofs will GC */
	return (0);
}

PSEUDOFS(procfs, 1, VFCF_JAIL);
