// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */
#include "fsverity_private.h"

#include <linux/export.h>
#include <linux/fsverity.h>
#include <linux/pagemap.h>

/**
 * generic_read_merkle_tree_page - generic ->read_merkle_tree_page helper
 * @inode:	inode containing the Merkle tree
 * @index:	0-based index of the Merkle tree page in the inode
 *
 * The caller needs to adjust @index from the Merkle-tree relative index passed
 * to ->read_merkle_tree_page to the actual index where the Merkle tree is
 * stored in the page cache for @inode.
 */
struct page *generic_read_merkle_tree_page(struct inode *inode, pgoff_t index)
{
	struct folio *folio;

	folio = read_mapping_folio(inode->i_mapping, index, NULL);
	if (IS_ERR(folio))
		return ERR_CAST(folio);
	return folio_file_page(folio, index);
}
EXPORT_SYMBOL_GPL(generic_read_merkle_tree_page);

/**
 * generic_readahead_merkle_tree() - generic ->readahead_merkle_tree helper
 * @inode:	inode containing the Merkle tree
 * @index:	0-based index of the first Merkle tree page to read ahead in the
 *		inode
 * @nr_pages:	the number of Merkle tree pages that should be read ahead
 *
 * The caller needs to adjust @index from the Merkle-tree relative index passed
 * to ->read_merkle_tree_page to the actual index where the Merkle tree is
 * stored in the page cache for @inode.
 */
void generic_readahead_merkle_tree(struct inode *inode, pgoff_t index,
				   unsigned long nr_pages)
{
	struct folio *folio;

	lockdep_assert_held(&inode->i_mapping->invalidate_lock);

	folio = __filemap_get_folio(inode->i_mapping, index, FGP_ACCESSED, 0);
	if (folio == ERR_PTR(-ENOENT) ||
	    (!IS_ERR(folio) && !folio_test_uptodate(folio))) {
		DEFINE_READAHEAD(ractl, NULL, NULL, inode->i_mapping, index);

		page_cache_ra_unbounded(&ractl, nr_pages, 0);
	}
	if (!IS_ERR(folio))
		folio_put(folio);
}
EXPORT_SYMBOL_GPL(generic_readahead_merkle_tree);

/**
 * fsverity_fill_zerohash() - fill folio with hashes of zero data block
 * @folio:	folio to fill
 * @offset:	offset in the folio to start
 * @len:	length of the range to fill with hashes
 * @vi:		fsverity info
 */
void fsverity_fill_zerohash(struct folio *folio, size_t offset, size_t len,
			      struct fsverity_info *vi)
{
	size_t off = offset;

	WARN_ON_ONCE(!IS_ALIGNED(offset, vi->tree_params.digest_size));
	WARN_ON_ONCE(!IS_ALIGNED(len, vi->tree_params.digest_size));

	for (; off < (offset + len); off += vi->tree_params.digest_size)
		memcpy_to_folio(folio, off, vi->tree_params.zero_digest,
				vi->tree_params.digest_size);
}
EXPORT_SYMBOL_GPL(fsverity_fill_zerohash);
