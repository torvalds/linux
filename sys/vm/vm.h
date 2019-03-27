/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND MIT-CMU)
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)vm.h	8.2 (Berkeley) 12/13/93
 *	@(#)vm_prot.h	8.1 (Berkeley) 6/11/93
 *	@(#)vm_inherit.h	8.1 (Berkeley) 6/11/93
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

#ifndef VM_H
#define VM_H

#include <machine/vm.h>

typedef char vm_inherit_t;	/* inheritance codes */

#define	VM_INHERIT_SHARE	((vm_inherit_t) 0)
#define	VM_INHERIT_COPY		((vm_inherit_t) 1)
#define	VM_INHERIT_NONE		((vm_inherit_t) 2)
#define	VM_INHERIT_ZERO		((vm_inherit_t) 3)
#define	VM_INHERIT_DEFAULT	VM_INHERIT_COPY

typedef u_char vm_prot_t;	/* protection codes */

#define	VM_PROT_NONE		((vm_prot_t) 0x00)
#define	VM_PROT_READ		((vm_prot_t) 0x01)
#define	VM_PROT_WRITE		((vm_prot_t) 0x02)
#define	VM_PROT_EXECUTE		((vm_prot_t) 0x04)
#define	VM_PROT_COPY		((vm_prot_t) 0x08)	/* copy-on-read */
#define	VM_PROT_PRIV_FLAG	((vm_prot_t) 0x10)
#define	VM_PROT_FAULT_LOOKUP	VM_PROT_PRIV_FLAG
#define	VM_PROT_QUICK_NOFAULT	VM_PROT_PRIV_FLAG	/* same to save bits */

#define	VM_PROT_ALL		(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE)
#define VM_PROT_RW		(VM_PROT_READ|VM_PROT_WRITE)
#define	VM_PROT_DEFAULT		VM_PROT_ALL

enum obj_type { OBJT_DEFAULT, OBJT_SWAP, OBJT_VNODE, OBJT_DEVICE, OBJT_PHYS,
		OBJT_DEAD, OBJT_SG, OBJT_MGTDEVICE };
typedef u_char objtype_t;

union vm_map_object;
typedef union vm_map_object vm_map_object_t;

struct vm_map_entry;
typedef struct vm_map_entry *vm_map_entry_t;

struct vm_map;
typedef struct vm_map *vm_map_t;

struct vm_object;
typedef struct vm_object *vm_object_t;

#ifndef _KERNEL
/*
 * This is defined in <sys/types.h> for the kernel so that non-vm kernel
 * sources (mainly Mach-derived ones such as ddb) don't have to include
 * vm stuff.  Defining it there for applications might break things.
 * Define it here for "applications" that include vm headers (e.g.,
 * genassym).
 */
typedef int boolean_t;

/*
 * The exact set of memory attributes is machine dependent.  However,
 * every machine is required to define VM_MEMATTR_DEFAULT and
 * VM_MEMATTR_UNCACHEABLE.
 */
typedef	char vm_memattr_t;	/* memory attribute codes */

/*
 * This is defined in <sys/types.h> for the kernel so that vnode_if.h
 * doesn't have to include <vm/vm.h>.
 */
struct vm_page;
typedef struct vm_page *vm_page_t;
#endif				/* _KERNEL */

struct vm_reserv;
typedef struct vm_reserv *vm_reserv_t;

/*
 * Information passed from the machine-independant VM initialization code
 * for use by machine-dependant code (mainly for MMU support)
 */
struct kva_md_info {
	vm_offset_t	buffer_sva;
	vm_offset_t	buffer_eva;
	vm_offset_t	clean_sva;
	vm_offset_t	clean_eva;
};

extern struct kva_md_info	kmi;
extern void vm_ksubmap_init(struct kva_md_info *);

extern int old_mlock;

extern int vm_ndomains;

struct ucred;
int swap_reserve(vm_ooffset_t incr);
int swap_reserve_by_cred(vm_ooffset_t incr, struct ucred *cred);
void swap_reserve_force(vm_ooffset_t incr);
void swap_release(vm_ooffset_t decr);
void swap_release_by_cred(vm_ooffset_t decr, struct ucred *cred);
void swapper(void);

#endif				/* VM_H */

