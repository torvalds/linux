/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000, BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD$
 *
 */

#ifndef _PCI_PRIVATE_H_
#define	_PCI_PRIVATE_H_

/*
 * Export definitions of the pci bus so that we can more easily share
 * it with "subclass" buses.
 */
DECLARE_CLASS(pci_driver);

struct pci_softc {
	bus_dma_tag_t sc_dma_tag;
#ifdef PCI_RES_BUS
	struct resource *sc_bus;
#endif
};

extern int 	pci_do_power_resume;
extern int 	pci_do_power_suspend;

void		pci_add_children(device_t dev, int domain, int busno);
void		pci_add_child(device_t bus, struct pci_devinfo *dinfo);
device_t	pci_add_iov_child(device_t bus, device_t pf, uint16_t rid,
		    uint16_t vid, uint16_t did);
void		pci_add_resources(device_t bus, device_t dev, int force,
		    uint32_t prefetchmask);
void		pci_add_resources_ea(device_t bus, device_t dev, int alloc_iov);
struct pci_devinfo *pci_alloc_devinfo_method(device_t dev);
int		pci_attach_common(device_t dev);
int		pci_rescan_method(device_t dev);
void		pci_driver_added(device_t dev, driver_t *driver);
int		pci_ea_is_enabled(device_t dev, int rid);
int		pci_print_child(device_t dev, device_t child);
void		pci_probe_nomatch(device_t dev, device_t child);
int		pci_read_ivar(device_t dev, device_t child, int which,
		    uintptr_t *result);
int		pci_write_ivar(device_t dev, device_t child, int which,
		    uintptr_t value);
int		pci_setup_intr(device_t dev, device_t child,
		    struct resource *irq, int flags, driver_filter_t *filter,
		    driver_intr_t *intr, void *arg, void **cookiep);
int		pci_teardown_intr(device_t dev, device_t child,
		    struct resource *irq, void *cookie);
int		pci_get_vpd_ident_method(device_t dev, device_t child,
		    const char **identptr);
int		pci_get_vpd_readonly_method(device_t dev, device_t child,
		    const char *kw, const char **vptr);
int		pci_set_powerstate_method(device_t dev, device_t child,
		    int state);
int		pci_get_powerstate_method(device_t dev, device_t child);
uint32_t	pci_read_config_method(device_t dev, device_t child, 
		    int reg, int width);
void		pci_write_config_method(device_t dev, device_t child, 
		    int reg, uint32_t val, int width);
int		pci_enable_busmaster_method(device_t dev, device_t child);
int		pci_disable_busmaster_method(device_t dev, device_t child);
int		pci_enable_io_method(device_t dev, device_t child, int space);
int		pci_disable_io_method(device_t dev, device_t child, int space);
int		pci_find_cap_method(device_t dev, device_t child,
		    int capability, int *capreg);
int		pci_find_next_cap_method(device_t dev, device_t child,
		    int capability, int start, int *capreg);
int		pci_find_extcap_method(device_t dev, device_t child,
		    int capability, int *capreg);
int		pci_find_next_extcap_method(device_t dev, device_t child,
		    int capability, int start, int *capreg);
int		pci_find_htcap_method(device_t dev, device_t child,
		    int capability, int *capreg);
int		pci_find_next_htcap_method(device_t dev, device_t child,
		    int capability, int start, int *capreg);
int		pci_alloc_msi_method(device_t dev, device_t child, int *count);
int		pci_alloc_msix_method(device_t dev, device_t child, int *count);
void		pci_enable_msi_method(device_t dev, device_t child,
		    uint64_t address, uint16_t data);
void		pci_enable_msix_method(device_t dev, device_t child,
		    u_int index, uint64_t address, uint32_t data);
void		pci_disable_msi_method(device_t dev, device_t child);
int		pci_remap_msix_method(device_t dev, device_t child,
		    int count, const u_int *vectors);
int		pci_release_msi_method(device_t dev, device_t child);
int		pci_msi_count_method(device_t dev, device_t child);
int		pci_msix_count_method(device_t dev, device_t child);
int		pci_msix_pba_bar_method(device_t dev, device_t child);
int		pci_msix_table_bar_method(device_t dev, device_t child);
struct resource	*pci_alloc_resource(device_t dev, device_t child, 
		    int type, int *rid, rman_res_t start, rman_res_t end,
		    rman_res_t count, u_int flags);
int		pci_release_resource(device_t dev, device_t child, int type,
		    int rid, struct resource *r);
int		pci_activate_resource(device_t dev, device_t child, int type,
		    int rid, struct resource *r);
int		pci_deactivate_resource(device_t dev, device_t child, int type,
		    int rid, struct resource *r);
void		pci_delete_resource(device_t dev, device_t child, 
		    int type, int rid);
struct resource_list *pci_get_resource_list (device_t dev, device_t child);
struct pci_devinfo *pci_read_device(device_t pcib, device_t bus, int d, int b,
		    int s, int f);
void		pci_print_verbose(struct pci_devinfo *dinfo);
int		pci_freecfg(struct pci_devinfo *dinfo);
void		pci_child_deleted(device_t dev, device_t child);
void		pci_child_detached(device_t dev, device_t child);
int		pci_child_location_str_method(device_t cbdev, device_t child,
		    char *buf, size_t buflen);
int		pci_child_pnpinfo_str_method(device_t cbdev, device_t child,
		    char *buf, size_t buflen);
int		pci_assign_interrupt_method(device_t dev, device_t child);
int		pci_resume(device_t dev);
int		pci_resume_child(device_t dev, device_t child);
int		pci_suspend_child(device_t dev, device_t child);
bus_dma_tag_t pci_get_dma_tag(device_t bus, device_t dev);
void		pci_child_added_method(device_t dev, device_t child);

/** Restore the config register state.  The state must be previously
 * saved with pci_cfg_save.  However, the pci bus driver takes care of
 * that.  This function will also return the device to PCI_POWERSTATE_D0
 * if it is currently in a lower power mode.
 */
void		pci_cfg_restore(device_t, struct pci_devinfo *);

/** Save the config register state.  Optionally set the power state to D3
 * if the third argument is non-zero.
 */
void		pci_cfg_save(device_t, struct pci_devinfo *, int);

int		pci_mapsize(uint64_t testval);
void		pci_read_bar(device_t dev, int reg, pci_addr_t *mapp,
		    pci_addr_t *testvalp, int *bar64);
struct pci_map *pci_add_bar(device_t dev, int reg, pci_addr_t value,
		    pci_addr_t size);

struct resource *pci_alloc_multi_resource(device_t dev, device_t child,
		    int type, int *rid, rman_res_t start, rman_res_t end,
		    rman_res_t count, u_long num, u_int flags);

int		pci_iov_attach_method(device_t bus, device_t dev,
		    struct nvlist *pf_schema, struct nvlist *vf_schema,
		    const char *name);
int		pci_iov_detach_method(device_t bus, device_t dev);

device_t	pci_create_iov_child_method(device_t bus, device_t pf,
		    uint16_t rid, uint16_t vid, uint16_t did);

struct resource *pci_vf_alloc_mem_resource(device_t dev, device_t child,
		    int *rid, rman_res_t start, rman_res_t end,
		    rman_res_t count, u_int flags);
int		pci_vf_release_mem_resource(device_t dev, device_t child,
		    int rid, struct resource *r);
#endif /* _PCI_PRIVATE_H_ */
