/*	$OpenBSD: uvm_aobj.h,v 1.20 2023/05/13 09:24:59 mpi Exp $	*/
/*	$NetBSD: uvm_aobj.h,v 1.10 2000/01/11 06:57:49 chs Exp $	*/

/*
 * Copyright (c) 1998 Chuck Silvers, Charles D. Cranor and
 *                    Washington University.
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
 * from: Id: uvm_aobj.h,v 1.1.2.4 1998/02/06 05:19:28 chs Exp
 */
/*
 * uvm_aobj.h: anonymous memory uvm_object pager
 *
 * author: Chuck Silvers <chuq@chuq.com>
 * started: Jan-1998
 *
 * - design mostly from Chuck Cranor
 */

#ifndef _UVM_UVM_AOBJ_H_
#define _UVM_UVM_AOBJ_H_

/*
 * flags
 */

/* flags for uao_create: can only be used one time (at bootup) */
#define UAO_FLAG_KERNOBJ	0x1	/* create kernel object */
#define UAO_FLAG_KERNSWAP	0x2	/* enable kernel swap */
#define UAO_FLAG_CANFAIL	0x4	/* creation can fail */

/* internal flags */
#define UAO_FLAG_NOSWAP		0x8	/* aobj can't swap (kernel obj only!) */

#ifdef _KERNEL

/*
 * prototypes
 */

void uao_init(void);
int uao_set_swslot(struct uvm_object *, int, int);
int uao_find_swslot(struct uvm_object *, int);
int uao_dropswap(struct uvm_object *, int);
int uao_swap_off(int, int);
int uao_shrink(struct uvm_object *, int);
int uao_grow(struct uvm_object *, int);

/*
 * globals
 */

extern const struct uvm_pagerops aobj_pager;

#endif /* _KERNEL */

#endif /* _UVM_UVM_AOBJ_H_ */
