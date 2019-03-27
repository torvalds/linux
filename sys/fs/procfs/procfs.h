/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)procfs.h	8.9 (Berkeley) 5/14/95
 *
 * From:
 * $FreeBSD$
 */

#ifdef _KERNEL

int	 procfs_docurproc(PFS_FILL_ARGS);
int	 procfs_doosrel(PFS_FILL_ARGS);
int	 procfs_doproccmdline(PFS_FILL_ARGS);
int	 procfs_doprocdbregs(PFS_FILL_ARGS);
int	 procfs_doprocfile(PFS_FILL_ARGS);
int	 procfs_doprocfpregs(PFS_FILL_ARGS);
int	 procfs_doprocmap(PFS_FILL_ARGS);
int	 procfs_doprocmem(PFS_FILL_ARGS);
int	 procfs_doprocnote(PFS_FILL_ARGS);
int	 procfs_doprocregs(PFS_FILL_ARGS);
int	 procfs_doprocrlimit(PFS_FILL_ARGS);
int	 procfs_doprocstatus(PFS_FILL_ARGS);
int	 procfs_doproctype(PFS_FILL_ARGS);
int	 procfs_ioctl(PFS_IOCTL_ARGS);
int	 procfs_close(PFS_CLOSE_ARGS);

/* Attributes */
int	 procfs_attr_w(PFS_ATTR_ARGS);
int	 procfs_attr_rw(PFS_ATTR_ARGS);
int	 procfs_attr_all_rx(PFS_ATTR_ARGS);

/* Visibility */
int	 procfs_notsystem(PFS_VIS_ARGS);
int	 procfs_candebug(PFS_VIS_ARGS);

#endif /* _KERNEL */
