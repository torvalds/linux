/*	$OpenBSD: uvm_device.h,v 1.11 2014/07/11 16:35:40 jsg Exp $	*/
/*	$NetBSD: uvm_device.h,v 1.9 2000/05/28 10:21:55 drochner Exp $	*/

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
 * from: Id: uvm_device.h,v 1.1.2.2 1997/10/03 17:39:44 chuck Exp
 */

#ifndef _UVM_UVM_DEVICE_H_
#define _UVM_UVM_DEVICE_H_

/*
 * uvm_device.h
 *
 * device handle into the VM system.
 */

/*
 * the uvm_device structure.   object is put at the top of the data structure.
 * this allows:
 *   (struct uvm_device *) == (struct uvm_object *)
 */

struct uvm_device {
	struct uvm_object u_obj;	/* the actual VM object */
	int u_flags;			/* flags [LOCKED BY UDV_LOCK!] */
	dev_t u_device;		/* our device */
	LIST_ENTRY(uvm_device) u_list; /* list of device objects */
};

/*
 * u_flags values
 */

#define UVM_DEVICE_HOLD		0x1	/* someone has a "hold" on it */
#define UVM_DEVICE_WANTED	0x2	/* someone wants to put a "hold" on */

#ifdef _KERNEL

/*
 * prototypes
 */

struct uvm_object *udv_attach(dev_t, vm_prot_t, voff_t, vsize_t);
struct uvm_object *udv_attach_drm(dev_t, vm_prot_t, voff_t, vsize_t);

#endif /* _KERNEL */

#endif /* _UVM_UVM_DEVICE_H_ */
