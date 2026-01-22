// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Copyright (C) 2025 Amazon.com Inc. or its affiliates.
 * Pratyush Yadav <ptyadav@amazon.de>
 */

/**
 * DOC: Memfd Preservation via LUO
 *
 * Overview
 * ========
 *
 * Memory file descriptors (memfd) can be preserved over a kexec using the Live
 * Update Orchestrator (LUO) file preservation. This allows userspace to
 * transfer its memory contents to the next kernel after a kexec.
 *
 * The preservation is not intended to be transparent. Only select properties of
 * the file are preserved. All others are reset to default. The preserved
 * properties are described below.
 *
 * .. note::
 *    The LUO API is not stabilized yet, so the preserved properties of a memfd
 *    are also not stable and are subject to backwards incompatible changes.
 *
 * .. note::
 *    Currently a memfd backed by Hugetlb is not supported. Memfds created
 *    with ``MFD_HUGETLB`` will be rejected.
 *
 * Preserved Properties
 * ====================
 *
 * The following properties of the memfd are preserved across kexec:
 *
 * File Contents
 *   All data stored in the file is preserved.
 *
 * File Size
 *   The size of the file is preserved. Holes in the file are filled by
 *   allocating pages for them during preservation.
 *
 * File Position
 *   The current file position is preserved, allowing applications to continue
 *   reading/writing from their last position.
 *
 * File Status Flags
 *   memfds are always opened with ``O_RDWR`` and ``O_LARGEFILE``. This property
 *   is maintained.
 *
 * Non-Preserved Properties
 * ========================
 *
 * All properties which are not preserved must be assumed to be reset to
 * default. This section describes some of those properties which may be more of
 * note.
 *
 * ``FD_CLOEXEC`` flag
 *   A memfd can be created with the ``MFD_CLOEXEC`` flag that sets the
 *   ``FD_CLOEXEC`` on the file. This flag is not preserved and must be set
 *   again after restore via ``fcntl()``.
 *
 * Seals
 *   File seals are not preserved. The file is unsealed on restore and if
 *   needed, must be sealed again via ``fcntl()``.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/io.h>
#include <linux/kexec_handover.h>
#include <linux/kho/abi/memfd.h>
#include <linux/liveupdate.h>
#include <linux/shmem_fs.h>
#include <linux/vmalloc.h>
#include <linux/memfd.h>
#include "internal.h"

static int memfd_luo_preserve_folios(struct file *file,
				     struct kho_vmalloc *kho_vmalloc,
				     struct memfd_luo_folio_ser **out_folios_ser,
				     u64 *nr_foliosp)
{
	struct inode *inode = file_inode(file);
	struct memfd_luo_folio_ser *folios_ser;
	unsigned int max_folios;
	long i, size, nr_pinned;
	struct folio **folios;
	int err = -EINVAL;
	pgoff_t offset;
	u64 nr_folios;

	size = i_size_read(inode);
	/*
	 * If the file has zero size, then the folios and nr_folios properties
	 * are not set.
	 */
	if (!size) {
		*nr_foliosp = 0;
		*out_folios_ser = NULL;
		memset(kho_vmalloc, 0, sizeof(*kho_vmalloc));
		return 0;
	}

	/*
	 * Guess the number of folios based on inode size. Real number might end
	 * up being smaller if there are higher order folios.
	 */
	max_folios = PAGE_ALIGN(size) / PAGE_SIZE;
	folios = kvmalloc_array(max_folios, sizeof(*folios), GFP_KERNEL);
	if (!folios)
		return -ENOMEM;

	/*
	 * Pin the folios so they don't move around behind our back. This also
	 * ensures none of the folios are in CMA -- which ensures they don't
	 * fall in KHO scratch memory. It also moves swapped out folios back to
	 * memory.
	 *
	 * A side effect of doing this is that it allocates a folio for all
	 * indices in the file. This might waste memory on sparse memfds. If
	 * that is really a problem in the future, we can have a
	 * memfd_pin_folios() variant that does not allocate a page on empty
	 * slots.
	 */
	nr_pinned = memfd_pin_folios(file, 0, size - 1, folios, max_folios,
				     &offset);
	if (nr_pinned < 0) {
		err = nr_pinned;
		pr_err("failed to pin folios: %d\n", err);
		goto err_free_folios;
	}
	nr_folios = nr_pinned;

	folios_ser = vcalloc(nr_folios, sizeof(*folios_ser));
	if (!folios_ser) {
		err = -ENOMEM;
		goto err_unpin;
	}

	for (i = 0; i < nr_folios; i++) {
		struct memfd_luo_folio_ser *pfolio = &folios_ser[i];
		struct folio *folio = folios[i];
		unsigned int flags = 0;

		err = kho_preserve_folio(folio);
		if (err)
			goto err_unpreserve;

		if (folio_test_dirty(folio))
			flags |= MEMFD_LUO_FOLIO_DIRTY;
		if (folio_test_uptodate(folio))
			flags |= MEMFD_LUO_FOLIO_UPTODATE;

		pfolio->pfn = folio_pfn(folio);
		pfolio->flags = flags;
		pfolio->index = folio->index;
	}

	err = kho_preserve_vmalloc(folios_ser, kho_vmalloc);
	if (err)
		goto err_unpreserve;

	kvfree(folios);
	*nr_foliosp = nr_folios;
	*out_folios_ser = folios_ser;

	/*
	 * Note: folios_ser is purposely not freed here. It is preserved
	 * memory (via KHO). In the 'unpreserve' path, we use the vmap pointer
	 * that is passed via private_data.
	 */
	return 0;

err_unpreserve:
	for (i = i - 1; i >= 0; i--)
		kho_unpreserve_folio(folios[i]);
	vfree(folios_ser);
err_unpin:
	unpin_folios(folios, nr_folios);
err_free_folios:
	kvfree(folios);

	return err;
}

static void memfd_luo_unpreserve_folios(struct kho_vmalloc *kho_vmalloc,
					struct memfd_luo_folio_ser *folios_ser,
					u64 nr_folios)
{
	long i;

	if (!nr_folios)
		return;

	kho_unpreserve_vmalloc(kho_vmalloc);

	for (i = 0; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &folios_ser[i];
		struct folio *folio;

		if (!pfolio->pfn)
			continue;

		folio = pfn_folio(pfolio->pfn);

		kho_unpreserve_folio(folio);
		unpin_folio(folio);
	}

	vfree(folios_ser);
}

static int memfd_luo_preserve(struct liveupdate_file_op_args *args)
{
	struct inode *inode = file_inode(args->file);
	struct memfd_luo_folio_ser *folios_ser;
	struct memfd_luo_ser *ser;
	u64 nr_folios;
	int err = 0;

	inode_lock(inode);
	shmem_freeze(inode, true);

	/* Allocate the main serialization structure in preserved memory */
	ser = kho_alloc_preserve(sizeof(*ser));
	if (IS_ERR(ser)) {
		err = PTR_ERR(ser);
		goto err_unlock;
	}

	ser->pos = args->file->f_pos;
	ser->size = i_size_read(inode);

	err = memfd_luo_preserve_folios(args->file, &ser->folios,
					&folios_ser, &nr_folios);
	if (err)
		goto err_free_ser;

	ser->nr_folios = nr_folios;
	inode_unlock(inode);

	args->private_data = folios_ser;
	args->serialized_data = virt_to_phys(ser);

	return 0;

err_free_ser:
	kho_unpreserve_free(ser);
err_unlock:
	shmem_freeze(inode, false);
	inode_unlock(inode);
	return err;
}

static int memfd_luo_freeze(struct liveupdate_file_op_args *args)
{
	struct memfd_luo_ser *ser;

	if (WARN_ON_ONCE(!args->serialized_data))
		return -EINVAL;

	ser = phys_to_virt(args->serialized_data);

	/*
	 * The pos might have changed since prepare. Everything else stays the
	 * same.
	 */
	ser->pos = args->file->f_pos;

	return 0;
}

static void memfd_luo_unpreserve(struct liveupdate_file_op_args *args)
{
	struct inode *inode = file_inode(args->file);
	struct memfd_luo_ser *ser;

	if (WARN_ON_ONCE(!args->serialized_data))
		return;

	inode_lock(inode);
	shmem_freeze(inode, false);

	ser = phys_to_virt(args->serialized_data);

	memfd_luo_unpreserve_folios(&ser->folios, args->private_data,
				    ser->nr_folios);

	kho_unpreserve_free(ser);
	inode_unlock(inode);
}

static void memfd_luo_discard_folios(const struct memfd_luo_folio_ser *folios_ser,
				     u64 nr_folios)
{
	u64 i;

	for (i = 0; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &folios_ser[i];
		struct folio *folio;
		phys_addr_t phys;

		if (!pfolio->pfn)
			continue;

		phys = PFN_PHYS(pfolio->pfn);
		folio = kho_restore_folio(phys);
		if (!folio) {
			pr_warn_ratelimited("Unable to restore folio at physical address: %llx\n",
					    phys);
			continue;
		}

		folio_put(folio);
	}
}

static void memfd_luo_finish(struct liveupdate_file_op_args *args)
{
	struct memfd_luo_folio_ser *folios_ser;
	struct memfd_luo_ser *ser;

	if (args->retrieved)
		return;

	ser = phys_to_virt(args->serialized_data);
	if (!ser)
		return;

	if (ser->nr_folios) {
		folios_ser = kho_restore_vmalloc(&ser->folios);
		if (!folios_ser)
			goto out;

		memfd_luo_discard_folios(folios_ser, ser->nr_folios);
		vfree(folios_ser);
	}

out:
	kho_restore_free(ser);
}

static int memfd_luo_retrieve_folios(struct file *file,
				     struct memfd_luo_folio_ser *folios_ser,
				     u64 nr_folios)
{
	struct inode *inode = file_inode(file);
	struct address_space *mapping = inode->i_mapping;
	struct folio *folio;
	int err = -EIO;
	long i;

	for (i = 0; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &folios_ser[i];
		phys_addr_t phys;
		u64 index;
		int flags;

		if (!pfolio->pfn)
			continue;

		phys = PFN_PHYS(pfolio->pfn);
		folio = kho_restore_folio(phys);
		if (!folio) {
			pr_err("Unable to restore folio at physical address: %llx\n",
			       phys);
			goto put_folios;
		}
		index = pfolio->index;
		flags = pfolio->flags;

		/* Set up the folio for insertion. */
		__folio_set_locked(folio);
		__folio_set_swapbacked(folio);

		err = mem_cgroup_charge(folio, NULL, mapping_gfp_mask(mapping));
		if (err) {
			pr_err("shmem: failed to charge folio index %ld: %d\n",
			       i, err);
			goto unlock_folio;
		}

		err = shmem_add_to_page_cache(folio, mapping, index, NULL,
					      mapping_gfp_mask(mapping));
		if (err) {
			pr_err("shmem: failed to add to page cache folio index %ld: %d\n",
			       i, err);
			goto unlock_folio;
		}

		if (flags & MEMFD_LUO_FOLIO_UPTODATE)
			folio_mark_uptodate(folio);
		if (flags & MEMFD_LUO_FOLIO_DIRTY)
			folio_mark_dirty(folio);

		err = shmem_inode_acct_blocks(inode, 1);
		if (err) {
			pr_err("shmem: failed to account folio index %ld: %d\n",
			       i, err);
			goto unlock_folio;
		}

		shmem_recalc_inode(inode, 1, 0);
		folio_add_lru(folio);
		folio_unlock(folio);
		folio_put(folio);
	}

	return 0;

unlock_folio:
	folio_unlock(folio);
	folio_put(folio);
put_folios:
	/*
	 * Note: don't free the folios already added to the file. They will be
	 * freed when the file is freed. Free the ones not added yet here.
	 */
	for (long j = i + 1; j < nr_folios; j++) {
		const struct memfd_luo_folio_ser *pfolio = &folios_ser[j];

		folio = kho_restore_folio(pfolio->pfn);
		if (folio)
			folio_put(folio);
	}

	return err;
}

static int memfd_luo_retrieve(struct liveupdate_file_op_args *args)
{
	struct memfd_luo_folio_ser *folios_ser;
	struct memfd_luo_ser *ser;
	struct file *file;
	int err;

	ser = phys_to_virt(args->serialized_data);
	if (!ser)
		return -EINVAL;

	file = memfd_alloc_file("", 0);
	if (IS_ERR(file)) {
		pr_err("failed to setup file: %pe\n", file);
		err = PTR_ERR(file);
		goto free_ser;
	}

	vfs_setpos(file, ser->pos, MAX_LFS_FILESIZE);
	file->f_inode->i_size = ser->size;

	if (ser->nr_folios) {
		folios_ser = kho_restore_vmalloc(&ser->folios);
		if (!folios_ser) {
			err = -EINVAL;
			goto put_file;
		}

		err = memfd_luo_retrieve_folios(file, folios_ser, ser->nr_folios);
		vfree(folios_ser);
		if (err)
			goto put_file;
	}

	args->file = file;
	kho_restore_free(ser);

	return 0;

put_file:
	fput(file);
free_ser:
	kho_restore_free(ser);
	return err;
}

static bool memfd_luo_can_preserve(struct liveupdate_file_handler *handler,
				   struct file *file)
{
	struct inode *inode = file_inode(file);

	return shmem_file(file) && !inode->i_nlink;
}

static const struct liveupdate_file_ops memfd_luo_file_ops = {
	.freeze = memfd_luo_freeze,
	.finish = memfd_luo_finish,
	.retrieve = memfd_luo_retrieve,
	.preserve = memfd_luo_preserve,
	.unpreserve = memfd_luo_unpreserve,
	.can_preserve = memfd_luo_can_preserve,
	.owner = THIS_MODULE,
};

static struct liveupdate_file_handler memfd_luo_handler = {
	.ops = &memfd_luo_file_ops,
	.compatible = MEMFD_LUO_FH_COMPATIBLE,
};

static int __init memfd_luo_init(void)
{
	int err = liveupdate_register_file_handler(&memfd_luo_handler);

	if (err && err != -EOPNOTSUPP) {
		pr_err("Could not register luo filesystem handler: %pe\n",
		       ERR_PTR(err));

		return err;
	}

	return 0;
}
late_initcall(memfd_luo_init);
