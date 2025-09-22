/* $OpenBSD: tsp_bus_io.c,v 1.3 2003/09/02 17:25:21 miod Exp $ */
/* $NetBSD: tsp_bus_io.c,v 1.4 2000/06/26 19:46:24 thorpej Exp $ */

/*-
 * Copyright (c) 1999 by Ross Harvey.  All rights reserved.
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
 *	This product includes software developed by Ross Harvey.
 * 4. The name of Ross Harvey may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ROSS HARVEY ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURP0SE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ROSS HARVEY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <alpha/pci/tsreg.h>
#include <alpha/pci/tsvar.h>

#define	CHIP	tsp

typedef struct tsp_config *TSPCON;

#define	CHIP_EX_MALLOC_SAFE(v)  (((TSPCON)(v))->pc_mallocsafe)
#define CHIP_IO_EXTENT(v)       (((TSPCON)(v))->pc_io_ex)

#define CHIP_IO_SYS_START(v)    (((TSPCON)(v))->pc_iobase | P_PCI_IO)

/*
 * Tsunami core logic appears on EV6.  We require at least EV56
 * support for the assembler to emit BWX opcodes.
 */
__asm(".arch ev6");

#define	CHIP_EXTENT_NAME(v)	((struct tsp_config *)(v))->pc_io_ex_name
#define	CHIP_EXTENT_STORAGE(v)	((struct tsp_config *)(v))->pc_io_ex_storage

#include <alpha/pci/pci_bwx_bus_io_chipdep.c>
