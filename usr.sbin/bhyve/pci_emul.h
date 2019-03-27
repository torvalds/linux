/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
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

#ifndef _PCI_EMUL_H_
#define _PCI_EMUL_H_

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/_pthreadtypes.h>

#include <dev/pci/pcireg.h>

#include <assert.h>

#define	PCI_BARMAX	PCIR_MAX_BAR_0	/* BAR registers in a Type 0 header */

struct vmctx;
struct pci_devinst;
struct memory_region;

struct pci_devemu {
	char      *pe_emu;		/* Name of device emulation */

	/* instance creation */
	int       (*pe_init)(struct vmctx *, struct pci_devinst *,
			     char *opts);

	/* ACPI DSDT enumeration */
	void	(*pe_write_dsdt)(struct pci_devinst *);

	/* config space read/write callbacks */
	int	(*pe_cfgwrite)(struct vmctx *ctx, int vcpu,
			       struct pci_devinst *pi, int offset,
			       int bytes, uint32_t val);
	int	(*pe_cfgread)(struct vmctx *ctx, int vcpu,
			      struct pci_devinst *pi, int offset,
			      int bytes, uint32_t *retval);

	/* BAR read/write callbacks */
	void      (*pe_barwrite)(struct vmctx *ctx, int vcpu,
				 struct pci_devinst *pi, int baridx,
				 uint64_t offset, int size, uint64_t value);
	uint64_t  (*pe_barread)(struct vmctx *ctx, int vcpu,
				struct pci_devinst *pi, int baridx,
				uint64_t offset, int size);
};
#define PCI_EMUL_SET(x)   DATA_SET(pci_devemu_set, x);

enum pcibar_type {
	PCIBAR_NONE,
	PCIBAR_IO,
	PCIBAR_MEM32,
	PCIBAR_MEM64,
	PCIBAR_MEMHI64
};

struct pcibar {
	enum pcibar_type	type;		/* io or memory */
	uint64_t		size;
	uint64_t		addr;
};

#define PI_NAMESZ	40

struct msix_table_entry {
	uint64_t	addr;
	uint32_t	msg_data;
	uint32_t	vector_control;
} __packed;

/* 
 * In case the structure is modified to hold extra information, use a define
 * for the size that should be emulated.
 */
#define	MSIX_TABLE_ENTRY_SIZE	16
#define MAX_MSIX_TABLE_ENTRIES	2048
#define	PBA_SIZE(msgnum)	(roundup2((msgnum), 64) / 8)

enum lintr_stat {
	IDLE,
	ASSERTED,
	PENDING
};

struct pci_devinst {
	struct pci_devemu *pi_d;
	struct vmctx *pi_vmctx;
	uint8_t	  pi_bus, pi_slot, pi_func;
	char	  pi_name[PI_NAMESZ];
	int	  pi_bar_getsize;
	int	  pi_prevcap;
	int	  pi_capend;

	struct {
		int8_t    	pin;
		enum lintr_stat	state;
		int		pirq_pin;
		int	  	ioapic_irq;
		pthread_mutex_t	lock;
	} pi_lintr;

	struct {
		int		enabled;
		uint64_t	addr;
		uint64_t	msg_data;
		int		maxmsgnum;
	} pi_msi;

	struct {
		int	enabled;
		int	table_bar;
		int	pba_bar;
		uint32_t table_offset;
		int	table_count;
		uint32_t pba_offset;
		int	pba_size;
		int	function_mask; 	
		struct msix_table_entry *table;	/* allocated at runtime */
		void	*pba_page;
		int	pba_page_offset;
	} pi_msix;

	void      *pi_arg;		/* devemu-private data */

	u_char	  pi_cfgdata[PCI_REGMAX + 1];
	struct pcibar pi_bar[PCI_BARMAX + 1];
};

struct msicap {
	uint8_t		capid;
	uint8_t		nextptr;
	uint16_t	msgctrl;
	uint32_t	addrlo;
	uint32_t	addrhi;
	uint16_t	msgdata;
} __packed;
static_assert(sizeof(struct msicap) == 14, "compile-time assertion failed");

struct msixcap {
	uint8_t		capid;
	uint8_t		nextptr;
	uint16_t	msgctrl;
	uint32_t	table_info;	/* bar index and offset within it */
	uint32_t	pba_info;	/* bar index and offset within it */
} __packed;
static_assert(sizeof(struct msixcap) == 12, "compile-time assertion failed");

struct pciecap {
	uint8_t		capid;
	uint8_t		nextptr;
	uint16_t	pcie_capabilities;

	uint32_t	dev_capabilities;	/* all devices */
	uint16_t	dev_control;
	uint16_t	dev_status;

	uint32_t	link_capabilities;	/* devices with links */
	uint16_t	link_control;
	uint16_t	link_status;

	uint32_t	slot_capabilities;	/* ports with slots */
	uint16_t	slot_control;
	uint16_t	slot_status;

	uint16_t	root_control;		/* root ports */
	uint16_t	root_capabilities;
	uint32_t	root_status;

	uint32_t	dev_capabilities2;	/* all devices */
	uint16_t	dev_control2;
	uint16_t	dev_status2;

	uint32_t	link_capabilities2;	/* devices with links */
	uint16_t	link_control2;
	uint16_t	link_status2;

	uint32_t	slot_capabilities2;	/* ports with slots */
	uint16_t	slot_control2;
	uint16_t	slot_status2;
} __packed;
static_assert(sizeof(struct pciecap) == 60, "compile-time assertion failed");

typedef void (*pci_lintr_cb)(int b, int s, int pin, int pirq_pin,
    int ioapic_irq, void *arg);

int	init_pci(struct vmctx *ctx);
void	msicap_cfgwrite(struct pci_devinst *pi, int capoff, int offset,
	    int bytes, uint32_t val);
void	msixcap_cfgwrite(struct pci_devinst *pi, int capoff, int offset,
	    int bytes, uint32_t val);
void	pci_callback(void);
int	pci_emul_alloc_bar(struct pci_devinst *pdi, int idx,
	    enum pcibar_type type, uint64_t size);
int	pci_emul_alloc_pbar(struct pci_devinst *pdi, int idx,
	    uint64_t hostbase, enum pcibar_type type, uint64_t size);
int	pci_emul_add_msicap(struct pci_devinst *pi, int msgnum);
int	pci_emul_add_pciecap(struct pci_devinst *pi, int pcie_device_type);
void	pci_generate_msi(struct pci_devinst *pi, int msgnum);
void	pci_generate_msix(struct pci_devinst *pi, int msgnum);
void	pci_lintr_assert(struct pci_devinst *pi);
void	pci_lintr_deassert(struct pci_devinst *pi);
void	pci_lintr_request(struct pci_devinst *pi);
int	pci_msi_enabled(struct pci_devinst *pi);
int	pci_msix_enabled(struct pci_devinst *pi);
int	pci_msix_table_bar(struct pci_devinst *pi);
int	pci_msix_pba_bar(struct pci_devinst *pi);
int	pci_msi_maxmsgnum(struct pci_devinst *pi);
int	pci_parse_slot(char *opt);
void    pci_print_supported_devices();
void	pci_populate_msicap(struct msicap *cap, int msgs, int nextptr);
int	pci_emul_add_msixcap(struct pci_devinst *pi, int msgnum, int barnum);
int	pci_emul_msix_twrite(struct pci_devinst *pi, uint64_t offset, int size,
			     uint64_t value);
uint64_t pci_emul_msix_tread(struct pci_devinst *pi, uint64_t offset, int size);
int	pci_count_lintr(int bus);
void	pci_walk_lintr(int bus, pci_lintr_cb cb, void *arg);
void	pci_write_dsdt(void);
uint64_t pci_ecfg_base(void);
int	pci_bus_configured(int bus);

static __inline void 
pci_set_cfgdata8(struct pci_devinst *pi, int offset, uint8_t val)
{
	assert(offset <= PCI_REGMAX);
	*(uint8_t *)(pi->pi_cfgdata + offset) = val;
}

static __inline void 
pci_set_cfgdata16(struct pci_devinst *pi, int offset, uint16_t val)
{
	assert(offset <= (PCI_REGMAX - 1) && (offset & 1) == 0);
	*(uint16_t *)(pi->pi_cfgdata + offset) = val;
}

static __inline void 
pci_set_cfgdata32(struct pci_devinst *pi, int offset, uint32_t val)
{
	assert(offset <= (PCI_REGMAX - 3) && (offset & 3) == 0);
	*(uint32_t *)(pi->pi_cfgdata + offset) = val;
}

static __inline uint8_t
pci_get_cfgdata8(struct pci_devinst *pi, int offset)
{
	assert(offset <= PCI_REGMAX);
	return (*(uint8_t *)(pi->pi_cfgdata + offset));
}

static __inline uint16_t
pci_get_cfgdata16(struct pci_devinst *pi, int offset)
{
	assert(offset <= (PCI_REGMAX - 1) && (offset & 1) == 0);
	return (*(uint16_t *)(pi->pi_cfgdata + offset));
}

static __inline uint32_t
pci_get_cfgdata32(struct pci_devinst *pi, int offset)
{
	assert(offset <= (PCI_REGMAX - 3) && (offset & 3) == 0);
	return (*(uint32_t *)(pi->pi_cfgdata + offset));
}

#endif /* _PCI_EMUL_H_ */
