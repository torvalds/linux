/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
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

#define CT_PTP_NUM	1	/* num of device page table pages */

#include <linux/spinlock.h>
#include <linux/list.h>

struct ct_vm_block {
	unsigned int addr;	/* starting logical addr of this block */
	unsigned int size;	/* size of this device virtual mem block */
	struct list_head list;
};

/* Virtual memory management object for card device */
struct ct_vm {
	void *ptp[CT_PTP_NUM];		/* Device page table pages */
	unsigned int size;		/* Available addr space in bytes */
	struct list_head unused;	/* List of unused blocks */
	struct list_head used;		/* List of used blocks */

	/* Map host addr (kmalloced/vmalloced) to device logical addr. */
	struct ct_vm_block *(*map)(struct ct_vm *, void *host_addr, int size);
	/* Unmap device logical addr area. */
	void (*unmap)(struct ct_vm *, struct ct_vm_block *block);
	void *(*get_ptp_virt)(struct ct_vm *vm, int index);
};

int ct_vm_create(struct ct_vm **rvm);
void ct_vm_destroy(struct ct_vm *vm);

#endif /* CTVMEM_H */
