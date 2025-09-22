/*	$OpenBSD: uvm_pager.h,v 1.33 2021/10/12 07:38:22 mpi Exp $	*/
/*	$NetBSD: uvm_pager.h,v 1.20 2000/11/27 08:40:05 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 * from: Id: uvm_pager.h,v 1.1.2.14 1998/01/13 19:00:50 chuck Exp
 */

/*
 * Copyright (c) 1990 University of Utah.
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *	@(#)vm_pager.h	8.5 (Berkeley) 7/7/94
 */

#ifndef _UVM_UVM_PAGER_H_
#define _UVM_UVM_PAGER_H_

#ifdef _KERNEL

struct uvm_pagerops {
						/* init pager */
	void			(*pgo_init)(void);
						/* add reference to obj */
	void			(*pgo_reference)(struct uvm_object *);
						/* drop reference to obj */
	void			(*pgo_detach)(struct uvm_object *);
						/* special nonstd fault fn */
	int			(*pgo_fault)(struct uvm_faultinfo *, vaddr_t,
				 vm_page_t *, int, int, vm_fault_t,
				 vm_prot_t, int);
						/* flush pages out of obj */
	boolean_t		(*pgo_flush)(struct uvm_object *, voff_t,
				 voff_t, int);
						/* get/read page */
	int			(*pgo_get)(struct uvm_object *, voff_t,
				 vm_page_t *, int *, int, vm_prot_t, int, int);
						/* put/write page */
	int			(*pgo_put)(struct uvm_object *, vm_page_t *,
				 int, boolean_t);
						/* return range of cluster */
	void			(*pgo_cluster)(struct uvm_object *, voff_t,
				 voff_t *, voff_t *);
						/* make "put" cluster */
	struct vm_page **	(*pgo_mk_pcluster)(struct uvm_object *,
				 struct vm_page **, int *, struct vm_page *,
				 int, voff_t, voff_t);
};

/* pager flags [mostly for flush] */

#define PGO_CLEANIT	0x001	/* write dirty pages to backing store */
#define PGO_SYNCIO	0x002	/* if PGO_CLEANIT: use sync I/O? */
#define PGO_DEACTIVATE	0x004	/* deactivate flushed pages */
#define PGO_FREE	0x008	/* free flushed pages */
/* if PGO_FREE is not set then the pages stay where they are. */

#define PGO_ALLPAGES	0x010	/* flush whole object/get all pages */
#define PGO_DOACTCLUST	0x020	/* flag to mk_pcluster to include active */
#define PGO_LOCKED	0x040	/* fault data structures are locked [get] */
#define PGO_PDFREECLUST	0x080	/* daemon's free cluster flag [uvm_pager_put] */
#define PGO_REALLOCSWAP	0x100	/* reallocate swap area [pager_dropcluster] */
#define PGO_NOWAIT	0x200	/* do not wait for inode lock */

/* page we are not interested in getting */
#define PGO_DONTCARE ((struct vm_page *) -1L)	/* [get only] */

/*
 * prototypes
 */

void		uvm_pager_dropcluster(struct uvm_object *, struct vm_page *,
		    struct vm_page **, int *, int);
void		uvm_pager_init(void);
int		uvm_pager_put(struct uvm_object *, struct vm_page *, 
		    struct vm_page ***, int *, int, voff_t, voff_t);


vaddr_t		uvm_pagermapin(struct vm_page **, int, int);
void		uvm_pagermapout(vaddr_t, int);
struct vm_page **uvm_mk_pcluster(struct uvm_object *, struct vm_page **,
		    int *, struct vm_page *, int, voff_t, voff_t);

/* Flags to uvm_pagermapin() */
#define	UVMPAGER_MAPIN_WAITOK	0x01	/* it's okay to wait */
#define	UVMPAGER_MAPIN_READ	0x02	/* host <- device */
#define	UVMPAGER_MAPIN_WRITE	0x00	/* device -> host (pseudo flag) */

/*
 * get/put return values
 * OK	   operation was successful
 * BAD	   specified data was out of the accepted range
 * FAIL	   specified data was in range, but doesn't exist
 * PEND	   operations was initiated but not completed
 * ERROR   error while accessing data that is in range and exists
 * AGAIN   temporary resource shortage prevented operation from happening
 * UNLOCK  unlock the map and try again
 * REFAULT [uvm_fault internal use only!] unable to relock data structures,
 *         thus the mapping needs to be reverified before we can proceed
 */
#define	VM_PAGER_OK		0
#define	VM_PAGER_BAD		1
#define	VM_PAGER_FAIL		2
#define	VM_PAGER_PEND		3
#define	VM_PAGER_ERROR		4
#define VM_PAGER_AGAIN		5
#define VM_PAGER_UNLOCK		6
#define VM_PAGER_REFAULT	7

/*
 * XXX
 * this is needed until the device strategy interface
 * is changed to do physically-addressed i/o.
 */

#ifndef PAGER_MAP_SIZE
#define PAGER_MAP_SIZE       (16 * 1024 * 1024)
#endif

#endif /* _KERNEL */

#endif /* _UVM_UVM_PAGER_H_ */
