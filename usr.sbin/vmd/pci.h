/*	$OpenBSD: pci.h,v 1.16 2025/08/08 13:40:12 dv Exp $	*/

/*
 * Copyright (c) 2015 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <dev/pci/pcireg.h>

#include "vmd.h"

#ifndef _PCI_H_
#define _PCI_H_

#define PCI_MODE1_ENABLE	0x80000000UL
#define PCI_MODE1_ADDRESS_REG	0x0cf8
#define PCI_MODE1_DATA_REG	0x0cfc
#define PCI_CONFIG_MAX_DEV	32
#define PCI_MAX_BARS		6
#define PCI_MAX_CAPS		8

#define PCI_BAR_TYPE_IO		0x0
#define PCI_BAR_TYPE_MMIO	0x1

#define PCI_MMIO_BAR_BASE	0xF0000000ULL
#define PCI_MMIO_BAR_END	0xFFBFFFFFULL		/* 4 MiB below 4 GiB */

#define PCI_MAX_PIC_IRQS	10

typedef int (*pci_cs_fn_t)(int dir, uint8_t reg, uint32_t *data);
typedef int (*pci_iobar_fn_t)(int dir, uint16_t reg, uint32_t *data, uint8_t *,
    void *, uint8_t);
typedef int (*pci_mmiobar_fn_t)(int dir, uint32_t ofs, uint32_t *data);

/*
 * Represents a PCI Capability entry with enough space for the virtio-specific
 * capabilities.
 */
struct pci_cap {
	uint8_t		pc_vndr;	/* Vendor-specific ID */
	uint8_t		pc_next;	/* Link to next capability */
	uint8_t		pc_extra[22];	/* Enough space for Virtio PCI data. */
} __packed;

struct pci_dev {
	union {
		uint32_t pd_cfg_space[PCI_CONFIG_SPACE_SIZE / 4];
		struct {
			uint16_t pd_vid;
			uint16_t pd_did;
			uint16_t pd_cmd;
			uint16_t pd_status;
			uint8_t pd_rev;
			uint8_t pd_prog_if;
			uint8_t pd_subclass;
			uint8_t pd_class;
			uint8_t pd_cache_size;
			uint8_t pd_lat_timer;
			uint8_t pd_header_type;
			uint8_t pd_bist;
			uint32_t pd_bar[PCI_MAX_BARS];
			uint32_t pd_cardbus_cis;
			uint16_t pd_subsys_vid;
			uint16_t pd_subsys_id;
			uint32_t pd_exp_rom_addr;
			uint8_t pd_cap;
			uint32_t pd_reserved0 : 24;
			uint32_t pd_reserved1;
			uint8_t pd_irq;
			uint8_t pd_int;
			uint8_t pd_min_grant;
			uint8_t pd_max_grant;
			struct pci_cap pd_caps[PCI_MAX_CAPS];
		} __packed;
	};

	uint8_t pd_bar_ct;
	uint8_t pd_cap_ct;
	pci_cs_fn_t pd_csfunc;

	uint8_t pd_bartype[PCI_MAX_BARS];
	uint32_t pd_barsize[PCI_MAX_BARS];
	void *pd_barfunc[PCI_MAX_BARS];
	void *pd_bar_cookie[PCI_MAX_BARS];
};

struct pci {
	uint8_t pci_dev_ct;
	uint64_t pci_next_mmio_bar;
	uint64_t pci_next_io_bar;
	uint8_t pci_next_pic_irq;
	uint32_t pci_addr_reg;

	struct pci_dev pci_devices[PCI_CONFIG_MAX_DEV];
};

int pci_find_first_device(uint16_t);
void pci_init(void);
int pci_add_device(uint8_t *, uint16_t, uint16_t, uint8_t, uint8_t, uint16_t,
    uint16_t, uint8_t, uint8_t, pci_cs_fn_t);
int pci_add_capability(uint8_t, struct pci_cap *);
int pci_add_bar(uint8_t, uint32_t, void *, void *);
int pci_set_bar_fn(uint8_t, uint8_t, void *, void *);
uint8_t pci_get_dev_irq(uint8_t);
uint16_t pci_get_subsys_id(uint8_t);

#ifdef __amd64__
void pci_handle_address_reg(struct vm_run_params *);
void pci_handle_data_reg(struct vm_run_params *);
uint8_t pci_handle_io(struct vm_run_params *);
#endif /* __amd64__ */

#endif /* _PCI_H_ */
