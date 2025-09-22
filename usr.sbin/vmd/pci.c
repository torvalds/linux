/*	$OpenBSD: pci.c,v 1.39 2025/08/02 15:16:18 dv Exp $	*/

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
#include <dev/pci/pcidevs.h>
#include <dev/vmm/vmm.h>

#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "vmd.h"
#include "pci.h"
#include "atomicio.h"

struct pci pci;

extern struct vmd_vm current_vm;
extern char *__progname;

/* PIC IRQs, assigned to devices in order */
const uint8_t pci_pic_irqs[PCI_MAX_PIC_IRQS] = {3, 5, 6, 7, 9, 10, 11, 12,
    14, 15};

/*
 * pci_add_bar
 *
 * Adds a BAR for the PCI device 'id'. On access, 'barfn' will be
 * called, and passed 'cookie' as an identifier.
 *
 * BARs are fixed size, meaning all I/O BARs requested have the
 * same size and all MMIO BARs have the same size.
 *
 * Parameters:
 *  id: PCI device to add the BAR to (local count, eg if id == 4,
 *      this BAR is to be added to the VM's 5th PCI device)
 *  type: type of the BAR to add (PCI_MAPREG_TYPE_xxx)
 *  barfn: callback function invoked on BAR access
 *  cookie: cookie passed to barfn on access
 *
 * Returns the index of the BAR if added successfully, -1 otherwise.
 */
int
pci_add_bar(uint8_t id, uint32_t type, void *barfn, void *cookie)
{
	uint8_t bar_reg_idx, bar_ct;

	/* Check id */
	if (id >= pci.pci_dev_ct)
		return (-1);

	/* Can only add PCI_MAX_BARS BARs to any device */
	bar_ct = pci.pci_devices[id].pd_bar_ct;
	if (bar_ct >= PCI_MAX_BARS)
		return (-1);

	/* Compute BAR address and add */
	bar_reg_idx = (PCI_MAPREG_START + (bar_ct * 4)) / 4;
	if (type == PCI_MAPREG_TYPE_MEM) {
		if (pci.pci_next_mmio_bar >= PCI_MMIO_BAR_END)
			return (-1);

		pci.pci_devices[id].pd_cfg_space[bar_reg_idx] =
		    PCI_MAPREG_MEM_ADDR(pci.pci_next_mmio_bar);
		pci.pci_next_mmio_bar += VM_PCI_MMIO_BAR_SIZE;
		pci.pci_devices[id].pd_barfunc[bar_ct] = barfn;
		pci.pci_devices[id].pd_bar_cookie[bar_ct] = cookie;
		pci.pci_devices[id].pd_bartype[bar_ct] = PCI_BAR_TYPE_MMIO;
		pci.pci_devices[id].pd_barsize[bar_ct] = VM_PCI_MMIO_BAR_SIZE;
		pci.pci_devices[id].pd_bar_ct++;
	}
#ifdef __amd64__
	else if (type == PCI_MAPREG_TYPE_IO) {
		if (pci.pci_next_io_bar >= VM_PCI_IO_BAR_END)
			return (-1);

		pci.pci_devices[id].pd_cfg_space[bar_reg_idx] =
		    PCI_MAPREG_IO_ADDR(pci.pci_next_io_bar) |
		    PCI_MAPREG_TYPE_IO;
		pci.pci_next_io_bar += VM_PCI_IO_BAR_SIZE;
		pci.pci_devices[id].pd_barfunc[bar_ct] = barfn;
		pci.pci_devices[id].pd_bar_cookie[bar_ct] = cookie;
		DPRINTF("%s: adding pci bar cookie for dev %d bar %d = %p",
		    __progname, id, bar_ct, cookie);
		pci.pci_devices[id].pd_bartype[bar_ct] = PCI_BAR_TYPE_IO;
		pci.pci_devices[id].pd_barsize[bar_ct] = VM_PCI_IO_BAR_SIZE;
		pci.pci_devices[id].pd_bar_ct++;
	}
#endif /* __amd64__ */

	return ((int)bar_ct);
}

int
pci_set_bar_fn(uint8_t id, uint8_t bar_ct, void *barfn, void *cookie)
{
	/* Check id */
	if (id >= pci.pci_dev_ct)
		return (1);

	if (bar_ct >= PCI_MAX_BARS)
		return (1);

	pci.pci_devices[id].pd_barfunc[bar_ct] = barfn;
	pci.pci_devices[id].pd_bar_cookie[bar_ct] = cookie;

	return (0);
}

/*
 * pci_get_dev_irq
 *
 * Returns the IRQ for the specified PCI device
 *
 * Parameters:
 *  id: PCI device id to return IRQ for
 *
 * Return values:
 *  The IRQ for the device, or 0xff if no device IRQ assigned
 */
uint8_t
pci_get_dev_irq(uint8_t id)
{
	if (pci.pci_devices[id].pd_int)
		return pci.pci_devices[id].pd_irq;
	else
		return 0xFF;
}

/*
 * pci_add_device
 *
 * Adds a PCI device to the guest VM defined by the supplied parameters.
 *
 * Parameters:
 *  id: the new PCI device ID (0 .. PCI_CONFIG_MAX_DEV)
 *  vid: PCI VID of the new device
 *  pid: PCI PID of the new device
 *  class: PCI 'class' of the new device
 *  subclass: PCI 'subclass' of the new device
 *  subsys_vid: subsystem VID of the new device
 *  subsys_id: subsystem ID of the new device
 *  rev_id: revision id
 *  irq_needed: 1 if an IRQ should be assigned to this PCI device, 0 otherwise
 *  csfunc: PCI config space callback function when the guest VM accesses
 *      CS of this PCI device
 *
 * Return values:
 *  0: the PCI device was added successfully. The PCI device ID is in 'id'.
 *  1: the PCI device addition failed.
 */
int
pci_add_device(uint8_t *id, uint16_t vid, uint16_t pid, uint8_t class,
    uint8_t subclass, uint16_t subsys_vid, uint16_t subsys_id,
    uint8_t rev_id, uint8_t irq_needed, pci_cs_fn_t csfunc)
{
	/* Exceeded max devices? */
	if (pci.pci_dev_ct >= PCI_CONFIG_MAX_DEV)
		return (1);

	/* Exceeded max IRQs? */
	/* XXX we could share IRQs ... */
	if (pci.pci_next_pic_irq >= PCI_MAX_PIC_IRQS && irq_needed)
		return (1);

	*id = pci.pci_dev_ct;

	pci.pci_devices[*id].pd_vid = vid;
	pci.pci_devices[*id].pd_did = pid;
	pci.pci_devices[*id].pd_rev = rev_id;
	pci.pci_devices[*id].pd_class = class;
	pci.pci_devices[*id].pd_subclass = subclass;
	pci.pci_devices[*id].pd_subsys_vid = subsys_vid;
	pci.pci_devices[*id].pd_subsys_id = subsys_id;

	pci.pci_devices[*id].pd_csfunc = csfunc;

	if (irq_needed) {
		pci.pci_devices[*id].pd_irq =
		    pci_pic_irqs[pci.pci_next_pic_irq];
		pci.pci_devices[*id].pd_int = 1;
		pci.pci_next_pic_irq++;
		DPRINTF("assigned irq %d to pci dev %d",
		    pci.pci_devices[*id].pd_irq, *id);
		intr_toggle_el(&current_vm, pci.pci_devices[*id].pd_irq, 1);
	}

	pci.pci_dev_ct++;

	return (0);
}

int
pci_add_capability(uint8_t id, struct pci_cap *cap)
{
	uint8_t cid;
	struct pci_dev *dev = NULL;

	if (id >= pci.pci_dev_ct)
		return (-1);
	dev = &pci.pci_devices[id];

	if (dev->pd_cap_ct >= PCI_MAX_CAPS)
		return (-1);
	cid = dev->pd_cap_ct;

	memcpy(&dev->pd_caps[cid], cap, sizeof(dev->pd_caps[0]));

	/* Update the linkage. */
	if (cid > 0)
		dev->pd_caps[cid - 1].pc_next = (sizeof(struct pci_cap) * cid) +
		    offsetof(struct pci_dev, pd_caps);

	dev->pd_cap_ct++;
	dev->pd_cap = offsetof(struct pci_dev, pd_caps);
	dev->pd_status |= (PCI_STATUS_CAPLIST_SUPPORT >> 16);

	return (cid);
}

/*
 * pci_init
 *
 * Initializes the PCI subsystem for the VM by adding a PCI host bridge
 * as the first PCI device.
 */
void
pci_init(void)
{
	uint8_t id;

	memset(&pci, 0, sizeof(pci));

	/* Check if changes to struct pci_dev create an invalid config space. */
	CTASSERT(sizeof(pci.pci_devices[0].pd_cfg_space) <= 256);

	pci.pci_next_mmio_bar = PCI_MMIO_BAR_BASE;
#ifdef __amd64__
	pci.pci_next_io_bar = VM_PCI_IO_BAR_BASE;
#endif /* __amd64__ */

	if (pci_add_device(&id, PCI_VENDOR_OPENBSD, PCI_PRODUCT_OPENBSD_PCHB,
	    PCI_CLASS_BRIDGE, PCI_SUBCLASS_BRIDGE_HOST,
	    PCI_VENDOR_OPENBSD, 0, 0, 0, NULL)) {
		log_warnx("%s: can't add PCI host bridge", __progname);
		return;
	}
}

#ifdef __amd64__
void
pci_handle_address_reg(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;

	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * The guest wrote to the address register.
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		get_input_data(vei, &pci.pci_addr_reg);
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * The guest read the address register
		 */
		set_return_data(vei, pci.pci_addr_reg);
	}
}

uint8_t
pci_handle_io(struct vm_run_params *vrp)
{
	int i, j;
	uint16_t reg, b_hi, b_lo;
	pci_iobar_fn_t fn = NULL;
	void *cookie = NULL;
	uint8_t intr = 0xFF, irq = 0xFF, dir, sz;
	struct vm_exit *vei = vrp->vrp_exit;

	reg = vei->vei.vei_port;
	dir = vei->vei.vei_dir;
	sz = vei->vei.vei_size;

	for (i = 0 ; i < pci.pci_dev_ct; i++) {
		for (j = 0 ; j < pci.pci_devices[i].pd_bar_ct; j++) {
			b_lo = PCI_MAPREG_IO_ADDR(pci.pci_devices[i].pd_bar[j]);
			b_hi = b_lo + VM_PCI_IO_BAR_SIZE;
			if (reg >= b_lo && reg < b_hi) {
				fn = pci.pci_devices[i].pd_barfunc[j];
				reg = reg - b_lo;
				cookie = pci.pci_devices[i].pd_bar_cookie[j];
				irq = pci.pci_devices[i].pd_irq;
				goto found;
			}
		}
	}
found:
	if (fn == NULL) {
		DPRINTF("%s: no pci i/o function for reg 0x%llx (dir=%d "
		    "guest %%rip=0x%llx", __progname, (uint64_t)reg, dir,
		    vei->vrs.vrs_gprs[VCPU_REGS_RIP]);
		/* Reads from undefined ports return 0xFF */
		if (dir == VEI_DIR_IN)
			set_return_data(vei, 0xFFFFFFFF);
		return (0xFF);
	}

	if (fn(dir, reg, &vei->vei.vei_data, &intr, cookie, sz))
		log_warnx("%s: pci i/o access function failed", __progname);
	if (intr != 0xFF)
		intr = irq;

	return (intr);
}

void
pci_handle_data_reg(struct vm_run_params *vrp)
{
	struct vm_exit *vei = vrp->vrp_exit;
	struct pci_dev *pd = NULL;
	uint8_t b, d, f, o, baridx, cfgidx, ofs, sz;
	uint32_t data = 0;
	int ret;
	pci_cs_fn_t csfunc;

	/* abort if the address register is wack */
	if (!(pci.pci_addr_reg & PCI_MODE1_ENABLE)) {
		/* if read, return FFs */
		if (vei->vei.vei_dir == VEI_DIR_IN)
			set_return_data(vei, 0xFFFFFFFF);
		log_warnx("invalid address register during pci read: "
		    "0x%llx", (uint64_t)pci.pci_addr_reg);
		return;
	}

	/* I/Os to 0xCFC..0xCFF are permitted */
	ofs = vei->vei.vei_port - 0xCFC;
	sz = vei->vei.vei_size;

	b = (pci.pci_addr_reg >> 16) & 0xff;
	d = (pci.pci_addr_reg >> 11) & 0x1f;
	f = (pci.pci_addr_reg >> 8) & 0x7;
	o = (pci.pci_addr_reg & 0xfc);

	if (d >= pci.pci_dev_ct) {
		/* Device out of range. Return 0xFF's if a read. */
		DPRINTF("%s: invalid pci device access (%u)", __func__, d);
		if (vei->vei.vei_dir == VEI_DIR_IN)
			set_return_data(vei, 0xFFFFFFFF);
		return;
	}
	pd = &pci.pci_devices[d];

	cfgidx = (o / 4);
	if (cfgidx >= nitems(pd->pd_cfg_space)) {
		DPRINTF("%s: out of range config space access", __func__);
		if (vei->vei.vei_dir == VEI_DIR_IN)
			set_return_data(vei, 0xFFFFFFFF);
	}
	baridx = cfgidx - 4;	/* baridx checked below */

	csfunc = pd->pd_csfunc;
	if (csfunc != NULL) {
		ret = csfunc(vei->vei.vei_dir, cfgidx, &vei->vei.vei_data);
		if (ret)
			log_warnx("cfg space access function failed for "
			    "pci device %d", d);
		return;
	}

	/* No config space function, fallback to default simple r/w impl. */
	o += ofs;

	/*
	 * vei_dir == VEI_DIR_OUT : out instruction
	 *
	 * The guest wrote to the config space location denoted by the current
	 * value in the address register.
	 */
	if (vei->vei.vei_dir == VEI_DIR_OUT) {
		if ((o >= 0x10 && o <= 0x24) &&
		    vei->vei.vei_data == 0xffffffff) {
			/*
			 * Compute BAR index:
			 * o = 0x10 -> baridx = 0
			 * o = 0x14 -> baridx = 1
			 * o = 0x18 -> baridx = 2
			 * o = 0x1c -> baridx = 3
			 * o = 0x20 -> baridx = 4
			 * o = 0x24 -> baridx = 5
			 */
			if (baridx < pd->pd_bar_ct)
				vei->vei.vei_data = 0xfffff000;
			else
				vei->vei.vei_data = 0;
		}

		/* IOBAR registers must have bit 0 set */
		if (o >= 0x10 && o <= 0x24) {
			if (baridx < pd->pd_bar_ct &&
			    pd->pd_bartype[baridx] == PCI_BAR_TYPE_IO)
				vei->vei.vei_data |= 1;
		}

		/*
		 * Discard writes to "option rom base address" as none of our
		 * emulated devices have PCI option roms. Accept any other
		 * writes and copy data to config space registers.
		 */
		if (o != PCI_EXROMADDR_0)
			get_input_data(vei, &pd->pd_cfg_space[cfgidx]);
	} else {
		/*
		 * vei_dir == VEI_DIR_IN : in instruction
		 *
		 * The guest read from the config space location determined by
		 * the current value in the address register.
		 */
		if (d > pci.pci_dev_ct || b > 0 || f > 0)
			set_return_data(vei, 0xFFFFFFFF);
		else {
			data = pd->pd_cfg_space[cfgidx];
			switch (sz) {
			case 4:
				set_return_data(vei, data);
				break;
			case 2:
				if (ofs == 0)
					set_return_data(vei, data);
				else
					set_return_data(vei, data >> 16);
				break;
			case 1:
				set_return_data(vei, data >> (ofs * 8));
				break;
			}
		}
	}
}
#endif /* __amd64__ */

/*
 * Find the first PCI device based on PCI Subsystem ID
 * (e.g. PCI_PRODUCT_VIRTIO_BLOCK).
 *
 * Returns the PCI device id of the first matching device, if found.
 * Otherwise, returns -1.
 */
int
pci_find_first_device(uint16_t subsys_id)
{
	int i;

	for (i = 0; i < pci.pci_dev_ct; i++)
		if (pci.pci_devices[i].pd_subsys_id == subsys_id)
			return (i);
	return (-1);
}

/*
 * Retrieve the subsystem identifier for a PCI device if found, otherwise 0.
 */
uint16_t
pci_get_subsys_id(uint8_t pci_id)
{
	if (pci_id >= pci.pci_dev_ct)
		return (0);
	else
		return (pci.pci_devices[pci_id].pd_subsys_id);
}
