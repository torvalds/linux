/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994,1995 Stefan Esser, Wolfgang StanglMeier
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000 BSDi
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __PCIB_PRIVATE_H__
#define	__PCIB_PRIVATE_H__

#include <sys/_callout.h>
#include <sys/_task.h>

#ifdef NEW_PCIB
/*
 * Data structure and routines that Host to PCI bridge drivers can use
 * to restrict allocations for child devices to ranges decoded by the
 * bridge.
 */
struct pcib_host_resources {
	device_t	hr_pcib;
	struct resource_list hr_rl;
};

int		pcib_host_res_init(device_t pcib,
		    struct pcib_host_resources *hr);
int		pcib_host_res_free(device_t pcib,
		    struct pcib_host_resources *hr);
int		pcib_host_res_decodes(struct pcib_host_resources *hr, int type,
		    rman_res_t start, rman_res_t end, u_int flags);
struct resource *pcib_host_res_alloc(struct pcib_host_resources *hr,
		    device_t dev, int type, int *rid, rman_res_t start,
		    rman_res_t end, rman_res_t count, u_int flags);
int		pcib_host_res_adjust(struct pcib_host_resources *hr,
		    device_t dev, int type, struct resource *r, rman_res_t start,
		    rman_res_t end);
#endif

/*
 * Export portions of generic PCI:PCI bridge support so that it can be
 * used by subclasses.
 */
DECLARE_CLASS(pcib_driver);

#ifdef NEW_PCIB
#define	WIN_IO		0x1
#define	WIN_MEM		0x2
#define	WIN_PMEM	0x4

struct pcib_window {
	pci_addr_t	base;		/* base address */
	pci_addr_t	limit;		/* topmost address */
	struct rman	rman;
	struct resource **res;
	int		count;		/* size of 'res' array */
	int		reg;		/* resource id from parent */
	int		valid;
	int		mask;		/* WIN_* bitmask of this window */
	int		step;		/* log_2 of window granularity */
	const char	*name;
};
#endif

struct pcib_secbus {
	u_int		sec;
	u_int		sub;
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	device_t	dev;
	struct rman	rman;
	struct resource	*res;
	const char	*name;
	int		sub_reg;
#endif
};

/*
 * Bridge-specific data.
 */
struct pcib_softc 
{
    device_t	dev;
    device_t	child;
    uint32_t	flags;		/* flags */
#define	PCIB_SUBTRACTIVE	0x1
#define	PCIB_DISABLE_MSI	0x2
#define	PCIB_DISABLE_MSIX	0x4
#define	PCIB_ENABLE_ARI		0x8
#define	PCIB_HOTPLUG		0x10
#define	PCIB_HOTPLUG_CMD_PENDING 0x20
#define	PCIB_DETACH_PENDING	0x40
#define	PCIB_DETACHING		0x80
    u_int	domain;		/* domain number */
    u_int	pribus;		/* primary bus number */
    struct pcib_secbus bus;	/* secondary bus numbers */
#ifdef NEW_PCIB
    struct pcib_window io;	/* I/O port window */
    struct pcib_window mem;	/* memory window */
    struct pcib_window pmem;	/* prefetchable memory window */
#else
    pci_addr_t	pmembase;	/* base address of prefetchable memory */
    pci_addr_t	pmemlimit;	/* topmost address of prefetchable memory */
    pci_addr_t	membase;	/* base address of memory window */
    pci_addr_t	memlimit;	/* topmost address of memory window */
    uint32_t	iobase;		/* base address of port window */
    uint32_t	iolimit;	/* topmost address of port window */
#endif
    uint16_t	bridgectl;	/* bridge control register */
    uint16_t	pcie_link_sta;
    uint16_t	pcie_slot_sta;
    uint32_t	pcie_slot_cap;
    struct resource *pcie_irq;
    void	*pcie_ihand;
    struct task	pcie_hp_task;
    struct callout pcie_ab_timer;
    struct callout pcie_cc_timer;
    struct callout pcie_dll_timer;
};

#define	PCIB_SUPPORTED_ARI_VER	1

typedef uint32_t pci_read_config_fn(int b, int s, int f, int reg, int width);

int		host_pcib_get_busno(pci_read_config_fn read_config, int bus,
    int slot, int func, uint8_t *busnum);
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
struct resource *pci_domain_alloc_bus(int domain, device_t dev, int *rid,
		    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags);
int		pci_domain_adjust_bus(int domain, device_t dev,
		    struct resource *r, rman_res_t start, rman_res_t end);
int		pci_domain_release_bus(int domain, device_t dev, int rid,
		    struct resource *r);
struct resource *pcib_alloc_subbus(struct pcib_secbus *bus, device_t child,
		    int *rid, rman_res_t start, rman_res_t end, rman_res_t count,
		    u_int flags);
void		pcib_free_secbus(device_t dev, struct pcib_secbus *bus);
void		pcib_setup_secbus(device_t dev, struct pcib_secbus *bus,
    int min_count);
#endif
int		pcib_attach(device_t dev);
int		pcib_attach_child(device_t dev);
void		pcib_attach_common(device_t dev);
void		pcib_bridge_init(device_t dev);	
#ifdef NEW_PCIB
const char	*pcib_child_name(device_t child);
#endif
int		pcib_child_present(device_t dev, device_t child);
int		pcib_detach(device_t dev);
int		pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result);
int		pcib_write_ivar(device_t dev, device_t child, int which, uintptr_t value);
struct resource *pcib_alloc_resource(device_t dev, device_t child, int type, int *rid, 
					    rman_res_t start, rman_res_t end,
					    rman_res_t count, u_int flags);
#ifdef NEW_PCIB
int		pcib_adjust_resource(device_t bus, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end);
int		pcib_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r);
#endif
int		pcib_maxslots(device_t dev);
int		pcib_maxfuncs(device_t dev);
int		pcib_route_interrupt(device_t pcib, device_t dev, int pin);
int		pcib_alloc_msi(device_t pcib, device_t dev, int count, int maxcount, int *irqs);
int		pcib_release_msi(device_t pcib, device_t dev, int count, int *irqs);
int		pcib_alloc_msix(device_t pcib, device_t dev, int *irq);
int		pcib_release_msix(device_t pcib, device_t dev, int irq);
int		pcib_map_msi(device_t pcib, device_t dev, int irq, uint64_t *addr, uint32_t *data);
int		pcib_get_id(device_t pcib, device_t dev, enum pci_id_type type,
		    uintptr_t *id);
void		pcib_decode_rid(device_t pcib, uint16_t rid, int *bus, 
		    int *slot, int *func);
int		pcib_request_feature(device_t dev, enum pci_feature feature);
int		pcib_request_feature_allow(device_t pcib, device_t dev, enum pci_feature feature);

#endif
