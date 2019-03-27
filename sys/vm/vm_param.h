/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University.
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
 *	from: @(#)vm_param.h	8.1 (Berkeley) 6/11/93
 *
 *
 * Copyright (c) 1987, 1990 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Avadis Tevanian, Jr., Michael Wayne Young
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 *
 * $FreeBSD$
 */

/*
 *	Machine independent virtual memory parameters.
 */

#ifndef	_VM_PARAM_
#define	_VM_PARAM_

#include <machine/vmparam.h>

/*
 * CTL_VM identifiers
 */
#define	VM_TOTAL		1	/* struct vmtotal */
#define	VM_METER                VM_TOTAL/* deprecated, use VM_TOTAL */
#define	VM_LOADAVG	 	2	/* struct loadavg */
#define VM_V_FREE_MIN		3	/* vm_cnt.v_free_min */
#define VM_V_FREE_TARGET	4	/* vm_cnt.v_free_target */
#define VM_V_FREE_RESERVED	5	/* vm_cnt.v_free_reserved */
#define VM_V_INACTIVE_TARGET	6	/* vm_cnt.v_inactive_target */
#define	VM_OBSOLETE_7		7	/* unused, formerly v_cache_min */
#define	VM_OBSOLETE_8		8	/* unused, formerly v_cache_max */
#define VM_V_PAGEOUT_FREE_MIN	9	/* vm_cnt.v_pageout_free_min */
#define	VM_OBSOLETE_10		10	/* pageout algorithm */
#define VM_SWAPPING_ENABLED	11	/* swapping enabled */
#define VM_OVERCOMMIT		12	/* vm.overcommit */
#define	VM_MAXID		13	/* number of valid vm ids */

/*
 * Structure for swap device statistics
 */
#define XSWDEV_VERSION	2
struct xswdev {
	u_int	xsw_version;
	dev_t	xsw_dev;
	int	xsw_flags;
	int	xsw_nblks;
	int     xsw_used;
};

/*
 *	Return values from the VM routines.
 */
#define	KERN_SUCCESS		0
#define	KERN_INVALID_ADDRESS	1
#define	KERN_PROTECTION_FAILURE	2
#define	KERN_NO_SPACE		3
#define	KERN_INVALID_ARGUMENT	4
#define	KERN_FAILURE		5
#define	KERN_RESOURCE_SHORTAGE	6
#define	KERN_NOT_RECEIVER	7
#define	KERN_NO_ACCESS		8

#ifndef PA_LOCK_COUNT
#ifdef SMP
#define	PA_LOCK_COUNT	32
#else
#define PA_LOCK_COUNT	1
#endif	/* !SMP */
#endif	/* !PA_LOCK_COUNT */

#ifndef ASSEMBLER
#ifdef _KERNEL
#define num_pages(x) \
	((vm_offset_t)((((vm_offset_t)(x)) + PAGE_MASK) >> PAGE_SHIFT))
extern	unsigned long maxtsiz;
extern	unsigned long dfldsiz;
extern	unsigned long maxdsiz;
extern	unsigned long dflssiz;
extern	unsigned long maxssiz;
extern	unsigned long sgrowsiz;
#endif				/* _KERNEL */
#endif				/* ASSEMBLER */
#endif				/* _VM_PARAM_ */
