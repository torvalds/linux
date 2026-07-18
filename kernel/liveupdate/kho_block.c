// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2026, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: KHO Serialization Blocks
 *
 * KHO provides a mechanism to preserve stateful data across a kexec handover
 * by serializing it into memory blocks, and provides the common
 * infrastructure for managing these blocks.
 *
 * Each block consists of a header (struct kho_block_header_ser) followed by an
 * array of serialized entries. Multiple blocks are linked together via a
 * physical pointer in the header, forming a linked list that can be easily
 * traversed in both the current and the next kernel.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/kexec_handover.h>
#include <linux/kho/abi/block.h>
#include <linux/kho_block.h>
#include <linux/slab.h>

/*
 * Safeguard limit for the number of serialization blocks. This is used to
 * prevent infinite loops and excessive memory allocation in case of memory
 * corruption in the preserved state.
 *
 * With a 4KB page size, 10k blocks is about 40MB. For 32-byte entries
 * (e.g. 4 u64s), each block holds up to 127 entries (accounting for the
 * 16-byte header), allowing the block set to hold up to 1.27M entries.
 */
#define KHO_MAX_BLOCKS 10000

/**
 * kho_block_set_init - Initialize a block set.
 * @bs:         The block set to initialize.
 * @entry_size: The size of each entry in the blocks.
 */
void kho_block_set_init(struct kho_block_set *bs, size_t entry_size)
{
	*bs = (struct kho_block_set)KHO_BLOCK_SET_INIT(*bs, entry_size);
	WARN_ON_ONCE(!bs->count_per_block);
}

/* Serialized entries start immediately after the block header */
static void *kho_block_entries(struct kho_block *block)
{
	return (void *)(block->ser + 1);
}

/* Get the address of the serialized entry at the specified index */
static void *kho_block_entry(struct kho_block_set_it *it, u64 index)
{
	return kho_block_entries(it->block) + (index * it->bs->entry_size);
}

/* Free serialized data */
static void kho_block_free_ser(struct kho_block_set *bs,
			       struct kho_block_header_ser *ser)
{
	if (bs->incoming)
		kho_restore_free(ser);
	else
		kho_unpreserve_free(ser);
}

static struct kho_block_header_ser *kho_block_alloc_ser(struct kho_block_set *bs)
{
	WARN_ON_ONCE(bs->incoming);
	return kho_alloc_preserve(KHO_BLOCK_SIZE);
}

static int kho_block_add(struct kho_block_set *bs,
			 struct kho_block_header_ser *ser)
{
	struct kho_block *block, *last;

	if (bs->nblocks >= KHO_MAX_BLOCKS)
		return -ENOSPC;

	block = kzalloc_obj(*block);
	if (!block)
		return -ENOMEM;

	block->ser = ser;
	last = list_last_entry_or_null(&bs->blocks, struct kho_block, list);
	list_add_tail(&block->list, &bs->blocks);
	bs->nblocks++;

	if (last)
		last->ser->next = virt_to_phys(ser);
	else
		bs->head_pa = virt_to_phys(ser);

	return 0;
}

static int kho_block_set_grow_one(struct kho_block_set *bs)
{
	struct kho_block_header_ser *ser;
	int err;

	ser = kho_block_alloc_ser(bs);
	if (IS_ERR(ser))
		return PTR_ERR(ser);

	err = kho_block_add(bs, ser);
	if (err) {
		kho_block_free_ser(bs, ser);
		return err;
	}

	return 0;
}

static void kho_block_set_shrink_one(struct kho_block_set *bs)
{
	struct kho_block *last, *new_last;

	if (list_empty(&bs->blocks))
		return;

	last = list_last_entry(&bs->blocks, struct kho_block, list);
	list_del(&last->list);
	bs->nblocks--;
	kho_block_free_ser(bs, last->ser);
	kfree(last);

	new_last = list_last_entry_or_null(&bs->blocks, struct kho_block, list);
	if (new_last)
		new_last->ser->next = 0;
	else
		bs->head_pa = 0;
}

/**
 * kho_block_set_grow - Expand the block set to accommodate the target count.
 * @bs:    The block set.
 * @count: The target number of valid entries to accommodate.
 *
 * Dynamically preallocates and links preserved memory blocks if the target
 * entry count exceeds the current total capacity of the set, ensuring they
 * are available during serialization/deserialization.
 *
 * Context: Caller must hold a lock protecting the block set.
 * Return: 0 on success, or a negative errno on failure.
 */
int kho_block_set_grow(struct kho_block_set *bs, u64 count)
{
	long orig_nblocks = bs->nblocks;
	int err;

	if (WARN_ON_ONCE(bs->incoming))
		return -EINVAL;

	while (count > bs->nblocks * bs->count_per_block) {
		err = kho_block_set_grow_one(bs);
		if (err)
			goto err_shrink;
	}

	return 0;

err_shrink:
	while (bs->nblocks > orig_nblocks)
		kho_block_set_shrink_one(bs);
	return err;
}

/**
 * kho_block_set_shrink - Shrink the block set to accommodate the target count.
 * @bs:              The block set.
 * @count:           The target number of valid entries to accommodate.
 *
 * Releases and unallocates redundant preserved memory blocks. Checks if the
 * last block in the set can be removed because the remaining entry count is
 * fully accommodated by the preceding blocks.
 *
 * Note: It is the caller's responsibility to ensure that entries are removed
 * in the reverse order of their insertion. Because shrinking destroys the last
 * block in the set, removing entries in any other order would corrupt active
 * data.
 *
 * Context: Caller must hold a lock protecting the block set.
 */
void kho_block_set_shrink(struct kho_block_set *bs, u64 count)
{
	while (bs->nblocks > 0 && count <= (bs->nblocks - 1) * bs->count_per_block)
		kho_block_set_shrink_one(bs);
}

/*
 * kho_block_set_is_cyclic - Check for cycles in a linked list of blocks.
 * Uses Floyd's cycle-finding algorithm to ensure sanity of the incoming list.
 *
 * Return: true if a cycle or corruption is detected, false otherwise.
 */
static bool kho_block_set_is_cyclic(struct kho_block_set *bs)
{
	struct kho_block_header_ser *fast;
	struct kho_block_header_ser *slow;
	int count = 0;

	fast = phys_to_virt(bs->head_pa);
	slow = fast;

	while (fast) {
		if (count++ >= KHO_MAX_BLOCKS) {
			pr_err("Block set is corrupted\n");
			return true;
		}

		if (!fast->next)
			break;

		fast = phys_to_virt(fast->next);
		if (!fast->next)
			break;

		fast = phys_to_virt(fast->next);
		slow = phys_to_virt(slow->next);

		if (slow == fast) {
			pr_err("Block set is corrupted\n");
			return true;
		}
	}

	return false;
}

/**
 * kho_block_set_restore - Restore a block set from a physical address.
 * @bs:      The block set to restore.
 * @head_pa: Physical address of the first block header.
 *
 * Restores a serialized block set from a given physical address. The caller is
 * responsible for ensuring that the block set @bs has been allocated and
 * initialized prior to calling this function.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int kho_block_set_restore(struct kho_block_set *bs, u64 head_pa)
{
	struct kho_block_header_ser *ser;
	u64 next_pa = head_pa;
	int err;

	/* Restored block sets use size from the previous kernel */
	bs->incoming = true;
	if (!head_pa)
		return 0;

	bs->head_pa = head_pa;
	if (kho_block_set_is_cyclic(bs)) {
		bs->head_pa = 0;
		return -EINVAL;
	}

	while (next_pa) {
		ser = phys_to_virt(next_pa);
		if (!ser->count || ser->count > bs->count_per_block) {
			pr_warn("Block contains invalid entry count: %llu\n",
				ser->count);
			err = -EINVAL;
			goto err_destroy;
		}
		err = kho_block_add(bs, ser);
		if (err)
			goto err_destroy;
		next_pa = ser->next;
	}

	return 0;

err_destroy:
	kho_block_set_destroy(bs);

	/* Free the remaining un-restored blocks in the physical chain */
	while (next_pa) {
		struct kho_block_header_ser *next_ser = phys_to_virt(next_pa);

		next_pa = next_ser->next;
		kho_block_free_ser(bs, next_ser);
	}
	return err;
}

/**
 * kho_block_set_destroy - Destroy all blocks in a block set.
 * @bs:          The block set.
 */
void kho_block_set_destroy(struct kho_block_set *bs)
{
	struct kho_block *block, *tmp;

	list_for_each_entry_safe(block, tmp, &bs->blocks, list) {
		list_del(&block->list);
		kho_block_free_ser(bs, block->ser);
		kfree(block);
	}
	bs->nblocks = 0;
	bs->head_pa = 0;
}

/**
 * kho_block_set_clear - Clear all serialized data in a block set.
 * @bs: The block set to clear.
 */
void kho_block_set_clear(struct kho_block_set *bs)
{
	struct kho_block *block;

	list_for_each_entry(block, &bs->blocks, list) {
		block->ser->count = 0;
		memset(block->ser + 1, 0, KHO_BLOCK_SIZE - sizeof(*block->ser));
	}
}

/**
 * kho_block_set_it_init - Initialize a block set iterator.
 * @it:         The iterator to initialize.
 * @bs:         The block set to iterate over.
 */
void kho_block_set_it_init(struct kho_block_set_it *it, struct kho_block_set *bs)
{
	it->bs = bs;
	it->block = list_first_entry_or_null(&bs->blocks, struct kho_block, list);
	it->i = 0;
}

/**
 * kho_block_set_it_reserve_entry - Reserve and return the next available slot for writing.
 * @it: The block iterator.
 *
 * Reserves a slot in the current block during state serialization to add a new
 * entry, advancing the internal index. If the current block is full, it
 * automatically moves to the next block in the set.
 *
 * Return: A pointer to the reserved entry slot, or NULL if the block set's
 * capacity is fully exhausted.
 */
void *kho_block_set_it_reserve_entry(struct kho_block_set_it *it)
{
	void *entry;

	if (!it->block)
		return NULL;

	if (it->i == it->bs->count_per_block) {
		if (list_is_last(&it->block->list, &it->bs->blocks))
			return NULL;
		it->block = list_next_entry(it->block, list);
		it->i = 0;
	}

	entry = kho_block_entry(it, it->i++);
	it->block->ser->count = it->i;
	return entry;
}

/**
 * kho_block_set_it_read_entry - Read the next serialized entry from the block set.
 * @it: The block iterator.
 *
 * Iterates through previously written entries during state deserialization,
 * respecting the actual count stored in each block's header.
 *
 * Return: A pointer to the next serialized entry, or NULL if all serialized
 * entries have been read.
 */
void *kho_block_set_it_read_entry(struct kho_block_set_it *it)
{
	if (!it->block)
		return NULL;

	if (it->i == it->block->ser->count) {
		if (list_is_last(&it->block->list, &it->bs->blocks))
			return NULL;
		it->block = list_next_entry(it->block, list);
		it->i = 0;
	}

	return kho_block_entry(it, it->i++);
}

/**
 * kho_block_set_it_prev - Return the previous entry slot in the block set.
 * @it: The block iterator.
 *
 * If the current index is at the start of a block, it automatically moves to
 * the end of the previous block.
 *
 * Return: A pointer to the previous entry slot, or NULL if at the very
 * beginning of the block set.
 */
void *kho_block_set_it_prev(struct kho_block_set_it *it)
{
	if (!it->block)
		return NULL;

	if (it->i == 0) {
		if (list_is_first(&it->block->list, &it->bs->blocks))
			return NULL;
		it->block = list_prev_entry(it->block, list);
		it->i = it->bs->count_per_block;
	}

	return kho_block_entry(it, --it->i);
}
