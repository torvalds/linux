// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * @File    ctvmem.c
 *
 * @Brief
 * This file contains the implementation of virtual memory management object
 * for card device.
 *
 * @Author Liu Chun
 * @Date Apr 1 2008
 */

#include "ctvmem.h"
#include "ctatc.h"
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <sound/pcm.h>

#define CT_PTES_PER_PAGE (CT_PAGE_SIZE / sizeof(void *))
#define CT_ADDRS_PER_PAGE (CT_PTES_PER_PAGE * CT_PAGE_SIZE)

/* *
 * Find or create vm block based on requested @size.
 * @size must be page aligned.
 * */
static struct ct_vm_block *
get_vm_block(struct ct_vm *vm, unsigned int size, struct ct_atc *atc)
{
	struct ct_vm_block *block = NULL, *entry;
	struct list_head *pos;

	size = CT_PAGE_ALIGN(size);
	if (size > vm->size) {
		dev_err(atc->card->dev,
			"Fail! No sufficient device virtual memory space available!\n");
		return NULL;
	}

	mutex_lock(&vm->lock);
	list_for_each(pos, &vm->unused) {
		entry = list_entry(pos, struct ct_vm_block, list);
		if (entry->size >= size)
			break; /* found a block that is big enough */
	}
	if (pos == &vm->unused)
		goto out;

	if (entry->size == size) {
		/* Move the vm node from unused list to used list directly */
		list_move(&entry->list, &vm->used);
		vm->size -= size;
		block = entry;
		goto out;
	}

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block)
		goto out;

	block->addr = entry->addr;
	block->size = size;
	list_add(&block->list, &vm->used);
	entry->addr += size;
	entry->size -= size;
	vm->size -= size;

 out:
	mutex_unlock(&vm->lock);
	return block;
}

static void put_vm_block(struct ct_vm *vm, struct ct_vm_block *block)
{
	struct ct_vm_block *entry, *pre_ent;
	struct list_head *pos, *pre;

	block->size = CT_PAGE_ALIGN(block->size);

	mutex_lock(&vm->lock);
	list_del(&block->list);
	vm->size += block->size;

	list_for_each(pos, &vm->unused) {
		entry = list_entry(pos, struct ct_vm_block, list);
		if (entry->addr >= (block->addr + block->size))
			break; /* found a position */
	}
	if (pos == &vm->unused) {
		list_add_tail(&block->list, &vm->unused);
		entry = block;
	} else {
		if ((block->addr + block->size) == entry->addr) {
			entry->addr = block->addr;
			entry->size += block->size;
			kfree(block);
		} else {
			__list_add(&block->list, pos->prev, pos);
			entry = block;
		}
	}

	pos = &entry->list;
	pre = pos->prev;
	while (pre != &vm->unused) {
		entry = list_entry(pos, struct ct_vm_block, list);
		pre_ent = list_entry(pre, struct ct_vm_block, list);
		if ((pre_ent->addr + pre_ent->size) > entry->addr)
			break;

		pre_ent->size += entry->size;
		list_del(pos);
		kfree(entry);
		pos = pre;
		pre = pos->prev;
	}
	mutex_unlock(&vm->lock);
}

/* Map host addr (kmalloced/vmalloced) to device logical addr. */
static struct ct_vm_block *
ct_vm_map(struct ct_vm *vm, struct snd_pcm_substream *substream, int size)
{
	struct ct_vm_block *block;
	unsigned int pte_start;
	unsigned i, pages;
	unsigned long *ptp;
	struct ct_atc *atc = snd_pcm_substream_chip(substream);

	block = get_vm_block(vm, size, atc);
	if (block == NULL) {
		dev_err(atc->card->dev,
			"No virtual memory block that is big enough to allocate!\n");
		return NULL;
	}

	ptp = (unsigned long *)vm->ptp[0].area;
	pte_start = (block->addr >> CT_PAGE_SHIFT);
	pages = block->size >> CT_PAGE_SHIFT;
	for (i = 0; i < pages; i++) {
		unsigned long addr;
		addr = snd_pcm_sgbuf_get_addr(substream, i << CT_PAGE_SHIFT);
		ptp[pte_start + i] = addr;
	}

	block->size = size;
	return block;
}

static void ct_vm_unmap(struct ct_vm *vm, struct ct_vm_block *block)
{
	/* do unmapping */
	put_vm_block(vm, block);
}

/* *
 * return the host physical addr of the @index-th device
 * page table page on success, or ~0UL on failure.
 * The first returned ~0UL indicates the termination.
 * */
static dma_addr_t
ct_get_ptp_phys(struct ct_vm *vm, int index)
{
	return (index >= CT_PTP_NUM) ? ~0UL : vm->ptp[index].addr;
}

int ct_vm_create(struct ct_vm **rvm, struct pci_dev *pci)
{
	struct ct_vm *vm;
	struct ct_vm_block *block;
	int i, err = 0;

	*rvm = NULL;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return -ENOMEM;

	mutex_init(&vm->lock);

	/* Allocate page table pages */
	for (i = 0; i < CT_PTP_NUM; i++) {
		err = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
					  snd_dma_pci_data(pci),
					  PAGE_SIZE, &vm->ptp[i]);
		if (err < 0)
			break;
	}
	if (err < 0) {
		/* no page table pages are allocated */
		ct_vm_destroy(vm);
		return -ENOMEM;
	}
	vm->size = CT_ADDRS_PER_PAGE * i;
	vm->map = ct_vm_map;
	vm->unmap = ct_vm_unmap;
	vm->get_ptp_phys = ct_get_ptp_phys;
	INIT_LIST_HEAD(&vm->unused);
	INIT_LIST_HEAD(&vm->used);
	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (NULL != block) {
		block->addr = 0;
		block->size = vm->size;
		list_add(&block->list, &vm->unused);
	}

	*rvm = vm;
	return 0;
}

/* The caller must ensure no mapping pages are being used
 * by hardware before calling this function */
void ct_vm_destroy(struct ct_vm *vm)
{
	int i;
	struct list_head *pos;
	struct ct_vm_block *entry;

	/* free used and unused list nodes */
	while (!list_empty(&vm->used)) {
		pos = vm->used.next;
		list_del(pos);
		entry = list_entry(pos, struct ct_vm_block, list);
		kfree(entry);
	}
	while (!list_empty(&vm->unused)) {
		pos = vm->unused.next;
		list_del(pos);
		entry = list_entry(pos, struct ct_vm_block, list);
		kfree(entry);
	}

	/* free allocated page table pages */
	for (i = 0; i < CT_PTP_NUM; i++)
		snd_dma_free_pages(&vm->ptp[i]);

	vm->size = 0;

	kfree(vm);
}
