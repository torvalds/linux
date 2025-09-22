/* $OpenBSD: mcpcia_bus_mem.c,v 1.2 2012/12/05 23:20:10 deraadt Exp $ */
/* $NetBSD: mcpcia_bus_mem.c,v 1.3 2000/06/29 08:58:47 mrg Exp $ */

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>

#include <alpha/pci/mcpciareg.h>
#include <alpha/pci/mcpciavar.h>

#define	CHIP		mcpcia

#define	CHIP_EX_MALLOC_SAFE(v)	(((struct mcpcia_config *)(v))->cc_mallocsafe)
#define	CHIP_D_MEM_EXTENT(v)	(((struct mcpcia_config *)(v))->cc_d_mem_ex)
#define	CHIP_D_MEM_EX_STORE(v)						\
	(((struct mcpcia_config *)(v))->cc_dmem_exstorage)
#define	CHIP_D_MEM_EX_STORE_SIZE(v)					\
	(sizeof (((struct mcpcia_config *)(v))->cc_dmem_exstorage))
#define	CHIP_S_MEM_EXTENT(v)	(((struct mcpcia_config *)(v))->cc_s_mem_ex)
#define	CHIP_S_MEM_EX_STORE(v)						\
	(((struct mcpcia_config *)(v))->cc_smem_exstorage)
#define	CHIP_S_MEM_EX_STORE_SIZE(v)					\
	(sizeof (((struct mcpcia_config *)(v))->cc_smem_exstorage))

/* Dense region 1 */
#define	CHIP_D_MEM_W1_BUS_START(v)	0x00000000UL
#define	CHIP_D_MEM_W1_BUS_END(v)	0x7fffffffUL
#define	CHIP_D_MEM_W1_SYS_START(v)					\
	(((struct mcpcia_config *)(v))->cc_sysbase | MCPCIA_PCI_DENSE)
#define	CHIP_D_MEM_W1_SYS_END(v)					\
	(CHIP_D_MEM_W1_SYS_START(v) + 0x7fffffffUL)

/* Sparse region 1 */
#define	CHIP_S_MEM_W1_BUS_START(v)	0x00000000UL
#define	CHIP_S_MEM_W1_BUS_END(v)	0x00ffffffUL
#define	CHIP_S_MEM_W1_SYS_START(v)					\
	(((struct mcpcia_config *)(v))->cc_sysbase | MCPCIA_PCI_SPARSE)
#define	CHIP_S_MEM_W1_SYS_END(v)					\
	(CHIP_S_MEM_W1_SYS_START(v) + ((CHIP_S_MEM_W1_BUS_END(v) + 1) << 5) - 1)

/* Sparse region 2 */
#define	CHIP_S_MEM_W2_BUS_START(v)	0x01000000UL
#define	CHIP_S_MEM_W2_BUS_END(v)	0x07FFFFFFUL
#define	CHIP_S_MEM_W2_SYS_START(v)					\
	((((struct mcpcia_config *)(v))->cc_sysbase|MCPCIA_PCI_SPARSE) + \
	(0x01000000UL<<5))
#define	CHIP_S_MEM_W2_SYS_END(v)					\
	(CHIP_S_MEM_W1_SYS_START(v) + ((CHIP_S_MEM_W2_BUS_END(v) + 1) << 5) - 1)

#define CHIP_EXTENT_DNAME(v)		((struct mcpcia_config *)(v))->pc_mem_dex_name
#define CHIP_EXTENT_SNAME(v)		((struct mcpcia_config *)(v))->pc_mem_sex_name
#define CHIP_EXTENT_DSTORAGE(v)		((struct mcpcia_config *)(v))->pc_mem_dex_storage
#define CHIP_EXTENT_SSTORAGE(v)		((struct mcpcia_config *)(v))->pc_mem_sex_storage

#include "alpha/pci/pci_swiz_bus_mem_chipdep.c"
