/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * @File    ctvmem.h
 *
 * @Brief
 * This file contains the definition of virtual memory management object
 * for card device.
 *
 * @Author Liu Chun
 * @Date Mar 28 2008
 */

#ifndef CTVMEM_H
#define CTVMEM_H

#define CT_PTP_NUM	4	/* num of device page table pages */

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <sound/memalloc.h>

/* The chip can handle the page table of 4k pages
 * (emu20k1 can handle even 8k pages, but we don't use it right now)
 */
#define CT_PAGE_SIZE	4096
#define CT_PAGE_SHIFT	12
#define CT_PAGE_MASK	(~(PAGE_SIZE - 1))
#define CT_PAGE_ALIGN(addr)	ALIGN(addr, CT_PAGE_SIZE)

struct ct_vm_block {
	unsigned int addr;	/* starting logical addr of this block */
	unsigned int size;	/* size of this device virtual mem block */
	struct list_head list;
};

struct snd_pcm_substream;

/* Virtual memory management object for card device */
struct ct_vm {
	struct snd_dma_buffer ptp[CT_PTP_NUM];	/* Device page table pages */
	unsigned int size;		/* Available addr space in bytes */
	struct list_head unused;	/* List of unused blocks */
	struct list_head used;		/* List of used blocks */
	struct mutex lock;

	/* Map host addr (kmalloced/vmalloced) to device logical addr. */
	struct ct_vm_block *(*map)(struct ct_vm *, struct snd_pcm_substream *,
				   int size);
	/* Unmap device logical addr area. */
	void (*unmap)(struct ct_vm *, struct ct_vm_block *block);
	dma_addr_t (*get_ptp_phys)(struct ct_vm *vm, int index);
};

int ct_vm_create(struct ct_vm **rvm, struct pci_dev *pci);
void ct_vm_destroy(struct ct_vm *vm);

#endif /* CTVMEM_H */
